#include "evaluate.h"

#include "attacks.h"
#include "bitboard.h"
#include "magic.h"
#include "nnue.h"
#include "types.h"
#include "zobrist.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <mutex>

namespace aurora {

namespace {

EvalParams g_eval_params{};

// ---------------------------------------------------------------------------
// Score helpers
// ---------------------------------------------------------------------------
struct Score {
  int mg = 0;
  int eg = 0;
};
inline Score operator+(Score a, Score b) { return {a.mg + b.mg, a.eg + b.eg}; }
inline Score operator-(Score a, Score b) { return {a.mg - b.mg, a.eg - b.eg}; }
inline Score& operator+=(Score& a, Score b) { a.mg += b.mg; a.eg += b.eg; return a; }
inline Score& operator-=(Score& a, Score b) { a.mg -= b.mg; a.eg -= b.eg; return a; }
inline Score operator*(Score a, int s) { return {a.mg * s, a.eg * s}; }
constexpr Score S(int mg, int eg) { return {mg, eg}; }

// Used for simple integer comparisons (e.g., SEE, material checks)
static constexpr int PieceValue[] = {100, 320, 330, 500, 900, 20000};

// ---------------------------------------------------------------------------
// Piece-Square Tables (PeSTO-style MG/EG pairs)
// From white's perspective, indexed by white-relative square
// ---------------------------------------------------------------------------

static constexpr std::array<int, 64> PawnPST_MG = {
   0,   0,   0,   0,   0,   0,   0,   0,
  98, 134,  61,  95,  68, 126,  34, -11,
  -6,   7,  26,  31,  65,  56,  25, -20,
 -14,  13,   6,  21,  23,  12,  17, -23,
 -27,  -2,  -5,  12,  17,   6,  10, -25,
 -26,  -4,  -4, -10,   3,   3,  33, -12,
 -35,  -1, -20, -23, -15,  24,  38, -22,
   0,   0,   0,   0,   0,   0,   0,   0,
};

static constexpr std::array<int, 64> PawnPST_EG = {
   0,   0,   0,   0,   0,   0,   0,   0,
 178, 173, 158, 134, 147, 132, 165, 187,
  94, 100,  85,  67,  56,  53,  82,  84,
  32,  24,  13,   5,  -2,   4,  17,  17,
  13,   9,  -3,  -7,  -7,  -8,   3,  -1,
   4,   7,  -6,   1,   0,  -5,  -1,  -8,
  13,   8,   8,  10,  13,   0,   2,  -7,
   0,   0,   0,   0,   0,   0,   0,   0,
};

static constexpr std::array<int, 64> KnightPST_MG = {
-167, -89, -34, -49,  61, -97, -15, -107,
 -73, -41,  72,  36,  23,  62,   7,  -17,
 -47,  60,  37,  65,  84, 129,  73,   44,
  -9,  17,  19,  53,  37,  69,  18,   22,
 -13,   4,  16,  13,  28,  19,  21,   -8,
 -23,  -9,  12,  10,  19,  17,  25,  -16,
 -29, -53, -12,  -3,  -1,  18, -14,  -19,
-105, -21, -58, -33, -17, -28, -19,  -23,
};

static constexpr std::array<int, 64> KnightPST_EG = {
 -58, -38, -13, -28, -31, -27, -63, -99,
 -25,  -8, -25,  -2,  -9, -25, -24, -52,
 -24, -20,  10,   9,  -1,  -9, -19, -41,
 -17,   3,  22,  22,  22,  11,   8, -18,
 -18,  -6,  16,  25,  16,  17,   4, -18,
 -23,  -3,  -1,  15,  10,  -3, -20, -22,
 -42, -20, -10,  -5,  -2, -20, -23, -44,
 -29, -51, -23, -15, -22, -18, -50, -64,
};

static constexpr std::array<int, 64> BishopPST_MG = {
 -29,   4, -82, -37, -25, -42,   7,  -8,
 -26,  16, -18, -13,  30,  59,  18, -47,
 -16,  37,  43,  40,  35,  50,  37,  -2,
  -4,   5,  19,  50,  37,  37,   7,  -2,
  -6,  13,  13,  26,  34,  12,  10,   4,
   0,  15,  15,  15,  14,  27,  18,  10,
   4,  15,  16,   0,   7,  21,  33,   1,
 -33,  -3, -14, -21, -13, -12, -39, -21,
};

static constexpr std::array<int, 64> BishopPST_EG = {
 -14, -21, -11,  -8,  -7,  -9, -17, -24,
  -8,  -4,   7, -12,  -3, -13,  -4, -14,
   2,  -8,   0,  -1,  -2,   6,   0,   4,
  -3,   9,  12,   9,  14,  10,   3,   2,
  -6,   3,  13,  19,   7,  10,  -3,  -9,
 -12,  -3,   8,  10,  13,   3,  -7, -15,
 -14, -18,  -7,  -1,   4,  -9, -15, -27,
 -23,  -9, -23,  -5,  -9, -16,  -5, -17,
};

static constexpr std::array<int, 64> RookPST_MG = {
  32,  42,  32,  51,  63,   9,  31,  43,
  27,  32,  58,  62,  80,  67,  26,  44,
  -5,  19,  26,  36,  17,  45,  61,  16,
 -24, -11,   7,  26,  24,  35,  -8, -20,
 -36, -26, -12,  -1,   9,  -7,   6, -23,
 -45, -25, -16, -17,   3,   0,  -5, -33,
 -44, -16, -20,  -9,  -1,  11,  -6, -71,
 -19, -13,   1,  17,  16,   7, -37, -26,
};

static constexpr std::array<int, 64> RookPST_EG = {
  13,  10,  18,  15,  12,  12,   8,   5,
  11,  13,  13,  11,  -3,   3,   8,   3,
   7,   7,   7,   5,   4,  -3,  -5,  -3,
   4,   3,  13,   1,   2,   1,  -1,   2,
   3,   5,   8,   4,  -5,  -6,  -8, -11,
  -4,   0,  -5,  -1,  -7, -12,  -8, -16,
  -6,  -6,   0,   2,  -9,  -9, -11,  -3,
  -9,   2,   3,  -1,  -5,  -13,   4,  -20,
};

static constexpr std::array<int, 64> QueenPST_MG = {
 -28,   0,  29,  12,  59,  44,  43,  45,
 -24, -39,  -5,   1, -16,  57,  28,  54,
 -13, -17,   7,   8,  29,  56,  47,  57,
 -27, -27, -16, -16,  -1,  17,  -2,   1,
  -9, -26,  -9, -10,  -2,  -4,   3,  -3,
 -14,   2, -11,  -2,  -5,   2,  14,   5,
 -35,  -8,  11,   2,   8,  15,  -3,   1,
  -1, -18,  -9,  11, -25, -31, -50, -27,
};

static constexpr std::array<int, 64> QueenPST_EG = {
  -9,  22,  22,  27,  27,  19,  10,  20,
 -17,  20,  32,  41,  58,  25,  30,   0,
 -20,   6,   9,  49,  47,  35,  19,   9,
   3,  22,  24,  45,  57,  40,  57,  36,
 -18,  28,  19,  47,  31,  34,  39,  23,
 -16, -27,  15,   6,   9,  17,  10,   5,
 -22, -23, -30, -16, -16, -23, -36, -32,
 -33, -28, -22, -43,  -5, -32, -20, -41,
};

static constexpr std::array<int, 64> KingPST_MG = {
 -65,  23,  16, -15, -56, -34,   2,  13,
  29,  -1, -20,  -7,  -8,  -4, -38, -29,
  -9,  24,   2, -16, -20,   6,  22, -22,
 -17, -20, -12, -27, -30, -25, -14, -36,
 -49,  -1, -27, -39, -46, -44, -33, -51,
 -14, -14, -22, -46, -44, -30, -15, -27,
   1,   7,  -8, -64, -43, -16,   9,   8,
 -15,  36,  12, -54,   8, -28,  24,  14,
};

static constexpr std::array<int, 64> KingPST_EG = {
 -74, -35, -18, -18, -11,  15,   4, -17,
 -12,  17,  14,  17,  17,  38,  23,  11,
  10,  17,  23,  15,  20,  45,  44,  13,
  -8,  22,  24,  27,  26,  33,  26,   3,
 -18,  -4,  21,  24,  27,  23,   9, -11,
 -19,  -3,  11,  21,  23,  16,   7,  -9,
 -27, -11,   4,  13,  14,   4,  -5, -17,
 -53, -34, -21, -11, -28, -14, -24, -43,
};

// ---------------------------------------------------------------------------
// Mobility tables (Stockfish-inspired MG/EG per count)
// ---------------------------------------------------------------------------
static constexpr Score KnightMobility[] = {
  S(-62,-81), S(-53,-56), S(-12,-30), S(-4,-14),
  S(3,8),     S(13,15),   S(22,23),   S(28,27),
  S(33,33),
};

static constexpr Score BishopMobility[] = {
  S(-48,-59), S(-20,-23), S(16,-3),  S(26,13),
  S(38,24),   S(51,42),   S(55,54),  S(63,57),
  S(63,65),   S(68,73),   S(81,78),  S(81,86),
  S(91,83),   S(98,88),
};

static constexpr Score RookMobility[] = {
  S(-58,-76), S(-27,-18), S(-15,28), S(-10,55),
  S(-5,69),   S(-2,82),   S(9,112),  S(16,118),
  S(30,132),  S(29,142),  S(32,155), S(38,165),
  S(46,166),  S(48,169),  S(58,171),
};

static constexpr Score QueenMobility[] = {
  S(-39,-36), S(-21,-15), S(3,8),    S(3,18),
  S(14,34),   S(22,54),   S(28,61),  S(41,73),
  S(43,79),   S(48,92),   S(56,94),  S(60,104),
  S(60,113),  S(66,120),  S(67,123), S(70,126),
  S(71,133),  S(73,136),  S(79,140), S(88,143),
  S(88,148),  S(99,166),  S(102,170),S(102,175),
  S(106,184), S(109,191), S(113,206),S(116,212),
};

// ---------------------------------------------------------------------------
// Material imbalance (Stockfish quadratic model)
// Piece types: Bishop pair indicator, Pawn, Knight, Bishop, Rook, Queen
// ---------------------------------------------------------------------------
static constexpr int QuadraticOurs[][6] = {
  //          BP    P     N     B     R     Q
  /* BP */ {1419                              },
  /* P  */ { 101,  37                         },
  /* N  */ {  57,  249, -65                   },
  /* B  */ {   0,  104,   4,    0             },
  /* R  */ { -17, -175, -91, -136,  253       },
  /* Q  */ {  -6, -184,  24, -159,  252, -36  },
};

static constexpr int QuadraticTheirs[][6] = {
  //          BP    P     N     B     R     Q
  /* BP */ {   0                              },
  /* P  */ {  36,   0                         },
  /* N  */ {   9,  63,    0                   },
  /* B  */ {  59,  65,   42,    0             },
  /* R  */ {  46,  39,  -12,  -93,    0       },
  /* Q  */ {  97, 100, -42,  137, 268,    0   },
};

// King attack weights and safe check bonuses now come from EvalParams

// Connected pawn bonus now comes from EvalParams

// ---------------------------------------------------------------------------
// Push-to-edge and push-close tables for endgame mating
// ---------------------------------------------------------------------------
static constexpr int PushToEdges[] = {
  100, 90, 80, 70, 70, 80, 90, 100,
   90, 70, 60, 50, 50, 60, 70,  90,
   80, 60, 40, 30, 30, 40, 60,  80,
   70, 50, 30, 20, 20, 30, 50,  70,
   70, 50, 30, 20, 20, 30, 50,  70,
   80, 60, 40, 30, 30, 40, 60,  80,
   90, 70, 60, 50, 50, 60, 70,  90,
  100, 90, 80, 70, 70, 80, 90, 100,
};

static constexpr int PushClose[] = {0, 0, 100, 80, 60, 40, 20, 10};

// ---------------------------------------------------------------------------
// Pawn hash table
// ---------------------------------------------------------------------------
static constexpr int kPawnTableSize = 16384;

struct PawnEntry {
  ZobristKey key = 0;
  Score score{};
  Bitboard passed[2]{};
};

static thread_local std::array<PawnEntry, kPawnTableSize> g_pawn_table{};

ZobristKey computePawnKey(const Position& pos) {
  ZobristKey key = 0;
  for (int c = 0; c < 2; ++c) {
    Bitboard bb = pos.pieces(static_cast<Color>(c), PieceType::Pawn);
    while (bb) {
      int sq = pop_lsb(bb);
      key ^= zobrist::piece[c][0][sq];
    }
  }
  return key;
}

// ---------------------------------------------------------------------------
// Helper: flip square for black
// ---------------------------------------------------------------------------
constexpr int flip_square(int sq) {
  return sq ^ 56;
}

constexpr int relativeRank(Color c, int sq) {
  int rank = sq >> 3;
  return c == Color::White ? rank : 7 - rank;
}

// ---------------------------------------------------------------------------
// Forward-fill helpers for passed pawn detection
// ---------------------------------------------------------------------------
Bitboard frontSpanBB(Color c, int sq) {
  int file = sq & 7;
  int rank = sq >> 3;
  Bitboard span = 0;
  if (c == Color::White) {
    for (int r = rank + 1; r < 8; ++r) {
      if (file > 0) span |= 1ULL << (r * 8 + file - 1);
      span |= 1ULL << (r * 8 + file);
      if (file < 7) span |= 1ULL << (r * 8 + file + 1);
    }
  } else {
    for (int r = rank - 1; r >= 0; --r) {
      if (file > 0) span |= 1ULL << (r * 8 + file - 1);
      span |= 1ULL << (r * 8 + file);
      if (file < 7) span |= 1ULL << (r * 8 + file + 1);
    }
  }
  return span;
}

Bitboard frontFileBB(Color c, int sq) {
  int file = sq & 7;
  int rank = sq >> 3;
  Bitboard mask = 0;
  if (c == Color::White) {
    for (int r = rank + 1; r < 8; ++r) mask |= 1ULL << (r * 8 + file);
  } else {
    for (int r = rank - 1; r >= 0; --r) mask |= 1ULL << (r * 8 + file);
  }
  return mask;
}

// ---------------------------------------------------------------------------
// Main evaluation
// ---------------------------------------------------------------------------
int evaluateInternal(const Position& pos);

}  // anonymous namespace

const EvalParams& evalParams() {
  return g_eval_params;
}

void setEvalParams(const EvalParams& params) {
  g_eval_params = params;
}

int calculateGamePhase(const Position& pos) {
  int phase = 24;
  phase -= popcount(pos.pieces(Color::White, PieceType::Queen)) * 4;
  phase -= popcount(pos.pieces(Color::Black, PieceType::Queen)) * 4;
  phase -= popcount(pos.pieces(Color::White, PieceType::Rook)) * 2;
  phase -= popcount(pos.pieces(Color::Black, PieceType::Rook)) * 2;
  phase -= popcount(pos.pieces(Color::White, PieceType::Bishop));
  phase -= popcount(pos.pieces(Color::Black, PieceType::Bishop));
  phase -= popcount(pos.pieces(Color::White, PieceType::Knight));
  phase -= popcount(pos.pieces(Color::Black, PieceType::Knight));
  return std::max(0, std::min(24, phase));
}

int evaluate(const Position& pos) {
  if (nnue::enabled()) {
    return nnue::evaluate(pos);
  }
  return evaluateInternal(pos);
}

namespace {

// ---------------------------------------------------------------------------
// Build EvalContext: per-piece attack tables
// ---------------------------------------------------------------------------
void initAttackTables(const Position& pos, EvalContext& ctx) {
  const Bitboard occ = pos.occ_all();

  for (int ci = 0; ci < 2; ++ci) {
    Color c = static_cast<Color>(ci);

    // Pawn attacks
    Bitboard pawns = pos.pieces(c, PieceType::Pawn);
    Bitboard pawnAtt;
    if (c == Color::White) {
      pawnAtt = shift_north_east(pawns) | shift_north_west(pawns);
    } else {
      pawnAtt = shift_south_east(pawns) | shift_south_west(pawns);
    }
    ctx.attackedBy[ci][0] = pawnAtt;

    // King attacks
    Square kingSq = pos.king_square(c);
    Bitboard kingAtt = 0;
    if (kingSq != Square::None) {
      kingAtt = attacks::KingAtt[square_index(kingSq)];
    }
    ctx.attackedBy[ci][5] = kingAtt;

    ctx.attackedBy2[ci] = 0;
    ctx.attackedByAll[ci] = 0;

    // King ring (3x3 zone + forward rank)
    if (kingSq != Square::None) {
      ctx.kingRing[ci] = kingAtt | square_bb(kingSq);
    }

    const auto addAttacks = [&ctx, ci](Bitboard att) {
      ctx.attackedBy2[ci] |= ctx.attackedByAll[ci] & att;
      ctx.attackedByAll[ci] |= att;
    };

    // Add each piece's attacks separately so multiple pieces of the same type
    // contribute to attackedBy2.
    Bitboard pawnPieces = pawns;
    while (pawnPieces) {
      const Bitboard pawn = 1ULL << pop_lsb(pawnPieces);
      const Bitboard att = c == Color::White
                               ? shift_north_east(pawn) | shift_north_west(pawn)
                               : shift_south_east(pawn) | shift_south_west(pawn);
      addAttacks(att);
    }
    addAttacks(kingAtt);

    for (int pt = 1; pt <= 4; ++pt) {
      Bitboard pieces = pos.pieces(c, static_cast<PieceType>(pt));
      Bitboard ptAtt = 0;
      while (pieces) {
        int sq = pop_lsb(pieces);
        Bitboard att = 0;
        switch (pt) {
          case 1: att = attacks::KnightAtt[sq]; break;
          case 2: att = magic::bishopAttacks(sq, occ); break;
          case 3: att = magic::rookAttacks(sq, occ); break;
          case 4: att = magic::queenAttacks(sq, occ); break;
        }
        ptAtt |= att;
        addAttacks(att);
      }
      ctx.attackedBy[ci][pt] = ptAtt;
    }
  }
}

// ---------------------------------------------------------------------------
// Material + PST evaluation
// ---------------------------------------------------------------------------
Score evaluateMaterialPST(const Position& pos, Color c) {
  const EvalParams& params = g_eval_params;
  Score score{};
  int ci = static_cast<int>(c);

  const std::array<int, 64>* pst_mg[] = {
    &PawnPST_MG, &KnightPST_MG, &BishopPST_MG, &RookPST_MG, &QueenPST_MG
  };
  const std::array<int, 64>* pst_eg[] = {
    &PawnPST_EG, &KnightPST_EG, &BishopPST_EG, &RookPST_EG, &QueenPST_EG
  };

  for (int p = 0; p < 5; ++p) {
    Bitboard bb = pos.pieces(c, static_cast<PieceType>(p));
    while (bb) {
      int sq = pop_lsb(bb);
      int idx = (c == Color::White) ? sq : flip_square(sq);
      score.mg += params.piece_value_mg[p] + (*pst_mg[p])[idx];
      score.eg += params.piece_value_eg[p] + (*pst_eg[p])[idx];
    }
  }

  // King PST
  Square kingSq = pos.king_square(c);
  if (kingSq != Square::None) {
    int idx = (c == Color::White) ? square_index(kingSq) : flip_square(square_index(kingSq));
    score.mg += KingPST_MG[idx];
    score.eg += KingPST_EG[idx];
  }

  return score;
}

// ---------------------------------------------------------------------------
// Material imbalance (Stockfish quadratic polynomial)
// ---------------------------------------------------------------------------
Score evaluateImbalance(const Position& pos) {
  int pieceCount[2][6]{};

  for (int ci = 0; ci < 2; ++ci) {
    Color c = static_cast<Color>(ci);
    pieceCount[ci][0] = (popcount(pos.pieces(c, PieceType::Bishop)) >= 2) ? 1 : 0;
    pieceCount[ci][1] = popcount(pos.pieces(c, PieceType::Pawn));
    pieceCount[ci][2] = popcount(pos.pieces(c, PieceType::Knight));
    pieceCount[ci][3] = popcount(pos.pieces(c, PieceType::Bishop));
    pieceCount[ci][4] = popcount(pos.pieces(c, PieceType::Rook));
    pieceCount[ci][5] = popcount(pos.pieces(c, PieceType::Queen));
  }

  int bonus[2] = {0, 0};
  for (int ci = 0; ci < 2; ++ci) {
    int them = 1 - ci;
    for (int pt1 = 0; pt1 < 6; ++pt1) {
      if (!pieceCount[ci][pt1]) continue;
      int v = 0;
      for (int pt2 = 0; pt2 <= pt1; ++pt2) {
        v += QuadraticOurs[pt1][pt2] * pieceCount[ci][pt2]
           + QuadraticTheirs[pt1][pt2] * pieceCount[them][pt2];
      }
      bonus[ci] += pieceCount[ci][pt1] * v;
    }
  }

  int imb = bonus[0] - bonus[1];
  return S(imb / 16, imb / 16);
}

// ---------------------------------------------------------------------------
// Piece evaluation: mobility + piece-specific bonuses
// ---------------------------------------------------------------------------
Score evaluatePieces(const Position& pos, EvalContext& ctx, Color c) {
  const EvalParams& params = g_eval_params;
  Score score{};
  int ci = static_cast<int>(c);
  int them_i = 1 - ci;
  Color them = opposite(c);
  const Bitboard occ = pos.occ_all();
  const Bitboard friendly = pos.occ(c);
  Bitboard ourPawns = pos.pieces(c, PieceType::Pawn);
  Bitboard theirPawns = pos.pieces(them, PieceType::Pawn);

  // Mobility area: all squares except own blocked pawns and squares attacked by enemy pawns
  Bitboard blockedPawns = (c == Color::White)
    ? (ourPawns & shift_south(occ))
    : (ourPawns & shift_north(occ));
  Bitboard mobilityArea = ~(blockedPawns | ctx.attackedBy[them_i][0]);

  // Knight evaluation
  {
    Bitboard pieces = pos.pieces(c, PieceType::Knight);
    while (pieces) {
      int sq = pop_lsb(pieces);
      Bitboard att = attacks::KnightAtt[sq];

      // Mobility
      int mob = popcount(att & mobilityArea & ~friendly);
      mob = std::min(mob, 8);
      score += KnightMobility[mob];

      // King attacks
      if (att & ctx.kingRing[them_i]) {
        ctx.kingAttackersCount[ci]++;
        ctx.kingAttackersWeight[ci] += params.king_attacker_weight[1];
        ctx.kingAttacksCount[ci] += popcount(att & ctx.kingRing[them_i]);
      }

      // Minor behind pawn
      int advSq = (c == Color::White) ? sq + 8 : sq - 8;
      if (advSq >= 0 && advSq < 64 && (ourPawns & (1ULL << advSq))) {
        score += S(params.minor_behind_pawn, params.minor_behind_pawn);
      }

      // King protector
      if (pos.king_square(c) != Square::None) {
        int kf = square_index(pos.king_square(c)) & 7;
        int kr = square_index(pos.king_square(c)) >> 3;
        int nf = sq & 7;
        int nr = sq >> 3;
        int dist = std::abs(kf - nf) + std::abs(kr - nr);
        score += S(params.king_protector * dist, 0);
      }

      // Outpost (rank 4-6 relative, on square not attackable by enemy pawns)
      int relRank = relativeRank(c, sq);
      if (relRank >= 3 && relRank <= 5) {
        Bitboard epa = ctx.attackedBy[them_i][0];
        if (!(epa & (1ULL << sq))) {
          Bitboard support = (c == Color::White)
            ? (shift_south_east(1ULL << sq) | shift_south_west(1ULL << sq)) & ourPawns
            : (shift_north_east(1ULL << sq) | shift_north_west(1ULL << sq)) & ourPawns;
          if (support) {
            score += S(params.knight_outpost_bonus, params.knight_outpost_bonus / 2);
          }
        }
      }
    }
  }

  // Bishop evaluation
  {
    Bitboard pieces = pos.pieces(c, PieceType::Bishop);
    while (pieces) {
      int sq = pop_lsb(pieces);
      // X-ray through queens for bishop mobility
      Bitboard occXray = occ ^ pos.pieces(c, PieceType::Queen);
      Bitboard att = magic::bishopAttacks(sq, occXray);

      int mob = popcount(att & mobilityArea & ~friendly);
      mob = std::min(mob, 13);
      score += BishopMobility[mob];

      if (att & ctx.kingRing[them_i]) {
        ctx.kingAttackersCount[ci]++;
        ctx.kingAttackersWeight[ci] += params.king_attacker_weight[2];
        ctx.kingAttacksCount[ci] += popcount(att & ctx.kingRing[them_i]);
      }

      // Minor behind pawn
      int advSq = (c == Color::White) ? sq + 8 : sq - 8;
      if (advSq >= 0 && advSq < 64 && (ourPawns & (1ULL << advSq))) {
        score += S(params.minor_behind_pawn, params.minor_behind_pawn);
      }

      // Bishop pawns penalty: own pawns on same color squares
      bool lightSq = ((sq & 7) + (sq >> 3)) & 1;
      Bitboard sameSqPawns = 0;
      Bitboard tmp = ourPawns;
      while (tmp) {
        int psq = pop_lsb(tmp);
        bool pawnLight = ((psq & 7) + (psq >> 3)) & 1;
        if (pawnLight == lightSq) sameSqPawns |= 1ULL << psq;
      }
      int sameColorCount = popcount(sameSqPawns);
      score += S(params.bishop_pawns_penalty * sameColorCount, params.bishop_pawns_penalty * sameColorCount);

      // Long diagonal bishop: on a1-h8 or h1-a8 diagonal seeing center
      Bitboard centerSquares = (1ULL << 27) | (1ULL << 28) | (1ULL << 35) | (1ULL << 36);
      Bitboard diagAttacks = magic::bishopAttacks(sq, occ);
      if (popcount(diagAttacks & centerSquares) >= 2) {
        score += S(params.long_diagonal_bishop, 0);
      }

      // King protector
      if (pos.king_square(c) != Square::None) {
        int kf = square_index(pos.king_square(c)) & 7;
        int kr = square_index(pos.king_square(c)) >> 3;
        int bf = sq & 7;
        int br = sq >> 3;
        int dist = std::abs(kf - bf) + std::abs(kr - br);
        score += S(params.king_protector * dist, 0);
      }

      // Outpost for bishop
      int relRank = relativeRank(c, sq);
      if (relRank >= 3 && relRank <= 5) {
        if (!(ctx.attackedBy[them_i][0] & (1ULL << sq))) {
          Bitboard support = (c == Color::White)
            ? (shift_south_east(1ULL << sq) | shift_south_west(1ULL << sq)) & ourPawns
            : (shift_north_east(1ULL << sq) | shift_north_west(1ULL << sq)) & ourPawns;
          if (support) {
            score += S(params.bishop_outpost_bonus, params.bishop_outpost_bonus / 2);
          }
        }
      }
    }
  }

  // Rook evaluation
  {
    Bitboard pieces = pos.pieces(c, PieceType::Rook);
    while (pieces) {
      int sq = pop_lsb(pieces);
      // X-ray through queens and friendly rooks
      Bitboard occXray = occ ^ pos.pieces(c, PieceType::Queen) ^ pos.pieces(c, PieceType::Rook);
      Bitboard att = magic::rookAttacks(sq, occXray);

      int mob = popcount(att & mobilityArea & ~friendly);
      mob = std::min(mob, 14);
      score += RookMobility[mob];

      if (att & ctx.kingRing[them_i]) {
        ctx.kingAttackersCount[ci]++;
        ctx.kingAttackersWeight[ci] += params.king_attacker_weight[3];
        ctx.kingAttacksCount[ci] += popcount(att & ctx.kingRing[them_i]);
      }

      // Rook on open/semi-open file
      int file = sq & 7;
      if (!(ourPawns & FileBB[file])) {
        if (!(theirPawns & FileBB[file]))
          score += S(params.rook_open_file, params.rook_open_file / 2);
        else
          score += S(params.rook_semi_open_file, params.rook_semi_open_file / 2);
      }

      // Rook on queen file
      if (FileBB[file] & (pos.pieces(Color::White, PieceType::Queen) | pos.pieces(Color::Black, PieceType::Queen))) {
        score += S(params.rook_on_queen_file, params.rook_on_queen_file);
      }

      // Trapped rook
      if (mob <= 3) {
        Square kSq = pos.king_square(c);
        if (kSq != Square::None) {
          int kFile = square_index(kSq) & 7;
          bool kingSide = kFile > 3;
          bool rookSide = (sq & 7) > 3;
          if (kingSide == rookSide) {
            bool canCastle = (c == Color::White)
              ? (pos.castling_rights() & 0x3) != 0
              : (pos.castling_rights() & 0xC) != 0;
            int penalty = canCastle ? params.trapped_rook / 2 : params.trapped_rook;
            score += S(penalty, 0);
          }
        }
      }
    }
  }

  // Queen evaluation
  {
    Bitboard pieces = pos.pieces(c, PieceType::Queen);
    while (pieces) {
      int sq = pop_lsb(pieces);
      Bitboard att = magic::queenAttacks(sq, occ);

      int mob = popcount(att & mobilityArea & ~friendly);
      mob = std::min(mob, 27);
      score += QueenMobility[mob];

      if (att & ctx.kingRing[them_i]) {
        ctx.kingAttackersCount[ci]++;
        ctx.kingAttackersWeight[ci] += params.king_attacker_weight[4];
        ctx.kingAttacksCount[ci] += popcount(att & ctx.kingRing[them_i]);
      }
    }
  }

  return score;
}

// ---------------------------------------------------------------------------
// Pawn structure evaluation (with pawn hash)
// ---------------------------------------------------------------------------
Score evaluatePawnsCached(const Position& pos, Bitboard passedOut[2]) {
  ZobristKey pawnKey = computePawnKey(pos);
  int idx = pawnKey & (kPawnTableSize - 1);
  PawnEntry& entry = g_pawn_table[idx];
  if (entry.key == pawnKey) {
    passedOut[0] = entry.passed[0];
    passedOut[1] = entry.passed[1];
    return entry.score;
  }

  Score score{};
  Bitboard passed[2]{};

  for (int ci = 0; ci < 2; ++ci) {
    Color c = static_cast<Color>(ci);
    Color them = opposite(c);
    Bitboard ourPawns = pos.pieces(c, PieceType::Pawn);
    Bitboard theirPawns = pos.pieces(them, PieceType::Pawn);
    int sign = (ci == 0) ? 1 : -1;

    Bitboard bb = ourPawns;
    while (bb) {
      int sq = pop_lsb(bb);
      int file = sq & 7;
      int relRank = relativeRank(c, sq);

      // Doubled pawn
      Bitboard frontFile = frontFileBB(c, sq);
      if (frontFile & ourPawns) {
        score.mg += sign * g_eval_params.doubled_pawn_penalty;
        score.eg += sign * g_eval_params.doubled_pawn_penalty;
      }

      // Isolated pawn
      bool isolated = true;
      if (file > 0 && (ourPawns & FileBB[file - 1])) isolated = false;
      if (file < 7 && (ourPawns & FileBB[file + 1])) isolated = false;

      if (isolated) {
        score.mg += sign * g_eval_params.isolated_pawn_penalty;
        score.eg += sign * (g_eval_params.isolated_pawn_penalty * 4 / 3);
      }

      // Backward pawn
      if (!isolated) {
        Bitboard stopSq = (c == Color::White) ? (1ULL << (sq + 8)) : (1ULL << (sq - 8));
        Bitboard enemyPawnAtt = (c == Color::White)
          ? (shift_south_east(theirPawns) | shift_south_west(theirPawns))
          : (shift_north_east(theirPawns) | shift_north_west(theirPawns));
        if (stopSq & enemyPawnAtt) {
          Bitboard behindMask = 0;
          if (c == Color::White) {
            for (int r = 0; r < (sq >> 3); ++r) {
              if (file > 0) behindMask |= 1ULL << (r * 8 + file - 1);
              if (file < 7) behindMask |= 1ULL << (r * 8 + file + 1);
            }
          } else {
            for (int r = (sq >> 3) + 1; r < 8; ++r) {
              if (file > 0) behindMask |= 1ULL << (r * 8 + file - 1);
              if (file < 7) behindMask |= 1ULL << (r * 8 + file + 1);
            }
          }
          if (!(behindMask & ourPawns)) {
            score.mg += sign * g_eval_params.backward_pawn_penalty;
            score.eg += sign * g_eval_params.backward_pawn_penalty;
          }
        }
      }

      // Connected pawn
      Bitboard supportMask = (c == Color::White)
        ? (shift_south_east(1ULL << sq) | shift_south_west(1ULL << sq))
        : (shift_north_east(1ULL << sq) | shift_north_west(1ULL << sq));
      Bitboard phalanxMask = ((file > 0) ? (1ULL << (sq - 1)) : 0) |
                             ((file < 7) ? (1ULL << (sq + 1)) : 0);
      bool supported = (supportMask & ourPawns) != 0;
      bool phalanx = (phalanxMask & ourPawns) != 0;

      if (supported || phalanx) {
        int bonus = g_eval_params.connected_bonus[relRank];
        if (phalanx) bonus += bonus / 2;
        if (supported) bonus += bonus / 4;
        // Opposed pawn reduces bonus
        if (frontFileBB(c, sq) & theirPawns) bonus /= 2;
        score.mg += sign * bonus;
        score.eg += sign * (bonus * (relRank - 2) / 4);
      }

      // Passed pawn detection
      Bitboard span = frontSpanBB(c, sq);
      if (!(span & theirPawns)) {
        passed[ci] |= 1ULL << sq;
      }
    }
  }

  entry.key = pawnKey;
  entry.score = score;
  entry.passed[0] = passed[0];
  entry.passed[1] = passed[1];
  passedOut[0] = passed[0];
  passedOut[1] = passed[1];
  return score;
}

// ---------------------------------------------------------------------------
// Passed pawn evaluation (applied in main eval, not endgame-gated)
// ---------------------------------------------------------------------------
Score evaluatePassedPawns(const Position& pos, const EvalContext& ctx, Color c, Bitboard passed) {
  const EvalParams& params = g_eval_params;
  Score score{};
  Color them = opposite(c);
  int them_i = static_cast<int>(them);

  while (passed) {
    int sq = pop_lsb(passed);
    int relRank = relativeRank(c, sq);
    int file = sq & 7;

    score.mg += params.passed_pawn_bonus_mg[relRank];
    score.eg += params.passed_pawn_bonus_eg[relRank];

    // File penalty: edge passers worth less
    if (file == 0 || file == 7) {
      score.mg += params.passed_pawn_file_penalty;
      score.eg += params.passed_pawn_file_penalty;
    }

    // Free to advance (rank 4+)
    if (relRank >= 3) {
      int advSq = (c == Color::White) ? sq + 8 : sq - 8;
      if (advSq >= 0 && advSq < 64) {
        bool empty = !(pos.occ_all() & (1ULL << advSq));
        bool safe = !(ctx.attackedByAll[them_i] & (1ULL << advSq));
        if (empty && safe) {
          int bonus = params.passed_pawn_free_advance * relRank / 4;
          score.mg += bonus;
          score.eg += bonus * 3 / 2;
        } else if (empty) {
          int bonus = params.passed_pawn_free_advance * relRank / 8;
          score.mg += bonus;
          score.eg += bonus;
        }
      }
    }

    // King proximity to passed pawn (EG feature)
    if (relRank >= 3) {
      Square ourKing = pos.king_square(c);
      Square theirKing = pos.king_square(them);
      if (ourKing != Square::None && theirKing != Square::None) {
        int promoSq = (c == Color::White) ? (7 * 8 + file) : file;
        int ourDist = std::abs((square_index(ourKing) & 7) - (promoSq & 7))
                    + std::abs((square_index(ourKing) >> 3) - (promoSq >> 3));
        int theirDist = std::abs((square_index(theirKing) & 7) - (promoSq & 7))
                      + std::abs((square_index(theirKing) >> 3) - (promoSq >> 3));
        score.eg += params.passed_pawn_king_proximity * (theirDist - ourDist) * relRank;
      }
    }

    // Supported passed pawn
    Bitboard support = (c == Color::White)
      ? (shift_south_east(1ULL << sq) | shift_south_west(1ULL << sq)) & pos.pieces(c, PieceType::Pawn)
      : (shift_north_east(1ULL << sq) | shift_north_west(1ULL << sq)) & pos.pieces(c, PieceType::Pawn);
    if (support) {
      score.mg += params.passed_pawn_bonus_mg[relRank] / 3;
      score.eg += params.passed_pawn_bonus_eg[relRank] / 3;
    }

    // Rook behind passed pawn
    Bitboard rooks = pos.pieces(c, PieceType::Rook);
    while (rooks) {
      int rsq = pop_lsb(rooks);
      if ((rsq & 7) == file) {
        bool behind = (c == Color::White) ? ((rsq >> 3) < (sq >> 3)) : ((rsq >> 3) > (sq >> 3));
        if (behind) {
          score += S(params.rook_behind_passer_mg, params.rook_behind_passer_eg);
        }
      }
    }
  }

  return score;
}

// ---------------------------------------------------------------------------
// Threats evaluation (using per-piece attack tables)
// ---------------------------------------------------------------------------
Score evaluateThreats(const Position& pos, const EvalContext& ctx, Color c) {
  const EvalParams& params = g_eval_params;
  Score score{};
  int ci = static_cast<int>(c);
  int them_i = 1 - ci;
  Color them = opposite(c);

  Bitboard defended = ctx.attackedByAll[them_i];
  Bitboard stronglyProtected = ctx.attackedBy[them_i][0] | (ctx.attackedBy2[them_i] & ~ctx.attackedBy2[ci]);

  // Threat by minor (knights and bishops attacking enemy pieces)
  Bitboard minorAttacks = ctx.attackedBy[ci][1] | ctx.attackedBy[ci][2];
  Bitboard threatTargets = pos.occ(them) & ~pos.pieces(them, PieceType::Pawn);

  Bitboard threatened = minorAttacks & threatTargets & ~stronglyProtected;
  while (threatened) {
    int sq = pop_lsb(threatened);
    Piece piece = pos.piece_on(static_cast<Square>(sq));
    PieceType pt = piece_type_of(piece);
    if (pt != PieceType::None && static_cast<int>(pt) < 6) {
      int bonus = params.threat_by_minor[static_cast<int>(pt)];
      score += S(bonus, bonus);
    }
  }

  // Threat by rook
  Bitboard rookAttacks = ctx.attackedBy[ci][3];
  threatened = rookAttacks & pos.occ(them) & ~pos.pieces(them, PieceType::Pawn) & ~stronglyProtected;
  while (threatened) {
    int sq = pop_lsb(threatened);
    Piece piece = pos.piece_on(static_cast<Square>(sq));
    PieceType pt = piece_type_of(piece);
    if (pt != PieceType::None && static_cast<int>(pt) < 6) {
      int bonus = params.threat_by_rook[static_cast<int>(pt)];
      score += S(bonus, bonus);
    }
  }

  // Hanging pieces: attacked by us and not defended by them
  Bitboard hanging = pos.occ(them) & ~pos.pieces(them, PieceType::King)
                   & ctx.attackedByAll[ci] & ~defended;
  if (hanging) {
    score += S(params.hanging_penalty * popcount(hanging), params.hanging_penalty * popcount(hanging) / 2);
  }

  // Safe pawn threats: our pawns attacking enemy non-pawn pieces on safe squares
  Bitboard pawnThreats = ctx.attackedBy[ci][0]
                       & pos.occ(them)
                       & ~pos.pieces(them, PieceType::Pawn)
                       & ~ctx.attackedByAll[them_i];
  if (pawnThreats) {
    score += S(params.safe_pawn_threat * popcount(pawnThreats) / 100,
               params.safe_pawn_threat * popcount(pawnThreats) / 100);
  }

  // Restricted pieces: enemy pieces with limited mobility due to our attacks
  Bitboard restricted = ctx.attackedByAll[ci] & ~ctx.attackedBy[ci][0]
                       & ctx.attackedByAll[them_i] & ~ctx.attackedBy[them_i][0];
  if (restricted) {
    score += S(params.restricted_piece * popcount(restricted),
               params.restricted_piece * popcount(restricted));
  }

  return score;
}

// ---------------------------------------------------------------------------
// King safety (quadratic danger model)
// ---------------------------------------------------------------------------
Score evaluateKingSafety(const Position& pos, const EvalContext& ctx, Color c) {
  const EvalParams& params = g_eval_params;
  Score score{};
  int ci = static_cast<int>(c);
  int them_i = 1 - ci;
  Color them = opposite(c);

  Square kingSq = pos.king_square(c);
  if (kingSq == Square::None) return score;
  int kingIdx = square_index(kingSq);

  // Pawn shield
  Bitboard ourPawns = pos.pieces(c, PieceType::Pawn);
  Bitboard kingArea = ctx.kingRing[ci];
  Bitboard shield;
  if (c == Color::White)
    shield = shift_north(kingArea) & ourPawns;
  else
    shield = shift_south(kingArea) & ourPawns;
  int shieldCount = popcount(shield);
  score += S(shieldCount * params.pawn_shield_bonus, 0);

  // Castling rights bonus
  std::uint8_t castling = pos.castling_rights();
  if (c == Color::White) {
    if (castling & 0x3) score += S(params.king_castle_bonus, 0);
  } else {
    if (castling & 0xC) score += S(params.king_castle_bonus, 0);
  }

  // --- Quadratic king danger model ---
  int danger = 0;
  int attackerCount = ctx.kingAttackersCount[them_i];
  int attackerWeight = ctx.kingAttackersWeight[them_i];
  int attackCount = ctx.kingAttacksCount[them_i];

  if (attackerCount < 2) return score;

  danger += attackerWeight;
  danger += params.king_attack_count_weight * attackCount;

  // Safe checks
  const Bitboard occ = pos.occ_all();
  Bitboard safe = ~ctx.attackedByAll[ci] | (~ctx.attackedBy2[ci] & ctx.attackedBy[ci][5]);

  // Knight safe checks
  Bitboard knightChecks = attacks::KnightAtt[kingIdx] & ctx.attackedBy[them_i][1] & safe;
  if (knightChecks) danger += params.safe_check_bonus[1];

  // Bishop safe checks
  Bitboard bishopChecks = magic::bishopAttacks(kingIdx, occ) & ctx.attackedBy[them_i][2] & safe;
  if (bishopChecks) danger += params.safe_check_bonus[2];

  // Rook safe checks
  Bitboard rookChecks = magic::rookAttacks(kingIdx, occ) & ctx.attackedBy[them_i][3] & safe;
  if (rookChecks) danger += params.safe_check_bonus[3];

  // Queen safe checks
  Bitboard queenChecks = magic::queenAttacks(kingIdx, occ) & ctx.attackedBy[them_i][4] & safe;
  if (queenChecks) danger += params.safe_check_bonus[4];

  // Weak squares around king
  Bitboard weak = ctx.attackedByAll[them_i] & ~ctx.attackedBy2[ci]
               & (~ctx.attackedByAll[ci] | ctx.attackedBy[ci][5]);
  danger += params.king_weak_sq_weight * popcount(weak & ctx.kingRing[ci]);

  // Open files near king
  int kingFile = kingIdx & 7;
  for (int f = std::max(0, kingFile - 1); f <= std::min(7, kingFile + 1); ++f) {
    if (!(FileBB[f] & (pos.pieces(Color::White, PieceType::Pawn) | pos.pieces(Color::Black, PieceType::Pawn)))) {
      danger += params.king_open_file_weight;
    }
  }

  // No enemy queen greatly reduces danger
  if (!pos.pieces(them, PieceType::Queen)) {
    danger /= std::max(1, params.king_no_queen_divisor);
  }

  danger = std::max(0, danger);

  // Quadratic conversion
  int mg_value = -danger * danger / 4096;
  score.mg += mg_value;

  return score;
}

// ---------------------------------------------------------------------------
// Space evaluation
// ---------------------------------------------------------------------------
Score evaluateSpace(const Position& pos, const EvalContext& ctx, Color c) {
  const EvalParams& params = g_eval_params;
  Score score{};
  int ci = static_cast<int>(c);

  // Central control (d4-e4-d5-e5)
  Bitboard center = (FileBB[3] | FileBB[4]) & (RankBB[3] | RankBB[4]);
  int centerCount = popcount(ctx.attackedByAll[ci] & center);
  score.mg += centerCount * params.center_control_weight;

  // Space behind our pawns in central files
  Bitboard ourPawns = pos.pieces(c, PieceType::Pawn);
  Bitboard spaceArea;
  if (c == Color::White) {
    spaceArea = (FileBB[2] | FileBB[3] | FileBB[4] | FileBB[5]) & (RankBB[1] | RankBB[2] | RankBB[3]);
  } else {
    spaceArea = (FileBB[2] | FileBB[3] | FileBB[4] | FileBB[5]) & (RankBB[4] | RankBB[5] | RankBB[6]);
  }
  Bitboard behind = spaceArea & ~ourPawns & ~ctx.attackedBy[1 - ci][0];
  score.mg += popcount(behind) * params.enemy_territory_weight / 2;

  return score;
}

// ---------------------------------------------------------------------------
// Endgame: king distance mating logic
// ---------------------------------------------------------------------------
Score evaluateEndgameKingDist(const Position& pos, int phase) {
  if (phase < 18) return S(0, 0);

  Score score{};
  Square wKing = pos.king_square(Color::White);
  Square bKing = pos.king_square(Color::Black);
  if (wKing == Square::None || bKing == Square::None) return score;

  int wIdx = square_index(wKing);
  int bIdx = square_index(bKing);

  // Material comparison
  int wMat = popcount(pos.pieces(Color::White, PieceType::Queen)) * 900
           + popcount(pos.pieces(Color::White, PieceType::Rook)) * 500
           + popcount(pos.pieces(Color::White, PieceType::Bishop)) * 330
           + popcount(pos.pieces(Color::White, PieceType::Knight)) * 320;
  int bMat = popcount(pos.pieces(Color::Black, PieceType::Queen)) * 900
           + popcount(pos.pieces(Color::Black, PieceType::Rook)) * 500
           + popcount(pos.pieces(Color::Black, PieceType::Bishop)) * 330
           + popcount(pos.pieces(Color::Black, PieceType::Knight)) * 320;

  int matAdv = wMat - bMat;

  if (matAdv > g_eval_params.endgame_mat_threshold) {
    // White winning: push black king to edge, bring white king close
    int dist = std::abs((wIdx & 7) - (bIdx & 7)) + std::abs((wIdx >> 3) - (bIdx >> 3));
    int pushClose = (dist < 8) ? PushClose[dist] : 0;
    score.eg += PushToEdges[bIdx] + pushClose;
  } else if (matAdv < -g_eval_params.endgame_mat_threshold) {
    // Black winning: push white king to edge, bring black king close
    int dist = std::abs((wIdx & 7) - (bIdx & 7)) + std::abs((wIdx >> 3) - (bIdx >> 3));
    int pushClose = (dist < 8) ? PushClose[dist] : 0;
    score.eg -= PushToEdges[wIdx] + pushClose;
  }

  // Opposition bonus (K+P endings)
  int fileDiff = std::abs((wIdx & 7) - (bIdx & 7));
  int rankDiff = std::abs((wIdx >> 3) - (bIdx >> 3));
  if ((fileDiff == 0 && rankDiff == 2) || (rankDiff == 0 && fileDiff == 2)) {
    if (pos.side_to_move() == Color::Black && matAdv >= 0) {
      score.eg += g_eval_params.opposition_bonus;
    } else if (pos.side_to_move() == Color::White && matAdv <= 0) {
      score.eg -= g_eval_params.opposition_bonus;
    }
  }

  return score;
}

// ---------------------------------------------------------------------------
// Initiative / complexity correction (Tier 3C)
// ---------------------------------------------------------------------------
int evaluateInitiative(const Position& pos, int eg) {
  if (eg == 0) return 0;

  Bitboard wPawns = pos.pieces(Color::White, PieceType::Pawn);
  Bitboard bPawns = pos.pieces(Color::Black, PieceType::Pawn);
  int totalPawns = popcount(wPawns | bPawns);

  // Check if pawns on both flanks
  bool pawnsKingSide = (wPawns | bPawns) & (FileBB[5] | FileBB[6] | FileBB[7]);
  bool pawnsQueenSide = (wPawns | bPawns) & (FileBB[0] | FileBB[1] | FileBB[2]);
  bool bothFlanks = pawnsKingSide && pawnsQueenSide;

  int complexity = g_eval_params.initiative_pawn_weight * totalPawns
                 + g_eval_params.initiative_both_flanks * (bothFlanks ? 1 : 0)
                 + g_eval_params.initiative_offset;

  int sign = (eg > 0) ? 1 : -1;
  int adjusted = sign * std::max(std::abs(eg) + complexity, 0);

  // Never flip sign
  if (sign > 0 && adjusted < 0) adjusted = 0;
  if (sign < 0 && adjusted > 0) adjusted = 0;

  return adjusted - eg;
}

// ---------------------------------------------------------------------------
// Endgame scale factors (Tier 3D)
// ---------------------------------------------------------------------------
int computeScaleFactor(const Position& pos, int eg) {
  int sf = 64; // normal = 64

  Color strong = (eg > 0) ? Color::White : Color::Black;
  Color weak = opposite(strong);

  int strongPawns = popcount(pos.pieces(strong, PieceType::Pawn));

  // No pawns on strong side: very difficult to win
  if (strongPawns == 0) {
    int strongMinors = popcount(pos.pieces(strong, PieceType::Knight))
                     + popcount(pos.pieces(strong, PieceType::Bishop));
    int strongMajors = popcount(pos.pieces(strong, PieceType::Rook))
                     + popcount(pos.pieces(strong, PieceType::Queen));
    if (strongMajors == 0 && strongMinors <= 1) {
      sf = 0;
    } else if (strongMajors == 0) {
      sf = 12;
    } else {
      sf = std::min(sf, 36);
    }
  }

  // Opposite-color bishops
  if (popcount(pos.pieces(Color::White, PieceType::Bishop)) == 1 &&
      popcount(pos.pieces(Color::Black, PieceType::Bishop)) == 1) {
    int wBishSq = lsb_index(pos.pieces(Color::White, PieceType::Bishop));
    int bBishSq = lsb_index(pos.pieces(Color::Black, PieceType::Bishop));
    bool wLight = ((wBishSq & 7) + (wBishSq >> 3)) & 1;
    bool bLight = ((bBishSq & 7) + (bBishSq >> 3)) & 1;
    if (wLight != bLight) {
      int wNonPawnNonBish = popcount(pos.pieces(Color::White, PieceType::Knight))
                          + popcount(pos.pieces(Color::White, PieceType::Rook))
                          + popcount(pos.pieces(Color::White, PieceType::Queen));
      int bNonPawnNonBish = popcount(pos.pieces(Color::Black, PieceType::Knight))
                          + popcount(pos.pieces(Color::Black, PieceType::Rook))
                          + popcount(pos.pieces(Color::Black, PieceType::Queen));
      if (wNonPawnNonBish == 0 && bNonPawnNonBish == 0) {
        sf = std::min(sf, 22);
      } else {
        sf = std::min(sf, 44);
      }
    }
  }

  // 50-move rule proximity: reduce as halfmove clock increases
  int rule50 = pos.halfmove_clock();
  if (rule50 > 12) {
    sf = sf * (120 - rule50) / 108;
    sf = std::max(sf, 0);
  }

  return sf;
}

// ---------------------------------------------------------------------------
// Main evaluation body
// ---------------------------------------------------------------------------
int evaluateInternal(const Position& pos) {
  const EvalParams& params = g_eval_params;
  int phase = calculateGamePhase(pos);

  // Build attack tables
  EvalContext ctx{};
  initAttackTables(pos, ctx);

  // Material + PST
  Score wScore = evaluateMaterialPST(pos, Color::White);
  Score bScore = evaluateMaterialPST(pos, Color::Black);
  Score total = wScore - bScore;

  // Material imbalance
  total += evaluateImbalance(pos);

  // Pieces (mobility + piece bonuses + king attack accumulation)
  total += evaluatePieces(pos, ctx, Color::White);
  total -= evaluatePieces(pos, ctx, Color::Black);

  // Pawn structure (cached)
  Bitboard passed[2]{};
  total += evaluatePawnsCached(pos, passed);

  // Passed pawns (not endgame-gated)
  total += evaluatePassedPawns(pos, ctx, Color::White, passed[0]);
  total -= evaluatePassedPawns(pos, ctx, Color::Black, passed[1]);

  // Threats
  total += evaluateThreats(pos, ctx, Color::White);
  total -= evaluateThreats(pos, ctx, Color::Black);

  // King safety
  total += evaluateKingSafety(pos, ctx, Color::White);
  total -= evaluateKingSafety(pos, ctx, Color::Black);

  // Space
  total += evaluateSpace(pos, ctx, Color::White);
  total -= evaluateSpace(pos, ctx, Color::Black);

  // Endgame king distance
  total += evaluateEndgameKingDist(pos, phase);

  // Tempo
  int tempo = (pos.side_to_move() == Color::White) ? params.tempo_bonus : -params.tempo_bonus;

  // Taper
  int mg = total.mg;
  int eg = total.eg;
  int tapered = (mg * (24 - phase) + eg * phase) / 24;

  // Initiative correction on EG component
  int egForInit = (eg * phase) / 24;
  int initAdj = evaluateInitiative(pos, egForInit);
  tapered += initAdj;

  // Scale factor on EG portion
  int sf = computeScaleFactor(pos, eg);
  if (sf != 64) {
    int mgPart = (mg * (24 - phase)) / 24;
    int egPart = (eg * phase) / 24;
    egPart = egPart * sf / 64;
    tapered = mgPart + egPart + initAdj;
  }

  return tapered + tempo;
}

}  // anonymous namespace
}  // namespace aurora
