#include "move.h"

#include <cassert>

int main() {
  {
    const auto move = aurora::make_move(
        aurora::Square::E2,
        aurora::Square::E4,
        aurora::Piece::WhitePawn);
    assert(aurora::toUci(move) == "e2e4");
  }

  {
    const auto move = aurora::make_move(
        aurora::Square::A7,
        aurora::Square::A8,
        aurora::Piece::WhitePawn,
        aurora::Piece::None,
        aurora::PieceType::Queen);
    assert(aurora::toUci(move) == "a7a8q");
  }

  {
    const auto move = aurora::fromUci("g1f3");
    assert(aurora::toUci(move) == "g1f3");
  }

  return 0;
}
