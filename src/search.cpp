#include "search.h"

#include "attacks.h"
#include "evaluate.h"
#include "makemove.h"
#include "magic.h"
#include "movegen.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <vector>
namespace {
constexpr int kMateScore = 32000;
constexpr int kMaxSearchPly = 64;

inline int eval_for_side(aurora::Position& position) {
  const int raw = aurora::evaluate(position);
  return (position.side_to_move() == aurora::Color::White) ? raw : -raw;
}

int score_from_tt(int score, int ply) {
  if (score >= kMateScore - kMaxSearchPly) return score - ply;
  if (score <= -kMateScore + kMaxSearchPly) return score + ply;
  return score;
}

int score_to_tt(int score, int ply) {
  if (score >= kMateScore - kMaxSearchPly) return score + ply;
  if (score <= -kMateScore + kMaxSearchPly) return score - ply;
  return score;
}
}  // namespace


namespace aurora {

Search::Search() {
  for (int d = 0; d < kLmrMaxDepth; ++d) {
    for (int m = 0; m < kLmrMaxMoves; ++m) {
      if (d == 0 || m == 0) {
        lmr_table_[d][m] = 0;
      } else {
        lmr_table_[d][m] = static_cast<int>(0.75 + std::log(d) * std::log(m) / 2.25);
      }
    }
  }
}
Search::~Search() = default;

void Search::clearKillers() {
  for (int i = 0; i < kMaxDepth; ++i) {
    killers_[i][0] = kNullMove;
    killers_[i][1] = kNullMove;
  }
  for (int color = 0; color < 2; ++color) {
    for (auto& v : history_[color]) v /= 2;
  }
  for (auto& a : capture_history_) {
    for (auto& b : a) {
      for (auto& v : b) v /= 2;
    }
  }
  for (auto& a : countermove_) {
    for (auto& m : a) m = kNullMove;
  }
}

void Search::updateHistory(Color color, int from, int to, int bonus) {
  static constexpr int kHistoryMax = 8192;
  int& entry = history_[static_cast<int>(color)][from * 64 + to];
  entry = std::clamp(entry + bonus, -kHistoryMax, kHistoryMax);
}

void Search::updateCaptureHistory(Piece piece, Square to, PieceType captured, int depth) {
  static constexpr int kCapHistMax = 8192;
  if (piece == Piece::None || to == Square::None || captured == PieceType::None) return;
  int& entry = capture_history_[static_cast<int>(piece)][square_index(to)][static_cast<int>(captured)];
  entry += depth * depth;
  if (entry > kCapHistMax) entry = kCapHistMax;
}

int Search::getMvvLvaScore(Move move) const {
  // MVV-LVA: Most Valuable Victim - Least Valuable Attacker
  // Return higher scores for better captures
  
  static const int PieceValues[] = {
    100,   // Pawn
    320,   // Knight
    330,   // Bishop
    500,   // Rook
    900,   // Queen
    20000  // King
  };

  const Piece captured = captured_piece(move);
  const Piece moved = moved_piece(move);
  
  if (captured == Piece::None) {
    return 0;  // Not a capture
  }

  const PieceType victim_type = piece_type_of(captured);
  const PieceType attacker_type = piece_type_of(moved);

  // Victim value * 100 - attacker value
  const int victim_value = PieceValues[static_cast<int>(victim_type)];
  const int attacker_value = PieceValues[static_cast<int>(attacker_type)];

  return victim_value * 100 - attacker_value;
}

bool Search::isLegal(Position& position, Move move) {
  // Quick legality check: make move and check if original side's king is in check
  const Color original_side = position.side_to_move();
  Undo undo;
  doMove(position, move, undo);
  
  // After doMove, side_to_move has flipped, so check the original side
  const bool illegal = position.inCheck(original_side);
  
  undoMove(position, move, undo);
  return !illegal;
}

Bitboard Search::allAttackers(const Position& position, int sq, Bitboard occ) const {
  using namespace attacks;
  Bitboard att = 0;
  att |= PawnAtt[0][sq] & position.pieces(Color::Black, PieceType::Pawn);
  att |= PawnAtt[1][sq] & position.pieces(Color::White, PieceType::Pawn);
  att |= KnightAtt[sq] & (position.pieces(Color::White, PieceType::Knight) |
                           position.pieces(Color::Black, PieceType::Knight));
  att |= KingAtt[sq] & (position.pieces(Color::White, PieceType::King) |
                         position.pieces(Color::Black, PieceType::King));
  att |= magic::bishopAttacks(sq, occ) &
         (position.pieces(Color::White, PieceType::Bishop) |
          position.pieces(Color::Black, PieceType::Bishop) |
          position.pieces(Color::White, PieceType::Queen) |
          position.pieces(Color::Black, PieceType::Queen));
  att |= magic::rookAttacks(sq, occ) &
         (position.pieces(Color::White, PieceType::Rook) |
          position.pieces(Color::Black, PieceType::Rook) |
          position.pieces(Color::White, PieceType::Queen) |
          position.pieces(Color::Black, PieceType::Queen));
  return att & occ;
}

int Search::see(const Position& position, Square from, Square to) const {
  static constexpr int SeeValue[] = {100, 320, 330, 500, 900, 20000};

  const Piece target = position.piece_on(to);
  if (target == Piece::None) return 0;
  const Piece attacker = position.piece_on(from);
  if (attacker == Piece::None) return 0;

  const int sq = square_index(to);
  int gain[32];
  int d = 0;
  Bitboard occ = position.occ_all();
  Bitboard from_bb = square_bb(from);
  Color stm = color_of(attacker);

  gain[d] = SeeValue[static_cast<int>(piece_type_of(target))];
  PieceType current_pt = piece_type_of(attacker);

  do {
    d++;
    gain[d] = SeeValue[static_cast<int>(current_pt)] - gain[d - 1];

    if (std::max(-gain[d - 1], gain[d]) < 0) break;

    occ ^= from_bb;
    stm = opposite(stm);

    Bitboard att = allAttackers(position, sq, occ);
    Bitboard stm_att = att & position.occ(stm);
    if (!stm_att) break;

    // Pick least valuable attacker
    from_bb = 0;
    for (int pt = 0; pt < 6; ++pt) {
      from_bb = stm_att & position.pieces(stm, static_cast<PieceType>(pt));
      if (from_bb) {
        current_pt = static_cast<PieceType>(pt);
        from_bb = from_bb & (~from_bb + 1); // isolate LSB
        break;
      }
    }
  } while (from_bb);

  while (--d) {
    gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
  }

  return gain[0];
}

bool Search::givesCheck(Position& position, Move move) const {
  // Check if move gives check by making the move and checking
  Undo undo;
  doMove(position, move, undo);
  
  const Color them = position.side_to_move();  // After move, side flipped
  const bool in_check = position.inCheck(them);
  
  undoMove(position, move, undo);
  return in_check;
}

bool Search::isEndgame(const Position& position) const {
  // Simple endgame detection: count non-pawn, non-king pieces
  // If total is <= 6, consider it endgame (avoid null move zugzwang)
  int piece_count = 0;
  for (int color = 0; color < 2; ++color) {
    const Bitboard knights = position.pieces(static_cast<Color>(color), PieceType::Knight);
    const Bitboard bishops = position.pieces(static_cast<Color>(color), PieceType::Bishop);
    const Bitboard rooks = position.pieces(static_cast<Color>(color), PieceType::Rook);
    const Bitboard queens = position.pieces(static_cast<Color>(color), PieceType::Queen);
    piece_count += popcount(knights) + popcount(bishops) + popcount(rooks) + popcount(queens);
  }
  return piece_count <= 6;  // Endgame threshold
}

int Search::quiescence(Position& position, int alpha, int beta, int qdepth,
                       std::vector<ZobristKey>& position_history, int ply) {
  ++nodes_searched_;
  if (stop_search_) {
    return eval_for_side(position);
  }

  const ZobristKey qkey = position.key();
  const bool current_is_last = !position_history.empty() &&
                               position_history.back() == qkey;
  const std::size_t history_end =
      position_history.size() - (current_is_last ? 1 : 0);
  int previous_occurrences = 0;
  for (std::size_t i = 0; i < history_end; ++i) {
    if (position_history[i] == qkey) {
      ++previous_occurrences;
    }
  }
  if (previous_occurrences >= 2) {
    return contempt_factor_;
  }

  struct HistoryGuard {
    std::vector<ZobristKey>& history;
    bool added;
    ~HistoryGuard() {
      if (added) history.pop_back();
    }
  };
  if (!current_is_last) {
    position_history.push_back(qkey);
  }
  HistoryGuard history_guard{position_history, !current_is_last};

  const bool in_check = position.inCheck(position.side_to_move());
  if (!in_check && position.halfmove_clock() >= 100) {
    return contempt_factor_;
  }

  MoveList captures;
  if (!in_check) {
    generateCaptures(position, captures);
    bool has_legal_capture = false;
    for (std::size_t i = 0; i < captures.size(); ++i) {
      if (isLegal(position, captures[i])) {
        has_legal_capture = true;
        break;
      }
    }
    if (!has_legal_capture) {
      MoveList legal_moves;
      generateLegalMoves(position, legal_moves);
      if (legal_moves.size() == 0) {
        return contempt_factor_;
      }
    }
  }

  // Qsearch entries are selective and must not be treated as exact.
  constexpr int kQsearchDepth = -1;
  TTEntry qtt;
  if (!in_check && tt_.probe(qkey, qtt)) {
    const int tt_score = score_from_tt(qtt.score, ply);
    if (qtt.bound == BoundType::Exact && qtt.depth >= 0) return tt_score;
    if (qtt.bound == BoundType::Lower && tt_score >= beta) return tt_score;
    if (qtt.bound == BoundType::Upper && tt_score <= alpha) return tt_score;
  }

  if (in_check) {
    MoveList evasions;
    generateLegalMoves(position, evasions);
    if (evasions.size() == 0) {
      return -kScoreMate + ply;
    }
    if (position.halfmove_clock() >= 100) {
      return contempt_factor_;
    }

    int best_score = -kScoreInfinity;
    std::vector<std::pair<Move, int>> scored_evasions;
    for (std::size_t i = 0; i < evasions.size(); ++i) {
      const Move move = evasions[i];
      const int move_score = captured_piece(move) == Piece::None
                                 ? 0
                                 : getMvvLvaScore(move) + 1000000;
      scored_evasions.push_back({move, move_score});
    }
    std::sort(scored_evasions.begin(), scored_evasions.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    for (const auto& [move, _] : scored_evasions) {
      Undo undo;
      doMove(position, move, undo);
      const int next_qdepth = qdepth + 1 +
                              (qdepth < params_.quiescence_max_depth -
                                           params_.q_extension_margin
                                   ? params_.q_check_extension
                                   : 0);
      const int score = -quiescence(position, -beta, -alpha, next_qdepth,
                                    position_history, ply + 1);
      undoMove(position, move, undo);

      best_score = std::max(best_score, score);
      alpha = std::max(alpha, score);
      if (score >= beta) {
        return score;
      }
    }
    return best_score;
  }

  const int stand_pat = eval_for_side(position);
  if (stand_pat >= beta) return stand_pat;
  if (alpha < stand_pat) alpha = stand_pat;

  if (qdepth >= params_.quiescence_max_depth) {
    return stand_pat;
  }

  std::vector<std::pair<Move, int>> scored_captures;
  static const int PieceValues[] = {100, 320, 330, 500, 900, 20000};

  for (std::size_t i = 0; i < captures.size(); ++i) {
    const Move move = captures[i];
    if (!isLegal(position, move)) {
      continue;
    }

    const Square from = from_square(move);
    const Square to = to_square(move);
    const int see_score = see(position, from, to);

    const Piece captured = captured_piece(move);
    if (captured != Piece::None) {
      const PieceType captured_type = piece_type_of(captured);
      const int capture_value = PieceValues[static_cast<int>(captured_type)];
      if (stand_pat + capture_value + params_.delta_margin < alpha) {
        continue;
      }
    }

    if (see_score < 0 && promotion_piece(move) == PieceType::None) {
      continue;
    }

    const int mvv_lva = getMvvLvaScore(move);
    const int score = see_score * 10000 + mvv_lva;
    scored_captures.push_back({move, score});
  }

  std::sort(scored_captures.begin(), scored_captures.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  int best_score = stand_pat;
  for (const auto& [move, _] : scored_captures) {
    Undo undo;
    doMove(position, move, undo);

    int search_qdepth = qdepth + 1;
    if (promotion_piece(move) != PieceType::None &&
        qdepth < params_.quiescence_max_depth - params_.q_extension_margin) {
      search_qdepth += params_.q_promo_extension;
    }

    const int score = -quiescence(position, -beta, -alpha, search_qdepth,
                                  position_history, ply + 1);
    undoMove(position, move, undo);

    if (score > best_score) {
      best_score = score;
      if (score > alpha) alpha = score;
    }
    if (score >= beta) {
      tt_.store(qkey, move, score_to_tt(score, ply), kQsearchDepth,
                BoundType::Lower);
      return score;
    }
  }

  tt_.store(qkey, kNullMove, score_to_tt(best_score, ply), kQsearchDepth,
            BoundType::Upper);
  return best_score;
}

int Search::negamax(Position& position, int depth, int alpha, int beta, int ply,
                    std::vector<ZobristKey>& position_history, Move prev_move) {
  ++nodes_searched_;
  if (stop_search_) {
    return eval_for_side(position);
  }
  if ((nodes_searched_ & 2047) == 0) {
    if (checkTime()) {
      stop_search_ = true;
      return eval_for_side(position);
    }
    if (max_nodes_ > 0 && nodes_searched_ >= max_nodes_) {
      stop_search_ = true;
      return eval_for_side(position);
    }
  }
  if (ply == 0) {
    root_best_move_ = kNullMove;
  }

  // Check for threefold repetition
  const ZobristKey current_key = position.key();
  int repetition_count = 1;  // Count current position
  for (std::size_t i = 0; i < position_history.size(); ++i) {
    if (position_history[i] == current_key) {
      repetition_count++;
    }
  }
  if (repetition_count >= 3) {
    return contempt_factor_;  // Draw by threefold repetition
  }

  if (ply >= kMaxDepth) {
    return quiescence(position, alpha, beta, 0, position_history, ply);
  }
  
  // Push current position key to history
  position_history.push_back(current_key);

  // Check if we need to extend depth for checks
  bool in_check = position.inCheck(position.side_to_move());
  int extension = 0;
  if (in_check && depth > 0) {
    extension = params_.check_extension;  // Check extension
  }
  int new_depth = depth + extension - 1;

  // Terminal conditions
  if (depth <= 0) {
    int score = quiescence(position, alpha, beta, 0, position_history, ply);
    position_history.pop_back();  // Pop before returning
    return score;
  }

  // Probe transposition table
  const ZobristKey key = current_key;
  TTEntry tt_entry;
  Move hash_move = kNullMove;
  const bool tt_hit = tt_.probe(key, tt_entry);
  bool pv_node = (beta - alpha > 1);  // PVS: full window means PV node

  if (tt_hit) {
    hash_move = tt_entry.bestMove;

    if (tt_entry.depth >= depth &&
        position.halfmove_clock() + depth < 100) {
      const int tt_score = score_from_tt(tt_entry.score, ply);

      if (!pv_node) {
        if (tt_entry.bound == BoundType::Exact) {
          if (ply == 0) root_best_move_ = hash_move;
          position_history.pop_back();
          return tt_score;
        } else if (tt_entry.bound == BoundType::Lower && tt_score >= beta) {
          if (ply == 0) root_best_move_ = hash_move;
          position_history.pop_back();
          return tt_score;
        } else if (tt_entry.bound == BoundType::Upper && tt_score <= alpha) {
          if (ply == 0) root_best_move_ = hash_move;
          position_history.pop_back();
          return tt_score;
        }
      }
    }
  }

  if (hash_move == kNullMove && depth >= 6) {
    const int iid_depth = depth - 2;
    if (iid_depth >= 1) {
      // IID searches this same node; don't count the node itself as a prior
      // occurrence when checking for repetition inside the reduced search.
      position_history.pop_back();
      negamax(position, iid_depth, alpha, beta, ply, position_history);
      position_history.push_back(current_key);
      TTEntry iid_entry;
      if (tt_.probe(key, iid_entry) && iid_entry.bestMove != kNullMove) {
        hash_move = iid_entry.bestMove;
      }
    }
  }

  // Check if position has legal moves
  MoveList legal_moves;
  generateLegalMoves(position, legal_moves);

  // Checkmate / stalemate detection
  if (legal_moves.size() == 0) {
    if (in_check) {
      // Checkmate
      position_history.pop_back();
      return -kScoreMate + ply;  // Prefer faster mates
    } else {
      // Stalemate: apply contempt factor to avoid draws when ahead
      position_history.pop_back();
      return contempt_factor_;
    }
  }

  // 50-move rule (simple check)
  if (position.halfmove_clock() >= 100) {
    position_history.pop_back();
    return contempt_factor_;  // Draw by 50-move rule: apply contempt
  }

  const Color original_color = position.side_to_move();

  // Compute and store static eval for this ply
  const int static_eval = in_check ? 0 : eval_for_side(position);
  eval_stack_[ply] = static_eval;
  const bool improving = !in_check && ply >= 2 && static_eval > eval_stack_[ply - 2];

  if (!in_check && !pv_node && depth <= 3) {
    static constexpr int kRazorMargin[] = {0, 300, 600, 900};
    const int razor_eval = eval_for_side(position);
    if (razor_eval + kRazorMargin[depth] < alpha) {
      if (depth == 1) {
        const int q_score =
            quiescence(position, alpha, beta, 0, position_history, ply);
        position_history.pop_back();
        return q_score;
      }
      const int q_score =
          quiescence(position, alpha, beta, 0, position_history, ply);
      if (q_score < alpha) {
        position_history.pop_back();
        return q_score;
      }
    }
  }

  int best_score = -kScoreInfinity;
  Move best_move = kNullMove;
  BoundType bound_type = BoundType::Upper;
  bool hash_move_searched = false;

  // Null Move Pruning
  // Conditions: not in check, not endgame (zugzwang risk), sufficient depth, not PV node
  if (!in_check && !isEndgame(position) && depth >= 3 && !pv_node && legal_moves.size() > 0) {
    // Make null move: flip side, update key, clear EP
    const ZobristKey original_key = position.key();
    const Square original_ep = position.ep_square();
    const Color original_side = position.side_to_move();
    
    // Flip side to move
    position.side_to_move_ = opposite(original_side);
    position.key_ ^= zobrist::side;
    
    // Clear EP square if exists
    if (original_ep != Square::None) {
      position.key_ ^= zobrist::ep_file[file_index(file_of(original_ep))];
      position.ep_square_ = Square::None;
    }
    
    // Search with reduced depth (depth - 1 - R)
    // Dynamic reduction: R = 3 + (depth > 6 ? 1 : 0)
    // Deeper searches can use larger reductions safely
    const int null_reduction =
      params_.null_reduction_base +
      (depth > params_.null_reduction_depth_threshold ? params_.null_reduction_extra : 0);
    const int null_depth = std::max(1, depth - 1 - null_reduction);
      const int null_score = -negamax(position, null_depth, -beta, -beta + 1, ply + 1, position_history);
    
    // Undo null move
    position.side_to_move_ = original_side;
    position.key_ = original_key;
    position.ep_square_ = original_ep;
    
    if (null_score >= beta) {
      position_history.pop_back();
      return null_score;
    }
  }

  // 1. Try hash move first (if exists and legal)
  bool hash_move_legal = false;
  for (std::size_t i = 0; i < legal_moves.size(); ++i) {
    if (legal_moves[i] == hash_move) {
      hash_move_legal = true;
      break;
    }
  }

  // Singular extension: check if the TT move is significantly better than alternatives
  int singular_ext = 0;
  if (hash_move != kNullMove && hash_move_legal && !in_check && depth >= 6 &&
      tt_hit && tt_entry.depth >= depth - 3 &&
      tt_entry.bound != BoundType::Upper) {
    const int tt_score = score_from_tt(tt_entry.score, ply);
    const int se_beta = tt_score - params_.singular_margin;
    const int se_depth = (depth - 1) / 2;
    // Search all moves except the hash move at reduced depth
    bool all_fail_low = true;
    for (std::size_t i = 0; i < legal_moves.size() && all_fail_low; ++i) {
      if (legal_moves[i] == hash_move) continue;
      Undo se_undo;
      doMove(position, legal_moves[i], se_undo);
      int se_score = -negamax(position, se_depth, -se_beta - 1, -se_beta, ply + 1, position_history, legal_moves[i]);
      undoMove(position, legal_moves[i], se_undo);
      if (se_score > se_beta) {
        all_fail_low = false;
      }
    }
    if (all_fail_low) {
      singular_ext = 1;
    }
  }

  if (hash_move != kNullMove && hash_move_legal) {
    hash_move_searched = true;

    Undo undo;
    doMove(position, hash_move, undo);
    int score = -negamax(position, new_depth + singular_ext, -beta, -alpha, ply + 1, position_history, hash_move);
    undoMove(position, hash_move, undo);

    if (score > best_score) {
      best_score = score;
      best_move = hash_move;
      if (ply == 0) root_best_move_ = best_move;
    }

    if (score >= beta) {
      const Piece hm_cap = captured_piece(hash_move);
      if (hm_cap == Piece::None) {
        if (killers_[ply][0] != hash_move) {
          killers_[ply][1] = killers_[ply][0];
          killers_[ply][0] = hash_move;
        }
        const int from = square_index(from_square(hash_move));
        const int to = square_index(to_square(hash_move));
        updateHistory(original_color, from, to, depth * depth);
        if (prev_move != kNullMove) {
          const Piece pp = moved_piece(prev_move);
          if (pp != Piece::None)
            countermove_[static_cast<int>(pp)][square_index(to_square(prev_move))] = hash_move;
        }
      } else {
        updateCaptureHistory(moved_piece(hash_move), to_square(hash_move), piece_type_of(hm_cap), depth);
      }
      const int store_score = score_to_tt(score, ply);
      tt_.store(key, hash_move, store_score, depth, BoundType::Lower);
      if (ply == 0) root_best_move_ = best_move;
      position_history.pop_back();
      return score;
    }

    if (score > alpha) {
      alpha = score;
      bound_type = BoundType::Exact;
    }
  }

  // Determine countermove for this position
  Move counter = kNullMove;
  if (prev_move != kNullMove) {
    const Piece prev_piece = moved_piece(prev_move);
    const int prev_to = square_index(to_square(prev_move));
    if (prev_piece != Piece::None) {
      counter = countermove_[static_cast<int>(prev_piece)][prev_to];
    }
  }

  std::vector<std::pair<Move, int>> scored_captures;
  std::vector<std::pair<Move, int>> scored_quiets;

  for (std::size_t i = 0; i < legal_moves.size(); ++i) {
    const Move move = legal_moves[i];
    if (move == hash_move && hash_move_searched) continue;

    const Piece captured = captured_piece(move);
    if (captured != Piece::None) {
      const Square from = from_square(move);
      const Square to = to_square(move);
      const int see_score = see(position, from, to);
      const int mvv_lva = getMvvLvaScore(move);
      const Piece mp = moved_piece(move);
      int cap_hist = 0;
      if (mp != Piece::None) {
        cap_hist = capture_history_[static_cast<int>(mp)][square_index(to)][static_cast<int>(piece_type_of(captured))];
      }
      const int score = see_score * 10000 + mvv_lva + cap_hist;
      scored_captures.push_back({move, score});
    } else {
      const int from = square_index(from_square(move));
      const int to = square_index(to_square(move));
      int score = history_[static_cast<int>(original_color)][from * 64 + to];
      if (move == killers_[ply][0]) score += 1000000;
      else if (move == killers_[ply][1]) score += 500000;
      if (move == counter) score += 800000;
      scored_quiets.push_back({move, score});
    }
  }

  // Sort moves
  std::sort(scored_captures.begin(), scored_captures.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
  std::sort(scored_quiets.begin(), scored_quiets.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  int move_number = 0;
  bool searched_first_move = false;

  // 3. Search captures
  for (const auto& [move, _] : scored_captures) {
    move_number++;
    
    // ProbCut: probabilistic cutoff based on shallow search
    if (depth >= 8 && !pv_node && move_number > 1) {
      const int probcut_depth = depth - 4;
      if (probcut_depth >= 1) {
        Undo undo;
        doMove(position, move, undo);
        const int probcut_score = -negamax(position, probcut_depth, -beta - 1, -beta, ply + 1, position_history);
        undoMove(position, move, undo);
        
        const int depth_diff = depth - probcut_depth;
        const int probcut_margin =
          params_.probcut_margin_base + params_.probcut_margin_scale * depth_diff;
        
        if (probcut_score >= beta + probcut_margin) {
          position_history.pop_back();
          return probcut_score;
        }
      }
    }
    
    Undo undo;
    doMove(position, move, undo);

    int score;
    if (!searched_first_move && pv_node) {
      score = -negamax(position, new_depth, -beta, -alpha, ply + 1, position_history, move);
      searched_first_move = true;
    } else {
      score = -negamax(position, new_depth, -alpha - 1, -alpha, ply + 1, position_history, move);
      if (score > alpha && score < beta) {
        score = -negamax(position, new_depth, -beta, -alpha, ply + 1, position_history, move);
      }
    }

    undoMove(position, move, undo);

    if (score > best_score) {
      best_score = score;
      best_move = move;
      if (ply == 0) root_best_move_ = best_move;
    }

    if (score >= beta) {
      updateCaptureHistory(moved_piece(move), to_square(move),
                           piece_type_of(captured_piece(move)), depth);
      const int store_score = score_to_tt(score, ply);
      tt_.store(key, move, store_score, depth, BoundType::Lower);
      if (ply == 0) root_best_move_ = best_move;
      position_history.pop_back();
      return score;
    }

    if (score > alpha) {
      alpha = score;
      bound_type = BoundType::Exact;
    }
  }

  // 4. Try killer moves
  for (int i = 0; i < 2; ++i) {
    const Move killer = killers_[ply][i];
    if (killer == kNullMove || killer == hash_move) {
      continue;
    }

    bool killer_legal = false;
    for (const auto& [m, _] : scored_quiets) {
      if (m == killer) {
        killer_legal = true;
        break;
      }
    }
    if (!killer_legal) {
      continue;
    }

    move_number++;
    
    Undo undo;
    doMove(position, killer, undo);
    
    int score;
    if (!searched_first_move && pv_node) {
      score = -negamax(position, new_depth, -beta, -alpha, ply + 1, position_history, killer);
      searched_first_move = true;
    } else {
      score = -negamax(position, new_depth, -alpha - 1, -alpha, ply + 1, position_history, killer);
      if (score > alpha && score < beta) {
        score = -negamax(position, new_depth, -beta, -alpha, ply + 1, position_history, killer);
      }
    }

    undoMove(position, killer, undo);

    if (score > best_score) {
      best_score = score;
      best_move = killer;
      if (ply == 0) root_best_move_ = best_move;
    }

    if (score >= beta) {
      if (killers_[ply][0] != killer) {
        killers_[ply][1] = killers_[ply][0];
        killers_[ply][0] = killer;
      }
      const int from = square_index(from_square(killer));
      const int to = square_index(to_square(killer));
      updateHistory(original_color, from, to, depth * depth);
      if (prev_move != kNullMove) {
        const Piece pp = moved_piece(prev_move);
        if (pp != Piece::None)
          countermove_[static_cast<int>(pp)][square_index(to_square(prev_move))] = killer;
      }
      const int store_score = score_to_tt(score, ply);
      tt_.store(key, killer, store_score, depth, BoundType::Lower);
      if (ply == 0) root_best_move_ = best_move;
      position_history.pop_back();
      return score;
    }

    if (score > alpha) {
      alpha = score;
      bound_type = BoundType::Exact;
    }
  }

  // 5. Search quiet moves with LMR and LMP
  for (const auto& [move, _] : scored_quiets) {
    if (move == hash_move && hash_move_searched) {
      continue;
    }
    if (move == killers_[ply][0] || move == killers_[ply][1]) {
      continue;
    }

    move_number++;

    if (!pv_node && !in_check) {
      int prune_after = params_.lmp_base + params_.lmp_depth_sq_coeff * depth * depth;
      if (!improving) prune_after /= 2;
      if (move_number > prune_after) {
        continue;
      }
    }

    if (move_number > 1 && new_depth <= 3 && !in_check && !pv_node &&
        captured_piece(move) == Piece::None) {
      const int futility_margin =
          params_.futility_margin_per_depth * std::max(1, new_depth);
      if (static_eval + futility_margin < alpha) {
        continue;
      }
    }

    Undo undo;
    doMove(position, move, undo);
    
    int search_depth = new_depth;
    bool extended = false;
    
    int reduction = 0;
    bool do_reduction = false;
    if (depth >= params_.lmr_min_depth && !in_check &&
        move_number > params_.lmr_move_threshold &&
        captured_piece(move) == Piece::None && !extended) {
      const int d = std::min(depth, kLmrMaxDepth - 1);
      const int m = std::min(move_number, kLmrMaxMoves - 1);
      reduction = lmr_table_[d][m];
      if (pv_node) reduction--;
      if (!improving) reduction++;
      // Reduce less for killer/countermove
      if (move == killers_[ply][0] || move == killers_[ply][1] || move == counter) {
        reduction--;
      }
      // Reduce more/less based on history
      const int from_idx = square_index(from_square(move));
      const int to_idx = square_index(to_square(move));
      const int hist = history_[static_cast<int>(original_color)][from_idx * 64 + to_idx];
      if (hist > 2000) reduction--;
      else if (hist < -500) reduction++;
      reduction = std::max(0, std::min(reduction, search_depth - 1));
      if (reduction > 0) do_reduction = true;
    }

    const int alpha_before_move = alpha;
    int score;
    if (!searched_first_move && pv_node) {
      score = -negamax(position, search_depth, -beta, -alpha, ply + 1, position_history, move);
      searched_first_move = true;
    } else if (do_reduction) {
      score = -negamax(position, search_depth - reduction, -alpha - 1, -alpha, ply + 1, position_history, move);
      if (score > alpha) {
        score = -negamax(position, search_depth, -beta, -alpha, ply + 1, position_history, move);
      }
    } else {
      score = -negamax(position, search_depth, -alpha - 1, -alpha, ply + 1, position_history, move);
      if (score > alpha && score < beta) {
        score = -negamax(position, search_depth, -beta, -alpha, ply + 1, position_history, move);
      }
    }

    undoMove(position, move, undo);

    if (score <= alpha_before_move) {
      const int from = square_index(from_square(move));
      const int to = square_index(to_square(move));
      updateHistory(original_color, from, to, -depth * depth);
    }

    if (score > best_score) {
      best_score = score;
      best_move = move;
      if (ply == 0) root_best_move_ = best_move;
    }

    if (score >= beta) {
      if (captured_piece(move) == Piece::None) {
        if (killers_[ply][0] != move) {
          killers_[ply][1] = killers_[ply][0];
          killers_[ply][0] = move;
        }
        const int from = square_index(from_square(move));
        const int to = square_index(to_square(move));
        updateHistory(original_color, from, to, depth * depth);
        if (prev_move != kNullMove) {
          const Piece pp = moved_piece(prev_move);
          if (pp != Piece::None)
            countermove_[static_cast<int>(pp)][square_index(to_square(prev_move))] = move;
        }
      }
      const int store_score = score_to_tt(score, ply);
      tt_.store(key, move, store_score, depth, BoundType::Lower);
      if (ply == 0) root_best_move_ = best_move;
      position_history.pop_back();
      return score;
    }

    if (score > alpha) {
      alpha = score;
      bound_type = BoundType::Exact;
    }
  }

  {
    const int store_score = score_to_tt(best_score, ply);
    if (best_move != kNullMove) {
      tt_.store(key, best_move, store_score, depth, bound_type);
    }
  }

  if (ply == 0) {
    root_best_move_ = best_move;
  }
  position_history.pop_back();
  return best_score;
}

bool Search::checkTime() const {
  if (hard_time_ms_ <= 0) return false;
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - start_time_).count();
  return elapsed >= hard_time_ms_;
}

Move Search::start(Position& position, int maxDepth, std::uint64_t maxNodes, int maxTimeMs) {
  SearchLimits limits;
  limits.max_depth = maxDepth;
  limits.max_nodes = maxNodes;
  limits.hard_time_ms = maxTimeMs;
  limits.soft_time_ms = maxTimeMs;
  return start(position, limits);
}

Move Search::start(Position& position, const SearchLimits& limits) {
  clearKillers();
  tt_.newSearch();
  nodes_searched_ = 0;
  pv_.clear();
  last_score_key_ = position.key();
  last_score_valid_ = false;
  last_root_move_ = kNullMove;
  max_nodes_ = limits.max_nodes;
  hard_time_ms_ = limits.hard_time_ms;
  soft_time_ms_ = limits.soft_time_ms;
  stop_search_ = false;
  start_time_ = std::chrono::steady_clock::now();

  Move best_move = kNullMove;
  Move prev_best_move = kNullMove;
  int prev_score = 0;
  int best_move_changes = 0;
  std::vector<ZobristKey> position_history;
  if (limits.game_history) {
    position_history = *limits.game_history;
    if (!position_history.empty() && position_history.back() == position.key()) {
      position_history.pop_back();
    }
  }

  for (int depth = 1; depth <= limits.max_depth; ++depth) {
    int score;

    if (depth >= 3 && prev_score > -kScoreMate + kMaxDepth && prev_score < kScoreMate - kMaxDepth) {
      int delta = params_.aspiration_window;
      int alpha = prev_score - delta;
      int beta = prev_score + delta;

      while (true) {
        score = negamax(position, depth, alpha, beta, 0, position_history);
        if (stop_search_) break;
        if (score <= alpha) {
          alpha = std::max(alpha - delta, -kScoreInfinity);
          delta *= 2;
        } else if (score >= beta) {
          beta = std::min(beta + delta, kScoreInfinity);
          delta *= 2;
        } else {
          break;
        }
        if (delta > 1000) {
          score = negamax(position, depth, -kScoreInfinity, kScoreInfinity, 0, position_history);
          break;
        }
      }
    } else {
      score = negamax(position, depth, -kScoreInfinity, kScoreInfinity, 0, position_history);
    }

    prev_score = score;
    if (stop_search_) {
      break;
    }
    last_completed_score_ = score;
    last_score_valid_ = true;

    best_move = root_best_move_;
    if (best_move == kNullMove) {
      MoveList moves;
      generateLegalMoves(position, moves);
      if (moves.size() > 0) {
        best_move = moves[0];
      }
    }
    last_root_move_ = best_move;

    if (best_move != prev_best_move && depth > 1) {
      best_move_changes++;
    }
    prev_best_move = best_move;

    if (limits.on_depth) {
      SearchInfo info;
      info.depth = depth;
      info.score = score;
      info.nodes = nodes_searched_;
      info.elapsed_ms = static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - start_time_).count());
      info.best_move = best_move;
      info.pv = getPV(position);
      limits.on_depth(info);
    }

    if (score >= kScoreMate - kMaxDepth || score <= -kScoreMate + kMaxDepth) {
      break;
    }

    // Soft time check: stop iterating if we've used enough of our soft limit
    if (soft_time_ms_ > 0 && depth >= 4) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start_time_).count();
      int scale = 100;
      if (best_move_changes >= 3) scale = 140;
      else if (best_move_changes >= 1) scale = 120;
      const int effective_soft = soft_time_ms_ * scale / 100;
      if (elapsed >= effective_soft * 60 / 100) {
        break;
      }
    }
  }

