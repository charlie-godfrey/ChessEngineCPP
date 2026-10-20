#include "transposition.h"

#include <algorithm>
#include <climits>
#include <cstring>

namespace aurora {

TranspositionTable::TranspositionTable() {
  resize(64);
}

TranspositionTable::~TranspositionTable() = default;

void TranspositionTable::resize(std::size_t mb) {
  std::size_t bytes = mb * 1024ULL * 1024ULL;
  std::size_t bucket_count = bytes / sizeof(Bucket);
  // Round down to power of 2
  std::size_t n = 1;
  while (n * 2 <= bucket_count) n *= 2;
  allocate(n);
}

void TranspositionTable::allocate(std::size_t num_buckets) {
  num_buckets_ = num_buckets;
  index_mask_ = num_buckets_ - 1;
  buckets_ = std::make_unique<Bucket[]>(num_buckets_);
  clear();
}

bool TranspositionTable::probe(ZobristKey key, TTEntry& entry) const {
  const std::size_t index = key & index_mask_;
  const Bucket& bucket = buckets_[index];

  for (int i = 0; i < kBucketSize; ++i) {
    if (bucket.entries[i].key == key && bucket.entries[i].bound != BoundType::None) {
      entry = bucket.entries[i];
      return true;
    }
  }
  return false;
}

void TranspositionTable::store(ZobristKey key, Move bestMove, int score, int depth, BoundType bound) {
  const std::size_t index = key & index_mask_;
  Bucket& bucket = buckets_[index];

  // Find the best slot to replace
  int replace_idx = 0;
  int worst_score = INT32_MAX;

  for (int i = 0; i < kBucketSize; ++i) {
    TTEntry& e = bucket.entries[i];

    // Same position: only replace if the new entry is at least as deep.
    // Never let a shallow Exact (e.g. qsearch) wipe a deeper result, and
    // never replace a stored hash move with a null move.
    if (e.key == key) {
      if (bestMove == kNullMove) {
        bestMove = e.bestMove;
      }
      if (depth >= e.depth) {
        replace_idx = i;
        worst_score = -1;
        break;
      }
      if (bestMove != kNullMove) {
        e.bestMove = bestMove;
      }
      return;
    }

    // Empty slot
    if (e.bound == BoundType::None) {
      replace_idx = i;
      worst_score = -1;
      break;
    }

    // Replacement scoring: prefer replacing old-generation, then lowest depth
    int replacement_value = static_cast<int>(e.depth) * 4;
    if (e.generation == generation_) replacement_value += 256;
    if (e.bound == BoundType::Exact) replacement_value += 128;

    if (replacement_value < worst_score) {
      worst_score = replacement_value;
      replace_idx = i;
    }
  }

  TTEntry& target = bucket.entries[replace_idx];
  if (bestMove == kNullMove && target.key == key) {
    bestMove = target.bestMove;
  }
  target.key = key;
  target.bestMove = bestMove;
  target.score = static_cast<std::int16_t>(std::clamp(score, -32000, 32000));
  target.depth = static_cast<std::int8_t>(std::clamp(depth, -127, 127));
  target.bound = bound;
  target.generation = generation_;
}

void TranspositionTable::clear() {
  std::memset(buckets_.get(), 0, num_buckets_ * sizeof(Bucket));
  generation_ = 0;
}

}  // namespace aurora
