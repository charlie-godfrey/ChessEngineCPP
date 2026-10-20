#include "uci.h"

#include "evaluate.h"
#include "makemove.h"
#include "move.h"
#include "movegen.h"
#include "nnue.h"
#include "position.h"
#include "search.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>

namespace aurora {

Uci::Uci() : position_(), search_(), debug_(false) {}

Uci::~Uci() {
  waitForSearch();
}

void Uci::waitForSearch() {
  if (search_thread_ && search_thread_->joinable()) {
    search_.stop();
    search_thread_->join();
    search_thread_.reset();
  }
}

void Uci::run() {
  std::string line;

  while (std::getline(std::cin, line)) {
    if (line.empty()) {
      continue;
    }

    if (debug_) {
      std::cerr << "UCI received: " << line << std::endl;
    }

    if (handleCommand(line)) {
      break;  // quit command
    }
  }
}

bool Uci::handleCommand(const std::string& line) {
  // Parse command and arguments
  std::istringstream iss(line);
  std::string command;
  iss >> command;

  if (command == "uci") {
    handleUci();
  } else if (command == "isready") {
    handleIsReady();
  } else if (command == "ucinewgame") {
    handleUciNewGame();
  } else if (command == "position") {
    handlePosition(iss);
  } else if (command == "go") {
    handleGo(iss);
  } else if (command == "setoption") {
    handleSetOption(iss);
  } else if (command == "stop") {
    handleStop();
  } else if (command == "quit") {
    waitForSearch();
    return true;
  } else if (command == "debug") {
    handleDebug(iss);
  } else if (command == "print") {
    handlePrint();  // Debug: print internal state
  } else {
    // Unknown command - silently ignore
    if (debug_) {
      std::cerr << "Unknown UCI command: " << command << std::endl;
    }
  }

  return false;  // Continue running
}

void Uci::handleUci() {
  std::cout << "id name Aurora" << std::endl;
  std::cout << "id author Aurora Team" << std::endl;
  std::cout << "option name Hash type spin default 64 min 1 max 4096" << std::endl;
  std::cout << "option name UseNNUE type check default false" << std::endl;
  std::cout << "option name NNUEFile type string default" << std::endl;
  std::cout << "uciok" << std::endl;
}

void Uci::handleIsReady() {
  waitForSearch();
  std::cout << "readyok" << std::endl;
}

void Uci::handleUciNewGame() {
  waitForSearch();
  position_.set_startpos();
  search_.clearTT();
  game_keys_.clear();
  game_keys_.push_back(position_.key());
}

void Uci::handlePosition(std::istringstream& iss) {
  waitForSearch();
  game_keys_.clear();
  std::string token;
  iss >> token;

  bool need_moves = false;

  if (token == "startpos") {
    position_.set_startpos();
    // Check for "moves" keyword
    std::string next;
    if (iss >> next && next == "moves") {
      need_moves = true;
    }
  } else if (token == "fen") {
    std::string fen;
    std::string word;
    int field_count = 0;

    while (iss >> word) {
      if (word == "moves") {
        need_moves = true;
        break;
      }
      if (!fen.empty()) fen += " ";
      fen += word;
      field_count++;
      if (field_count >= 6) {
        // Check if next token is "moves"
        std::string next;
        if (iss >> next && next == "moves") {
          need_moves = true;
        }
        break;
      }
    }

    while (!fen.empty() && fen.back() == ' ') fen.pop_back();

    if (!position_.setFromFEN(fen)) {
      if (debug_) {
        std::cerr << "Invalid FEN: " << fen << std::endl;
      }
      return;
    }
  } else {
    std::string rest_of_line;
    std::getline(iss, rest_of_line);
    std::string full_line = token;
    if (!rest_of_line.empty()) full_line += " " + rest_of_line;

    std::size_t moves_pos = full_line.find(" moves ");
    std::string fen;

    if (moves_pos != std::string::npos) {
      fen = full_line.substr(0, moves_pos);
      need_moves = true;
      // Re-populate iss with the moves portion
      std::string moves_str = full_line.substr(moves_pos + 7);
      iss.clear();
      iss.str(moves_str);
    } else {
      fen = full_line;
    }
    while (!fen.empty() && fen[0] == ' ') fen.erase(0, 1);
    while (!fen.empty() && fen.back() == ' ') fen.pop_back();

    if (!position_.setFromFEN(fen)) {
      if (debug_) {
        std::cerr << "Invalid FEN: " << fen << std::endl;
      }
      return;
    }
  }

  game_keys_.push_back(position_.key());

  if (need_moves) {
    std::string move_str;
    while (iss >> move_str) {
      Move move = fromUci(move_str);
      if (move == kNullMove) {
        if (debug_) {
          std::cerr << "Invalid move: " << move_str << std::endl;
        }
        continue;
      }

      MoveList legal_moves;
      generateLegalMoves(position_, legal_moves);

      Move matched_move = kNullMove;
      for (std::size_t i = 0; i < legal_moves.size(); ++i) {
        const Move legal = legal_moves[i];
        if (from_square(legal) == from_square(move) &&
            to_square(legal) == to_square(move) &&
            promotion_piece(legal) == promotion_piece(move)) {
          matched_move = legal;
          break;
        }
      }

      if (matched_move != kNullMove) {
        Undo undo;
        doMove(position_, matched_move, undo);
        game_keys_.push_back(position_.key());
      } else {
        if (debug_) {
          std::cerr << "Move not legal: " << move_str << std::endl;
        }
      }
    }
  }
}

void Uci::handleGo(std::istringstream& iss) {
  waitForSearch();

  std::string token;
  int depth = 0;
  int movetime = 0;
  int wtime = 0, btime = 0, winc = 0, binc = 0, movestogo = 0;
  bool depth_set = false;
  bool infinite = false;

  while (iss >> token) {
    if (token == "depth") { iss >> depth; depth_set = true; }
    else if (token == "movetime") { iss >> movetime; }
    else if (token == "wtime") { iss >> wtime; }
    else if (token == "btime") { iss >> btime; }
    else if (token == "winc") { iss >> winc; }
    else if (token == "binc") { iss >> binc; }
    else if (token == "movestogo") { iss >> movestogo; }
    else if (token == "infinite") { infinite = true; }
  }

  SearchLimits limits;

  if (infinite) {
    limits.max_depth = 64;
  } else if (depth_set) {
    limits.max_depth = depth;
    limits.hard_time_ms = movetime > 0 ? movetime : 0;
    limits.soft_time_ms = limits.hard_time_ms;
  } else if (movetime > 0) {
    limits.max_depth = 64;
    limits.hard_time_ms = movetime;
    limits.soft_time_ms = movetime;
  } else {
    // Clock-based time management
    const bool white_to_move = (position_.side_to_move() == Color::White);
    const int remaining = white_to_move ? wtime : btime;
    const int increment = white_to_move ? winc : binc;

    if (remaining > 0) {
      limits.max_depth = 64;
      int mtg = movestogo > 0 ? movestogo : 30;
      int base = remaining / mtg + increment * 3 / 4;
      // Safety: never use more than 1/3 of remaining time
      int max_use = remaining / 3;
      if (base > max_use) base = max_use;
      // Minimum 10ms
      if (base < 10) base = 10;
      limits.soft_time_ms = base;
      limits.hard_time_ms = std::min(base * 3, remaining - 50);
      if (limits.hard_time_ms < 10) limits.hard_time_ms = 10;
    } else {
      limits.max_depth = 6;
    }
  }

  limits.game_history = &game_keys_;

  // Copy position for the search thread
  Position search_pos = position_;
  search_thread_ = std::make_unique<std::thread>([this, search_pos, limits]() mutable {
    Move best_move = search_.start(search_pos, limits);

    int eval = search_.getScore(search_pos);
    std::vector<Move> pv = search_.getPV(search_pos);

    constexpr int kScoreMate = 32000;
    constexpr int kMaxDepth = 64;
    constexpr int kMateThreshold = kScoreMate - kMaxDepth;

    if (eval >= kMateThreshold) {
      int mate_in = (kScoreMate - eval + 1) / 2;
      std::cout << "info score mate " << mate_in;
    } else if (eval <= -kMateThreshold) {
      int mate_in = (kScoreMate + eval + 1) / 2;
      std::cout << "info score mate -" << mate_in;
    } else {
      std::cout << "info score cp " << eval;
    }

    std::cout << " nodes " << search_.nodesSearched();

    if (!pv.empty()) {
      std::cout << " pv";
      for (const Move move : pv) {
        std::cout << " " << toUci(move);
      }
    }
    std::cout << std::endl;

    if (best_move != kNullMove) {
      std::cout << "bestmove " << toUci(best_move) << std::endl;
    } else {
      std::cout << "bestmove 0000" << std::endl;
    }
  });
}

void Uci::handleStop() {
  search_.stop();
  waitForSearch();
}

void Uci::handleSetOption(std::istringstream& iss) {
  std::string token;
  std::string name;
  std::string value;
  while (iss >> token) {
    if (token == "name") {
      std::getline(iss, name);
      if (!name.empty() && name[0] == ' ') {
        name.erase(0, 1);
      }
      std::size_t value_pos = name.find(" value ");
      if (value_pos != std::string::npos) {
        value = name.substr(value_pos + 7);
        name = name.substr(0, value_pos);
      }
      break;
    }
  }

  if (name == "Hash") {
    int mb = 64;
    try { mb = std::stoi(value); } catch (...) {}
    if (mb < 1) mb = 1;
    if (mb > 4096) mb = 4096;
    search_.resizeTT(static_cast<std::size_t>(mb));
    std::cout << "info string Hash set to " << mb << " MB" << std::endl;
  } else if (name == "UseNNUE") {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    const bool enable = (lower == "true" || lower == "1" || lower == "yes" || lower == "on");
    nnue::setEnabled(enable);
    std::cout << "info string NNUE " << (enable ? "enabled" : "disabled") << std::endl;
  } else if (name == "NNUEFile") {
    if (!value.empty()) {
      if (nnue::load(value)) {
        nnue::setEnabled(true);
        std::cout << "info string NNUE loaded " << value << std::endl;
      } else {
        std::cout << "info string NNUE load failed " << value << std::endl;
      }
    }
  }
}

void Uci::handleDebug(std::istringstream& iss) {
  std::string token;
  iss >> token;
  debug_ = (token == "on");
}

void Uci::handlePrint() {
  // Debug: print internal state
  std::cerr << "Position FEN: " << position_.toFEN() << std::endl;
  std::cerr << "Side to move: " 
            << (position_.side_to_move() == Color::White ? "White" : "Black") 
            << std::endl;
  std::cerr << "Zobrist key: " << position_.key() << std::endl;
  
  MoveList moves;
  generateLegalMoves(position_, moves);
  std::cerr << "Legal moves: " << moves.size() << std::endl;
  for (std::size_t i = 0; i < moves.size() && i < 10; ++i) {
    std::cerr << "  " << toUci(moves[i]) << std::endl;
  }
  if (moves.size() > 10) {
    std::cerr << "  ..." << std::endl;
  }
}

}  // namespace aurora
