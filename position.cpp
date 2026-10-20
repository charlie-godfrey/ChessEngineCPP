#include "position.h"

#include "attacks.h"
#include "bitboard.h"
#include "magic.h"
#include "makemove.h"

#include <cassert>
#include <string>

namespace aurora {

namespace {

constexpr char piece_to_char(Piece piece) {
  switch (piece) {
    case Piece::WhitePawn: return 'P';
    case Piece::WhiteKnight: return 'N';
    case Piece::WhiteBishop: return 'B';
    case Piece::WhiteRook: return 'R';
    case Piece::WhiteQueen: return 'Q';
    case Piece::WhiteKing: return 'K';
    case Piece::BlackPawn: return 'p';
    case Piece::BlackKnight: return 'n';
    case Piece::BlackBishop: return 'b';
    case Piece::BlackRook: return 'r';
    case Piece::BlackQueen: return 'q';
    case Piece::BlackKing: return 'k';
    default: return '.';
  }
}

constexpr Piece piece_from_char(char c) {
  switch (c) {
    case 'P': return Piece::WhitePawn;
    case 'N': return Piece::WhiteKnight;
    case 'B': return Piece::WhiteBishop;
    case 'R': return Piece::WhiteRook;
    case 'Q': return Piece::WhiteQueen;
    case 'K': return Piece::WhiteKing;
    case 'p': return Piece::BlackPawn;
    case 'n': return Piece::BlackKnight;
    case 'b': return Piece::BlackBishop;
    case 'r': return Piece::BlackRook;
    case 'q': return Piece::BlackQueen;
    case 'k': return Piece::BlackKing;
    default: return Piece::None;
  }
}

constexpr std::uint8_t castle_bit(char c) {
  switch (c) {
    case 'K': return 1;
    case 'Q': return 2;
    case 'k': return 4;
    case 'q': return 8;
    default: return 0;
  }
}

constexpr char file_char(File file) {
  return static_cast<char>('a' + file_index(file));
}

constexpr char rank_char(Rank rank) {
  return static_cast<char>('1' + rank_index(rank));
}

constexpr File char_to_file(char c) {
  return (c >= 'a' && c <= 'h') ? static_cast<File>(c - 'a') : File::None;
}

constexpr Rank char_to_rank(char c) {
  return (c >= '1' && c <= '8') ? static_cast<Rank>(c - '1') : Rank::None;
}

bool parse_int(std::string_view text, int& out) {
  if (text.empty()) {
    return false;
  }
  int value = 0;
  for (char c : text) {
    if (c < '0' || c > '9') {
      return false;
    }
    value = value * 10 + (c - '0');
  }
  out = value;
  return true;
}

}  // namespace

Position::Position() {
  zobrist::init_zobrist();
  attacks::init_attacks();
  magic::init_magic();
  set_startpos();
}

void Position::clear() {
  for (auto& color : pieces_) {
    color.fill(0);
  }
  occ_.fill(0);
  occ_all_ = 0;
  piece_on_.fill(Piece::None);
  king_sq_ = {Square::None, Square::None};
  side_to_move_ = Color::White;
  castling_rights_ = 0;
  ep_square_ = Square::None;
  halfmove_clock_ = 0;
  ply_ = 1;
  key_ = 0;
  nnue::invalidate(nnue_state_);
}

void Position::put_piece(Square square, Piece piece) {
  const int sq = square_index(square);
  piece_on_[sq] = piece;

  if (piece == Piece::None) {
    return;
  }

  const Color color = color_of(piece);
  const PieceType type = piece_type_of(piece);
  if (color == Color::None || type == PieceType::None) {
    return;
  }

  pieces_[static_cast<int>(color)][static_cast<int>(type)] |= square_bb(square);
  if (type == PieceType::King) {
    king_sq_[static_cast<int>(color)] = square;
  }
}

void Position::remove_piece_at(Square square) {
  const int sq = square_index(square);
  const Piece piece = piece_on_[sq];
  if (piece == Piece::None) {
    return;
  }

  const Color color = color_of(piece);
  const PieceType type = piece_type_of(piece);
  if (color != Color::None && type != PieceType::None) {
    pieces_[static_cast<int>(color)][static_cast<int>(type)] &= ~square_bb(square);
  }
  piece_on_[sq] = Piece::None;
  if (type == PieceType::King && color != Color::None) {
    king_sq_[static_cast<int>(color)] = Square::None;
  }
}

void Position::add_piece_at(Square square, Piece piece) {
  const int sq = square_index(square);
  piece_on_[sq] = piece;

  if (piece == Piece::None) {
    return;
  }

  const Color color = color_of(piece);
  const PieceType type = piece_type_of(piece);
  if (color != Color::None && type != PieceType::None) {
    pieces_[static_cast<int>(color)][static_cast<int>(type)] |= square_bb(square);
  }
  if (type == PieceType::King && color != Color::None) {
    king_sq_[static_cast<int>(color)] = square;
  }
}

void Position::set_startpos() {
  setFromFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

bool Position::setFromFEN(std::string_view fen) {
  if (fen == "startpos") {
    set_startpos();
    return true;
  }

  clear();

  std::string_view fields[6]{};
  std::size_t count = 0;
  std::size_t start = 0;
  while (start < fen.size() && count < 6) {
    std::size_t end = fen.find(' ', start);
    if (end == std::string_view::npos) {
      end = fen.size();
    }
    fields[count++] = fen.substr(start, end - start);
    start = end + 1;
    while (start < fen.size() && fen[start] == ' ') {
      ++start;
    }
  }

  if (count < 4) {
    return false;
  }

  int rank = 7;
  int file = 0;
  for (char c : fields[0]) {
    if (c == '/') {
      if (file != 8 || rank == 0) {
        return false;
      }
      --rank;
      file = 0;
      continue;
    }

    if (c >= '1' && c <= '8') {
      file += c - '0';
      if (file > 8) {
        return false;
      }
      continue;
    }

    if (file >= 8) {
      return false;
    }

    const Piece piece = piece_from_char(c);
    if (piece == Piece::None) {
      return false;
    }

    const Square square = static_cast<Square>((rank << 3) + file);
    put_piece(square, piece);
    ++file;
  }

  if (rank != 0 || file != 8) {
    return false;
  }

  if (fields[1] == "w") {
    side_to_move_ = Color::White;
  } else if (fields[1] == "b") {
    side_to_move_ = Color::Black;
  } else {
    return false;
  }

  if (fields[2] != "-") {
    for (char c : fields[2]) {
      const std::uint8_t bit = castle_bit(c);
      if (bit == 0) {
        return false;
      }
      castling_rights_ |= bit;
    }
  }

  if (fields[3] != "-") {
    if (fields[3].size() != 2) {
      return false;
    }
    const Square ep = square_from_chars(fields[3][0], fields[3][1]);
    if (ep == Square::None) {
      return false;
    }
    ep_square_ = ep;
  }

  if (count >= 5) {
    if (!parse_int(fields[4], halfmove_clock_)) {
      return false;
    }
  }

  if (count >= 6) {
    if (!parse_int(fields[5], ply_)) {
      return false;
    }
  }

  recomputeOccupancy();
  key_ = compute_key();
  nnue::invalidate(nnue_state_);
  return true;
}

std::string Position::toFEN() const {
  std::string out;
  out.reserve(64);

  for (int rank = 7; rank >= 0; --rank) {
    int empty = 0;
    for (int file = 0; file < 8; ++file) {
      const Square square = static_cast<Square>((rank << 3) + file);
      const Piece piece = piece_on_[square_index(square)];
      if (piece == Piece::None) {
        ++empty;
      } else {
        if (empty > 0) {
          out.push_back(static_cast<char>('0' + empty));
          empty = 0;
        }
        out.push_back(piece_to_char(piece));
      }
    }
    if (empty > 0) {
      out.push_back(static_cast<char>('0' + empty));
    }
    if (rank > 0) {
      out.push_back('/');
    }
  }

  out.push_back(' ');
  out.push_back(side_to_move_ == Color::White ? 'w' : 'b');
  out.push_back(' ');

  if (castling_rights_ == 0) {
    out.push_back('-');
  } else {
    if (castling_rights_ & 1) out.push_back('K');
    if (castling_rights_ & 2) out.push_back('Q');
    if (castling_rights_ & 4) out.push_back('k');
    if (castling_rights_ & 8) out.push_back('q');
  }

  out.push_back(' ');
  if (ep_square_ == Square::None) {
    out.push_back('-');
  } else {
    out.push_back(file_char(file_of(ep_square_)));
    out.push_back(rank_char(rank_of(ep_square_)));
  }

  out.push_back(' ');
  out += std::to_string(halfmove_clock_);
  out.push_back(' ');
  out += std::to_string(ply_);

  return out;
}

void Position::recomputeOccupancy() {
  occ_.fill(0);
  for (int color = 0; color < 2; ++color) {
    Bitboard occ = 0;
    for (int type = 0; type < 6; ++type) {
      occ |= pieces_[color][type];
    }
    occ_[color] = occ;
  }
  occ_all_ = occ_[0] | occ_[1];
}

ZobristKey Position::compute_key() const {
  ZobristKey key = 0;

  for (int color = 0; color < 2; ++color) {
    for (int type = 0; type < 6; ++type) {
      Bitboard bb = pieces_[color][type];
      while (bb) {
        const int sq = pop_lsb(bb);
        key ^= zobrist::piece[color][type][sq];
      }
    }
  }

  key ^= zobrist::castling[castling_rights_ & 0xF];

  if (side_to_move_ == Color::Black) {
    key ^= zobrist::side;
  }

  if (ep_square_ != Square::None) {
    const int file = file_index(file_of(ep_square_));
    if (file >= 0 && file < 8) {
      key ^= zobrist::ep_file[file];
    }
  }

  return key;
}

bool Position::verify() const {
#ifndef NDEBUG
  Bitboard occ_white = 0;
  Bitboard occ_black = 0;

  for (int type = 0; type < 6; ++type) {
    occ_white |= pieces_[0][type];
    occ_black |= pieces_[1][type];
  }

  if (occ_white != occ_[0] || occ_black != occ_[1]) {
    return false;
  }

  if ((occ_white | occ_black) != occ_all_) {
    return false;
  }

  for (int sq = 0; sq < 64; ++sq) {
    const Bitboard mask = 1ULL << sq;
    const Piece piece = piece_on_[sq];
    if (piece == Piece::None) {
      if ((occ_all_ & mask) != 0) {
        return false;
      }
    } else {
      const Color color = color_of(piece);
      const PieceType type = piece_type_of(piece);
      if (color == Color::None || type == PieceType::None) {
        return false;
      }
      if ((pieces_[static_cast<int>(color)][static_cast<int>(type)] & mask) == 0) {
        return false;
      }
    }
  }

  if (compute_key() != key_) {
    return false;
  }

  return true;
#else
  return true;
#endif
}

bool Position::isSquareAttacked(Square square, Color byColor) const {
  if (square == Square::None || byColor == Color::None) {
    return false;
  }

  const int sq = square_index(square);
  const Bitboard occ = occ_all_;

  const Bitboard knights = pieces_[static_cast<int>(byColor)][static_cast<int>(PieceType::Knight)];
  if (attacks::KnightAtt[sq] & knights) {
    return true;
  }

  const Bitboard kings = pieces_[static_cast<int>(byColor)][static_cast<int>(PieceType::King)];
  if (attacks::KingAtt[sq] & kings) {
    return true;
  }

  const Bitboard bishops = pieces_[static_cast<int>(byColor)][static_cast<int>(PieceType::Bishop)];
  const Bitboard rooks = pieces_[static_cast<int>(byColor)][static_cast<int>(PieceType::Rook)];
  const Bitboard queens = pieces_[static_cast<int>(byColor)][static_cast<int>(PieceType::Queen)];

  if (magic::bishopAttacks(sq, occ) & (bishops | queens)) {
    return true;
  }
  if (magic::rookAttacks(sq, occ) & (rooks | queens)) {
    return true;
  }

  const Bitboard pawns = pieces_[static_cast<int>(byColor)][static_cast<int>(PieceType::Pawn)];
  const Bitboard target = square_bb(square);
  Bitboard pawn_attackers = 0;
  if (byColor == Color::White) {
    pawn_attackers = shift_south_east(target) | shift_south_west(target);
  } else {
    pawn_attackers = shift_north_east(target) | shift_north_west(target);
  }
  return (pawn_attackers & pawns) != 0;
}

Bitboard Position::attackersTo(Square square, Color byColor) const {
  if (square == Square::None || byColor == Color::None) {
    return 0;
  }

  const int sq = square_index(square);
  const Bitboard occ = occ_all_;

  Bitboard attackers = 0;

  const Bitboard knights = pieces_[static_cast<int>(byColor)][static_cast<int>(PieceType::Knight)];
  attackers |= attacks::KnightAtt[sq] & knights;

  const Bitboard kings = pieces_[static_cast<int>(byColor)][static_cast<int>(PieceType::King)];
  attackers |= attacks::KingAtt[sq] & kings;

  const Bitboard bishops = pieces_[static_cast<int>(byColor)][static_cast<int>(PieceType::Bishop)];
  const Bitboard rooks = pieces_[static_cast<int>(byColor)][static_cast<int>(PieceType::Rook)];
  const Bitboard queens = pieces_[static_cast<int>(byColor)][static_cast<int>(PieceType::Queen)];

  attackers |= magic::bishopAttacks(sq, occ) & (bishops | queens);
  attackers |= magic::rookAttacks(sq, occ) & (rooks | queens);

  const Bitboard pawns = pieces_[static_cast<int>(byColor)][static_cast<int>(PieceType::Pawn)];
  const Bitboard target = square_bb(square);
  Bitboard pawn_attackers = 0;
  if (byColor == Color::White) {
    pawn_attackers = shift_south_east(target) | shift_south_west(target);
  } else {
    pawn_attackers = shift_north_east(target) | shift_north_west(target);
  }
  attackers |= pawn_attackers & pawns;

  return attackers;
}

Bitboard Position::enemyAttackMap(Color enemy, Bitboard occ_without_king) const {
  if (enemy == Color::None) {
    return 0;
  }

  Bitboard attacks = 0;
  const int enemy_idx = static_cast<int>(enemy);

  Bitboard pawns = pieces_[enemy_idx][static_cast<int>(PieceType::Pawn)];
  while (pawns) {
    const int sq = pop_lsb(pawns);
    attacks |= attacks::PawnAtt[enemy_idx][sq];
  }

  Bitboard knights = pieces_[enemy_idx][static_cast<int>(PieceType::Knight)];
  while (knights) {
    const int sq = pop_lsb(knights);
    attacks |= attacks::KnightAtt[sq];
  }

  Bitboard bishops = pieces_[enemy_idx][static_cast<int>(PieceType::Bishop)];
  while (bishops) {
    const int sq = pop_lsb(bishops);
    attacks |= magic::bishopAttacks(sq, occ_without_king);
  }

  Bitboard rooks = pieces_[enemy_idx][static_cast<int>(PieceType::Rook)];
  while (rooks) {
    const int sq = pop_lsb(rooks);
    attacks |= magic::rookAttacks(sq, occ_without_king);
  }

  Bitboard queens = pieces_[enemy_idx][static_cast<int>(PieceType::Queen)];
  while (queens) {
    const int sq = pop_lsb(queens);
    attacks |= magic::queenAttacks(sq, occ_without_king);
  }

  Bitboard kings = pieces_[enemy_idx][static_cast<int>(PieceType::King)];
  while (kings) {
    const int sq = pop_lsb(kings);
    attacks |= attacks::KingAtt[sq];
  }

  return attacks;
}

bool Position::inCheck(Color color) const {
  if (color == Color::None) {
    return false;
  }

  const Square king_sq = king_sq_[static_cast<int>(color)];
  if (king_sq == Square::None) {
    return false;
  }

  return isSquareAttacked(king_sq, opposite(color));
}

void Position::apply_move(Move move) {
  Undo undo{};
  doMove(*this, move, undo);
}

}  // namespace aurora