  return best_move;
}

int Search::getScore(const Position& position) const {
  if (position.key() == last_score_key_) {
    return last_score_valid_ ? last_completed_score_ : 0;
  }

  TTEntry tt_entry;
  const ZobristKey key = position.key();
  if (tt_.probe(key, tt_entry)) {
    return tt_entry.score;
  }
  return 0;
}

std::vector<Move> Search::getPV(const Position& position) const {
  std::vector<Move> pv;
  Position temp_pos = position;  // Work on a copy to avoid modifying original
  const bool has_current_root =
      last_score_valid_ && position.key() == last_score_key_;
  
  // Extract PV by recursively probing the transposition table
  // Follow the best moves stored in TT until we hit a position not in TT
  for (int depth = 0; depth < kMaxDepth; ++depth) {
    Move pv_move = kNullMove;
    if (depth == 0 && has_current_root) {
      pv_move = last_root_move_;
    } else {
      TTEntry tt_entry;
      if (!tt_.probe(temp_pos.key(), tt_entry)) {
        break;  // No TT entry, end of PV
      }
      pv_move = tt_entry.bestMove;
    }

    if (pv_move == kNullMove) {
      break;  // No best move stored
    }
    
    // Verify the move is legal before adding to PV
    MoveList legal_moves;
    generateLegalMoves(temp_pos, legal_moves);
    
    bool move_legal = false;
    for (std::size_t i = 0; i < legal_moves.size(); ++i) {
      if (legal_moves[i] == pv_move) {
        move_legal = true;
        break;
      }
    }
    
    if (!move_legal) {
      break;  // Move is not legal, end of PV
    }
    
    // Add move to PV and make it to continue following the PV
    pv.push_back(pv_move);
    Undo undo;
    doMove(temp_pos, pv_move, undo);
  }
  
  return pv;
}

}  // namespace aurora
