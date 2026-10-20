#pragma once

#include <cstdint>

namespace aurora {

using U64 = std::uint64_t;
using Bitboard = U64;

enum class Color : std::uint8_t {
  White = 0,
  Black = 1,
  None = 2
};

enum class PieceType : std::uint8_t {
  Pawn = 0,
  Knight = 1,
  Bishop = 2,
  Rook = 3,
  Queen = 4,
  King = 5,
  None = 6
};

enum class Piece : std::uint8_t {
  WhitePawn = 0,
  WhiteKnight = 1,
  WhiteBishop = 2,
  WhiteRook = 3,
  WhiteQueen = 4,
  WhiteKing = 5,
  BlackPawn = 6,
  BlackKnight = 7,
  BlackBishop = 8,
  BlackRook = 9,
  BlackQueen = 10,
  BlackKing = 11,
  None = 12
};

enum class File : std::uint8_t {
  FileA = 0,
  FileB = 1,
  FileC = 2,
  FileD = 3,
  FileE = 4,
  FileF = 5,
  FileG = 6,
  FileH = 7,
  None = 8
};

enum class Rank : std::uint8_t {
  Rank1 = 0,
  Rank2 = 1,
  Rank3 = 2,
  Rank4 = 3,
  Rank5 = 4,
  Rank6 = 5,
  Rank7 = 6,
  Rank8 = 7,
  None = 8
};

enum class Square : std::uint8_t {
  A1 = 0,  B1 = 1,  C1 = 2,  D1 = 3,  E1 = 4,  F1 = 5,  G1 = 6,  H1 = 7,
  A2 = 8,  B2 = 9,  C2 = 10, D2 = 11, E2 = 12, F2 = 13, G2 = 14, H2 = 15,
  A3 = 16, B3 = 17, C3 = 18, D3 = 19, E3 = 20, F3 = 21, G3 = 22, H3 = 23,
  A4 = 24, B4 = 25, C4 = 26, D4 = 27, E4 = 28, F4 = 29, G4 = 30, H4 = 31,
  A5 = 32, B5 = 33, C5 = 34, D5 = 35, E5 = 36, F5 = 37, G5 = 38, H5 = 39,
  A6 = 40, B6 = 41, C6 = 42, D6 = 43, E6 = 44, F6 = 45, G6 = 46, H6 = 47,
  A7 = 48, B7 = 49, C7 = 50, D7 = 51, E7 = 52, F7 = 53, G7 = 54, H7 = 55,
  A8 = 56, B8 = 57, C8 = 58, D8 = 59, E8 = 60, F8 = 61, G8 = 62, H8 = 63,
  None = 64
};

constexpr int square_index(Square square) {
  return static_cast<int>(square);
}

constexpr int file_index(File file) {
  return static_cast<int>(file);
}

constexpr int rank_index(Rank rank) {
  return static_cast<int>(rank);
}

constexpr File file_of(Square square) {
  return square == Square::None ? File::None
                                : static_cast<File>(square_index(square) & 7);
}

constexpr Rank rank_of(Square square) {
  return square == Square::None ? Rank::None
                                : static_cast<Rank>(square_index(square) >> 3);
}

constexpr Square make_square(File file, Rank rank) {
  if (file == File::None || rank == Rank::None) {
    return Square::None;
  }
  return static_cast<Square>((rank_index(rank) << 3) + file_index(file));
}

constexpr Color opposite(Color color) {
  return color == Color::White ? Color::Black
                               : color == Color::Black ? Color::White : Color::None;
}

constexpr PieceType piece_type_of(Piece piece) {
  if (piece == Piece::None) {
    return PieceType::None;
  }
  const int p = static_cast<int>(piece);
  return static_cast<PieceType>(p % 6);
}

constexpr Color color_of(Piece piece) {
  if (piece == Piece::None) {
    return Color::None;
  }
  return static_cast<int>(piece) < 6 ? Color::White : Color::Black;
}

constexpr Piece make_piece(Color color, PieceType type) {
  if (color == Color::None || type == PieceType::None) {
    return Piece::None;
  }
  const int base = color == Color::White ? 0 : 6;
  return static_cast<Piece>(base + static_cast<int>(type));
}

}  // namespace aurora
