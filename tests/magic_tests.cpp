#include "magic.h"

#include <cassert>

namespace {

using aurora::Bitboard;

Bitboard rook_slow(int sq, Bitboard occ) {
  Bitboard attacks = 0;
  const int rank = sq / 8;
  const int file = sq % 8;

  for (int r = rank + 1; r <= 7; ++r) {
    const int s = r * 8 + file;
    attacks |= 1ULL << s;
    if (occ & (1ULL << s)) break;
  }
  for (int r = rank - 1; r >= 0; --r) {
    const int s = r * 8 + file;
    attacks |= 1ULL << s;
    if (occ & (1ULL << s)) break;
  }
  for (int f = file + 1; f <= 7; ++f) {
    const int s = rank * 8 + f;
    attacks |= 1ULL << s;
    if (occ & (1ULL << s)) break;
  }
  for (int f = file - 1; f >= 0; --f) {
    const int s = rank * 8 + f;
    attacks |= 1ULL << s;
    if (occ & (1ULL << s)) break;
  }

  return attacks;
}

Bitboard bishop_slow(int sq, Bitboard occ) {
  Bitboard attacks = 0;
  const int rank = sq / 8;
  const int file = sq % 8;

  for (int r = rank + 1, f = file + 1; r <= 7 && f <= 7; ++r, ++f) {
    const int s = r * 8 + f;
    attacks |= 1ULL << s;
    if (occ & (1ULL << s)) break;
  }
  for (int r = rank + 1, f = file - 1; r <= 7 && f >= 0; ++r, --f) {
    const int s = r * 8 + f;
    attacks |= 1ULL << s;
    if (occ & (1ULL << s)) break;
  }
  for (int r = rank - 1, f = file + 1; r >= 0 && f <= 7; --r, ++f) {
    const int s = r * 8 + f;
    attacks |= 1ULL << s;
    if (occ & (1ULL << s)) break;
  }
  for (int r = rank - 1, f = file - 1; r >= 0 && f >= 0; --r, --f) {
    const int s = r * 8 + f;
    attacks |= 1ULL << s;
    if (occ & (1ULL << s)) break;
  }

  return attacks;
}

Bitboard next_rand(Bitboard& state) {
  state ^= state << 13;
  state ^= state >> 7;
  state ^= state << 17;
  return state;
}

}  // namespace

int main() {
#ifndef NDEBUG
  aurora::magic::init_magic();

  const Bitboard patterns[] = {
    0ULL,
    ~0ULL,
    0x00000000FFFFFFFFULL,
    0xFFFF000000000000ULL,
    0x00FF00FF00FF00FFULL,
    0xFF00FF00FF00FF00ULL
  };

  for (int sq = 0; sq < 64; ++sq) {
    for (Bitboard occ : patterns) {
      assert(aurora::magic::rookAttacks(sq, occ) == rook_slow(sq, occ));
      assert(aurora::magic::bishopAttacks(sq, occ) == bishop_slow(sq, occ));
      assert(aurora::magic::queenAttacks(sq, occ)
             == (rook_slow(sq, occ) | bishop_slow(sq, occ)));
    }

    Bitboard state = 0x9E3779B97F4A7C15ULL ^ static_cast<Bitboard>(sq * 0xC6BC279692B5CC83ULL);
    for (int i = 0; i < 128; ++i) {
      const Bitboard occ = next_rand(state);
      assert(aurora::magic::rookAttacks(sq, occ) == rook_slow(sq, occ));
      assert(aurora::magic::bishopAttacks(sq, occ) == bishop_slow(sq, occ));
      assert(aurora::magic::queenAttacks(sq, occ)
             == (rook_slow(sq, occ) | bishop_slow(sq, occ)));
    }
  }
#endif

  return 0;
}
