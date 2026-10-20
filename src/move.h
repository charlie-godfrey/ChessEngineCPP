#pragma once

#include "types.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>

namespace aurora {

using Move = std::uint32_t;

constexpr Move kNullMove = 0;

enum MoveFieldShift : std::uint32_t {
  FromShift = 0,
  ToShift = 6,
  MovedShift = 12,
  CapturedShift = 16,
  PromoShift = 20,
  FlagsShift = 24
};

constexpr std::uint32_t field_mask(std::uint32_t bits) {
  return (1u << bits) - 1u;
}

constexpr Move pack_field(std::uint32_t value, std::uint32_t shift, std::uint32_t bits) {
  return (value & field_mask(bits)) << shift;
}

enum class MoveFlags : std::uint8_t {
  None = 0,
  EnPassant = 1 << 0,
  Castle = 1 << 1,
  DoublePush = 1 << 2,
  Check = 1 << 3
};

constexpr MoveFlags operator|(MoveFlags a, MoveFlags b) {
  return static_cast<MoveFlags>(static_cast<std::uint8_t>(a)
                                | static_cast<std::uint8_t>(b));
}

constexpr bool any_flag(MoveFlags flags, MoveFlags test) {
  return (static_cast<std::uint8_t>(flags) & static_cast<std::uint8_t>(test)) != 0;
}

constexpr Move make_move(Square from,
                         Square to,
                         Piece moved,
                         Piece captured = Piece::None,
                         PieceType promotion = PieceType::None,
                         MoveFlags flags = MoveFlags::None) {
  return pack_field(static_cast<std::uint32_t>(from), FromShift, 6)
       | pack_field(static_cast<std::uint32_t>(to), ToShift, 6)
       | pack_field(static_cast<std::uint32_t>(moved), MovedShift, 4)
       | pack_field(static_cast<std::uint32_t>(captured), CapturedShift, 4)
       | pack_field(static_cast<std::uint32_t>(promotion), PromoShift, 4)
       | pack_field(static_cast<std::uint32_t>(flags), FlagsShift, 4);
}

constexpr Square from_square(Move move) {
  return static_cast<Square>((move >> FromShift) & field_mask(6));
}

constexpr Square to_square(Move move) {
  return static_cast<Square>((move >> ToShift) & field_mask(6));
}

constexpr Piece moved_piece(Move move) {
  return static_cast<Piece>((move >> MovedShift) & field_mask(4));
}

constexpr Piece captured_piece(Move move) {
  return static_cast<Piece>((move >> CapturedShift) & field_mask(4));
}

constexpr PieceType promotion_piece(Move move) {
  return static_cast<PieceType>((move >> PromoShift) & field_mask(4));
}

constexpr MoveFlags move_flags(Move move) {
  return static_cast<MoveFlags>((move >> FlagsShift) & field_mask(4));
}

constexpr char file_to_char(File file) {
  return static_cast<char>('a' + file_index(file));
}

constexpr char rank_to_char(Rank rank) {
  return static_cast<char>('1' + rank_index(rank));
}

constexpr File char_to_file(char c) {
  return (c >= 'a' && c <= 'h') ? static_cast<File>(c - 'a') : File::None;
}

constexpr Rank char_to_rank(char c) {
  return (c >= '1' && c <= '8') ? static_cast<Rank>(c - '1') : Rank::None;
}

constexpr Square square_from_chars(char file, char rank) {
  return make_square(char_to_file(file), char_to_rank(rank));
}

inline std::string toUci(Move move) {
  if (move == kNullMove) {
    return "0000";
  }

  const Square from = from_square(move);
  const Square to = to_square(move);

  char out[6] = {
    file_to_char(file_of(from)),
    rank_to_char(rank_of(from)),
    file_to_char(file_of(to)),
    rank_to_char(rank_of(to)),
    '\0',
    '\0'
  };

  const PieceType promo = promotion_piece(move);
  if (promo != PieceType::None) {
    char promo_char = 'q';
    switch (promo) {
      case PieceType::Knight: promo_char = 'n'; break;
      case PieceType::Bishop: promo_char = 'b'; break;
      case PieceType::Rook: promo_char = 'r'; break;
      case PieceType::Queen: promo_char = 'q'; break;
      default: break;
    }
    out[4] = promo_char;
    out[5] = '\0';
  }

  return std::string(out);
}

inline Move fromUci(std::string_view uci) {
  if (uci.size() < 4) {
    return kNullMove;
  }

  const Square from = square_from_chars(uci[0], uci[1]);
  const Square to = square_from_chars(uci[2], uci[3]);

  if (from == Square::None || to == Square::None) {
    return kNullMove;
  }

  PieceType promo = PieceType::None;
  if (uci.size() >= 5) {
    switch (uci[4]) {
      case 'n': promo = PieceType::Knight; break;
      case 'b': promo = PieceType::Bishop; break;
      case 'r': promo = PieceType::Rook; break;
      case 'q': promo = PieceType::Queen; break;
      default: break;
    }
  }

  return make_move(from, to, Piece::None, Piece::None, promo, MoveFlags::None);
}

class MoveList {
 public:
  static constexpr std::size_t kCapacity = 256;

  constexpr void clear() { size_ = 0; }
  constexpr std::size_t size() const { return size_; }
  constexpr bool empty() const { return size_ == 0; }

  void push(Move move) {
#ifndef NDEBUG
    assert(size_ < kCapacity);
#endif
    moves_[size_++] = move;
  }

  constexpr Move operator[](std::size_t index) const {
#ifndef NDEBUG
    assert(index < size_);
#endif
    return moves_[index];
  }

  constexpr Move* data() { return moves_.data(); }
  constexpr const Move* data() const { return moves_.data(); }

 private:
  std::array<Move, kCapacity> moves_{};
  std::size_t size_ = 0;
};

}  // namespace aurora
