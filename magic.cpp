#include "magic.h"

#include <bit>

namespace aurora::magic {

std::array<Bitboard, 64> RookMask{};
std::array<Bitboard, 64> BishopMask{};
std::array<std::uint8_t, 64> RookShift{};
std::array<std::uint8_t, 64> BishopShift{};
std::array<std::array<Bitboard, 4096>, 64> RookTable{};
std::array<std::array<Bitboard, 512>, 64> BishopTable{};

std::array<U64, 64> RookMagic = {
  0x2080034000152080ULL,
  0x2440004020001000ULL,
  0x0780100008822000ULL,
  0x0600042008c01200ULL,
  0x0200100200040820ULL,
  0x4300020100080400ULL,
  0x1480008001000200ULL,
  0x2880003041000080ULL,
  0x0040800020400081ULL,
  0x0c82004205210381ULL,
  0x4a00801000200080ULL,
  0x2001000a10030020ULL,
  0x2008800800840080ULL,
  0x4422800200800400ULL,
  0x0000800100800200ULL,
  0x10020004a1084204ULL,
  0x804000800142a490ULL,
  0x0c40004040201000ULL,
  0x0040860010224200ULL,
  0x0020220040120008ULL,
  0x0000808004000800ULL,
  0x8006008080040002ULL,
  0x4004040021904208ULL,
  0x0001020004224093ULL,
  0x0500400480028174ULL,
  0x0100500240002000ULL,
  0x0490080020200400ULL,
  0x8280100080080080ULL,
  0x0508080080800400ULL,
  0x0202008200045048ULL,
  0x0080018400300208ULL,
  0x0100205200208401ULL,
  0x0180004001402008ULL,
  0x0020044022401004ULL,
  0x0119001049002000ULL,
  0x000201c052002008ULL,
  0x0400800400800800ULL,
  0x4004000200800480ULL,
  0x2208880204000110ULL,
  0x0010010042000084ULL,
  0x0000804000208000ULL,
  0x0040100800202001ULL,
  0x1142004080120022ULL,
  0x0600100008008080ULL,
  0x0391000800050010ULL,
  0x0028040002008080ULL,
  0x0400010002008080ULL,
  0x2062108400420001ULL,
  0x4012290040860200ULL,
  0x0020201004400240ULL,
  0x0086200011004100ULL,
  0x1215000810002100ULL,
  0x8000410080201002ULL,
  0x0400040080020080ULL,
  0x40b0800100020080ULL,
  0x0006008401004200ULL,
  0x008810270180c202ULL,
  0x0480102102884202ULL,
  0x8084881102200043ULL,
  0x1881000422081001ULL,
  0x0001000402100801ULL,
  0x0001000204000801ULL,
  0x801150234802018cULL,
  0x0010010a41840522ULL
};

std::array<U64, 64> BishopMagic = {
  0x0022821006088102ULL,
  0xe00424009e22040aULL,
  0x0010208220404008ULL,
  0x8a84404d80222000ULL,
  0x0002121001008006ULL,
  0x0001044240000008ULL,
  0x0100411010100008ULL,
  0x0282002882082010ULL,
  0x80002020a4010048ULL,
  0x4001424821410a01ULL,
  0xa00010008081100cULL,
  0x1040882060400090ULL,
  0x0004011140040280ULL,
  0x6284738260218104ULL,
  0x0807010a900c2054ULL,
  0x0400004042082000ULL,
  0x0040402042024205ULL,
  0x0242800450140100ULL,
  0x000100500704a301ULL,
  0x1804000824009400ULL,
  0x4005048820081002ULL,
  0x020700020105290cULL,
  0x8200900048084886ULL,
  0x0801011040480400ULL,
  0x28484100a0020220ULL,
  0x0044300020410150ULL,
  0x0c08140a08004010ULL,
  0x0a40040022101090ULL,
  0x5910040003802100ULL,
  0x0008020809100200ULL,
  0x4006240106008a01ULL,
  0x1800404202120208ULL,
  0x3001110802112004ULL,
  0x0008080400222c40ULL,
  0x2894040400084040ULL,
  0x0808200900200900ULL,
  0x0004028400820102ULL,
  0x0020021c10008040ULL,
  0x22108a0c420e010cULL,
  0x20440400880c4048ULL,
  0x7144104210100800ULL,
  0x0010880110148800ULL,
  0x0020104030004801ULL,
  0x0400020202018420ULL,
  0x0040411024010280ULL,
  0x0008305082006020ULL,
  0x0004080204000052ULL,
  0x0c4410c410400100ULL,
  0x0002080248448800ULL,
  0x00024110b0100200ULL,
  0x0020004600d00008ULL,
  0x0404000046081116ULL,
  0x0005000810240430ULL,
  0x4000081010008480ULL,
  0x0008281044006000ULL,
  0x0012500202a10000ULL,
  0x48420042209c2000ULL,
  0x5020502882082004ULL,
  0x1000040622091021ULL,
  0x8000100204840400ULL,
  0x0020080040914108ULL,
  0x1800014108010441ULL,
  0x4800403002c89500ULL,
  0x029a020801051202ULL
};

static bool ready = false;

static Bitboard rook_mask(int sq) {
  Bitboard mask = 0;
  const int rank = sq / 8;
  const int file = sq % 8;
  for (int r = rank + 1; r <= 6; ++r) mask |= 1ULL << (r * 8 + file);
  for (int r = rank - 1; r >= 1; --r) mask |= 1ULL << (r * 8 + file);
  for (int f = file + 1; f <= 6; ++f) mask |= 1ULL << (rank * 8 + f);
  for (int f = file - 1; f >= 1; --f) mask |= 1ULL << (rank * 8 + f);
  return mask;
}

static Bitboard bishop_mask(int sq) {
  Bitboard mask = 0;
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

static Bitboard rook_attacks(int sq, Bitboard occ) {
  Bitboard attacks = 0;
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

static Bitboard bishop_attacks(int sq, Bitboard occ) {
  Bitboard attacks = 0;
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

void init_magic() {
  if (ready) {
    return;
  }

  for (int sq = 0; sq < 64; ++sq) {
    const Bitboard rmask = rook_mask(sq);
    const Bitboard bmask = bishop_mask(sq);
    RookMask[sq] = rmask;
    BishopMask[sq] = bmask;

    const int rbits = std::popcount(rmask);
    const int bbits = std::popcount(bmask);
    RookShift[sq] = static_cast<std::uint8_t>(64 - rbits);
    BishopShift[sq] = static_cast<std::uint8_t>(64 - bbits);

    const int rcount = 1 << rbits;
    const int bcount = 1 << bbits;

    for (int i = 0; i < rcount; ++i) {
      RookTable[sq][i] = 0;
    }
    for (int i = 0; i < bcount; ++i) {
      BishopTable[sq][i] = 0;
    }

    Bitboard subset = 0;
    while (true) {
      const std::uint64_t rindex = (subset * RookMagic[sq]) >> RookShift[sq];
      RookTable[sq][rindex] = rook_attacks(sq, subset);
      if (subset == rmask) {
        break;
      }
      subset = (subset - rmask) & rmask;
    }

    subset = 0;
    while (true) {
      const std::uint64_t bindex = (subset * BishopMagic[sq]) >> BishopShift[sq];
      BishopTable[sq][bindex] = bishop_attacks(sq, subset);
      if (subset == bmask) {
        break;
      }
      subset = (subset - bmask) & bmask;
    }
  }

  ready = true;
}

bool initialized() {
  return ready;
}

}  // namespace aurora::magic
