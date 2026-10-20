#pragma once

#include <cstdint>

namespace aurora::nnue {

struct NnueHeader {
  char magic[8];
  std::uint32_t version = 2;
  std::uint32_t feature_dim = 0;
  std::uint32_t acc_dim = 0;
  std::uint32_t h1_dim = 0;
  std::uint32_t h2_dim = 0;
  std::int32_t in_scale = 256;
  std::int32_t out_scale = 256;
  std::int32_t activation_max = 127;
};

}  // namespace aurora::nnue
