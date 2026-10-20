#pragma once

#include "move.h"
#include "position.h"

#include <iosfwd>
#include <vector>

namespace aurora {

void generateCaptures(const Position& position, MoveList& moves);
void generateQuiets(const Position& position, MoveList& moves);
void generateMoves(const Position& position, MoveList& moves);
void generateLegalMoves(Position& position, MoveList& moves);

void resetMovegenProfile();
void dumpMovegenProfile(std::ostream& out);

}  // namespace aurora
