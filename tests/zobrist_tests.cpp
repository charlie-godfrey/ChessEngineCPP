#include "makemove.h"
#include "movegen.h"
#include "position.h"
#include "zobrist.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void verify_zobrist(const aurora::Position& position, const std::string& context) {
  // Verify that incremental key matches full recomputation
  aurora::Position mutable_pos = position;
  const aurora::ZobristKey incremental_key = position.key();
  const aurora::ZobristKey recomputed_key = aurora::zobrist::computeZobrist(mutable_pos);
  
  if (incremental_key != recomputed_key) {
    std::cerr << "[FAIL] Zobrist key mismatch in " << context << ": "
              << "incremental=" << incremental_key << ", "
              << "recomputed=" << recomputed_key << "\n";
    std::abort();
  }
}

void test_zobrist_move_sequence() {
  aurora::Position position;
  aurora::MoveList moves;
  
  // Test 1: Initial position
  verify_zobrist(position, "initial position");
  
  // Test 2: Simple quiet moves
  aurora::generateLegalMoves(position, moves);
  if (moves.size() > 0) {
    aurora::Undo undo;
    aurora::doMove(position, moves[0], undo);
    verify_zobrist(position, "after quiet move");
    aurora::undoMove(position, moves[0], undo);
    verify_zobrist(position, "after undoing quiet move");
  }
  
  std::cout << "[OK] Quiet move zobrist preservation\n";
}

void test_zobrist_captures() {
  // Setup position with capture opportunities
  aurora::Position position;
  const char* fen = "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4";
  assert(position.setFromFEN(fen));
  verify_zobrist(position, "capture test initial");
  
  aurora::MoveList moves;
  aurora::generateLegalMoves(position, moves);
  
  // Find a capture move
  for (std::size_t i = 0; i < moves.size(); ++i) {
    const aurora::Piece captured = aurora::captured_piece(moves[i]);
    if (captured != aurora::Piece::None) {
      aurora::Undo undo;
      aurora::doMove(position, moves[i], undo);
      verify_zobrist(position, "after capture move");
      aurora::undoMove(position, moves[i], undo);
      verify_zobrist(position, "after undoing capture move");
      break;
    }
  }
  
  std::cout << "[OK] Capture move zobrist preservation\n";
}

void test_zobrist_castling() {
  // Setup position with castling rights
  aurora::Position position;
  const char* fen = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1";
  assert(position.setFromFEN(fen));
  verify_zobrist(position, "castling test initial");
  
  aurora::MoveList moves;
  aurora::generateLegalMoves(position, moves);
  
  // Find castling moves
  bool found_white_ks_castle = false;
  bool found_white_qs_castle = false;
  
  for (std::size_t i = 0; i < moves.size(); ++i) {
    const aurora::MoveFlags flags = aurora::move_flags(moves[i]);
    if (aurora::any_flag(flags, aurora::MoveFlags::Castle)) {
      aurora::Undo undo;
      aurora::doMove(position, moves[i], undo);
      verify_zobrist(position, "after castling move");
      aurora::undoMove(position, moves[i], undo);
      verify_zobrist(position, "after undoing castling move");
      
      const aurora::Square from = aurora::from_square(moves[i]);
      const aurora::Square to = aurora::to_square(moves[i]);
      if (from == aurora::Square::E1 && to == aurora::Square::G1) {
        found_white_ks_castle = true;
      } else if (from == aurora::Square::E1 && to == aurora::Square::C1) {
        found_white_qs_castle = true;
      }
    }
  }
  
  // Test black castling
  position.setFromFEN("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1");
  aurora::generateLegalMoves(position, moves);
  
  for (std::size_t i = 0; i < moves.size(); ++i) {
    const aurora::MoveFlags flags = aurora::move_flags(moves[i]);
    if (aurora::any_flag(flags, aurora::MoveFlags::Castle)) {
      aurora::Undo undo;
      aurora::doMove(position, moves[i], undo);
      verify_zobrist(position, "after black castling move");
      aurora::undoMove(position, moves[i], undo);
      verify_zobrist(position, "after undoing black castling move");
    }
  }
  
  std::cout << "[OK] Castling move zobrist preservation\n";
}

void test_zobrist_en_passant() {
  // Setup position with en passant opportunity
  aurora::Position position;
  const char* fen = "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3";
  assert(position.setFromFEN(fen));
  verify_zobrist(position, "en passant test initial");
  
  aurora::MoveList moves;
  aurora::generateLegalMoves(position, moves);
  
  // Find en passant move
  bool found_ep = false;
  for (std::size_t i = 0; i < moves.size(); ++i) {
    const aurora::MoveFlags flags = aurora::move_flags(moves[i]);
    if (aurora::any_flag(flags, aurora::MoveFlags::EnPassant)) {
      aurora::Undo undo;
      aurora::doMove(position, moves[i], undo);
      verify_zobrist(position, "after en passant move");
      aurora::undoMove(position, moves[i], undo);
      verify_zobrist(position, "after undoing en passant move");
      found_ep = true;
      break;
    }
  }
  
  if (!found_ep) {
    std::cerr << "[WARN] No en passant move found in test position\n";
  }
  
  // Test setting EP square via double push
  position.setFromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  aurora::generateLegalMoves(position, moves);
  
  // Find a double pawn push
  for (std::size_t i = 0; i < moves.size(); ++i) {
    const aurora::MoveFlags flags = aurora::move_flags(moves[i]);
    if (aurora::any_flag(flags, aurora::MoveFlags::DoublePush)) {
      aurora::Undo undo;
      aurora::doMove(position, moves[i], undo);
      verify_zobrist(position, "after double push (setting EP)");
      aurora::undoMove(position, moves[i], undo);
      verify_zobrist(position, "after undoing double push");
      break;
    }
  }
  
  std::cout << "[OK] En passant move zobrist preservation\n";
}

