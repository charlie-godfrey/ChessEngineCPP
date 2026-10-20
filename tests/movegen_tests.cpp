#include "movegen.h"
#include "position.h"

#include <cassert>

int main() {
  aurora::Position position;
  aurora::MoveList moves;
  aurora::generateMoves(position, moves);
  assert(moves.size() == 20);
  return 0;
}
