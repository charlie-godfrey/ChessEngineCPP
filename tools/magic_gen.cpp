#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

using U64 = std::uint64_t;

constexpr U64 FileAMask = 0x0101010101010101ULL;
constexpr U64 FileHMask = 0x8080808080808080ULL;

static U64 splitmix64(U64& seed) {
  U64 z = (seed += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

static U64 random_fewbits(U64& seed) {
  return splitmix64(seed) & splitmix64(seed) & splitmix64(seed);
}

static int popcount(U64 bb) {
  return static_cast<int>(__builtin_popcountll(bb));
}

static int lsb_index(U64 bb) {
  return bb == 0 ? -1 : __builtin_ctzll(bb);
}

static U64 mask_rook(int sq) {
  U64 mask = 0;
  const int rank = sq / 8;
  const int file = sq % 8;
  for (int r = rank + 1; r <= 6; ++r) mask |= 1ULL << (r * 8 + file);
  for (int r = rank - 1; r >= 1; --r) mask |= 1ULL << (r * 8 + file);
  for (int f = file + 1; f <= 6; ++f) mask |= 1ULL << (rank * 8 + f);
  for (int f = file - 1; f >= 1; --f) mask |= 1ULL << (rank * 8 + f);
  return mask;
}

static U64 mask_bishop(int sq) {
  U64 mask = 0;
  const int rank = sq / 8;
  const int file = sq % 8;
  for (int r = rank + 1, f = file + 1; r <= 6 && f <= 6; ++r, ++f) {
    mask |= 1ULL << (r * 8 + f);
  }
  for (int r = rank + 1, f = file - 1; r <= 6 && f >= 1; ++r, --f) {
    mask |= 1ULL << (r * 8 + f);
  }
  for (int r = rank - 1, f = file + 1; r >= 1 && f <= 6; --r, ++f) {
    mask |= 1ULL << (r * 8 + f);
  }
  for (int r = rank - 1, f = file - 1; r >= 1 && f >= 1; --r, --f) {
    mask |= 1ULL << (r * 8 + f);
  }
  return mask;
}

static U64 rook_attacks(int sq, U64 occ) {
  U64 attacks = 0;
  const int rank = sq / 8;
  const int file = sq % 8;

  for (int r = rank + 1; r <= 7; ++r) {
    const int s = r * 8 + file;
    attacks |= 1ULL << s;
    if (occ & (1ULL << s)) break;
  }
  for (int r = rank - 1; r >= 0; --r) {
    const int s = r * 8 + file;
    attacks |= 1ULL << s;
    if (occ & (1ULL << s)) break;
  }
  for (int f = file + 1; f <= 7; ++f) {
    const int s = rank * 8 + f;
    attacks |= 1ULL << s;
    if (occ & (1ULL << s)) break;
  }
  for (int f = file - 1; f >= 0; --f) {
    const int s = rank * 8 + f;
    attacks |= 1ULL << s;
    if (occ & (1ULL << s)) break;
  }

  return attacks;
}

static U64 bishop_attacks(int sq, U64 occ) {
  U64 attacks = 0;
  const int rank = sq / 8;
  const int file = sq % 8;

  for (int r = rank + 1, f = file + 1; r <= 7 && f <= 7; ++r, ++f) {
    const int s = r * 8 + f;
    attacks |= 1ULL << s;
    if (occ & (1ULL << s)) break;
  }
  for (int r = rank + 1, f = file - 1; r <= 7 && f >= 0; ++r, --f) {
    const int s = r * 8 + f;
    attacks |= 1ULL << s;
    if (occ & (1ULL << s)) break;
  }
  for (int r = rank - 1, f = file + 1; r >= 0 && f <= 7; --r, ++f) {
    const int s = r * 8 + f;
    attacks |= 1ULL << s;
    if (occ & (1ULL << s)) break;
  }
  for (int r = rank - 1, f = file - 1; r >= 0 && f >= 0; --r, --f) {
    const int s = r * 8 + f;
    attacks |= 1ULL << s;
    if (occ & (1ULL << s)) break;
  }

  return attacks;
}

static std::vector<U64> enumerate_occ(U64 mask) {
  const int bits = popcount(mask);
  const int count = 1 << bits;
  std::vector<U64> occs(count, 0);

  std::vector<int> squares;
  squares.reserve(bits);
  U64 tmp = mask;
  while (tmp) {
    int sq = lsb_index(tmp);
    squares.push_back(sq);
    tmp &= tmp - 1;
  }

  for (int idx = 0; idx < count; ++idx) {
    U64 occ = 0;
    for (int bit = 0; bit < bits; ++bit) {
      if (idx & (1 << bit)) {
        occ |= 1ULL << squares[bit];
      }
    }
    occs[idx] = occ;
  }

  return occs;
}

static U64 find_magic(int sq, bool rook) {
  const U64 mask = rook ? mask_rook(sq) : mask_bishop(sq);
  const int bits = popcount(mask);
  const int size = 1 << bits;

  const auto occs = enumerate_occ(mask);
  std::vector<U64> attacks(size);
  for (int i = 0; i < size; ++i) {
    attacks[i] = rook ? rook_attacks(sq, occs[i]) : bishop_attacks(sq, occs[i]);
  }

  U64 seed = 0xA57C0FFEEBADC0DEULL ^ (static_cast<U64>(sq) * 0x9E3779B97F4A7C15ULL);
  std::vector<U64> used(size);
  std::vector<bool> filled(size);

  for (;;) {
    U64 magic = random_fewbits(seed);
    if (popcount((mask * magic) & 0xFF00000000000000ULL) < 6) {
      continue;
    }

    std::fill(used.begin(), used.end(), 0);
    std::fill(filled.begin(), filled.end(), false);

    bool fail = false;
    for (int i = 0; i < size; ++i) {
      const U64 index = (occs[i] * magic) >> (64 - bits);
      if (!filled[index]) {
        filled[index] = true;
        used[index] = attacks[i];
      } else if (used[index] != attacks[i]) {
        fail = true;
        break;
      }
    }

    if (!fail) {
      return magic;
    }
  }
}

int main() {
  std::array<U64, 64> rook_magics{};
  std::array<U64, 64> bishop_magics{};

  for (int sq = 0; sq < 64; ++sq) {
    rook_magics[sq] = find_magic(sq, true);
    bishop_magics[sq] = find_magic(sq, false);
    std::cerr << "square " << sq << " done\n";
  }

  auto print_array = [](const char* name, const std::array<U64, 64>& arr) {
    std::cout << "static constexpr U64 " << name << "[64] = {\n";
    for (int i = 0; i < 64; ++i) {
      std::cout << "  0x" << std::hex << std::setw(16) << std::setfill('0')
                << arr[i] << "ULL";
      if (i != 63) {
        std::cout << ",";
      }
      std::cout << "\n";
    }
    std::cout << "};\n";
  };

  print_array("RookMagic", rook_magics);
  print_array("BishopMagic", bishop_magics);
  return 0;
}
