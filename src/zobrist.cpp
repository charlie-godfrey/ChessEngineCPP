#include "zobrist.h"

#include "bitboard.h"
#include "position.h"
#include "types.h"

namespace aurora::zobrist {

PieceTable piece{};
std::array<ZobristKey, 16> castling{};
std::array<ZobristKey, 8> ep_file{};
ZobristKey side = 0;

static bool ready = false;

// Deterministic seed constant for reproducible Zobrist key generation
static constexpr std::uint64_t kZobristSeed = 0xA57C0FFEEBADC0DEULL;

static std::uint64_t splitmix64(std::uint64_t& seed) {
  std::uint64_t z = (seed += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

void init_zobrist() {
  if (ready) {
    return;
  }

  std::uint64_t seed = kZobristSeed;

  // Generate random 64-bit keys for piece-square combinations (2 colors * 6 types * 64 squares)
  for (int color = 0; color < 2; ++color) {
    for (int type = 0; type < 6; ++type) {
      for (int sq = 0; sq < 64; ++sq) {
        piece[color][type][sq] = splitmix64(seed);
      }
    }
  }

  // Generate random 64-bit keys for castling rights (16 combinations)
  for (int i = 0; i < 16; ++i) {
    castling[i] = splitmix64(seed);
  }

  // Generate random 64-bit keys for en passant file (8 files)
  for (int i = 0; i < 8; ++i) {
    ep_file[i] = splitmix64(seed);
  }

  // Generate random 64-bit key for side-to-move
  side = splitmix64(seed);
  ready = true;
}

bool initialized() {
  return ready;
}

ZobristKey computeZobrist(Position& position) {
  ZobristKey key = 0;

  // XOR in all piece-square combinations
  for (int color = 0; color < 2; ++color) {
    for (int type = 0; type < 6; ++type) {
      Bitboard bb = position.pieces(static_cast<Color>(color), static_cast<PieceType>(type));
      while (bb) {
        const int sq = pop_lsb(bb);
        key ^= piece[color][type][sq];
      }
    }
  }

  // XOR in castling rights
  key ^= castling[position.castling_rights() & 0xF];

  // XOR in side-to-move (Black to move)
  if (position.side_to_move() == Color::Black) {
    key ^= side;
  }

  // XOR in en passant file if applicable
  const Square ep_sq = position.ep_square();
  if (ep_sq != Square::None) {
    const int file = file_index(file_of(ep_sq));
    if (file >= 0 && file < 8) {
      key ^= ep_file[file];
    }
  }

  return key;
}

}  // namespace aurora::zobrist
