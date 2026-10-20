#include "attacks.h"

#include <iostream>

namespace aurora::attacks {

AttackTable KnightAtt{};
AttackTable KingAtt{};
std::array<AttackTable, 2> PawnAtt{};

static bool ready = false;

static constexpr Bitboard FileBMask = FileAMask << 1;
static constexpr Bitboard FileGMask = FileHMask >> 1;

static Bitboard knight_attacks(Bitboard bb) {
  Bitboard attacks = 0;
  attacks |= (bb & ~FileHMask) << 17;
  attacks |= (bb & ~FileAMask) << 15;
  attacks |= (bb & ~(FileGMask | FileHMask)) << 10;
  attacks |= (bb & ~(FileAMask | FileBMask)) << 6;
  attacks |= (bb & ~FileHMask) >> 15;
  attacks |= (bb & ~FileAMask) >> 17;
  attacks |= (bb & ~(FileGMask | FileHMask)) >> 6;
  attacks |= (bb & ~(FileAMask | FileBMask)) >> 10;
  return attacks;
}

static Bitboard king_attacks(Bitboard bb) {
  Bitboard attacks = 0;
  attacks |= shift_north(bb);
  attacks |= shift_south(bb);
  attacks |= shift_east(bb);
  attacks |= shift_west(bb);
  attacks |= shift_north_east(bb);
  attacks |= shift_north_west(bb);
  attacks |= shift_south_east(bb);
  attacks |= shift_south_west(bb);
  return attacks;
}

void init_attacks() {
  if (ready) {
    return;
  }

  for (int sq = 0; sq < 64; ++sq) {
    const Bitboard bb = 1ULL << sq;
    KnightAtt[sq] = knight_attacks(bb);
    KingAtt[sq] = king_attacks(bb);

    PawnAtt[static_cast<int>(Color::White)][sq]
        = shift_north_east(bb) | shift_north_west(bb);
    PawnAtt[static_cast<int>(Color::Black)][sq]
        = shift_south_east(bb) | shift_south_west(bb);
  }

  ready = true;
}

bool initialized() {
  return ready;
}

void print_bitboard(Bitboard bb, std::ostream& out) {
  for (int rank = 7; rank >= 0; --rank) {
    for (int file = 0; file < 8; ++file) {
      const int sq = (rank << 3) + file;
      out << (((bb >> sq) & 1ULL) ? "1 " : ". ");
    }
    out << '\n';
  }
}

}  // namespace aurora::attacks
