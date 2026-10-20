#pragma once

#include "move.h"
#include "params.h"
#include "position.h"
#include "transposition.h"

#include <atomic>
#include <chrono>
#include <array>
#include <functional>
#include <vector>

namespace aurora {

struct SearchInfo {
  int depth = 0;
  int score = 0;
  std::uint64_t nodes = 0;
  int elapsed_ms = 0;
  Move best_move = kNullMove;
  std::vector<Move> pv;
};

using SearchCallback = std::function<void(const SearchInfo&)>;

struct SearchLimits {
  int max_depth = 64;
  std::uint64_t max_nodes = 0;
  int hard_time_ms = 0;
  int soft_time_ms = 0;
  const std::vector<ZobristKey>* game_history = nullptr;
  SearchCallback on_depth = nullptr;
};

class Search {
 public:
  Search();
  ~Search();

  Move start(Position& position, const SearchLimits& limits);

  // Backwards-compatible overload
  Move start(Position& position,
             int maxDepth = 64,
             std::uint64_t maxNodes = 0,
             int maxTimeMs = 0);

  int getScore(const Position& position) const;
  std::uint64_t nodesSearched() const { return nodes_searched_; }
  void setParams(const SearchParams& params) { params_ = params; }
  const SearchParams& params() const { return params_; }
  std::vector<Move> getPV(const Position& position) const;

  void stop() { stop_search_ = true; }
  bool stopped() const { return stop_search_.load(); }
  void resizeTT(std::size_t mb) { tt_.resize(mb); }
  void clearTT() { tt_.clear(); }

 private:
  static constexpr int kMaxDepth = 64;
  static constexpr int kQuiescenceMaxDepth = 10;
  static constexpr int kScoreMate = 32000;
  static constexpr int kScoreInfinity = 33000;

  std::array<std::array<Move, 2>, kMaxDepth> killers_;

  // Quiet history: [color][from * 64 + to]
  std::array<std::array<int, 4096>, 2> history_;

  // Countermove: [piece][to_square] -> refutation move
  std::array<std::array<Move, 64>, 12> countermove_{};

  // Capture history: [piece][to][captured_type]
  std::array<std::array<std::array<int, 6>, 64>, 12> capture_history_{};

  static constexpr int kLmrMaxDepth = 64;
  static constexpr int kLmrMaxMoves = 64;
  std::array<std::array<int, kLmrMaxMoves>, kLmrMaxDepth> lmr_table_{};
  std::array<int, kMaxDepth + 4> eval_stack_{};

  TranspositionTable tt_;
  int contempt_factor_ = 0;
  std::uint64_t nodes_searched_ = 0;

  SearchParams params_{};
  std::uint64_t max_nodes_ = 0;
  int hard_time_ms_ = 0;
  int soft_time_ms_ = 0;
  std::atomic<bool> stop_search_{false};
  std::chrono::steady_clock::time_point start_time_;

  std::vector<Move> pv_;

  int negamax(Position& position, int depth, int alpha, int beta, int ply,
              std::vector<ZobristKey>& position_history, Move prev_move = kNullMove);
  int quiescence(Position& position, int alpha, int beta, int qdepth = 0);
  bool isLegal(Position& position, Move move);
  int getMvvLvaScore(Move move) const;
  int see(const Position& position, Square from, Square to) const;
  Bitboard allAttackers(const Position& position, int sq, Bitboard occ) const;
  bool givesCheck(Position& position, Move move) const;
  bool isEndgame(const Position& position) const;
  void clearKillers();
  void updateHistory(Color color, int from, int to, int depth);
  void updateCaptureHistory(Piece piece, Square to, PieceType captured, int depth);
  bool checkTime() const;
};

}  // namespace aurora
