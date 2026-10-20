#include "play.h"

#include "makemove.h"
#include "move.h"
#include "movegen.h"
#include "nnue.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace aurora {

PlayMode::PlayMode() : position_(), search_() {
  position_.set_startpos();
  game_keys_.push_back(position_.key());
}

void PlayMode::printBoard() const {
  std::cout << "\n";
  std::cout << "  a b c d e f g h\n";
  
  for (int rank = 7; rank >= 0; --rank) {
    std::cout << (rank + 1) << " ";
    for (int file = 0; file < 8; ++file) {
      const Square sq = make_square(static_cast<File>(file), static_cast<Rank>(rank));
      const Piece piece = position_.piece_on(sq);
      
      if (piece == Piece::None) {
        std::cout << ". ";
      } else {
        const Color color = color_of(piece);
        const PieceType type = piece_type_of(piece);
        
        char symbol = '?';
        switch (type) {
          case PieceType::Pawn:   symbol = 'P'; break;
          case PieceType::Knight: symbol = 'N'; break;
          case PieceType::Bishop: symbol = 'B'; break;
          case PieceType::Rook:   symbol = 'R'; break;
          case PieceType::Queen:  symbol = 'Q'; break;
          case PieceType::King:   symbol = 'K'; break;
          default: break;
        }
        
        if (color == Color::Black) {
          symbol = static_cast<char>(std::tolower(symbol));
        }
        
        std::cout << symbol << " ";
      }
    }
    std::cout << (rank + 1) << "\n";
  }
  
  std::cout << "  a b c d e f g h\n";
  std::cout << "\n";
  
  // Print side to move
  if (position_.side_to_move() == Color::White) {
    std::cout << "White to move\n";
  } else {
    std::cout << "Black to move\n";
  }
  
  // Print game status
  Position temp_pos = position_;  // Non-const copy for move generation
  MoveList legal_moves;
  generateLegalMoves(temp_pos, legal_moves);
  
  if (legal_moves.size() == 0) {
    if (position_.inCheck(position_.side_to_move())) {
      std::cout << "Checkmate!\n";
    } else {
      std::cout << "Stalemate!\n";
    }
  } else if (position_.inCheck(position_.side_to_move())) {
    std::cout << "Check!\n";
  }
}

void PlayMode::printMovePrompt() const {
  if (position_.side_to_move() == Color::White) {
    std::cout << "White (you): ";
  } else {
    std::cout << "Black (you): ";
  }
}

