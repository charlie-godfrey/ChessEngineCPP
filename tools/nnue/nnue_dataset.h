#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace aurora::nnue_tools {

struct ShardHeader {
  char magic[8];
  std::uint32_t version = 1;
  std::uint32_t input_dim = 0;
  std::uint32_t count = 0;
};

struct Sample {
  std::int16_t label = 0;
  std::uint8_t stm = 0;
  std::vector<std::uint16_t> white_indices;
  std::vector<std::uint16_t> black_indices;
};

class ShardWriter {
 public:
  explicit ShardWriter(const std::string& path, std::uint32_t input_dim) {
    out_.open(path, std::ios::binary);
    header_.input_dim = input_dim;
    std::memcpy(header_.magic, "AURDATA", 7);
    if (out_) {
      out_.write(reinterpret_cast<const char*>(&header_), sizeof(header_));
    }
  }

  bool ok() const { return static_cast<bool>(out_); }

  void writeSample(const Sample& sample) {
    if (!out_) {
      return;
    }
    const std::uint16_t white_count = static_cast<std::uint16_t>(sample.white_indices.size());
    const std::uint16_t black_count = static_cast<std::uint16_t>(sample.black_indices.size());
    out_.write(reinterpret_cast<const char*>(&sample.label), sizeof(sample.label));
    out_.write(reinterpret_cast<const char*>(&sample.stm), sizeof(sample.stm));
    out_.write(reinterpret_cast<const char*>(&white_count), sizeof(white_count));
    out_.write(reinterpret_cast<const char*>(&black_count), sizeof(black_count));
    out_.write(reinterpret_cast<const char*>(sample.white_indices.data()),
               static_cast<std::streamsize>(white_count * sizeof(std::uint16_t)));
    out_.write(reinterpret_cast<const char*>(sample.black_indices.data()),
               static_cast<std::streamsize>(black_count * sizeof(std::uint16_t)));
    header_.count += 1;
  }

  void close() {
    if (!out_) {
      return;
    }
    out_.seekp(0, std::ios::beg);
    out_.write(reinterpret_cast<const char*>(&header_), sizeof(header_));
    out_.close();
  }

 private:
  std::ofstream out_;
  ShardHeader header_{};
};

class ShardReader {
 public:
  explicit ShardReader(const std::string& path) {
    in_.open(path, std::ios::binary);
    if (in_) {
      in_.read(reinterpret_cast<char*>(&header_), sizeof(header_));
    }
  }

  bool ok() const { return static_cast<bool>(in_); }
  const ShardHeader& header() const { return header_; }

  bool readSample(Sample& sample) {
    if (!in_) {
      return false;
    }
    std::int16_t label = 0;
    std::uint8_t stm = 0;
    std::uint16_t white_count = 0;
    std::uint16_t black_count = 0;
    in_.read(reinterpret_cast<char*>(&label), sizeof(label));
    if (!in_) {
      return false;
    }
    in_.read(reinterpret_cast<char*>(&stm), sizeof(stm));
    if (!in_) {
      return false;
    }
    in_.read(reinterpret_cast<char*>(&white_count), sizeof(white_count));
    if (!in_) {
      return false;
    }
    in_.read(reinterpret_cast<char*>(&black_count), sizeof(black_count));
    if (!in_) {
      return false;
    }
    sample.label = label;
    sample.stm = stm;
    sample.white_indices.resize(white_count);
    sample.black_indices.resize(black_count);
    in_.read(reinterpret_cast<char*>(sample.white_indices.data()),
             static_cast<std::streamsize>(white_count * sizeof(std::uint16_t)));
    in_.read(reinterpret_cast<char*>(sample.black_indices.data()),
             static_cast<std::streamsize>(black_count * sizeof(std::uint16_t)));
    return static_cast<bool>(in_);
  }

 private:
  std::ifstream in_;
  ShardHeader header_{};
};

}  // namespace aurora::nnue_tools
