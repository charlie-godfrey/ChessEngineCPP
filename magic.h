#pragma once

#include "bitboard.h"

#include <array>
#include <cstdint>

namespace aurora::magic {

extern std::array<Bitboard, 64> RookMask;
extern std::array<Bitboard, 64> BishopMask;
extern std::array<std::uint8_t, 64> RookShift;
extern std::array<std::uint8_t, 64> BishopShift;
extern std::array<std::array<Bitboard, 4096>, 64> RookTable;
extern std::array<std::array<Bitboard, 512>, 64> BishopTable;
extern std::array<U64, 64> RookMagic;
extern std::array<U64, 64> BishopMagic;

void init_magic();
bool initialized();

inline Bitboard rookAttacks(int square, Bitboard occ) {
  occ &= RookMask[static_cast<std::size_t>(square)];
  const std::uint64_t index = (occ * RookMagic[static_cast<std::size_t>(square)])
                              >> RookShift[static_cast<std::size_t>(square)];
  return RookTable[static_cast<std::size_t>(square)][index];
}

inline Bitboard bishopAttacks(int square, Bitboard occ) {
  occ &= BishopMask[static_cast<std::size_t>(square)];
  const std::uint64_t index = (occ * BishopMagic[static_cast<std::size_t>(square)])
                              >> BishopShift[static_cast<std::size_t>(square)];
  return BishopTable[static_cast<std::size_t>(square)][index];
}

inline Bitboard queenAttacks(int square, Bitboard occ) {
  return rookAttacks(square, occ) | bishopAttacks(square, occ);
}

}  // namespace aurora::magic
