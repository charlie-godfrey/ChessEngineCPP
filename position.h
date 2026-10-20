#pragma once

#include "move.h"
#include "nnue.h"
#include "types.h"
#include "zobrist.h"

#include <array>
#include <string>
#include <string_view>

namespace aurora {

struct Undo;
void doMove(class Position& position, Move move, Undo& undo);
void undoMove(class Position& position, Move move, const Undo& undo);

class Position {
 public:
  Position();

  void set_startpos();
  bool setFromFEN(std::string_view fen);
  std::string toFEN() const;

  void recomputeOccupancy();
  bool verify() const;

  bool isSquareAttacked(Square square, Color byColor) const;
  Bitboard attackersTo(Square square, Color byColor) const;
  Bitboard enemyAttackMap(Color enemy, Bitboard occ_without_king) const;
  bool inCheck(Color color) const;

  void apply_move(Move move);

  Piece piece_on(Square square) const {
    return square == Square::None ? Piece::None : piece_on_[square_index(square)];
  }

  Bitboard pieces(Color color, PieceType type) const {
    return pieces_[static_cast<int>(color)][static_cast<int>(type)];
  }

  Bitboard occ(Color color) const { return occ_[static_cast<int>(color)]; }
  Bitboard occ_all() const { return occ_all_; }

  Color side_to_move() const { return side_to_move_; }
  std::uint8_t castling_rights() const { return castling_rights_; }
  Square ep_square() const { return ep_square_; }
  int halfmove_clock() const { return halfmove_clock_; }
  int ply() const { return ply_; }

  ZobristKey key() const { return key_; }
  Square king_square(Color color) const { return king_sq_[static_cast<int>(color)]; }
  nnue::NnueState& nnue_state() { return nnue_state_; }
  const nnue::NnueState& nnue_state() const { return nnue_state_; }

 private:
  static constexpr std::size_t kColors = 2;
  static constexpr std::size_t kPieceTypes = 6;

  std::array<std::array<Bitboard, kPieceTypes>, kColors> pieces_{};
  std::array<Bitboard, kColors> occ_{};
  Bitboard occ_all_ = 0;

  std::array<Piece, 64> piece_on_{};
  std::array<Square, 2> king_sq_{Square::None, Square::None};

  Color side_to_move_ = Color::White;
  std::uint8_t castling_rights_ = 0;
  Square ep_square_ = Square::None;
  int halfmove_clock_ = 0;
  int ply_ = 1;
  ZobristKey key_ = 0;
  nnue::NnueState nnue_state_{};

  void clear();
  void put_piece(Square square, Piece piece);
  void remove_piece_at(Square square);
  void add_piece_at(Square square, Piece piece);
  ZobristKey compute_key() const;

  friend void doMove(Position& position, Move move, Undo& undo);
  friend void undoMove(Position& position, Move move, const Undo& undo);
  friend class Search;  // Allow Search to access private members for null move pruning
};

}  // namespace aurora
