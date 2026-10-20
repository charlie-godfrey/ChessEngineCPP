#pragma once

#include "move.h"
#include "position.h"

namespace aurora {

struct Undo {
  Piece captured = Piece::None;
  Square captured_square = Square::None;
  Piece moving = Piece::None;
  std::uint8_t castling_rights = 0;
  Square ep_square = Square::None;
  int halfmove_clock = 0;
  int ply = 0;
  Color side_to_move = Color::White;
  ZobristKey key = 0;
  std::array<Square, 2> king_sq{Square::None, Square::None};
  Square rook_from = Square::None;
  Square rook_to = Square::None;
  Piece rook_piece = Piece::None;
};

void doMove(Position& position, Move move, Undo& undo);
void undoMove(Position& position, Move move, const Undo& undo);
void make_move(Position& position, Move move);

}  // namespace aurora
