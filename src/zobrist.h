#pragma once

#include <array>
#include <cstdint>

namespace aurora {

class Position;
using ZobristKey = std::uint64_t;

namespace zobrist {

using PieceTable = std::array<std::array<std::array<ZobristKey, 64>, 6>, 2>;

extern PieceTable piece;
extern std::array<ZobristKey, 16> castling;
extern std::array<ZobristKey, 8> ep_file;
extern ZobristKey side;

void init_zobrist();
bool initialized();
ZobristKey computeZobrist(Position& position);

}  // namespace zobrist

}  // namespace aurora
