#pragma once

#include "types.h"

#include <array>
#include <bit>

namespace aurora {

constexpr U64 FileAMask = 0x0101010101010101ULL;
constexpr U64 FileHMask = 0x8080808080808080ULL;
constexpr U64 Rank1Mask = 0x00000000000000FFULL;
constexpr U64 Rank8Mask = 0xFF00000000000000ULL;

inline int popcount(Bitboard bb) {
  return static_cast<int>(std::popcount(bb));
}

inline int lsb_index(Bitboard bb) {
  return bb == 0 ? -1 : std::countr_zero(bb);
}

inline int msb_index(Bitboard bb) {
  return bb == 0 ? -1 : 63 - std::countl_zero(bb);
}

inline int pop_lsb(Bitboard& bb) {
  const int index = lsb_index(bb);
  if (index >= 0) {
    bb &= (bb - 1);
  }
  return index;
}

constexpr Bitboard square_bb(Square square) {
  return square == Square::None ? 0ULL : (1ULL << square_index(square));
}

inline void set_bit(Bitboard& bb, Square square) {
  bb |= square_bb(square);
}

inline void clear_bit(Bitboard& bb, Square square) {
  bb &= ~square_bb(square);
}

inline bool test_bit(Bitboard bb, Square square) {
  return (bb & square_bb(square)) != 0;
}

constexpr Bitboard shift_north(Bitboard bb) {
  return bb << 8;
}

constexpr Bitboard shift_south(Bitboard bb) {
  return bb >> 8;
}

constexpr Bitboard shift_east(Bitboard bb) {
  return (bb & ~FileHMask) << 1;
}

constexpr Bitboard shift_west(Bitboard bb) {
  return (bb & ~FileAMask) >> 1;
}

constexpr Bitboard shift_north_east(Bitboard bb) {
  return (bb & ~FileHMask) << 9;
}

constexpr Bitboard shift_north_west(Bitboard bb) {
  return (bb & ~FileAMask) << 7;
}

constexpr Bitboard shift_south_east(Bitboard bb) {
  return (bb & ~FileHMask) >> 7;
}

constexpr Bitboard shift_south_west(Bitboard bb) {
  return (bb & ~FileAMask) >> 9;
}

constexpr std::array<U64, 64> make_square_bb() {
  std::array<U64, 64> table{};
  for (int i = 0; i < 64; ++i) {
    table[i] = 1ULL << i;
  }
  return table;
}

constexpr std::array<U64, 8> make_file_bb() {
  std::array<U64, 8> table{};
  for (int file = 0; file < 8; ++file) {
    U64 mask = 0;
    for (int rank = 0; rank < 8; ++rank) {
      mask |= 1ULL << (rank * 8 + file);
    }
    table[file] = mask;
  }
  return table;
}

constexpr std::array<U64, 8> make_rank_bb() {
  std::array<U64, 8> table{};
  for (int rank = 0; rank < 8; ++rank) {
    table[rank] = 0xFFULL << (rank * 8);
  }
  return table;
}

constexpr int sign_step(int value) {
  return (value > 0) ? 1 : (value < 0 ? -1 : 0);
}

constexpr U64 line_mask(int from, int to) {
  const int f_from = from & 7;
  const int r_from = from >> 3;
  const int f_to = to & 7;
  const int r_to = to >> 3;
  const int df = f_to - f_from;
  const int dr = r_to - r_from;
  if (!((df == 0 && dr != 0) || (dr == 0 && df != 0) || (df != 0 && dr != 0 && (df < 0 ? -df : df) == (dr < 0 ? -dr : dr)))) {
    return 0ULL;
  }
  const int step_f = sign_step(df);
  const int step_r = sign_step(dr);
  int f = f_from;
  int r = r_from;
  U64 mask = 0ULL;
  while (true) {
    mask |= 1ULL << (r * 8 + f);
    if (f == f_to && r == r_to) {
      break;
    }
    f += step_f;
    r += step_r;
  }
  return mask;
}

constexpr U64 between_mask(int from, int to) {
  const int f_from = from & 7;
  const int r_from = from >> 3;
  const int f_to = to & 7;
  const int r_to = to >> 3;
  const int df = f_to - f_from;
  const int dr = r_to - r_from;
  if (!((df == 0 && dr != 0) || (dr == 0 && df != 0) || (df != 0 && dr != 0 && (df < 0 ? -df : df) == (dr < 0 ? -dr : dr)))) {
    return 0ULL;
  }
  const int step_f = sign_step(df);
  const int step_r = sign_step(dr);
  int f = f_from + step_f;
  int r = r_from + step_r;
  U64 mask = 0ULL;
  while (f != f_to || r != r_to) {
    mask |= 1ULL << (r * 8 + f);
    f += step_f;
    r += step_r;
  }
  return mask;
}

constexpr std::array<std::array<U64, 64>, 64> make_line_bb() {
  std::array<std::array<U64, 64>, 64> table{};
  for (int from = 0; from < 64; ++from) {
    for (int to = 0; to < 64; ++to) {
      table[from][to] = line_mask(from, to);
    }
  }
  return table;
}

constexpr std::array<std::array<U64, 64>, 64> make_between_bb() {
  std::array<std::array<U64, 64>, 64> table{};
  for (int from = 0; from < 64; ++from) {
    for (int to = 0; to < 64; ++to) {
      table[from][to] = between_mask(from, to);
    }
  }
  return table;
}

inline constexpr std::array<U64, 64> SquareBB = make_square_bb();
inline constexpr std::array<U64, 8> FileBB = make_file_bb();
inline constexpr std::array<U64, 8> RankBB = make_rank_bb();
inline constexpr std::array<std::array<U64, 64>, 64> LineBB = make_line_bb();
inline constexpr std::array<std::array<U64, 64>, 64> BetweenBB = make_between_bb();

}  // namespace aurora
