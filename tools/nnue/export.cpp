#include "nnue_format.h"
#include "nnue.h"
#include "position.h"
#include "types.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace aurora::nnue;
using aurora::Position;
using aurora::Color;

namespace {

struct FloatHeader {
  char magic[8];
  std::uint32_t version = 2;
  std::uint32_t feature_dim = 0;
  std::uint32_t acc_dim = 0;
  std::uint32_t h1_dim = 0;
  std::uint32_t h2_dim = 0;
};

struct FloatModel {
  std::uint32_t feature_dim = 0;
  std::uint32_t acc_dim = 0;
  std::uint32_t h1_dim = 0;
  std::uint32_t h2_dim = 0;
  std::vector<float> acc_bias;
  std::vector<float> acc_weights;
  std::vector<float> l1_bias;
  std::vector<float> l1_weights;
  std::vector<float> l2_bias;
  std::vector<float> l2_weights;
  std::vector<float> out_weights;
  float out_bias = 0.0f;
};

bool load_float_model(const std::string& path, FloatModel& model) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  FloatHeader header{};
  in.read(reinterpret_cast<char*>(&header), sizeof(header));
  if (!in || std::memcmp(header.magic, "AURNNF", 6) != 0 || header.version != 2) {
    return false;
  }
  model.feature_dim = header.feature_dim;
  model.acc_dim = header.acc_dim;
  model.h1_dim = header.h1_dim;
  model.h2_dim = header.h2_dim;
  model.acc_bias.resize(model.acc_dim);
  model.acc_weights.resize(static_cast<std::size_t>(model.feature_dim) * model.acc_dim);
  model.l1_bias.resize(model.h1_dim);
  model.l1_weights.resize(static_cast<std::size_t>(model.h1_dim) * (model.acc_dim * 2));
  model.l2_bias.resize(model.h2_dim);
  model.l2_weights.resize(static_cast<std::size_t>(model.h2_dim) * model.h1_dim);
  model.out_weights.resize(model.h2_dim);
  in.read(reinterpret_cast<char*>(model.acc_bias.data()),
          static_cast<std::streamsize>(model.acc_bias.size() * sizeof(float)));
  in.read(reinterpret_cast<char*>(model.acc_weights.data()),
          static_cast<std::streamsize>(model.acc_weights.size() * sizeof(float)));
  in.read(reinterpret_cast<char*>(model.l1_bias.data()),
          static_cast<std::streamsize>(model.l1_bias.size() * sizeof(float)));
  in.read(reinterpret_cast<char*>(model.l1_weights.data()),
          static_cast<std::streamsize>(model.l1_weights.size() * sizeof(float)));
  in.read(reinterpret_cast<char*>(model.l2_bias.data()),
          static_cast<std::streamsize>(model.l2_bias.size() * sizeof(float)));
  in.read(reinterpret_cast<char*>(model.l2_weights.data()),
          static_cast<std::streamsize>(model.l2_weights.size() * sizeof(float)));
  in.read(reinterpret_cast<char*>(model.out_weights.data()),
          static_cast<std::streamsize>(model.out_weights.size() * sizeof(float)));
  in.read(reinterpret_cast<char*>(&model.out_bias), sizeof(model.out_bias));
  return static_cast<bool>(in);
}

std::int16_t quantize(float v, float scale) {
  const float scaled = v * scale;
  const float clamped = std::max(-32767.0f, std::min(32767.0f, scaled));
  return static_cast<std::int16_t>(std::lrint(clamped));
}

std::int16_t quantize_with_stats(float v, float scale, std::size_t& clipped) {
  const float scaled = v * scale;
  if (scaled > 32767.0f || scaled < -32767.0f) {
    clipped++;
  }
  const float clamped = std::max(-32767.0f, std::min(32767.0f, scaled));
  return static_cast<std::int16_t>(std::lrint(clamped));
}

