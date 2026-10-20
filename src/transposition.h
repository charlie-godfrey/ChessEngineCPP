#pragma once

#include "move.h"
#include "types.h"
#include "zobrist.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace aurora {

enum class BoundType : std::uint8_t {
  None = 0,
  Lower = 1,
  Upper = 2,
  Exact = 3
};

struct TTEntry {
  ZobristKey key = 0;
  Move bestMove = kNullMove;
  std::int16_t score = 0;
  std::int8_t depth = 0;
  BoundType bound = BoundType::None;
  std::uint8_t generation = 0;
};

class TranspositionTable {
 public:
  TranspositionTable();
  ~TranspositionTable();

  bool probe(ZobristKey key, TTEntry& entry) const;
  void store(ZobristKey key, Move bestMove, int score, int depth, BoundType bound);
  void clear();
  void newSearch() { ++generation_; }
  void resize(std::size_t mb);
  std::size_t size() const { return num_buckets_; }

 private:
  static constexpr int kBucketSize = 4;

  struct Bucket {
    TTEntry entries[kBucketSize];
  };

  std::unique_ptr<Bucket[]> buckets_;
  std::size_t num_buckets_ = 0;
  std::size_t index_mask_ = 0;
  std::uint8_t generation_ = 0;

  void allocate(std::size_t num_buckets);
};

}  // namespace aurora
