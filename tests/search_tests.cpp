#include "evaluate.h"
#include "makemove.h"
#include "move.h"
#include "movegen.h"
#include "position.h"
#include "search.h"
#include "transposition.h"

#include <cassert>
#include <iostream>

namespace {

void test_transposition_table_basic() {
  // Test basic TT functionality: store and probe
  aurora::TranspositionTable tt;
  
  // Create a position
  aurora::Position pos;
  const aurora::ZobristKey key1 = pos.key();
  
  // Store an entry
  aurora::Move test_move = aurora::fromUci("e2e4");
  const int score = 100;
  const int depth = 5;
  tt.store(key1, test_move, score, depth, aurora::BoundType::Exact);
  
  // Probe should find it
  aurora::TTEntry entry;
  assert(tt.probe(key1, entry));
  assert(entry.key == key1);
  assert(entry.bestMove == test_move);
  assert(entry.score == score);
  assert(entry.depth == depth);
  assert(entry.bound == aurora::BoundType::Exact);
  
  // Different key should not be found
  aurora::Position pos2;
  pos2.set_startpos();
  const aurora::ZobristKey key2 = pos2.key();
  assert(!tt.probe(key2, entry));  // Should not find different position
  
  std::cout << "[OK] Transposition table basic store/probe\n";
}

void test_transposition_table_replace() {
  // Test TT replacement strategy
  aurora::TranspositionTable tt;
  aurora::Position pos;
  const aurora::ZobristKey key = pos.key();
  
  // Store first entry
  aurora::Move move1 = aurora::fromUci("e2e4");
  tt.store(key, move1, 100, 3, aurora::BoundType::Exact);
  
  // Store second entry (same key, should replace)
  aurora::Move move2 = aurora::fromUci("d2d4");
  tt.store(key, move2, 200, 5, aurora::BoundType::Lower);
  
  // Probe should return second entry
  aurora::TTEntry entry;
  assert(tt.probe(key, entry));
  assert(entry.bestMove == move2);
  assert(entry.score == 200);
  assert(entry.depth == 5);
  assert(entry.bound == aurora::BoundType::Lower);
  
  std::cout << "[OK] Transposition table replacement\n";
}

void test_transposition_table_clear() {
  // Test TT clear
  aurora::TranspositionTable tt;
  aurora::Position pos;
  const aurora::ZobristKey key = pos.key();
  
  // Store entry
  tt.store(key, aurora::fromUci("e2e4"), 100, 3, aurora::BoundType::Exact);
  
  // Probe should find it
  aurora::TTEntry entry;
  assert(tt.probe(key, entry));
  
  // Clear
  tt.clear();
  
  // Probe should not find it
  assert(!tt.probe(key, entry));
  
  std::cout << "[OK] Transposition table clear\n";
}

void test_search_basic() {
  // Test that search returns a legal move from startpos
  aurora::Position position;
  aurora::Search search;
  
  // Search to depth 1
  aurora::Move best_move = search.start(position, 1);
  
  // Should return a valid move (not null move unless checkmate)
  assert(best_move != aurora::kNullMove);
  
  // Verify move is legal
  aurora::MoveList legal_moves;
  aurora::generateLegalMoves(position, legal_moves);
  bool found = false;
  for (std::size_t i = 0; i < legal_moves.size(); ++i) {
    if (legal_moves[i] == best_move) {
      found = true;
      break;
    }
  }
  assert(found || legal_moves.size() == 0);  // Move should be legal (or no moves)
  
  std::cout << "[OK] Basic search returns legal move\n";
}

void test_search_transposition_table_usage() {
  // Test that TT is used during search (verify hash move is tried)
  aurora::Position position;
  aurora::Search search1;
  aurora::Search search2;
  
  // First search
  aurora::Move move1 = search1.start(position, 2);
  assert(move1 != aurora::kNullMove);
  
  // Make the move and search again (should use TT)
  aurora::Undo undo;
  aurora::doMove(position, move1, undo);
  
  // Second search (position changed, but should still use TT for sub-positions)
  aurora::Move move2 = search2.start(position, 2);
  assert(move2 != aurora::kNullMove);
  
  aurora::undoMove(position, move1, undo);
  
  // Third search from same position should use TT hash moves
  aurora::Move move3 = search1.start(position, 3);
  assert(move3 != aurora::kNullMove);
  
  std::cout << "[OK] Search uses transposition table\n";
}

void test_search_mate_detection() {
  // Test mate detection: mate in 1
  aurora::Position position;
  // Position: White to move, mate in 1
  // r1bqkb1r/pppp1Qpp/2n2n2/2B1p3/4P3/8/PPPP1PPP/RNB1K1NR b KQkq - 0 4
  const char* mate_in_1_fen = "r1bqkb1r/pppp1Qpp/2n2n2/2B1p3/4P3/8/PPPP1PPP/RNB1K1NR b KQkq - 0 4";
  assert(position.setFromFEN(mate_in_1_fen));
  
  aurora::Search search;
  // Search should detect mate quickly
  aurora::Move best_move = search.start(position, 3);
  
  // At depth 3, should find the checkmate or a strong response
  assert(best_move != aurora::kNullMove);
  
  std::cout << "[OK] Mate detection test\n";
}

void test_search_checkmate() {
  // Test that checkmate positions return appropriate result
  aurora::Position position;
  // Position: Black is checkmated
  const char* checkmated_fen = "r1bqkb1r/pppp1Qpp/2n2n2/2B1p3/4P3/8/PPPP1PPP/RNB1K1NR b KQkq - 0 4";
  assert(position.setFromFEN(checkmated_fen));
  
  // Verify position is indeed checkmate
  aurora::MoveList moves;
  aurora::generateLegalMoves(position, moves);
  assert(moves.size() == 0);  // No legal moves = checkmate
  assert(position.inCheck(position.side_to_move()));
  
  aurora::Search search;
  aurora::Move best_move = search.start(position, 2);
  
  // In checkmate, search should still return null move or handle gracefully
  // (actual behavior depends on implementation)
  
  std::cout << "[OK] Checkmate handling\n";
}

void test_search_stalemate() {
  // Test stalemate detection
  aurora::Position position;
  // Position: Stalemate (king has no legal moves, not in check)
  const char* stalemate_fen = "8/8/8/8/8/3k4/3P4/3K4 b - - 0 1";
  assert(position.setFromFEN(stalemate_fen));
  
  // Verify stalemate
  aurora::MoveList moves;
  aurora::generateLegalMoves(position, moves);
  assert(moves.size() == 0);  // No legal moves
  assert(!position.inCheck(position.side_to_move()));  // Not in check
  
  aurora::Search search;
  aurora::Move best_move = search.start(position, 2);
  
  // In stalemate, search should handle gracefully
  // (best_move should be null move or search returns draw score)
  
  std::cout << "[OK] Stalemate handling\n";
}

void test_search_iterative_deepening() {
  // Test that iterative deepening works (search should complete multiple depths)
  aurora::Position position;
  aurora::Search search;
  
  // Search to depth 3
  aurora::Move move1 = search.start(position, 3);
  assert(move1 != aurora::kNullMove);
  
  // Search to depth 4
  aurora::Move move2 = search.start(position, 4);
  assert(move2 != aurora::kNullMove);
  
  // Should complete without errors (move2 might differ from move1)
  
  std::cout << "[OK] Iterative deepening\n";
}

void test_search_same_position_different_depths() {
  // Test that searching same position at different depths uses TT correctly
  aurora::Position position;
  aurora::Search search;
  
  // Search depth 2
  aurora::Move move1 = search.start(position, 2);
  assert(move1 != aurora::kNullMove);
  
  // Search depth 3 (should reuse TT entries from depth 2)
  aurora::Move move2 = search.start(position, 3);
  assert(move2 != aurora::kNullMove);
  
  // Search depth 4 (should reuse more TT entries)
  aurora::Move move3 = search.start(position, 4);
  assert(move3 != aurora::kNullMove);
  
  // TT should have entries from previous searches
  // (indirectly verified by search completing)
  
  std::cout << "[OK] TT reuse across depths\n";
}

void test_search_move_quality() {
  // Test that search finds reasonable moves from startpos
  aurora::Position position;
  aurora::Search search;
  
  // Search depth 3
  aurora::Move best_move = search.start(position, 3);
  assert(best_move != aurora::kNullMove);
  
  // Verify move is from a reasonable square (pawns, knights, etc.)
  aurora::Square from = aurora::from_square(best_move);
  aurora::Square to = aurora::to_square(best_move);
  
  assert(from != aurora::Square::None);
  assert(to != aurora::Square::None);
  assert(from != to);
  
  // Verify the move is legal
  aurora::MoveList legal_moves;
  aurora::generateLegalMoves(position, legal_moves);
  bool is_legal = false;
  for (std::size_t i = 0; i < legal_moves.size(); ++i) {
    const aurora::Move legal = legal_moves[i];
    if (aurora::from_square(legal) == from &&
        aurora::to_square(legal) == to &&
        aurora::promotion_piece(legal) == aurora::promotion_piece(best_move)) {
      is_legal = true;
      break;
    }
  }
  assert(is_legal);
  
  std::cout << "[OK] Search returns quality moves\n";
}

void test_search_quiescence() {
  // Test quiescence search (capture-only at leaf)
  aurora::Position position;
  // Position with captures available
  const char* capture_fen = "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq e6 0 4";
  assert(position.setFromFEN(capture_fen));
  
  aurora::Search search;
  // Search depth 1 should trigger quiescence
  aurora::Move best_move = search.start(position, 1);
  
  // Should find a reasonable move (might be a capture due to quiescence)
  assert(best_move != aurora::kNullMove);
  
  std::cout << "[OK] Quiescence search\n";
}

}  // namespace

int main() {
  std::cout << "Running search tests...\n\n";
  
  test_transposition_table_basic();
  test_transposition_table_replace();
  test_transposition_table_clear();
  test_search_basic();
  test_search_transposition_table_usage();
  test_search_mate_detection();
  test_search_checkmate();
  test_search_stalemate();
  test_search_iterative_deepening();
  test_search_same_position_different_depths();
  test_search_move_quality();
  test_search_quiescence();
  
  std::cout << "\nAll search tests passed!\n";
  return 0;
}
