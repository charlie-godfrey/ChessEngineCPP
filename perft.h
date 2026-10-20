#pragma once

#include "position.h"

#include <cstdint>

namespace aurora {

std::uint64_t perft(Position& position, int depth);
std::uint64_t perft_divide(Position& position, int depth);

}  // namespace aurora
