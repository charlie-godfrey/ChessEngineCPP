#pragma once

#include "bitboard.h"

#include <array>
#include <cstdint>
#include <iosfwd>

namespace aurora::attacks {

using AttackTable = std::array<Bitboard, 64>;

extern AttackTable KnightAtt;
extern AttackTable KingAtt;
extern std::array<AttackTable, 2> PawnAtt;

void init_attacks();
bool initialized();

void print_bitboard(Bitboard bb, std::ostream& out);

}  // namespace aurora::attacks
