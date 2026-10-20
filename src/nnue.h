#pragma once

#include "move.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace aurora {

class Position;

namespace nnue {

constexpr int kFeatureDim = 64 * 12 * 64;
constexpr int kAccDim = 256;
constexpr int kInputDim = 512;
constexpr int kH1Dim = 128;
constexpr int kH2Dim = 32;

struct NnueState {
  std::array<std::int32_t, kAccDim> acc_white{};
  std::array<std::int32_t, kAccDim> acc_black{};
  bool white_valid = false;
  bool black_valid = false;
};

bool load(const std::string& path);
bool isLoaded();
void setEnabled(bool enabled);
bool enabled();

void invalidate(NnueState& state);
void updateAddPiece(const Position& position, NnueState& state, Square square, Piece piece);
void updateRemovePiece(const Position& position, NnueState& state, Square square, Piece piece);

int evaluate(const Position& position);

void extractFeatures(const Position& position, Color perspective, std::vector<std::uint16_t>& out);

}  // namespace nnue
}  // namespace aurora
