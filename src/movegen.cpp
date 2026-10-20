#include "movegen.h"

#include "attacks.h"
#include "magic.h"
#include "makemove.h"

#include <cstdint>

namespace aurora {

namespace {

constexpr std::uint8_t CastleWK = 1;
constexpr std::uint8_t CastleWQ = 2;
constexpr std::uint8_t CastleBK = 4;
constexpr std::uint8_t CastleBQ = 8;


inline void push_promotions(MoveList& moves,
                            Square from,
                            Square to,
                            Piece moved,
                            Piece captured = Piece::None) {
  moves.push(make_move(from, to, moved, captured, PieceType::Queen));
  moves.push(make_move(from, to, moved, captured, PieceType::Rook));
  moves.push(make_move(from, to, moved, captured, PieceType::Bishop));
  moves.push(make_move(from, to, moved, captured, PieceType::Knight));
}

struct CheckInfo {
  Bitboard checkers = 0;
  Bitboard pinned = 0;
  std::array<Bitboard, 64> pin_mask{};
};

inline Bitboard xray_rook_attacks(int sq, Bitboard occ, Bitboard own) {
  const Bitboard attacks = magic::rookAttacks(sq, occ);
  const Bitboard blockers = attacks & own;
  return attacks ^ magic::rookAttacks(sq, occ ^ blockers);
}

inline Bitboard xray_bishop_attacks(int sq, Bitboard occ, Bitboard own) {
  const Bitboard attacks = magic::bishopAttacks(sq, occ);
  const Bitboard blockers = attacks & own;
  return attacks ^ magic::bishopAttacks(sq, occ ^ blockers);
}

CheckInfo compute_check_info(const Position& position, Color us) {
  CheckInfo info{};
  info.pin_mask.fill(0);

  const Color them = opposite(us);
  const Square king_sq = position.king_square(us);
  const Bitboard occ_all = position.occ_all();
  const int ksq = square_index(king_sq);
  const Bitboard target = square_bb(king_sq);

  Bitboard checkers = 0;

  const Bitboard knights = position.pieces(them, PieceType::Knight);
  checkers |= attacks::KnightAtt[ksq] & knights;

  const Bitboard kings = position.pieces(them, PieceType::King);
  checkers |= attacks::KingAtt[ksq] & kings;

  const Bitboard pawns = position.pieces(them, PieceType::Pawn);
  Bitboard pawn_attackers = 0;
  if (them == Color::White) {
    pawn_attackers = shift_south_east(target) | shift_south_west(target);
  } else {
    pawn_attackers = shift_north_east(target) | shift_north_west(target);
  }
  checkers |= pawn_attackers & pawns;

  const Bitboard bishops = position.pieces(them, PieceType::Bishop);
  const Bitboard rooks = position.pieces(them, PieceType::Rook);
  const Bitboard queens = position.pieces(them, PieceType::Queen);

  const Bitboard rook_attacks = magic::rookAttacks(ksq, occ_all);
  const Bitboard bishop_attacks = magic::bishopAttacks(ksq, occ_all);
  checkers |= rook_attacks & (rooks | queens);
  checkers |= bishop_attacks & (bishops | queens);

  info.checkers = checkers;

  const Bitboard own = position.occ(us);

  Bitboard pinners = 0;
  pinners |= xray_rook_attacks(ksq, occ_all, own) & (rooks | queens);
  pinners |= xray_bishop_attacks(ksq, occ_all, own) & (bishops | queens);

  while (pinners) {
    const int pinner_sq = pop_lsb(pinners);
    const Bitboard ray = LineBB[ksq][pinner_sq];
    const Bitboard blockers = BetweenBB[ksq][pinner_sq] & own;
    if (popcount(blockers) == 1) {
      const int pinned_sq = lsb_index(blockers);
      info.pinned |= square_bb(static_cast<Square>(pinned_sq));
      info.pin_mask[pinned_sq] = ray;
    }
  }

  return info;
}

}  // namespace

void generateCaptures(const Position& position, MoveList& moves) {
  const Color us = position.side_to_move();
  const Color them = opposite(us);
  const Bitboard occ_all = position.occ_all();
  const Bitboard opp_occ = position.occ(them);

  const int color_idx = static_cast<int>(us);

  // Pawns
  {
    Bitboard pawns = position.pieces(us, PieceType::Pawn);
    while (pawns) {
      const int from = pop_lsb(pawns);
      const Bitboard attacks = attacks::PawnAtt[color_idx][from] & opp_occ;
      Bitboard targets = attacks;
      while (targets) {
        const int to = pop_lsb(targets);
        const Square to_sq = static_cast<Square>(to);
        const Rank to_rank = rank_of(to_sq);
        const Piece captured = position.piece_on(to_sq);
        const Piece moved = make_piece(us, PieceType::Pawn);
        if (to_rank == (us == Color::White ? Rank::Rank8 : Rank::Rank1)) {
          push_promotions(moves, static_cast<Square>(from), to_sq, moved, captured);
        } else {
          moves.push(make_move(static_cast<Square>(from), to_sq, moved, captured));
        }
      }
    }

    const Square ep = position.ep_square();
    if (ep != Square::None) {
      Bitboard ep_bb = square_bb(ep);
      Bitboard attackers = 0;
      if (us == Color::White) {
        attackers = shift_south_east(ep_bb) | shift_south_west(ep_bb);
      } else {
        attackers = shift_north_east(ep_bb) | shift_north_west(ep_bb);
      }
      attackers &= position.pieces(us, PieceType::Pawn);
      while (attackers) {
        const int from = pop_lsb(attackers);
        moves.push(make_move(static_cast<Square>(from),
                             ep,
                             make_piece(us, PieceType::Pawn),
                             make_piece(them, PieceType::Pawn),
                             PieceType::None,
                             MoveFlags::EnPassant));
      }
    }
  }

  // Knights
  {
    const Piece moved = make_piece(us, PieceType::Knight);
    Bitboard knights = position.pieces(us, PieceType::Knight);
    while (knights) {
      const int from = pop_lsb(knights);
      Bitboard targets = attacks::KnightAtt[from] & opp_occ;
      while (targets) {
        const int to = pop_lsb(targets);
        const Square to_sq = static_cast<Square>(to);
        moves.push(make_move(static_cast<Square>(from),
                             to_sq,
                             moved,
                             position.piece_on(to_sq)));
      }
    }
  }

  // Bishops
  {
    const Piece moved = make_piece(us, PieceType::Bishop);
    Bitboard bishops = position.pieces(us, PieceType::Bishop);
    while (bishops) {
      const int from = pop_lsb(bishops);
      Bitboard targets = magic::bishopAttacks(from, occ_all) & opp_occ;
      while (targets) {
        const int to = pop_lsb(targets);
        const Square to_sq = static_cast<Square>(to);
        moves.push(make_move(static_cast<Square>(from),
                             to_sq,
                             moved,
                             position.piece_on(to_sq)));
      }
    }
  }

  // Rooks
  {
    const Piece moved = make_piece(us, PieceType::Rook);
    Bitboard rooks = position.pieces(us, PieceType::Rook);
    while (rooks) {
      const int from = pop_lsb(rooks);
      Bitboard targets = magic::rookAttacks(from, occ_all) & opp_occ;
      while (targets) {
        const int to = pop_lsb(targets);
        const Square to_sq = static_cast<Square>(to);
        moves.push(make_move(static_cast<Square>(from),
                             to_sq,
                             moved,
                             position.piece_on(to_sq)));
      }
    }
  }

  // Queens
  {
    const Piece moved = make_piece(us, PieceType::Queen);
    Bitboard queens = position.pieces(us, PieceType::Queen);
    while (queens) {
      const int from = pop_lsb(queens);
      Bitboard targets = magic::queenAttacks(from, occ_all) & opp_occ;
      while (targets) {
        const int to = pop_lsb(targets);
        const Square to_sq = static_cast<Square>(to);
        moves.push(make_move(static_cast<Square>(from),
                             to_sq,
                             moved,
                             position.piece_on(to_sq)));
      }
    }
  }

  // King
  {
    const Piece moved = make_piece(us, PieceType::King);
    Bitboard king = position.pieces(us, PieceType::King);
    if (king) {
      const int from = lsb_index(king);
      Bitboard targets = attacks::KingAtt[from] & opp_occ;
      while (targets) {
        const int to = pop_lsb(targets);
        const Square to_sq = static_cast<Square>(to);
        moves.push(make_move(static_cast<Square>(from),
                             to_sq,
                             moved,
                             position.piece_on(to_sq)));
      }
    }
  }
}

void generateQuiets(const Position& position, MoveList& moves) {
  const Color us = position.side_to_move();
  const Bitboard occ_all = position.occ_all();
  const Bitboard empty = ~occ_all;
  const int color_idx = static_cast<int>(us);

  // Pawns
  {
    Bitboard pawns = position.pieces(us, PieceType::Pawn);

    if (us == Color::White) {
      const Bitboard single = shift_north(pawns) & empty;
      Bitboard promotions = single & Rank8Mask;
      Bitboard quiets = single & ~Rank8Mask;

      while (promotions) {
        const int to = pop_lsb(promotions);
        const int from = to - 8;
        push_promotions(moves,
                        static_cast<Square>(from),
                        static_cast<Square>(to),
                        make_piece(us, PieceType::Pawn));
      }

      while (quiets) {
        const int to = pop_lsb(quiets);
        const int from = to - 8;
        moves.push(make_move(static_cast<Square>(from),
                             static_cast<Square>(to),
                             make_piece(us, PieceType::Pawn)));
      }

      const Bitboard rank2 = pawns & RankBB[static_cast<int>(Rank::Rank2)];
      const Bitboard single_from_rank2 = shift_north(rank2) & empty;
      Bitboard doubles = shift_north(single_from_rank2) & empty;
      while (doubles) {
        const int to = pop_lsb(doubles);
        const int from = to - 16;
        moves.push(make_move(static_cast<Square>(from),
                             static_cast<Square>(to),
                             make_piece(us, PieceType::Pawn),
                             Piece::None,
                             PieceType::None,
                             MoveFlags::DoublePush));
      }
    } else {
      const Bitboard single = shift_south(pawns) & empty;
      Bitboard promotions = single & Rank1Mask;
      Bitboard quiets = single & ~Rank1Mask;

      while (promotions) {
        const int to = pop_lsb(promotions);
        const int from = to + 8;
        push_promotions(moves,
                        static_cast<Square>(from),
                        static_cast<Square>(to),
                        make_piece(us, PieceType::Pawn));
      }

      while (quiets) {
        const int to = pop_lsb(quiets);
        const int from = to + 8;
        moves.push(make_move(static_cast<Square>(from),
                             static_cast<Square>(to),
                             make_piece(us, PieceType::Pawn)));
      }

      const Bitboard rank7 = pawns & RankBB[static_cast<int>(Rank::Rank7)];
      const Bitboard single_from_rank7 = shift_south(rank7) & empty;
      Bitboard doubles = shift_south(single_from_rank7) & empty;
      while (doubles) {
        const int to = pop_lsb(doubles);
        const int from = to + 16;
        moves.push(make_move(static_cast<Square>(from),
                             static_cast<Square>(to),
                             make_piece(us, PieceType::Pawn),
                             Piece::None,
                             PieceType::None,
                             MoveFlags::DoublePush));
      }
    }

    // No quiet pawn diagonals.
  }

  // Knights
  {
    const Piece moved = make_piece(us, PieceType::Knight);
    Bitboard knights = position.pieces(us, PieceType::Knight);
    while (knights) {
      const int from = pop_lsb(knights);
      Bitboard targets = attacks::KnightAtt[from] & empty;
      while (targets) {
        const int to = pop_lsb(targets);
        moves.push(make_move(static_cast<Square>(from),
                             static_cast<Square>(to),
                             moved));
      }
    }
  }

  // Bishops
  {
    const Piece moved = make_piece(us, PieceType::Bishop);
    Bitboard bishops = position.pieces(us, PieceType::Bishop);
    while (bishops) {
      const int from = pop_lsb(bishops);
      Bitboard targets = magic::bishopAttacks(from, occ_all) & empty;
      while (targets) {
        const int to = pop_lsb(targets);
        moves.push(make_move(static_cast<Square>(from),
                             static_cast<Square>(to),
                             moved));
      }
    }
  }

  // Rooks
  {
    const Piece moved = make_piece(us, PieceType::Rook);
    Bitboard rooks = position.pieces(us, PieceType::Rook);
    while (rooks) {
      const int from = pop_lsb(rooks);
      Bitboard targets = magic::rookAttacks(from, occ_all) & empty;
      while (targets) {
        const int to = pop_lsb(targets);
        moves.push(make_move(static_cast<Square>(from),
                             static_cast<Square>(to),
                             moved));
      }
    }
  }

  // Queens
  {
    const Piece moved = make_piece(us, PieceType::Queen);
    Bitboard queens = position.pieces(us, PieceType::Queen);
    while (queens) {
      const int from = pop_lsb(queens);
      Bitboard targets = magic::queenAttacks(from, occ_all) & empty;
      while (targets) {
        const int to = pop_lsb(targets);
        moves.push(make_move(static_cast<Square>(from),
                             static_cast<Square>(to),
                             moved));
      }
    }
  }

  // King
  {
    const Piece moved = make_piece(us, PieceType::King);
    Bitboard king = position.pieces(us, PieceType::King);
    if (king) {
      const int from = lsb_index(king);
      Bitboard targets = attacks::KingAtt[from] & empty;
      while (targets) {
        const int to = pop_lsb(targets);
        moves.push(make_move(static_cast<Square>(from),
                             static_cast<Square>(to),
                             moved));
      }
    }
  }

  // Castling (pseudolegal)
  {
    const std::uint8_t rights = position.castling_rights();
    if (us == Color::White) {
      if ((rights & CastleWK) != 0) {
        const Bitboard empty_ks = square_bb(Square::F1) | square_bb(Square::G1);
        if ((occ_all & empty_ks) == 0) {
          moves.push(make_move(Square::E1,
                               Square::G1,
                               make_piece(us, PieceType::King),
                               Piece::None,
                               PieceType::None,
                               MoveFlags::Castle));
        }
      }
      if ((rights & CastleWQ) != 0) {
        const Bitboard empty_qs = square_bb(Square::B1)
                                | square_bb(Square::C1)
                                | square_bb(Square::D1);
        if ((occ_all & empty_qs) == 0) {
          moves.push(make_move(Square::E1,
                               Square::C1,
                               make_piece(us, PieceType::King),
                               Piece::None,
                               PieceType::None,
                               MoveFlags::Castle));
        }
      }
    } else {
      if ((rights & CastleBK) != 0) {
        const Bitboard empty_ks = square_bb(Square::F8) | square_bb(Square::G8);
        if ((occ_all & empty_ks) == 0) {
          moves.push(make_move(Square::E8,
                               Square::G8,
                               make_piece(us, PieceType::King),
                               Piece::None,
                               PieceType::None,
                               MoveFlags::Castle));
        }
      }
      if ((rights & CastleBQ) != 0) {
        const Bitboard empty_qs = square_bb(Square::B8)
                                | square_bb(Square::C8)
                                | square_bb(Square::D8);
        if ((occ_all & empty_qs) == 0) {
          moves.push(make_move(Square::E8,
                               Square::C8,
                               make_piece(us, PieceType::King),
                               Piece::None,
                               PieceType::None,
                               MoveFlags::Castle));
        }
      }
    }
  }
}

void generateMoves(const Position& position, MoveList& moves) {
  moves.clear();

  const Color us = position.side_to_move();
  const Color them = opposite(us);
  const Bitboard occ_all = position.occ_all();
  const Bitboard opp_occ = position.occ(them);
  const Bitboard empty = ~occ_all;
  const int color_idx = static_cast<int>(us);

  // Pawns: captures + quiets + promotions + en passant.
  {
    const Piece moved = make_piece(us, PieceType::Pawn);
    Bitboard pawns = position.pieces(us, PieceType::Pawn);
    Bitboard pawns_caps = pawns;
    while (pawns_caps) {
      const int from = pop_lsb(pawns_caps);
      const Bitboard attacks = attacks::PawnAtt[color_idx][from] & opp_occ;
      Bitboard targets = attacks;
      while (targets) {
        const int to = pop_lsb(targets);
        const Square to_sq = static_cast<Square>(to);
        const Rank to_rank = rank_of(to_sq);
        const Piece captured = position.piece_on(to_sq);
        if (to_rank == (us == Color::White ? Rank::Rank8 : Rank::Rank1)) {
          push_promotions(moves, static_cast<Square>(from), to_sq, moved, captured);
        } else {
          moves.push(make_move(static_cast<Square>(from), to_sq, moved, captured));
        }
      }
    }

    const Square ep = position.ep_square();
    if (ep != Square::None) {
      Bitboard ep_bb = square_bb(ep);
      Bitboard attackers = 0;
      if (us == Color::White) {
        attackers = shift_south_east(ep_bb) | shift_south_west(ep_bb);
      } else {
        attackers = shift_north_east(ep_bb) | shift_north_west(ep_bb);
      }
      attackers &= pawns;
      while (attackers) {
        const int from = pop_lsb(attackers);
        moves.push(make_move(static_cast<Square>(from),
                             ep,
                             moved,
                             make_piece(them, PieceType::Pawn),
                             PieceType::None,
                             MoveFlags::EnPassant));
      }
    }

    if (us == Color::White) {
      const Bitboard single = shift_north(pawns) & empty;
      Bitboard promotions = single & Rank8Mask;
      Bitboard quiets = single & ~Rank8Mask;

      while (promotions) {
        const int to = pop_lsb(promotions);
        const int from = to - 8;
        push_promotions(moves,
                        static_cast<Square>(from),
                        static_cast<Square>(to),
                        moved);
      }

      while (quiets) {
        const int to = pop_lsb(quiets);
        const int from = to - 8;
        moves.push(make_move(static_cast<Square>(from),
                             static_cast<Square>(to),
                             moved));
      }

      const Bitboard rank2 = pawns & RankBB[static_cast<int>(Rank::Rank2)];
      const Bitboard single_from_rank2 = shift_north(rank2) & empty;
      Bitboard doubles = shift_north(single_from_rank2) & empty;
      while (doubles) {
        const int to = pop_lsb(doubles);
        const int from = to - 16;
        moves.push(make_move(static_cast<Square>(from),
                             static_cast<Square>(to),
                             moved,
                             Piece::None,
                             PieceType::None,
                             MoveFlags::DoublePush));
      }
    } else {
      const Bitboard single = shift_south(pawns) & empty;
      Bitboard promotions = single & Rank1Mask;
      Bitboard quiets = single & ~Rank1Mask;

      while (promotions) {
        const int to = pop_lsb(promotions);
        const int from = to + 8;
        push_promotions(moves,
                        static_cast<Square>(from),
                        static_cast<Square>(to),
                        moved);
      }

      while (quiets) {
        const int to = pop_lsb(quiets);
        const int from = to + 8;
        moves.push(make_move(static_cast<Square>(from),
                             static_cast<Square>(to),
                             moved));
      }

      const Bitboard rank7 = pawns & RankBB[static_cast<int>(Rank::Rank7)];
      const Bitboard single_from_rank7 = shift_south(rank7) & empty;
      Bitboard doubles = shift_south(single_from_rank7) & empty;
      while (doubles) {
        const int to = pop_lsb(doubles);
        const int from = to + 16;
        moves.push(make_move(static_cast<Square>(from),
                             static_cast<Square>(to),
                             moved,
                             Piece::None,
                             PieceType::None,
                             MoveFlags::DoublePush));
      }
    }
  }

  // Knights
  {
    const Piece moved = make_piece(us, PieceType::Knight);
    Bitboard knights = position.pieces(us, PieceType::Knight);
    while (knights) {
      const int from = pop_lsb(knights);
      const Bitboard attacks = attacks::KnightAtt[from];
      Bitboard targets = attacks & opp_occ;
      while (targets) {
        const int to = pop_lsb(targets);
        const Square to_sq = static_cast<Square>(to);
        moves.push(make_move(static_cast<Square>(from),
                             to_sq,
                             moved,
                             position.piece_on(to_sq)));
      }

      targets = attacks & empty;
      while (targets) {
        const int to = pop_lsb(targets);
        moves.push(make_move(static_cast<Square>(from),
                             static_cast<Square>(to),
                             moved));
      }
    }
  }

  // Bishops
  {
    const Piece moved = make_piece(us, PieceType::Bishop);
    Bitboard bishops = position.pieces(us, PieceType::Bishop);
    while (bishops) {
      const int from = pop_lsb(bishops);
      const Bitboard attacks = magic::bishopAttacks(from, occ_all);
      Bitboard targets = attacks & opp_occ;
      while (targets) {
        const int to = pop_lsb(targets);
        const Square to_sq = static_cast<Square>(to);
        moves.push(make_move(static_cast<Square>(from),
                             to_sq,
                             moved,
                             position.piece_on(to_sq)));
      }

      targets = attacks & empty;
      while (targets) {
        const int to = pop_lsb(targets);
        moves.push(make_move(static_cast<Square>(from),
                             static_cast<Square>(to),
                             moved));
      }
    }
  }

  // Rooks
  {
    const Piece moved = make_piece(us, PieceType::Rook);
    Bitboard rooks = position.pieces(us, PieceType::Rook);
    while (rooks) {
      const int from = pop_lsb(rooks);
      const Bitboard attacks = magic::rookAttacks(from, occ_all);
      Bitboard targets = attacks & opp_occ;
      while (targets) {
        const int to = pop_lsb(targets);
        const Square to_sq = static_cast<Square>(to);
        moves.push(make_move(static_cast<Square>(from),
                             to_sq,
                             moved,
                             position.piece_on(to_sq)));
      }

      targets = attacks & empty;
      while (targets) {
        const int to = pop_lsb(targets);
        moves.push(make_move(static_cast<Square>(from),
                             static_cast<Square>(to),
                             moved));
      }
    }
  }

  // Queens
  {
    const Piece moved = make_piece(us, PieceType::Queen);
    Bitboard queens = position.pieces(us, PieceType::Queen);
    while (queens) {
      const int from = pop_lsb(queens);
      const Bitboard attacks = magic::queenAttacks(from, occ_all);
      Bitboard targets = attacks & opp_occ;
      while (targets) {
        const int to = pop_lsb(targets);
        const Square to_sq = static_cast<Square>(to);
        moves.push(make_move(static_cast<Square>(from),
                             to_sq,
                             moved,
                             position.piece_on(to_sq)));
      }

      targets = attacks & empty;
      while (targets) {
        const int to = pop_lsb(targets);
        moves.push(make_move(static_cast<Square>(from),
                             static_cast<Square>(to),
                             moved));
      }
    }
  }

  // King
  {
    const Piece moved = make_piece(us, PieceType::King);
    Bitboard king = position.pieces(us, PieceType::King);
    if (king) {
      const int from = lsb_index(king);
      const Bitboard attacks = attacks::KingAtt[from];
      Bitboard targets = attacks & opp_occ;
      while (targets) {
        const int to = pop_lsb(targets);
        const Square to_sq = static_cast<Square>(to);
        moves.push(make_move(static_cast<Square>(from),
                             to_sq,
                             moved,
                             position.piece_on(to_sq)));
      }

      targets = attacks & empty;
      while (targets) {
        const int to = pop_lsb(targets);
        moves.push(make_move(static_cast<Square>(from),
                             static_cast<Square>(to),
                             moved));
      }
    }
  }

  // Castling (pseudolegal)
  {
    const std::uint8_t rights = position.castling_rights();
    if (us == Color::White) {
      if ((rights & CastleWK) != 0) {
        const Bitboard empty_ks = square_bb(Square::F1) | square_bb(Square::G1);
        if ((occ_all & empty_ks) == 0) {
          moves.push(make_move(Square::E1,
                               Square::G1,
                               make_piece(us, PieceType::King),
                               Piece::None,
                               PieceType::None,
                               MoveFlags::Castle));
        }
      }
      if ((rights & CastleWQ) != 0) {
        const Bitboard empty_qs = square_bb(Square::B1)
                                | square_bb(Square::C1)
                                | square_bb(Square::D1);
        if ((occ_all & empty_qs) == 0) {
          moves.push(make_move(Square::E1,
                               Square::C1,
                               make_piece(us, PieceType::King),
                               Piece::None,
                               PieceType::None,
                               MoveFlags::Castle));
        }
      }
    } else {
      if ((rights & CastleBK) != 0) {
        const Bitboard empty_ks = square_bb(Square::F8) | square_bb(Square::G8);
        if ((occ_all & empty_ks) == 0) {
          moves.push(make_move(Square::E8,
                               Square::G8,
                               make_piece(us, PieceType::King),
                               Piece::None,
                               PieceType::None,
                               MoveFlags::Castle));
        }
      }
      if ((rights & CastleBQ) != 0) {
        const Bitboard empty_qs = square_bb(Square::B8)
                                | square_bb(Square::C8)
                                | square_bb(Square::D8);
        if ((occ_all & empty_qs) == 0) {
          moves.push(make_move(Square::E8,
                               Square::C8,
                               make_piece(us, PieceType::King),
                               Piece::None,
                               PieceType::None,
                               MoveFlags::Castle));
        }
      }
    }
  }
}

void generateLegalMoves(Position& position, MoveList& moves) {
  moves.clear();

  const Color us = position.side_to_move();
  const Color them = opposite(us);
  const Square king_sq = position.king_square(us);
  const Bitboard occ_all = position.occ_all();
  const Bitboard occ_us = position.occ(us);
  const Bitboard occ_them = position.occ(them);
  const Bitboard occ_without_king = occ_all & ~square_bb(king_sq);
  const Bitboard enemy_attacks_king = position.enemyAttackMap(them, occ_without_king);
  const Bitboard enemy_attacks_castle = position.enemyAttackMap(them, occ_all);
  const std::uint8_t rights = position.castling_rights();
  bool castle_k_ok = true;
  bool castle_q_ok = true;
  if (us == Color::White) {
    if (rights & CastleWK) {
      const Bitboard mask = square_bb(Square::E1) | square_bb(Square::F1) | square_bb(Square::G1);
      castle_k_ok = (enemy_attacks_castle & mask) == 0;
    }
    if (rights & CastleWQ) {
      const Bitboard mask = square_bb(Square::E1) | square_bb(Square::D1) | square_bb(Square::C1);
      castle_q_ok = (enemy_attacks_castle & mask) == 0;
    }
  } else {
    if (rights & CastleBK) {
      const Bitboard mask = square_bb(Square::E8) | square_bb(Square::F8) | square_bb(Square::G8);
      castle_k_ok = (enemy_attacks_castle & mask) == 0;
    }
    if (rights & CastleBQ) {
      const Bitboard mask = square_bb(Square::E8) | square_bb(Square::D8) | square_bb(Square::C8);
      castle_q_ok = (enemy_attacks_castle & mask) == 0;
    }
  }

  const CheckInfo info = compute_check_info(position, us);
  const int checker_count = popcount(info.checkers);
  const bool in_check = checker_count > 0;

  auto push_king_moves = [&]() {
    Bitboard king = position.pieces(us, PieceType::King);
    if (!king) {
      return;
    }
    const int from = lsb_index(king);
    Bitboard targets = attacks::KingAtt[from] & ~occ_us;
    while (targets) {
      const int to = pop_lsb(targets);
      const Square to_sq = static_cast<Square>(to);
      const Bitboard to_mask = square_bb(to_sq);
      if ((to_mask & occ_them) == 0) {
        if ((to_mask & enemy_attacks_king) == 0) {
          moves.push(make_move(static_cast<Square>(from), to_sq, make_piece(us, PieceType::King)));
        }
        continue;
      }
      Undo undo{};
      const Move move = make_move(static_cast<Square>(from),
                                  to_sq,
                                  make_piece(us, PieceType::King),
                                  position.piece_on(to_sq));
      doMove(position, move, undo);
      const bool safe = !position.inCheck(us);
      undoMove(position, move, undo);
      if (safe) {
        moves.push(move);
      }
    }
  };

  if (in_check) {
    if (checker_count >= 2) {
      push_king_moves();
      return;
    }

    const int checker_idx = lsb_index(info.checkers);
    const Square checker_sq = static_cast<Square>(checker_idx);
    const Piece checker_piece = position.piece_on(checker_sq);
    const PieceType checker_type = piece_type_of(checker_piece);
    Bitboard check_mask = square_bb(checker_sq);
    if (checker_type == PieceType::Bishop
        || checker_type == PieceType::Rook
        || checker_type == PieceType::Queen) {
      check_mask = LineBB[square_index(king_sq)][checker_idx];
    }

    push_king_moves();

    const Bitboard pinned = info.pinned;

    // Pawn evasions: captures and blocks.
    {
      Bitboard pawns = position.pieces(us, PieceType::Pawn);
      if (pawns) {
        const int color_idx = static_cast<int>(us);
        Bitboard pawns_caps = pawns;
        while (pawns_caps) {
          const int from = pop_lsb(pawns_caps);
          Bitboard targets = attacks::PawnAtt[color_idx][from] & occ_them & check_mask;
          while (targets) {
            const int to = pop_lsb(targets);
            if ((pinned & square_bb(static_cast<Square>(from))) != 0) {
              if ((square_bb(static_cast<Square>(to)) & info.pin_mask[from]) == 0) {
                continue;
              }
            }
            const Square to_sq = static_cast<Square>(to);
            const Rank to_rank = rank_of(to_sq);
            const Piece captured = position.piece_on(to_sq);
            if (to_rank == (us == Color::White ? Rank::Rank8 : Rank::Rank1)) {
              push_promotions(moves, static_cast<Square>(from), to_sq, make_piece(us, PieceType::Pawn), captured);
            } else {
              moves.push(make_move(static_cast<Square>(from),
                                   to_sq,
                                   make_piece(us, PieceType::Pawn),
                                   captured));
            }
          }
        }

        if (us == Color::White) {
          Bitboard single = shift_north(pawns) & ~occ_all & check_mask;
          Bitboard promotions = single & Rank8Mask;
          Bitboard quiets = single & ~Rank8Mask;
          while (promotions) {
            const int to = pop_lsb(promotions);
            const int from = to - 8;
            if ((pinned & square_bb(static_cast<Square>(from))) != 0) {
              if ((square_bb(static_cast<Square>(to)) & info.pin_mask[from]) == 0) {
                continue;
              }
            }
            push_promotions(moves,
                            static_cast<Square>(from),
                            static_cast<Square>(to),
                            make_piece(us, PieceType::Pawn));
          }
          while (quiets) {
            const int to = pop_lsb(quiets);
            const int from = to - 8;
            if ((pinned & square_bb(static_cast<Square>(from))) != 0) {
              if ((square_bb(static_cast<Square>(to)) & info.pin_mask[from]) == 0) {
                continue;
              }
            }
            moves.push(make_move(static_cast<Square>(from),
                                 static_cast<Square>(to),
                                 make_piece(us, PieceType::Pawn)));
          }
          const Bitboard rank2 = pawns & RankBB[static_cast<int>(Rank::Rank2)];
          const Bitboard single_from_rank2 = shift_north(rank2) & ~occ_all;
          Bitboard doubles = shift_north(single_from_rank2) & ~occ_all & check_mask;
          while (doubles) {
            const int to = pop_lsb(doubles);
            const int from = to - 16;
            if ((pinned & square_bb(static_cast<Square>(from))) != 0) {
              if ((square_bb(static_cast<Square>(to)) & info.pin_mask[from]) == 0) {
                continue;
              }
            }
            moves.push(make_move(static_cast<Square>(from),
                                 static_cast<Square>(to),
                                 make_piece(us, PieceType::Pawn),
                                 Piece::None,
                                 PieceType::None,
                                 MoveFlags::DoublePush));
          }
        } else {
          Bitboard single = shift_south(pawns) & ~occ_all & check_mask;
          Bitboard promotions = single & Rank1Mask;
          Bitboard quiets = single & ~Rank1Mask;
          while (promotions) {
            const int to = pop_lsb(promotions);
            const int from = to + 8;
            if ((pinned & square_bb(static_cast<Square>(from))) != 0) {
              if ((square_bb(static_cast<Square>(to)) & info.pin_mask[from]) == 0) {
                continue;
              }
            }
            push_promotions(moves,
                            static_cast<Square>(from),
                            static_cast<Square>(to),
                            make_piece(us, PieceType::Pawn));
          }
          while (quiets) {
            const int to = pop_lsb(quiets);
            const int from = to + 8;
            if ((pinned & square_bb(static_cast<Square>(from))) != 0) {
              if ((square_bb(static_cast<Square>(to)) & info.pin_mask[from]) == 0) {
                continue;
              }
            }
            moves.push(make_move(static_cast<Square>(from),
                                 static_cast<Square>(to),
                                 make_piece(us, PieceType::Pawn)));
          }
          const Bitboard rank7 = pawns & RankBB[static_cast<int>(Rank::Rank7)];
          const Bitboard single_from_rank7 = shift_south(rank7) & ~occ_all;
          Bitboard doubles = shift_south(single_from_rank7) & ~occ_all & check_mask;
          while (doubles) {
            const int to = pop_lsb(doubles);
            const int from = to + 16;
            if ((pinned & square_bb(static_cast<Square>(from))) != 0) {
              if ((square_bb(static_cast<Square>(to)) & info.pin_mask[from]) == 0) {
                continue;
              }
            }
            moves.push(make_move(static_cast<Square>(from),
                                 static_cast<Square>(to),
                                 make_piece(us, PieceType::Pawn),
                                 Piece::None,
                                 PieceType::None,
                                 MoveFlags::DoublePush));
          }
        }
      }
    }

    // En passant evasion if it captures the checking pawn.
    const Square ep = position.ep_square();
    if (ep != Square::None && piece_type_of(checker_piece) == PieceType::Pawn) {
      const Square cap_sq = us == Color::White
                              ? static_cast<Square>(square_index(ep) - 8)
                              : static_cast<Square>(square_index(ep) + 8);
      if (cap_sq == checker_sq) {
        Bitboard ep_bb = square_bb(ep);
        Bitboard attackers = 0;
        if (us == Color::White) {
          attackers = shift_south_east(ep_bb) | shift_south_west(ep_bb);
        } else {
          attackers = shift_north_east(ep_bb) | shift_north_west(ep_bb);
        }
        attackers &= position.pieces(us, PieceType::Pawn);
        while (attackers) {
          const int from = pop_lsb(attackers);
          if ((pinned & square_bb(static_cast<Square>(from))) != 0) {
            if ((square_bb(ep) & info.pin_mask[from]) == 0) {
              continue;
            }
          }
          const Move move = make_move(static_cast<Square>(from),
                                      ep,
                                      make_piece(us, PieceType::Pawn),
                                      make_piece(them, PieceType::Pawn),
                                      PieceType::None,
                                      MoveFlags::EnPassant);
          Undo undo{};
          doMove(position, move, undo);
          const bool safe = !position.inCheck(us);
          undoMove(position, move, undo);
          if (safe) {
            moves.push(move);
          }
        }
      }
    }

    auto add_sliding_evasion = [&](PieceType type, auto attack_fn) {
      Bitboard pieces = position.pieces(us, type);
      const Piece moved = make_piece(us, type);
      while (pieces) {
        const int from = pop_lsb(pieces);
        Bitboard targets = attack_fn(from, occ_all) & check_mask & ~occ_us;
        while (targets) {
          const int to = pop_lsb(targets);
          if ((pinned & square_bb(static_cast<Square>(from))) != 0) {
            if ((square_bb(static_cast<Square>(to)) & info.pin_mask[from]) == 0) {
              continue;
            }
          }
          const Square to_sq = static_cast<Square>(to);
          moves.push(make_move(static_cast<Square>(from),
                               to_sq,
                               moved,
                               position.piece_on(to_sq)));
        }
      }
    };

    // Knights (non-sliding)
    {
      Bitboard knights = position.pieces(us, PieceType::Knight);
      const Piece moved = make_piece(us, PieceType::Knight);
      while (knights) {
        const int from = pop_lsb(knights);
        Bitboard targets = attacks::KnightAtt[from] & check_mask & ~occ_us;
        while (targets) {
          const int to = pop_lsb(targets);
          if ((pinned & square_bb(static_cast<Square>(from))) != 0) {
            if ((square_bb(static_cast<Square>(to)) & info.pin_mask[from]) == 0) {
              continue;
            }
          }
          const Square to_sq = static_cast<Square>(to);
          moves.push(make_move(static_cast<Square>(from),
                               to_sq,
                               moved,
                               position.piece_on(to_sq)));
        }
      }
    }

    add_sliding_evasion(PieceType::Bishop, magic::bishopAttacks);
    add_sliding_evasion(PieceType::Rook, magic::rookAttacks);
    add_sliding_evasion(PieceType::Queen, magic::queenAttacks);

    return;
  }

  MoveList pseudo;
  generateMoves(position, pseudo);
  Bitboard check_mask = ~0ULL;
  Square checker_sq = Square::None;
  if (checker_count == 1) {
    const int checker_idx = lsb_index(info.checkers);
    checker_sq = static_cast<Square>(checker_idx);
    const Piece checker_piece = position.piece_on(checker_sq);
    const PieceType checker_type = piece_type_of(checker_piece);
    if (checker_type == PieceType::Bishop
        || checker_type == PieceType::Rook
        || checker_type == PieceType::Queen) {
      check_mask = LineBB[square_index(position.king_square(us))][checker_idx];
    } else {
      check_mask = square_bb(checker_sq);
    }
  }

  for (std::size_t i = 0; i < pseudo.size(); ++i) {
    const Move move = pseudo[i];
    const Square from = from_square(move);
    const Square to = to_square(move);
    const Piece moving = position.piece_on(from);
    const PieceType moving_type = piece_type_of(moving);
    const bool is_king = moving_type == PieceType::King;
    const bool is_castle = any_flag(move_flags(move), MoveFlags::Castle);
    bool castle_ok = true;
    if (is_castle) {
      if (in_check) {
        continue;
      }
      if (us == Color::White) {
        if (to == Square::G1) {
          castle_ok = castle_k_ok;
        } else if (to == Square::C1) {
          castle_ok = castle_q_ok;
        }
      } else {
        if (to == Square::G8) {
          castle_ok = castle_k_ok;
        } else if (to == Square::C8) {
          castle_ok = castle_q_ok;
        }
      }
    }

    const bool is_ep = any_flag(move_flags(move), MoveFlags::EnPassant);

    if (checker_count >= 2 && !is_king) {
      continue;
    }

    if (checker_count == 1 && !is_king) {
      const Bitboard to_mask = square_bb(to);
      if ((to_mask & check_mask) == 0) {
        if (!is_ep) {
          continue;
        }
        const Square cap_sq = us == Color::White
                                ? static_cast<Square>(square_index(to) - 8)
                                : static_cast<Square>(square_index(to) + 8);
        if (cap_sq != checker_sq) {
          continue;
        }
      }
    }

    if (info.pinned & square_bb(from)) {
      if ((square_bb(to) & info.pin_mask[square_index(from)]) == 0) {
        continue;
      }
    }

    if (is_castle) {
      if (castle_ok) {
        moves.push(move);
      }
      continue;
    }

    if (is_king) {
      const Bitboard to_mask = square_bb(to);
      if ((to_mask & occ_us) != 0) {
        continue;
      }
      if ((to_mask & occ_them) == 0) {
        if ((to_mask & enemy_attacks_king) == 0) {
          moves.push(move);
        }
        continue;
      }

      Undo undo{};
      doMove(position, move, undo);
      const bool safe = !position.inCheck(us);
      undoMove(position, move, undo);
      if (safe) {
        moves.push(move);
      }
      continue;
    }

    if (is_ep) {
      Undo undo{};
      doMove(position, move, undo);
      const bool safe = !position.inCheck(us);
      undoMove(position, move, undo);
      if (safe) {
        moves.push(move);
      }
      continue;
    }

    if (!in_check) {
      moves.push(move);
      continue;
    }

    Undo undo{};
    doMove(position, move, undo);
    const bool safe = castle_ok && !position.inCheck(us);
    undoMove(position, move, undo);
    if (safe) {
      moves.push(move);
    }
  }
}

void resetMovegenProfile() {
}

void dumpMovegenProfile(std::ostream& out) {
  (void)out;
}

}  // namespace aurora
