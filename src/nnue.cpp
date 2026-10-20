#include "nnue.h"

#include "nnue_format.h"
#include "bitboard.h"
#include "position.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace aurora::nnue {

namespace {

struct NnueWeights {
  NnueHeader header{};
  std::vector<std::int16_t> acc_bias;
  std::vector<std::int16_t> acc_weights;
  std::vector<std::int16_t> l1_bias;
  std::vector<std::int16_t> l1_weights;
  std::vector<std::int16_t> l2_bias;
  std::vector<std::int16_t> l2_weights;
  std::vector<std::int16_t> out_weights;
  std::int32_t out_bias = 0;
};

bool g_enabled = false;
NnueWeights g_weights{};

int orient_square(Color perspective, int sq) {
  return perspective == Color::White ? sq : (sq ^ 56);
}

int piece_index(Color perspective, Piece piece) {
  const Color color = color_of(piece);
  const PieceType type = piece_type_of(piece);
  if (type == PieceType::None) {
    return -1;
  }
  const int base = (color == perspective) ? 0 : 6;
  return base + static_cast<int>(type);
}

int feature_index(Color perspective, Square king_sq, Square sq, Piece piece) {
  if (king_sq == Square::None) {
    return -1;
  }
  const int pi = piece_index(perspective, piece);
  if (pi < 0) {
    return -1;
  }
  const int k = orient_square(perspective, square_index(king_sq));
  const int s = orient_square(perspective, square_index(sq));
  return k * (12 * 64) + pi * 64 + s;
}

void ensure_initialized(const Position& position, NnueState& state) {
  if (!isLoaded()) {
    return;
  }
  if (!state.white_valid) {
    state.acc_white.fill(0);
    for (int i = 0; i < kAccDim; ++i) {
      state.acc_white[i] = g_weights.acc_bias[i];
    }
    std::vector<std::uint16_t> feats;
    extractFeatures(position, Color::White, feats);
    for (std::uint16_t idx : feats) {
      const std::size_t base = static_cast<std::size_t>(idx) * kAccDim;
      for (int i = 0; i < kAccDim; ++i) {
        state.acc_white[i] += g_weights.acc_weights[base + i];
      }
    }
    state.white_valid = true;
  }
  if (!state.black_valid) {
    state.acc_black.fill(0);
    for (int i = 0; i < kAccDim; ++i) {
      state.acc_black[i] = g_weights.acc_bias[i];
    }
    std::vector<std::uint16_t> feats;
    extractFeatures(position, Color::Black, feats);
    for (std::uint16_t idx : feats) {
      const std::size_t base = static_cast<std::size_t>(idx) * kAccDim;
      for (int i = 0; i < kAccDim; ++i) {
        state.acc_black[i] += g_weights.acc_weights[base + i];
      }
    }
    state.black_valid = true;
  }
}

inline int clamp_activation(int v, int max_v) {
  if (v < 0) return 0;
  if (v > max_v) return max_v;
  return v;
}

int eval_network(const std::array<std::int32_t, kAccDim>& acc_a,
                 const std::array<std::int32_t, kAccDim>& acc_b,
                 int feature_count) {
  if (!isLoaded()) {
    return 0;
  }
  if (feature_count < 1) {
    feature_count = 1;
  }
  const int activation_max = g_weights.header.activation_max;
  const int in_scale = g_weights.header.in_scale;
  const int out_scale = g_weights.header.out_scale;

  std::array<int, kH1Dim> h1{};
  for (int i = 0; i < kH1Dim; ++i) {
    int sum = g_weights.l1_bias[i];
    const std::size_t base = static_cast<std::size_t>(i) * kInputDim;
    for (int j = 0; j < kAccDim; ++j) {
      const int v = static_cast<int>(acc_a[j] / in_scale / feature_count);
      sum += clamp_activation(v, activation_max) * g_weights.l1_weights[base + j];
    }
    for (int j = 0; j < kAccDim; ++j) {
      const int v = static_cast<int>(acc_b[j] / in_scale / feature_count);
      sum += clamp_activation(v, activation_max) * g_weights.l1_weights[base + kAccDim + j];
    }
    h1[i] = clamp_activation(sum, activation_max);
  }

  std::array<int, kH2Dim> h2{};
  for (int i = 0; i < kH2Dim; ++i) {
    int sum = g_weights.l2_bias[i];
    const std::size_t base = static_cast<std::size_t>(i) * kH1Dim;
    for (int j = 0; j < kH1Dim; ++j) {
      sum += h1[j] * g_weights.l2_weights[base + j];
    }
    h2[i] = clamp_activation(sum, activation_max);
  }

  std::int64_t out = g_weights.out_bias;
  for (int i = 0; i < kH2Dim; ++i) {
    out += static_cast<std::int64_t>(h2[i]) * g_weights.out_weights[i];
  }
  return static_cast<int>(out / out_scale);
}

}  // namespace