void test_zobrist_promotions() {
  // Setup position with promotion opportunities
  aurora::Position position;
  const char* fen = "8/P7/8/8/8/8/8/8 w - - 0 1";
  assert(position.setFromFEN(fen));
  verify_zobrist(position, "promotion test initial");
  
  aurora::MoveList moves;
  aurora::generateLegalMoves(position, moves);
  
  // Find promotion moves (all 4 types: Q, R, B, N)
  bool found_promotion = false;
  for (std::size_t i = 0; i < moves.size(); ++i) {
    const aurora::PieceType promo = aurora::promotion_piece(moves[i]);
    if (promo != aurora::PieceType::None) {
      aurora::Undo undo;
      aurora::doMove(position, moves[i], undo);
      verify_zobrist(position, "after promotion move");
      aurora::undoMove(position, moves[i], undo);
      verify_zobrist(position, "after undoing promotion move");
      found_promotion = true;
    }
  }
  
  if (!found_promotion) {
    std::cerr << "[WARN] No promotion move found in test position\n";
  } else {
    std::cout << "[OK] Promotion move zobrist preservation\n";
  }
  
  // Test capture promotion
  position.setFromFEN("n7/P7/8/8/8/8/8/8 w - - 0 1");
  aurora::generateLegalMoves(position, moves);
  
  for (std::size_t i = 0; i < moves.size(); ++i) {
    const aurora::PieceType promo = aurora::promotion_piece(moves[i]);
    const aurora::Piece captured = aurora::captured_piece(moves[i]);
    if (promo != aurora::PieceType::None && captured != aurora::Piece::None) {
      aurora::Undo undo;
      aurora::doMove(position, moves[i], undo);
      verify_zobrist(position, "after capture-promotion move");
      aurora::undoMove(position, moves[i], undo);
      verify_zobrist(position, "after undoing capture-promotion move");
      break;
    }
  }
  
  std::cout << "[OK] Capture-promotion move zobrist preservation\n";
}

void test_zobrist_random_sequences() {
  // Test that zobrist is preserved through random move sequences
  aurora::Position position;
  
  // Random seed for reproducibility (but different from zobrist seed)
  std::srand(42);
  
  for (int iteration = 0; iteration < 10; ++iteration) {
    // Randomly make moves and verify zobrist at each step
    std::vector<std::pair<aurora::Move, aurora::Undo>> move_history;
    
    for (int depth = 0; depth < 20; ++depth) {
      verify_zobrist(position, "random sequence check");
      
      aurora::MoveList moves;
      aurora::generateLegalMoves(position, moves);
      
      if (moves.size() == 0) {
        break;  // Checkmate or stalemate
      }
      
      // Pick a random move
      const int move_idx = std::rand() % moves.size();
      aurora::Undo undo;
      aurora::doMove(position, moves[move_idx], undo);
      move_history.push_back({moves[move_idx], undo});
      
      verify_zobrist(position, "random sequence after move");
    }
    
    // Undo all moves and verify we return to original state
    for (auto it = move_history.rbegin(); it != move_history.rend(); ++it) {
      aurora::undoMove(position, it->first, it->second);
      verify_zobrist(position, "random sequence after undo");
    }
    
    // Reset for next iteration
    position.setFromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  }
  
  std::cout << "[OK] Random move sequence zobrist preservation\n";
}

void test_zobrist_castling_rights_loss() {
  // Test that losing castling rights updates zobrist correctly
  aurora::Position position;
  const char* fen = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1";
  assert(position.setFromFEN(fen));
  verify_zobrist(position, "castling rights test initial");
  
  // Move king - should lose castling rights
  aurora::MoveList moves;
  aurora::generateLegalMoves(position, moves);
  
  // Find king move (not castling)
  for (std::size_t i = 0; i < moves.size(); ++i) {
    const aurora::Square from = aurora::from_square(moves[i]);
    if (from == aurora::Square::E1) {
      const aurora::MoveFlags flags = aurora::move_flags(moves[i]);
      if (!aurora::any_flag(flags, aurora::MoveFlags::Castle)) {
        aurora::Undo undo;
        aurora::doMove(position, moves[i], undo);
        verify_zobrist(position, "after king move (losing castling rights)");
        aurora::undoMove(position, moves[i], undo);
        verify_zobrist(position, "after undoing king move");
        break;
      }
    }
  }
  
  // Move rook - should lose that side's castling rights
  aurora::generateLegalMoves(position, moves);
  for (std::size_t i = 0; i < moves.size(); ++i) {
    const aurora::Square from = aurora::from_square(moves[i]);
    if (from == aurora::Square::H1 || from == aurora::Square::A1) {
      aurora::Undo undo;
      aurora::doMove(position, moves[i], undo);
      verify_zobrist(position, "after rook move (losing castling rights)");
      aurora::undoMove(position, moves[i], undo);
      verify_zobrist(position, "after undoing rook move");
      break;
    }
  }
  
  std::cout << "[OK] Castling rights loss zobrist preservation\n";
}

}  // namespace

int main() {
  std::cout << "Running zobrist hashing tests...\n\n";
  
  test_zobrist_move_sequence();
  test_zobrist_captures();
  test_zobrist_castling();
  test_zobrist_en_passant();
  test_zobrist_promotions();
  test_zobrist_castling_rights_loss();
  test_zobrist_random_sequences();
  
  std::cout << "\nAll zobrist tests passed!\n";
  return 0;
}
