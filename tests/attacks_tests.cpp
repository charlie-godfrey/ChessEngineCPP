#include "attacks.h"

#include <cassert>

int main() {
  aurora::attacks::init_attacks();

  const auto e4 = static_cast<int>(aurora::Square::E4);
  const auto g1 = static_cast<int>(aurora::Square::G1);

  const aurora::Bitboard expected_e4_knight =
      aurora::SquareBB[static_cast<int>(aurora::Square::C3)]
    | aurora::SquareBB[static_cast<int>(aurora::Square::C5)]
    | aurora::SquareBB[static_cast<int>(aurora::Square::D2)]
    | aurora::SquareBB[static_cast<int>(aurora::Square::D6)]
    | aurora::SquareBB[static_cast<int>(aurora::Square::F2)]
    | aurora::SquareBB[static_cast<int>(aurora::Square::F6)]
    | aurora::SquareBB[static_cast<int>(aurora::Square::G3)]
    | aurora::SquareBB[static_cast<int>(aurora::Square::G5)];

  assert(aurora::attacks::KnightAtt[e4] == expected_e4_knight);

  const aurora::Bitboard expected_g1_knight =
      aurora::SquareBB[static_cast<int>(aurora::Square::E2)]
    | aurora::SquareBB[static_cast<int>(aurora::Square::F3)]
    | aurora::SquareBB[static_cast<int>(aurora::Square::H3)];

  assert(aurora::attacks::KnightAtt[g1] == expected_g1_knight);

  const auto e2 = static_cast<int>(aurora::Square::E2);
  const aurora::Bitboard expected_white_pawn =
      aurora::SquareBB[static_cast<int>(aurora::Square::D3)]
    | aurora::SquareBB[static_cast<int>(aurora::Square::F3)];

  assert(aurora::attacks::PawnAtt[static_cast<int>(aurora::Color::White)][e2]
         == expected_white_pawn);

  return 0;
}
