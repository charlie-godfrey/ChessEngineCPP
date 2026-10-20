#include "movegen.h"
#include "position.h"

#include <cassert>

int main() {
  {
    aurora::Position position;
    aurora::MoveList moves;
    aurora::generateLegalMoves(position, moves);
    assert(moves.size() == 20);
  }

  {
    aurora::Position position;
    const char* fen = "4k3/8/8/8/8/8/4R3/4K3 b - - 0 1";
    assert(position.setFromFEN(fen));
    assert(position.inCheck(aurora::Color::Black));
    aurora::MoveList moves;
    aurora::generateLegalMoves(position, moves);
    assert(moves.size() == 4);
  }

  {
    aurora::Position position;
    const char* fen = "k3r3/8/8/8/8/8/4R3/4K3 w - - 0 1";
    assert(position.setFromFEN(fen));
    assert(!position.inCheck(aurora::Color::White));
    aurora::MoveList moves;
    aurora::generateLegalMoves(position, moves);
    assert(moves.size() == 10);
  }

  return 0;
}