std::optional<std::string> parse_fen_from_jsonl(const std::string& line) {
  const std::string key = "\"fen\"";
  std::size_t pos = line.find(key);
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  pos = line.find(':', pos + key.size());
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  pos = line.find('"', pos);
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  std::size_t end = line.find('"', pos + 1);
  if (end == std::string::npos || end <= pos + 1) {
    return std::nullopt;
  }
  return line.substr(pos + 1, end - pos - 1);
}

float evaluate_float(const FloatModel& model, const Position& position) {
  std::vector<std::uint16_t> feats_w;
  std::vector<std::uint16_t> feats_b;
  aurora::nnue::extractFeatures(position, Color::White, feats_w);
  aurora::nnue::extractFeatures(position, Color::Black, feats_b);
  const float feat_scale =
    1.0f / static_cast<float>(std::max<std::size_t>(1, feats_w.size()));

  std::vector<float> acc_w(model.acc_dim, 0.0f);
  std::vector<float> acc_b(model.acc_dim, 0.0f);
  for (std::size_t i = 0; i < model.acc_dim; ++i) {
    acc_w[i] = model.acc_bias[i];
    acc_b[i] = model.acc_bias[i];
  }
  for (std::uint16_t idx : feats_w) {
    const std::size_t base = static_cast<std::size_t>(idx) * model.acc_dim;
    for (std::size_t i = 0; i < model.acc_dim; ++i) {
      acc_w[i] += model.acc_weights[base + i] * feat_scale;
    }
  }
  for (std::uint16_t idx : feats_b) {
    const std::size_t base = static_cast<std::size_t>(idx) * model.acc_dim;
    for (std::size_t i = 0; i < model.acc_dim; ++i) {
      acc_b[i] += model.acc_weights[base + i] * feat_scale;
    }
  }

  std::vector<float> h1(model.h1_dim, 0.0f);
  const bool stm_white = (position.side_to_move() == Color::White);
  for (std::size_t i = 0; i < model.h1_dim; ++i) {
    float sum = model.l1_bias[i];
    const std::size_t base = i * (model.acc_dim * 2);
    for (std::size_t j = 0; j < model.acc_dim; ++j) {
      const float v = stm_white ? acc_w[j] : acc_b[j];
      sum += v * model.l1_weights[base + j];
    }
    for (std::size_t j = 0; j < model.acc_dim; ++j) {
      const float v = stm_white ? acc_b[j] : acc_w[j];
      sum += v * model.l1_weights[base + model.acc_dim + j];
    }
    h1[i] = std::max(0.0f, sum);
  }

  std::vector<float> h2(model.h2_dim, 0.0f);
  for (std::size_t i = 0; i < model.h2_dim; ++i) {
    float sum = model.l2_bias[i];
    const std::size_t base = i * model.h1_dim;
    for (std::size_t j = 0; j < model.h1_dim; ++j) {
      sum += h1[j] * model.l2_weights[base + j];
    }
    h2[i] = std::max(0.0f, sum);
  }

  float output = model.out_bias;
  for (std::size_t i = 0; i < model.h2_dim; ++i) {
    output += h2[i] * model.out_weights[i];
  }
  return output;
}

float max_abs(const std::vector<float>& values) {
  float m = 0.0f;
  for (float v : values) {
    m = std::max(m, std::abs(v));
  }
  return m;
}

float snap_pow2(float value) {
  if (value <= 1.0f) {
    return 1.0f;
  }
  const float exp = std::log2(value);
  const float snapped = std::round(exp);
  return std::pow(2.0f, snapped);
}

}  // namespace

