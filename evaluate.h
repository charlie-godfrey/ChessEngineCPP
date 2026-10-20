#pragma once

#include "bitboard.h"
#include "params.h"
#include "position.h"

#include <array>

namespace aurora {

int evaluate(const Position& position);
const EvalParams& evalParams();
void setEvalParams(const EvalParams& params);

int calculateGamePhase(const Position& position);

struct EvalContext {
  Bitboard attackedBy[2][7]{};   // [color][pieceType] attack bitboards
  Bitboard attackedBy2[2]{};     // [color] squares attacked by 2+ pieces
  Bitboard attackedByAll[2]{};   // [color] union of all attacks
  Bitboard kingRing[2]{};        // [color] squares around king
  int kingAttackersCount[2]{};   // [color] enemy pieces attacking king ring
  int kingAttackersWeight[2]{};  // [color] weighted attack sum
  int kingAttacksCount[2]{};     // [color] attack count on king adjacent squares
};

}  // namespace aurora
