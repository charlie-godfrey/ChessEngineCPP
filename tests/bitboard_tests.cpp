#include "bitboard.h"

#include <cassert>
#include <cstdint>

int main() {
  using aurora::Bitboard;

  {
    Bitboard bb = aurora::SquareBB[static_cast<int>(aurora::Square::A1)]
                  | aurora::SquareBB[static_cast<int>(aurora::Square::D4)];
    const int first = aurora::pop_lsb(bb);
    assert(first == static_cast<int>(aurora::Square::A1));
    const int second = aurora::pop_lsb(bb);
    assert(second == static_cast<int>(aurora::Square::D4));
    assert(bb == 0);
  }

  {
    const auto a1 = aurora::SquareBB[static_cast<int>(aurora::Square::A1)];
    const auto h8 = aurora::SquareBB[static_cast<int>(aurora::Square::H8)];
    assert(a1 == 1ULL);
    assert(h8 == (1ULL << 63));
  }

  {
    const auto file_a = aurora::FileBB[static_cast<int>(aurora::File::FileA)];
    const auto rank_1 = aurora::RankBB[static_cast<int>(aurora::Rank::Rank1)];
    assert(file_a == 0x0101010101010101ULL);
    assert(rank_1 == 0x00000000000000FFULL);
  }

  return 0;
}
