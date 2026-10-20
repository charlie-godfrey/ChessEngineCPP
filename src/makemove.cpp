#include "makemove.h"

#include "bitboard.h"
#include "zobrist.h"
#include "nnue.h"
#include <cassert>

namespace aurora {

void doMove(Position& position, Move move, Undo& undo) {
  auto xor_piece_key = [&](Piece piece, Square square) {
    const Color color = color_of(piece);
    const PieceType type = piece_type_of(piece);
    if (color == Color::None || type == PieceType::None) {
      return;
    }
    position.key_ ^= zobrist::piece[static_cast<int>(color)][static_cast<int>(type)]
                                  [square_index(square)];
  };

  auto remove_piece = [&](Square square) {
    const int sq = square_index(square);
    const Piece piece = position.piece_on_[sq];
    if (piece == Piece::None) {
      return;
    }
    const Color color = color_of(piece);
    const PieceType type = piece_type_of(piece);
    const Bitboard mask = square_bb(square);

    position.piece_on_[sq] = Piece::None;
    position.pieces_[static_cast<int>(color)][static_cast<int>(type)] &= ~mask;
    position.occ_[static_cast<int>(color)] &= ~mask;
    if (type == PieceType::King) {
      position.king_sq_[static_cast<int>(color)] = Square::None;
    }
  };

  auto add_piece = [&](Square square, Piece piece) {
    const int sq = square_index(square);
    const Color color = color_of(piece);
    const PieceType type = piece_type_of(piece);
    const Bitboard mask = square_bb(square);

    position.piece_on_[sq] = piece;
    position.pieces_[static_cast<int>(color)][static_cast<int>(type)] |= mask;
    position.occ_[static_cast<int>(color)] |= mask;
    if (type == PieceType::King) {
      position.king_sq_[static_cast<int>(color)] = square;
    }
  };

  const Square from = from_square(move);
  const Square to = to_square(move);
  const MoveFlags flags = move_flags(move);

  undo.captured = Piece::None;
  undo.captured_square = Square::None;
  undo.moving = moved_piece(move);
  undo.castling_rights = position.castling_rights_;
  undo.ep_square = position.ep_square_;
  undo.halfmove_clock = position.halfmove_clock_;
  undo.ply = position.ply_;
  undo.side_to_move = position.side_to_move_;
  undo.key = position.key_;
  undo.king_sq = position.king_sq_;
  undo.rook_from = Square::None;
  undo.rook_to = Square::None;
  undo.rook_piece = Piece::None;

  Piece moving = undo.moving;
  if (moving == Piece::None) {
    moving = position.piece_on(from);
    undo.moving = moving;
  }

  const Color us = position.side_to_move_;
  const Color them = opposite(us);
  const PieceType moving_type = piece_type_of(moving);
  const bool is_pawn = moving_type == PieceType::Pawn;

  position.key_ ^= zobrist::side;

  if (position.ep_square_ != Square::None) {
    position.key_ ^= zobrist::ep_file[file_index(file_of(position.ep_square_))];
  }

  position.ep_square_ = Square::None;

  if (any_flag(flags, MoveFlags::EnPassant)) {
    undo.captured = make_piece(them, PieceType::Pawn);
    undo.captured_square = us == Color::White
                               ? static_cast<Square>(square_index(to) - 8)
                               : static_cast<Square>(square_index(to) + 8);
  } else {
    const Piece captured = position.piece_on(to);
    if (captured != Piece::None) {
      undo.captured = captured;
      undo.captured_square = to;
    }
  }

  if (is_pawn || undo.captured != Piece::None) {
    position.halfmove_clock_ = 0;
  } else {
    ++position.halfmove_clock_;
  }

  xor_piece_key(moving, from);
  remove_piece(from);

  if (undo.captured != Piece::None) {
    xor_piece_key(undo.captured, undo.captured_square);
    remove_piece(undo.captured_square);
  }

  const PieceType promo = promotion_piece(move);
  const Piece placed = (promo != PieceType::None) ? make_piece(us, promo) : moving;

  xor_piece_key(placed, to);
  add_piece(to, placed);

  if (any_flag(flags, MoveFlags::Castle)) {
    if (from == Square::E1 && to == Square::G1) {
      undo.rook_from = Square::H1;
      undo.rook_to = Square::F1;
      undo.rook_piece = Piece::WhiteRook;
    } else if (from == Square::E1 && to == Square::C1) {
      undo.rook_from = Square::A1;
      undo.rook_to = Square::D1;
      undo.rook_piece = Piece::WhiteRook;
    } else if (from == Square::E8 && to == Square::G8) {
      undo.rook_from = Square::H8;
      undo.rook_to = Square::F8;
      undo.rook_piece = Piece::BlackRook;
    } else if (from == Square::E8 && to == Square::C8) {
      undo.rook_from = Square::A8;
      undo.rook_to = Square::D8;
      undo.rook_piece = Piece::BlackRook;
    }

    if (undo.rook_piece != Piece::None) {
      xor_piece_key(undo.rook_piece, undo.rook_from);
      remove_piece(undo.rook_from);
      xor_piece_key(undo.rook_piece, undo.rook_to);
      add_piece(undo.rook_to, undo.rook_piece);
    }
  }

  if (any_flag(flags, MoveFlags::DoublePush) && is_pawn) {
    position.ep_square_ = us == Color::White
                              ? static_cast<Square>(square_index(to) - 8)
                              : static_cast<Square>(square_index(to) + 8);
    position.key_ ^= zobrist::ep_file[file_index(file_of(position.ep_square_))];
  }

  position.key_ ^= zobrist::castling[position.castling_rights_ & 0xF];

  if (moving == Piece::WhiteKing) {
    position.castling_rights_ &= ~static_cast<std::uint8_t>(3);
  } else if (moving == Piece::BlackKing) {
    position.castling_rights_ &= ~static_cast<std::uint8_t>(12);
  }

  if (moving == Piece::WhiteRook) {
    if (from == Square::H1) position.castling_rights_ &= ~static_cast<std::uint8_t>(1);
    if (from == Square::A1) position.castling_rights_ &= ~static_cast<std::uint8_t>(2);
  } else if (moving == Piece::BlackRook) {
    if (from == Square::H8) position.castling_rights_ &= ~static_cast<std::uint8_t>(4);
    if (from == Square::A8) position.castling_rights_ &= ~static_cast<std::uint8_t>(8);
  }

  if (undo.captured == Piece::WhiteRook) {
    if (undo.captured_square == Square::H1) position.castling_rights_ &= ~static_cast<std::uint8_t>(1);
    if (undo.captured_square == Square::A1) position.castling_rights_ &= ~static_cast<std::uint8_t>(2);
  } else if (undo.captured == Piece::BlackRook) {
    if (undo.captured_square == Square::H8) position.castling_rights_ &= ~static_cast<std::uint8_t>(4);
    if (undo.captured_square == Square::A8) position.castling_rights_ &= ~static_cast<std::uint8_t>(8);
  }

  position.key_ ^= zobrist::castling[position.castling_rights_ & 0xF];

  position.side_to_move_ = them;
  ++position.ply_;
  position.occ_all_ = position.occ_[0] | position.occ_[1];

  const bool king_moved = moving_type == PieceType::King || any_flag(flags, MoveFlags::Castle);
  if (king_moved || undo.captured == Piece::WhiteKing || undo.captured == Piece::BlackKing) {
    nnue::invalidate(position.nnue_state_);
  } else {
    nnue::updateRemovePiece(position, position.nnue_state_, from, moving);
    if (undo.captured != Piece::None) {
      nnue::updateRemovePiece(position, position.nnue_state_, undo.captured_square, undo.captured);
    }
    nnue::updateAddPiece(position, position.nnue_state_, to, placed);
    if (any_flag(flags, MoveFlags::Castle) && undo.rook_piece != Piece::None) {
      nnue::updateRemovePiece(position, position.nnue_state_, undo.rook_from, undo.rook_piece);
      nnue::updateAddPiece(position, position.nnue_state_, undo.rook_to, undo.rook_piece);
    }
  }

#ifndef NDEBUG
  // Debug assertion: ensure incremental zobrist key matches full recomputation
  const ZobristKey recomputed = zobrist::computeZobrist(position);
  assert(recomputed == position.key_ && "Zobrist key mismatch after doMove");
  position.verify();
#endif
}

void undoMove(Position& position, Move move, const Undo& undo) {
  auto remove_piece = [&](Square square) {
    const int sq = square_index(square);
    const Piece piece = position.piece_on_[sq];
    if (piece == Piece::None) {
      return;
    }
    const Color color = color_of(piece);
    const PieceType type = piece_type_of(piece);
    const Bitboard mask = square_bb(square);

    position.piece_on_[sq] = Piece::None;
    position.pieces_[static_cast<int>(color)][static_cast<int>(type)] &= ~mask;
    position.occ_[static_cast<int>(color)] &= ~mask;
    if (type == PieceType::King) {
      position.king_sq_[static_cast<int>(color)] = Square::None;
    }
  };

  auto add_piece = [&](Square square, Piece piece) {
    const int sq = square_index(square);
    const Color color = color_of(piece);
    const PieceType type = piece_type_of(piece);
    const Bitboard mask = square_bb(square);

    position.piece_on_[sq] = piece;
    position.pieces_[static_cast<int>(color)][static_cast<int>(type)] |= mask;
    position.occ_[static_cast<int>(color)] |= mask;
    if (type == PieceType::King) {
      position.king_sq_[static_cast<int>(color)] = square;
    }
  };

  const Square from = from_square(move);
  const Square to = to_square(move);
  const MoveFlags flags = move_flags(move);

  position.side_to_move_ = undo.side_to_move;
  position.castling_rights_ = undo.castling_rights;
  position.ep_square_ = undo.ep_square;
  position.halfmove_clock_ = undo.halfmove_clock;
  position.ply_ = undo.ply;
  position.key_ = undo.key;
  position.king_sq_ = undo.king_sq;

  const Piece moving = undo.moving;

  remove_piece(to);
  add_piece(from, moving);

  if (undo.captured != Piece::None) {
    add_piece(undo.captured_square, undo.captured);
  }

  if (any_flag(flags, MoveFlags::Castle) && undo.rook_piece != Piece::None) {
    remove_piece(undo.rook_to);
    add_piece(undo.rook_from, undo.rook_piece);
  }

  position.occ_all_ = position.occ_[0] | position.occ_[1];

  const bool king_moved = piece_type_of(undo.moving) == PieceType::King || any_flag(flags, MoveFlags::Castle);
  if (king_moved || undo.captured == Piece::WhiteKing || undo.captured == Piece::BlackKing) {
    nnue::invalidate(position.nnue_state_);
  } else {
    const Color us = undo.side_to_move;
    const PieceType promo = promotion_piece(move);
    const Piece placed = (promo != PieceType::None) ? make_piece(us, promo) : undo.moving;
    nnue::updateRemovePiece(position, position.nnue_state_, to, placed);
    nnue::updateAddPiece(position, position.nnue_state_, from, undo.moving);
    if (undo.captured != Piece::None) {
      nnue::updateAddPiece(position, position.nnue_state_, undo.captured_square, undo.captured);
    }
    if (any_flag(flags, MoveFlags::Castle) && undo.rook_piece != Piece::None) {
      nnue::updateRemovePiece(position, position.nnue_state_, undo.rook_to, undo.rook_piece);
      nnue::updateAddPiece(position, position.nnue_state_, undo.rook_from, undo.rook_piece);
    }
  }

#ifndef NDEBUG
  // Debug assertion: ensure incremental zobrist key matches full recomputation
  const ZobristKey recomputed = zobrist::computeZobrist(position);
  assert(recomputed == position.key_ && "Zobrist key mismatch after undoMove");
  position.verify();
#endif
}

void make_move(Position& position, Move move) {
  Undo undo{};
  doMove(position, move, undo);
}

}  // namespace aurora