bool load(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  NnueHeader header{};
  in.read(reinterpret_cast<char*>(&header), sizeof(header));
  if (!in || std::memcmp(header.magic, "AURNNUE", 7) != 0) {
    return false;
  }
  if (header.version != 2 ||
      header.feature_dim != static_cast<std::uint32_t>(kFeatureDim) ||
      header.acc_dim != static_cast<std::uint32_t>(kAccDim) ||
      header.h1_dim != static_cast<std::uint32_t>(kH1Dim) ||
      header.h2_dim != static_cast<std::uint32_t>(kH2Dim)) {
    return false;
  }
  g_weights.header = header;
  g_weights.acc_bias.resize(kAccDim);
  g_weights.acc_weights.resize(static_cast<std::size_t>(kFeatureDim) * kAccDim);
  g_weights.l1_bias.resize(kH1Dim);
  g_weights.l1_weights.resize(static_cast<std::size_t>(kH1Dim) * kInputDim);
  g_weights.l2_bias.resize(kH2Dim);
  g_weights.l2_weights.resize(static_cast<std::size_t>(kH2Dim) * kH1Dim);
  g_weights.out_weights.resize(kH2Dim);

  in.read(reinterpret_cast<char*>(g_weights.acc_bias.data()),
          static_cast<std::streamsize>(g_weights.acc_bias.size() * sizeof(std::int16_t)));
  in.read(reinterpret_cast<char*>(g_weights.acc_weights.data()),
          static_cast<std::streamsize>(g_weights.acc_weights.size() * sizeof(std::int16_t)));
  in.read(reinterpret_cast<char*>(g_weights.l1_bias.data()),
          static_cast<std::streamsize>(g_weights.l1_bias.size() * sizeof(std::int16_t)));
  in.read(reinterpret_cast<char*>(g_weights.l1_weights.data()),
          static_cast<std::streamsize>(g_weights.l1_weights.size() * sizeof(std::int16_t)));
  in.read(reinterpret_cast<char*>(g_weights.l2_bias.data()),
          static_cast<std::streamsize>(g_weights.l2_bias.size() * sizeof(std::int16_t)));
  in.read(reinterpret_cast<char*>(g_weights.l2_weights.data()),
          static_cast<std::streamsize>(g_weights.l2_weights.size() * sizeof(std::int16_t)));
  in.read(reinterpret_cast<char*>(g_weights.out_weights.data()),
          static_cast<std::streamsize>(g_weights.out_weights.size() * sizeof(std::int16_t)));
  in.read(reinterpret_cast<char*>(&g_weights.out_bias), sizeof(g_weights.out_bias));
  return static_cast<bool>(in);
}

bool isLoaded() {
  return !g_weights.acc_weights.empty();
}

void setEnabled(bool enabled) {
  g_enabled = enabled;
}

bool enabled() {
  return g_enabled && isLoaded();
}

void invalidate(NnueState& state) {
  state.white_valid = false;
  state.black_valid = false;
}

void updateAddPiece(const Position& position, NnueState& state, Square square, Piece piece) {
  if (!isLoaded()) {
    return;
  }
  if (state.white_valid) {
    const int idx = feature_index(Color::White, position.king_square(Color::White), square, piece);
    if (idx >= 0) {
      const std::size_t base = static_cast<std::size_t>(idx) * kAccDim;
      for (int i = 0; i < kAccDim; ++i) {
        state.acc_white[i] += g_weights.acc_weights[base + i];
      }
    }
  }
  if (state.black_valid) {
    const int idx = feature_index(Color::Black, position.king_square(Color::Black), square, piece);
    if (idx >= 0) {
      const std::size_t base = static_cast<std::size_t>(idx) * kAccDim;
      for (int i = 0; i < kAccDim; ++i) {
        state.acc_black[i] += g_weights.acc_weights[base + i];
      }
    }
  }
}

void updateRemovePiece(const Position& position, NnueState& state, Square square, Piece piece) {
  if (!isLoaded()) {
    return;
  }
  if (state.white_valid) {
    const int idx = feature_index(Color::White, position.king_square(Color::White), square, piece);
    if (idx >= 0) {
      const std::size_t base = static_cast<std::size_t>(idx) * kAccDim;
      for (int i = 0; i < kAccDim; ++i) {
        state.acc_white[i] -= g_weights.acc_weights[base + i];
      }
    }
  }
  if (state.black_valid) {
    const int idx = feature_index(Color::Black, position.king_square(Color::Black), square, piece);
    if (idx >= 0) {
      const std::size_t base = static_cast<std::size_t>(idx) * kAccDim;
      for (int i = 0; i < kAccDim; ++i) {
        state.acc_black[i] -= g_weights.acc_weights[base + i];
      }
    }
  }
}

int evaluate(const Position& position) {
  if (!enabled()) {
    return 0;
  }
  auto& state = const_cast<Position&>(position).nnue_state();
  ensure_initialized(position, state);
  const int feature_count = popcount(position.occ_all());
  const Color stm = position.side_to_move();
  const int raw = (stm == Color::White)
                    ? eval_network(state.acc_white, state.acc_black, feature_count)
                    : eval_network(state.acc_black, state.acc_white, feature_count);
  return stm == Color::White ? raw : -raw;
}

void extractFeatures(const Position& position, Color perspective, std::vector<std::uint16_t>& out) {
  out.clear();
  const Square king_sq = position.king_square(perspective);
  if (king_sq == Square::None) {
    return;
  }
  for (int color = 0; color < 2; ++color) {
    for (int type = 0; type < 6; ++type) {
      Bitboard bb = position.pieces(static_cast<Color>(color), static_cast<PieceType>(type));
      while (bb) {
        const int sq = pop_lsb(bb);
        const Square square = static_cast<Square>(sq);
        const Piece piece = make_piece(static_cast<Color>(color), static_cast<PieceType>(type));
        const int idx = feature_index(perspective, king_sq, square, piece);
        if (idx >= 0) {
          out.push_back(static_cast<std::uint16_t>(idx));
        }
      }
    }
  }
}

}  // namespace aurora::nnue
