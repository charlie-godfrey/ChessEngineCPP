#include "perft.h"

#include "makemove.h"
#include "movegen.h"
#include "move.h"

#include <iostream>

namespace aurora {

std::uint64_t perft(Position& position, int depth) {
  if (depth <= 0) {
    return 1;
  }

  std::uint64_t nodes = 0;
  MoveList moves;
  generateLegalMoves(position, moves);
  for (std::size_t i = 0; i < moves.size(); ++i) {
    const Move move = moves[i];
    Undo undo{};
    doMove(position, move, undo);
    nodes += perft(position, depth - 1);
    undoMove(position, move, undo);
  }
  return nodes;
}

std::uint64_t perft_divide(Position& position, int depth) {
  if (depth <= 0) {
    return 1;
  }

  std::uint64_t nodes = 0;
  MoveList moves;
  generateLegalMoves(position, moves);
  for (std::size_t i = 0; i < moves.size(); ++i) {
    const Move move = moves[i];
    Undo undo{};
    doMove(position, move, undo);
    const auto count = perft(position, depth - 1);
    undoMove(position, move, undo);
    std::cout << toUci(move) << ": " << count << "\n";
    nodes += count;
  }
  std::cout << "Total: " << nodes << "\n";
  return nodes;
}

}  // namespace aurora