int main(int argc, char** argv) {
  std::string in_path;
  std::string out_path = "nnue.bin";
  float in_scale = 256.0f;
  float out_scale = 256.0f;
  int activation_max = 127;
  bool in_scale_set = false;
  bool out_scale_set = false;
  bool auto_scale = false;
  bool auto_input = false;
  std::string dataset_path;
  int auto_max_positions = 20000;
  float target_p95 = 600.0f;
  float target_hidden_p95 = 64.0f;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--in" && i + 1 < argc) {
      in_path = argv[++i];
    } else if (arg == "--out" && i + 1 < argc) {
      out_path = argv[++i];
    } else if (arg == "--in-scale" && i + 1 < argc) {
      in_scale = std::stof(argv[++i]);
      in_scale_set = true;
    } else if (arg == "--out-scale" && i + 1 < argc) {
      out_scale = std::stof(argv[++i]);
      out_scale_set = true;
    } else if (arg == "--activation-max" && i + 1 < argc) {
      activation_max = std::stoi(argv[++i]);
    } else if (arg == "--auto-scale") {
      auto_scale = true;
    } else if (arg == "--auto-input") {
      auto_input = true;
    } else if (arg == "--dataset" && i + 1 < argc) {
      dataset_path = argv[++i];
    } else if (arg == "--auto-max" && i + 1 < argc) {
      auto_max_positions = std::stoi(argv[++i]);
    } else if (arg == "--target-p95" && i + 1 < argc) {
      target_p95 = std::stof(argv[++i]);
    } else if (arg == "--target-hidden-p95" && i + 1 < argc) {
      target_hidden_p95 = std::stof(argv[++i]);
    }
  }

  if (in_path.empty()) {
    std::cerr << "Usage: export --in <float_model> [--out <nnue.bin>] [--in-scale X] [--out-scale X]\n"
                 "             [--auto-scale --dataset <jsonl> --auto-max N --target-p95 X]\n";
    return 1;
  }

  FloatModel model;
  if (!load_float_model(in_path, model)) {
    std::cerr << "Failed to load float model\n";
    return 1;
  }

  if (auto_scale || auto_input) {
    if (dataset_path.empty()) {
      std::cerr << "--auto-scale/--auto-input requires --dataset\n";
      return 1;
    }
    std::ifstream dataset_file(dataset_path);
    if (!dataset_file) {
      std::cerr << "Failed to open dataset: " << dataset_path << "\n";
      return 1;
    }
    std::vector<float> abs_outputs;
    std::vector<float> hidden_p95s;
    abs_outputs.reserve(static_cast<std::size_t>(auto_max_positions));
    hidden_p95s.reserve(static_cast<std::size_t>(auto_max_positions));
    std::string line;
    while (std::getline(dataset_file, line)) {
      if (line.empty()) {
        continue;
      }
      auto fen = parse_fen_from_jsonl(line);
      if (!fen) {
        continue;
      }
      Position position;
      if (!position.setFromFEN(*fen)) {
        continue;
      }
      std::vector<std::uint16_t> feats_w;
      std::vector<std::uint16_t> feats_b;
      aurora::nnue::extractFeatures(position, Color::White, feats_w);
      aurora::nnue::extractFeatures(position, Color::Black, feats_b);
      const float feat_scale =
        1.0f / static_cast<float>(std::max<std::size_t>(1, feats_w.size()));
      std::vector<float> acc_w(model.acc_dim, 0.0f);
      std::vector<float> acc_b(model.acc_dim, 0.0f);
      for (std::size_t i = 0; i < model.acc_dim; ++i) {
        acc_w[i] = model.acc_bias[i];
        acc_b[i] = model.acc_bias[i];
      }
      for (std::uint16_t idx : feats_w) {
        const std::size_t base = static_cast<std::size_t>(idx) * model.acc_dim;
        for (std::size_t i = 0; i < model.acc_dim; ++i) {
          acc_w[i] += model.acc_weights[base + i] * feat_scale;
        }
      }
      for (std::uint16_t idx : feats_b) {
        const std::size_t base = static_cast<std::size_t>(idx) * model.acc_dim;
        for (std::size_t i = 0; i < model.acc_dim; ++i) {
          acc_b[i] += model.acc_weights[base + i] * feat_scale;
        }
      }
      std::vector<float> acc_abs;
      acc_abs.reserve(model.acc_dim * 2);
      for (float v : acc_w) acc_abs.push_back(std::abs(v));
      for (float v : acc_b) acc_abs.push_back(std::abs(v));
      std::nth_element(acc_abs.begin(),
                       acc_abs.begin() + acc_abs.size() * 95 / 100,
                       acc_abs.end());
      hidden_p95s.push_back(acc_abs[acc_abs.size() * 95 / 100]);
      const float output = evaluate_float(model, position);
      abs_outputs.push_back(std::abs(output));
      if (auto_max_positions > 0 &&
          static_cast<int>(abs_outputs.size()) >= auto_max_positions) {
        break;
      }
    }
    if (!hidden_p95s.empty() && auto_input) {
      std::sort(hidden_p95s.begin(), hidden_p95s.end());
      const std::size_t idx = static_cast<std::size_t>(hidden_p95s.size() * 0.95);
      const float p95 = hidden_p95s[std::min(idx, hidden_p95s.size() - 1)];
      if (p95 > 1e-6f) {
        const float gain = target_hidden_p95 / p95;
        for (float& v : model.acc_weights) {
          v *= gain;
        }
        for (float& v : model.acc_bias) {
          v *= gain;
        }
        std::cout << "auto-input: p95 " << p95 << " gain " << gain << "\n";
      }
    }
    if (!abs_outputs.empty() && auto_scale) {
      std::sort(abs_outputs.begin(), abs_outputs.end());
      const std::size_t idx = static_cast<std::size_t>(abs_outputs.size() * 0.95);
      const float p95 = abs_outputs[std::min(idx, abs_outputs.size() - 1)];
      if (p95 > 1e-3f) {
        const float gain = target_p95 / p95;
        for (float& v : model.out_weights) {
          v *= gain;
        }
        model.out_bias *= gain;
        std::cout << "auto-scale: p95 " << p95 << " gain " << gain << "\n";
      }
    }
  }

  if (!in_scale_set) {
    const float max_w1 = max_abs(model.acc_weights);
    const float max_b1 = max_abs(model.acc_bias);
    const float max_abs_in = std::max(max_w1, max_b1);
    if (max_abs_in > 0.0f) {
      in_scale = std::min(10922.0f, 32000.0f / max_abs_in);
    }
  }
  if (!out_scale_set) {
    const float max_w2 = max_abs(model.out_weights);
    const float max_abs_out = std::max(max_w2, std::abs(model.out_bias));
    if (max_abs_out > 0.0f) {
      out_scale = std::min(4096.0f, 32000.0f / max_abs_out);
    }
  }
  if (!in_scale_set) {
    in_scale = snap_pow2(in_scale);
  }
  if (!out_scale_set) {
    out_scale = snap_pow2(out_scale);
  }

  std::ofstream out(out_path, std::ios::binary);
  if (!out) {
    std::cerr << "Failed to open output: " << out_path << "\n";
    return 1;
  }

  NnueHeader header{};
  std::memcpy(header.magic, "AURNNUE", 7);
  header.feature_dim = model.feature_dim;
  header.acc_dim = model.acc_dim;
  header.h1_dim = model.h1_dim;
  header.h2_dim = model.h2_dim;
  header.in_scale = static_cast<std::int32_t>(in_scale);
  header.out_scale = static_cast<std::int32_t>(out_scale);
  header.activation_max = activation_max;

  out.write(reinterpret_cast<const char*>(&header), sizeof(header));

  std::vector<std::int16_t> acc_bias(model.acc_dim);
  std::vector<std::int16_t> acc_weights(model.acc_weights.size());
  std::vector<std::int16_t> l1_bias(model.h1_dim);
  std::vector<std::int16_t> l1_weights(model.l1_weights.size());
  std::vector<std::int16_t> l2_bias(model.h2_dim);
  std::vector<std::int16_t> l2_weights(model.l2_weights.size());
  std::vector<std::int16_t> out_weights(model.out_weights.size());
  std::size_t clipped_acc_bias = 0;
  std::size_t clipped_acc_weights = 0;
  std::size_t clipped_l1_bias = 0;
  std::size_t clipped_l1_weights = 0;
  std::size_t clipped_l2_bias = 0;
  std::size_t clipped_l2_weights = 0;
  std::size_t clipped_out_weights = 0;
  for (std::size_t i = 0; i < model.acc_dim; ++i) {
    acc_bias[i] = quantize_with_stats(model.acc_bias[i], in_scale, clipped_acc_bias);
  }
  for (std::size_t i = 0; i < model.acc_weights.size(); ++i) {
    acc_weights[i] = quantize_with_stats(model.acc_weights[i], in_scale, clipped_acc_weights);
  }
  for (std::size_t i = 0; i < model.h1_dim; ++i) {
    l1_bias[i] = quantize_with_stats(model.l1_bias[i], out_scale, clipped_l1_bias);
  }
  for (std::size_t i = 0; i < model.l1_weights.size(); ++i) {
    l1_weights[i] = quantize_with_stats(model.l1_weights[i], out_scale, clipped_l1_weights);
  }
  for (std::size_t i = 0; i < model.h2_dim; ++i) {
    l2_bias[i] = quantize_with_stats(model.l2_bias[i], out_scale, clipped_l2_bias);
  }
  for (std::size_t i = 0; i < model.l2_weights.size(); ++i) {
    l2_weights[i] = quantize_with_stats(model.l2_weights[i], out_scale, clipped_l2_weights);
  }
  for (std::size_t i = 0; i < model.out_weights.size(); ++i) {
    out_weights[i] = quantize_with_stats(model.out_weights[i], out_scale, clipped_out_weights);
  }
  const std::int32_t out_bias = static_cast<std::int32_t>(std::lrint(model.out_bias * out_scale));

  std::cout << "quantize: in_scale " << in_scale << " out_scale " << out_scale
            << " activation_max " << activation_max << "\n";
  std::cout << "quantize: clipped acc_bias " << clipped_acc_bias
            << " acc_weights " << clipped_acc_weights
            << " l1_bias " << clipped_l1_bias
            << " l1_weights " << clipped_l1_weights
            << " l2_bias " << clipped_l2_bias
            << " l2_weights " << clipped_l2_weights
            << " out_weights " << clipped_out_weights << "\n";

  out.write(reinterpret_cast<const char*>(acc_bias.data()),
            static_cast<std::streamsize>(acc_bias.size() * sizeof(std::int16_t)));
  out.write(reinterpret_cast<const char*>(acc_weights.data()),
            static_cast<std::streamsize>(acc_weights.size() * sizeof(std::int16_t)));
  out.write(reinterpret_cast<const char*>(l1_bias.data()),
            static_cast<std::streamsize>(l1_bias.size() * sizeof(std::int16_t)));
  out.write(reinterpret_cast<const char*>(l1_weights.data()),
            static_cast<std::streamsize>(l1_weights.size() * sizeof(std::int16_t)));
  out.write(reinterpret_cast<const char*>(l2_bias.data()),
            static_cast<std::streamsize>(l2_bias.size() * sizeof(std::int16_t)));
  out.write(reinterpret_cast<const char*>(l2_weights.data()),
            static_cast<std::streamsize>(l2_weights.size() * sizeof(std::int16_t)));
  out.write(reinterpret_cast<const char*>(out_weights.data()),
            static_cast<std::streamsize>(out_weights.size() * sizeof(std::int16_t)));
  out.write(reinterpret_cast<const char*>(&out_bias), sizeof(out_bias));

  std::cout << "Exported NNUE to " << out_path << "\n";
  return 0;
}