bool PlayMode::handleUserMove(const std::string& move_str) {
  if (move_str.empty()) {
    return false;
  }
  
  // Handle special commands
  if (move_str == "quit" || move_str == "q") {
    return true;  // Signal to quit
  }
  
  if (move_str == "undo" || move_str == "u") {
    std::cout << "Undo not implemented yet\n";
    return false;
  }
  
  if (move_str == "depth" || move_str == "d") {
    std::cout << "Current search depth: " << search_depth_ << "\n";
    std::cout << "Enter new depth (1-30): ";
    int new_depth;
    if (std::cin >> new_depth && new_depth >= 1 && new_depth <= 30) {
      search_depth_ = new_depth;
      std::cout << "Search depth set to " << search_depth_ << "\n";
    } else {
      std::cout << "Invalid depth\n";
    }
    std::cin.ignore();  // Clear newline
    return false;
  }
  
  if (move_str == "help" || move_str == "h" || move_str == "?") {
    std::cout << "Commands:\n";
    std::cout << "  <move>    - Enter move in UCI notation (e.g., e2e4, e7e5)\n";
    std::cout << "  depth/d   - Change engine search depth (1-30)\n";
    std::cout << "  nnue <path> - Enable NNUE and load file\n";
    std::cout << "  nnue off  - Disable NNUE\n";
    std::cout << "  moves     - Show all legal moves\n";
    std::cout << "  quit/q    - Quit game\n";
    std::cout << "  help/h/?  - Show this help\n";
    return false;
  }

  if (move_str.rfind("nnue", 0) == 0) {
    std::istringstream iss(move_str);
    std::string cmd;
    std::string arg;
    iss >> cmd;
    if (!(iss >> arg)) {
      std::cout << "Usage: nnue <path> | nnue off\n";
      return false;
    }
    if (arg == "off") {
      nnue::setEnabled(false);
      std::cout << "NNUE disabled\n";
      return false;
    }
    nnue_path_ = arg;
    if (nnue::load(nnue_path_)) {
      nnue::setEnabled(true);
      std::cout << "NNUE enabled: " << nnue_path_ << "\n";
    } else {
      nnue::setEnabled(false);
      std::cout << "Failed to load NNUE: " << nnue_path_ << "\n";
    }
    return false;
  }
  
  if (move_str == "moves" || move_str == "m") {
    Position temp_pos = position_;
    MoveList legal_moves;
    generateLegalMoves(temp_pos, legal_moves);
    std::cout << "Legal moves (" << legal_moves.size() << "):\n";
    for (std::size_t i = 0; i < legal_moves.size(); ++i) {
      std::cout << "  " << toUci(legal_moves[i]);
      if ((i + 1) % 8 == 0) std::cout << "\n";
    }
    if (legal_moves.size() % 8 != 0) std::cout << "\n";
    return false;
  }
  
  // Parse move
  Move move = fromUci(move_str);
  if (move == kNullMove) {
    std::cout << "Invalid move format. Use UCI notation (e.g., e2e4)\n";
    return false;
  }
  
  // Find matching legal move
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
  
  if (matched_move == kNullMove) {
    std::cout << "Illegal move. Legal moves:\n";
    for (std::size_t i = 0; i < legal_moves.size() && i < 10; ++i) {
      std::cout << "  " << toUci(legal_moves[i]);
      if ((i + 1) % 8 == 0) std::cout << "\n";
    }
    if (legal_moves.size() > 10) {
      std::cout << "  ... (" << legal_moves.size() << " total)\n";
    } else {
      std::cout << "\n";
    }
    return false;
  }
  
  // Make the move
  Undo undo;
  doMove(position_, matched_move, undo);
  game_keys_.push_back(position_.key());
  return false;
}

void PlayMode::makeEngineMove() {
  std::cout << "Engine thinking...\n";
  
  const Color engine_side = position_.side_to_move();
  constexpr int kMateThreshold = 32000 - 64;

  SearchLimits limits;
  limits.max_depth = search_depth_;
  limits.game_history = &game_keys_;
  limits.on_depth = [&](const SearchInfo& info) {
    int display_score = info.score;
    if (engine_side == Color::Black) display_score = -display_score;

    std::cout << "  depth " << std::setw(2) << info.depth << "  ";

    if (display_score >= kMateThreshold) {
      int mate_in = (32000 - display_score + 1) / 2;
      std::cout << "mate " << mate_in;
    } else if (display_score <= -kMateThreshold) {
      int mate_in = (32000 + display_score + 1) / 2;
      std::cout << "mate -" << mate_in;
    } else {
      std::cout << std::fixed << std::setprecision(2)
                << (display_score > 0 ? "+" : "") << (display_score / 100.0);
    }

    std::cout << "  " << toUci(info.best_move);

    if (info.pv.size() > 1) {
      std::cout << " (";
      for (std::size_t i = 0; i < info.pv.size() && i < 6; ++i) {
        if (i > 0) std::cout << " ";
        std::cout << toUci(info.pv[i]);
      }
      if (info.pv.size() > 6) std::cout << " ...";
      std::cout << ")";
    }

    std::cout << "  " << info.nodes << " nodes";
    if (info.elapsed_ms > 0) {
      std::cout << "  " << info.elapsed_ms << "ms";
    }
    std::cout << "\n";
  };

  Move best_move = search_.start(position_, limits);
  
  if (best_move == kNullMove) {
    std::cout << "Engine has no legal moves\n";
    return;
  }
  
  int score = search_.getScore(position_);
  if (engine_side == Color::Black) {
    score = -score;
  }

  Undo undo;
  doMove(position_, best_move, undo);
  game_keys_.push_back(position_.key());
  
  std::cout << "Engine plays: " << toUci(best_move) << "\n";

  if (score >= kMateThreshold) {
    int mate_in = (32000 - score + 1) / 2;
    std::cout << "Evaluation: mate in " << mate_in << "\n";
  } else if (score <= -kMateThreshold) {
    int mate_in = (32000 + score + 1) / 2;
    std::cout << "Evaluation: mated in " << mate_in << "\n";
  } else {
    std::cout << "Evaluation: " << (score > 0 ? "+" : "")
              << std::fixed << std::setprecision(2) << (score / 100.0) << " pawns\n";
  }
}

