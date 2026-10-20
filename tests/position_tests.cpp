#include "position.h"

#include <cassert>

int main() {
  {
    aurora::Position position;
    const char* start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    assert(position.setFromFEN(start));
    assert(position.verify());
    assert(position.toFEN() == start);
    assert(position.piece_on(aurora::Square::E1) == aurora::Piece::WhiteKing);
    assert(position.piece_on(aurora::Square::E8) == aurora::Piece::BlackKing);
  }

  {
    aurora::Position position;
    const char* fen = "r1bqkbnr/pppppppp/2n5/8/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2";
    assert(position.setFromFEN(fen));
    assert(position.verify());
    assert(position.toFEN() == fen);
  }

  {
    aurora::Position position;
    const char* fen = "rnbqkbnr/pp1ppppp/8/2p5/3P4/8/PPP1PPPP/RNBQKBNR b KQkq d3 0 2";
    assert(position.setFromFEN(fen));
    assert(position.verify());
    assert(position.toFEN() == fen);
  }

  return 0;
}