void PlayMode::printGameResult() const {
  // Create a non-const copy for move generation
  Position temp_pos = position_;
  MoveList legal_moves;
  generateLegalMoves(temp_pos, legal_moves);
  
  if (legal_moves.size() == 0) {
    if (position_.inCheck(position_.side_to_move())) {
      // Checkmate
      if (position_.side_to_move() == Color::White) {
        std::cout << "\n*** Black wins by checkmate! ***\n";
      } else {
        std::cout << "\n*** White wins by checkmate! ***\n";
      }
    } else {
      std::cout << "\n*** Draw by stalemate! ***\n";
    }
  } else if (position_.halfmove_clock() >= 100) {
    std::cout << "\n*** Draw by 50-move rule! ***\n";
  }
}

bool PlayMode::isGameOver() const {
  // Create a non-const copy for move generation
  Position temp_pos = position_;
  MoveList legal_moves;
  generateLegalMoves(temp_pos, legal_moves);
  
  if (legal_moves.size() == 0) {
    return true;
  }
  
  if (position_.halfmove_clock() >= 100) {
    return true;
  }
  
  return false;
}

void PlayMode::run() {
  std::cout << "=== Aurora Chess Engine - Play Mode ===\n";
  std::cout << "Commands:\n";
  std::cout << "  <move>    - Enter move in UCI notation (e.g., e2e4, e7e5)\n";
  std::cout << "  depth/d   - Change engine search depth (1-30, default: 8)\n";
  std::cout << "  moves/m   - Show all legal moves\n";
  std::cout << "  quit/q    - Quit game\n";
  std::cout << "  help/h/?  - Show help\n";
  std::cout << "\n";
  
  // Ask which color user wants to play
  std::cout << "Play as (w)hite or (b)lack? [w]: ";
  std::string color_choice;
  std::getline(std::cin, color_choice);
  user_plays_white_ = (color_choice.empty() || color_choice[0] == 'w' || color_choice[0] == 'W');
  
  if (user_plays_white_) {
    std::cout << "You are playing White. Engine is Black.\n";
  } else {
    std::cout << "You are playing Black. Engine is White.\n";
    // Engine moves first
    printBoard();
    makeEngineMove();
  }
  
  // Main game loop
  while (true) {
    printBoard();
    
    if (isGameOver()) {
      printGameResult();
      break;
    }
    
    // Check if it's user's turn
    bool is_user_turn = (user_plays_white_ && position_.side_to_move() == Color::White) ||
                        (!user_plays_white_ && position_.side_to_move() == Color::Black);
    
    if (is_user_turn) {
      printMovePrompt();
      std::string move_str;
      std::getline(std::cin, move_str);
      
      // Trim whitespace
      move_str.erase(0, move_str.find_first_not_of(" \t\n\r"));
      move_str.erase(move_str.find_last_not_of(" \t\n\r") + 1);
      
      if (handleUserMove(move_str)) {
        // User wants to quit
        break;
      }
    } else {
      // Engine's turn
      makeEngineMove();
    }
    
    if (isGameOver()) {
      printBoard();
      printGameResult();
      break;
    }
  }
  
  std::cout << "\nGame over. Thanks for playing!\n";
}

}  // namespace aurora
