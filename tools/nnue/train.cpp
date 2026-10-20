#include "nnue_dataset.h"

#include "nnue.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace aurora;
using namespace aurora::nnue_tools;

namespace {

struct FloatHeader {
  char magic[8];
  std::uint32_t version = 2;
  std::uint32_t feature_dim = 0;
  std::uint32_t acc_dim = 0;
  std::uint32_t h1_dim = 0;
  std::uint32_t h2_dim = 0;
};

struct Model {
  std::uint32_t feature_dim = 0;
  std::uint32_t acc_dim = 0;
  std::uint32_t h1_dim = 0;
  std::uint32_t h2_dim = 0;
  std::vector<float> acc_weights;
  std::vector<float> acc_bias;
  std::vector<float> l1_weights;
  std::vector<float> l1_bias;
  std::vector<float> l2_weights;
  std::vector<float> l2_bias;
  std::vector<float> out_weights;
  float out_bias = 0.0f;
};

bool load_model(const std::string& path, Model& m) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  FloatHeader header{};
  in.read(reinterpret_cast<char*>(&header), sizeof(header));
  if (!in || std::memcmp(header.magic, "AURNNF", 6) != 0 || header.feature_dim == 0) {
    return false;
  }
  m.feature_dim = header.feature_dim;
  m.acc_dim = header.acc_dim;
  m.h1_dim = header.h1_dim;
  m.h2_dim = header.h2_dim;
  m.acc_weights.resize(static_cast<std::size_t>(m.feature_dim) * m.acc_dim);
  m.acc_bias.resize(m.acc_dim);
  m.l1_weights.resize(static_cast<std::size_t>(m.h1_dim) * (m.acc_dim * 2));
  m.l1_bias.resize(m.h1_dim);
  m.l2_weights.resize(static_cast<std::size_t>(m.h2_dim) * m.h1_dim);
  m.l2_bias.resize(m.h2_dim);
  m.out_weights.resize(m.h2_dim);
  in.read(reinterpret_cast<char*>(m.acc_bias.data()),
          static_cast<std::streamsize>(m.acc_bias.size() * sizeof(float)));
  in.read(reinterpret_cast<char*>(m.acc_weights.data()),
          static_cast<std::streamsize>(m.acc_weights.size() * sizeof(float)));
  in.read(reinterpret_cast<char*>(m.l1_bias.data()),
          static_cast<std::streamsize>(m.l1_bias.size() * sizeof(float)));
  in.read(reinterpret_cast<char*>(m.l1_weights.data()),
          static_cast<std::streamsize>(m.l1_weights.size() * sizeof(float)));
  in.read(reinterpret_cast<char*>(m.l2_bias.data()),
          static_cast<std::streamsize>(m.l2_bias.size() * sizeof(float)));
  in.read(reinterpret_cast<char*>(m.l2_weights.data()),
          static_cast<std::streamsize>(m.l2_weights.size() * sizeof(float)));
  in.read(reinterpret_cast<char*>(m.out_weights.data()),
          static_cast<std::streamsize>(m.out_weights.size() * sizeof(float)));
  in.read(reinterpret_cast<char*>(&m.out_bias), sizeof(m.out_bias));
  return static_cast<bool>(in);
}

Model init_model(std::uint32_t feature_dim,
                 std::uint32_t acc_dim,
                 std::uint32_t h1_dim,
                 std::uint32_t h2_dim,
                 std::mt19937& rng) {
  std::uniform_real_distribution<float> dist(-0.01f, 0.01f);
  Model m;
  m.feature_dim = feature_dim;
  m.acc_dim = acc_dim;
  m.h1_dim = h1_dim;
  m.h2_dim = h2_dim;
  m.acc_weights.resize(static_cast<std::size_t>(feature_dim) * acc_dim);
  m.acc_bias.resize(acc_dim, 0.0f);
  m.l1_weights.resize(static_cast<std::size_t>(h1_dim) * (acc_dim * 2));
  m.l1_bias.resize(h1_dim, 0.0f);
  m.l2_weights.resize(static_cast<std::size_t>(h2_dim) * h1_dim);
  m.l2_bias.resize(h2_dim, 0.0f);
  m.out_weights.resize(h2_dim);
  for (auto& v : m.acc_weights) v = dist(rng);
  for (auto& v : m.l1_weights) v = dist(rng);
  for (auto& v : m.l2_weights) v = dist(rng);
  for (auto& v : m.out_weights) v = dist(rng);
  return m;
}

void save_model(const std::string& path, const Model& m) {
  std::ofstream out(path, std::ios::binary);
  FloatHeader header{};
  std::memcpy(header.magic, "AURNNF", 6);
  header.feature_dim = m.feature_dim;
  header.acc_dim = m.acc_dim;
  header.h1_dim = m.h1_dim;
  header.h2_dim = m.h2_dim;
  out.write(reinterpret_cast<const char*>(&header), sizeof(header));
  out.write(reinterpret_cast<const char*>(m.acc_bias.data()),
            static_cast<std::streamsize>(m.acc_bias.size() * sizeof(float)));
  out.write(reinterpret_cast<const char*>(m.acc_weights.data()),
            static_cast<std::streamsize>(m.acc_weights.size() * sizeof(float)));
  out.write(reinterpret_cast<const char*>(m.l1_bias.data()),
            static_cast<std::streamsize>(m.l1_bias.size() * sizeof(float)));
  out.write(reinterpret_cast<const char*>(m.l1_weights.data()),
            static_cast<std::streamsize>(m.l1_weights.size() * sizeof(float)));
  out.write(reinterpret_cast<const char*>(m.l2_bias.data()),
            static_cast<std::streamsize>(m.l2_bias.size() * sizeof(float)));
  out.write(reinterpret_cast<const char*>(m.l2_weights.data()),
            static_cast<std::streamsize>(m.l2_weights.size() * sizeof(float)));
  out.write(reinterpret_cast<const char*>(m.out_weights.data()),
            static_cast<std::streamsize>(m.out_weights.size() * sizeof(float)));
  out.write(reinterpret_cast<const char*>(&m.out_bias), sizeof(m.out_bias));
}

void apply_weight_clip(Model& model, float weight_clip) {
  if (weight_clip <= 0.0f) {
    return;
  }
  const float wc = weight_clip;
  model.out_bias = std::clamp(model.out_bias, -wc, wc);
  for (std::size_t i = 0; i < model.h2_dim; ++i) {
    model.out_weights[i] = std::clamp(model.out_weights[i], -wc, wc);
    model.l2_bias[i] = std::clamp(model.l2_bias[i], -wc, wc);
  }
  for (std::size_t i = 0; i < model.h1_dim; ++i) {
    model.l1_bias[i] = std::clamp(model.l1_bias[i], -wc, wc);
  }
  for (std::size_t i = 0; i < model.acc_weights.size(); ++i) {
    model.acc_weights[i] = std::clamp(model.acc_weights[i], -wc, wc);
  }
  for (std::size_t i = 0; i < model.acc_bias.size(); ++i) {
    model.acc_bias[i] = std::clamp(model.acc_bias[i], -wc, wc);
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::string shard_path;
  std::string out_path = "nnue_float.bin";
  std::string checkpoint_prefix;
  std::string resume_path;
  int epochs = 1;
  float lr = 0.002f;
  float lr_decay = 0.85f;
  float label_scale = 2000.0f;
  float grad_clip = 5.0f;
  float weight_clip = 3.0f;
  float weight_decay = 1e-3f;
  float hidden_clip = 5.0f;
  std::uint32_t acc_dim = nnue::kAccDim;
  std::uint32_t h1_dim = nnue::kH1Dim;
  std::uint32_t h2_dim = nnue::kH2Dim;
  bool acc_dim_set = false;
  bool h1_dim_set = false;
  bool h2_dim_set = false;
  unsigned int seed = 42;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--shard" && i + 1 < argc) {
      shard_path = argv[++i];
    } else if (arg == "--out" && i + 1 < argc) {
      out_path = argv[++i];
    } else if (arg == "--epochs" && i + 1 < argc) {
      epochs = std::stoi(argv[++i]);
    } else if (arg == "--checkpoint" && i + 1 < argc) {
      checkpoint_prefix = argv[++i];
    } else if (arg == "--resume" && i + 1 < argc) {
      resume_path = argv[++i];
    } else if (arg == "--lr" && i + 1 < argc) {
      lr = std::stof(argv[++i]);
    } else if (arg == "--lr-decay" && i + 1 < argc) {
      lr_decay = std::clamp(std::stof(argv[++i]), 0.1f, 1.0f);
    } else if (arg == "--label-scale" && i + 1 < argc) {
      label_scale = std::max(1.0f, std::stof(argv[++i]));
    } else if (arg == "--grad-clip" && i + 1 < argc) {
      grad_clip = std::max(0.0f, std::stof(argv[++i]));
    } else if (arg == "--weight-clip" && i + 1 < argc) {
      weight_clip = std::max(0.0f, std::stof(argv[++i]));
    } else if (arg == "--weight-decay" && i + 1 < argc) {
      weight_decay = std::max(0.0f, std::stof(argv[++i]));
    } else if (arg == "--hidden-clip" && i + 1 < argc) {
      hidden_clip = std::max(0.0f, std::stof(argv[++i]));
    } else if (arg == "--acc-dim" && i + 1 < argc) {
      acc_dim = static_cast<std::uint32_t>(std::stoul(argv[++i]));
      acc_dim_set = true;
    } else if (arg == "--h1-dim" && i + 1 < argc) {
      h1_dim = static_cast<std::uint32_t>(std::stoul(argv[++i]));
      h1_dim_set = true;
    } else if (arg == "--h2-dim" && i + 1 < argc) {
      h2_dim = static_cast<std::uint32_t>(std::stoul(argv[++i]));
      h2_dim_set = true;
    } else if (arg == "--seed" && i + 1 < argc) {
      seed = static_cast<unsigned int>(std::stoul(argv[++i]));
    }
  }

  if (shard_path.empty()) {
    std::cerr << "Usage: train --shard <path> [--out <path>] [--epochs N] [--checkpoint prefix]\n"
                 "             [--resume <path>]\n"
                 "             [--lr X] [--lr-decay X] [--label-scale X]\n"
                 "             [--grad-clip X] [--weight-clip X] [--weight-decay X] [--hidden-clip X]\n"
                 "             [--acc-dim N] [--h1-dim N] [--h2-dim N]\n";
    return 1;
  }

  ShardReader reader(shard_path);
  if (!reader.ok()) {
    std::cerr << "Failed to open shard: " << shard_path << "\n";
    return 1;
  }
  if (std::memcmp(reader.header().magic, "AURDATA", 7) != 0 || reader.header().input_dim == 0) {
    std::cerr << "Invalid shard header (magic/input_dim)\n";
    return 1;
  }

  std::mt19937 rng(seed);
  Model model;
  if (!resume_path.empty()) {
    if (!load_model(resume_path, model)) {
      std::cerr << "Failed to load model: " << resume_path << "\n";
      return 1;
    }
    if (model.feature_dim != reader.header().input_dim) {
      std::cerr << "Model feature_dim does not match shard input_dim\n";
      return 1;
    }
    if ((acc_dim_set && model.acc_dim != acc_dim) ||
        (h1_dim_set && model.h1_dim != h1_dim) ||
        (h2_dim_set && model.h2_dim != h2_dim)) {
      std::cerr << "Model dimensions do not match requested dims\n";
      return 1;
    }
    apply_weight_clip(model, weight_clip);
  } else {
    model = init_model(reader.header().input_dim, acc_dim, h1_dim, h2_dim, rng);
  }

  std::vector<float> acc_w(model.acc_dim);
  std::vector<float> acc_b(model.acc_dim);
  std::vector<float> h1(model.h1_dim);
  std::vector<float> h2(model.h2_dim);
  std::vector<float> d_h1(model.h1_dim);
  std::vector<float> d_h2(model.h2_dim);
  std::vector<float> d_in(model.acc_dim * 2);
  Sample sample;
  double prev_mse = 0.0;
  float current_lr = lr;
  for (int epoch = 0; epoch < epochs; ++epoch) {
    ShardReader epoch_reader(shard_path);
    std::size_t count = 0;
    double loss_sum = 0.0;
    while (epoch_reader.readSample(sample)) {
      bool in_bounds = true;
      for (std::uint16_t idx : sample.white_indices) {
        if (idx >= model.feature_dim) {
          in_bounds = false;
          break;
        }
      }
      if (in_bounds) {
        for (std::uint16_t idx : sample.black_indices) {
          if (idx >= model.feature_dim) {
            in_bounds = false;
            break;
          }
        }
      }
      if (!in_bounds) {
        continue;
      }

      const std::size_t feat_count =
        std::max<std::size_t>(1, sample.white_indices.size());
      const float feat_scale = 1.0f / static_cast<float>(feat_count);

      for (std::size_t i = 0; i < model.acc_dim; ++i) {
        acc_w[i] = model.acc_bias[i];
        acc_b[i] = model.acc_bias[i];
      }
      for (std::uint16_t idx : sample.white_indices) {
        const std::size_t base = static_cast<std::size_t>(idx) * model.acc_dim;
        for (std::size_t i = 0; i < model.acc_dim; ++i) {
          acc_w[i] += model.acc_weights[base + i] * feat_scale;
        }
      }
      for (std::uint16_t idx : sample.black_indices) {
        const std::size_t base = static_cast<std::size_t>(idx) * model.acc_dim;
        for (std::size_t i = 0; i < model.acc_dim; ++i) {
          acc_b[i] += model.acc_weights[base + i] * feat_scale;
        }
      }

      for (std::size_t i = 0; i < model.h1_dim; ++i) {
        float sum = model.l1_bias[i];
        const std::size_t base = i * (model.acc_dim * 2);
        for (std::size_t j = 0; j < model.acc_dim; ++j) {
          const float v = (sample.stm == 0) ? acc_w[j] : acc_b[j];
          sum += v * model.l1_weights[base + j];
        }
        for (std::size_t j = 0; j < model.acc_dim; ++j) {
          const float v = (sample.stm == 0) ? acc_b[j] : acc_w[j];
          sum += v * model.l1_weights[base + model.acc_dim + j];
        }
        float v = std::max(0.0f, sum);
        if (hidden_clip > 0.0f && v > hidden_clip) {
          v = hidden_clip;
        }
        h1[i] = v;
      }

      for (std::size_t i = 0; i < model.h2_dim; ++i) {
        float sum = model.l2_bias[i];
        const std::size_t base = i * model.h1_dim;
        for (std::size_t j = 0; j < model.h1_dim; ++j) {
          sum += h1[j] * model.l2_weights[base + j];
        }
        float v = std::max(0.0f, sum);
        if (hidden_clip > 0.0f && v > hidden_clip) {
          v = hidden_clip;
        }
        h2[i] = v;
      }

      double output = model.out_bias;
      for (std::size_t i = 0; i < model.h2_dim; ++i) {
        output += static_cast<double>(h2[i]) * model.out_weights[i];
      }

      const float target = static_cast<float>(sample.label) / label_scale;
      double err = output - target;
      if (!std::isfinite(err) || !std::isfinite(output)) {
        continue;
      }
      loss_sum += static_cast<double>(err * err);

      float grad = static_cast<float>(2.0 * err);
      if (grad_clip > 0.0f) {
        if (grad > grad_clip) grad = grad_clip;
        if (grad < -grad_clip) grad = -grad_clip;
      }

      model.out_bias -= current_lr * grad;
      for (std::size_t i = 0; i < model.h2_dim; ++i) {
        model.out_weights[i] -= current_lr * grad * h2[i];
      }

      for (std::size_t i = 0; i < model.h2_dim; ++i) {
        d_h2[i] = (h2[i] > 0.0f ? grad * model.out_weights[i] : 0.0f);
      }

      for (std::size_t i = 0; i < model.h2_dim; ++i) {
        model.l2_bias[i] -= current_lr * d_h2[i];
        const std::size_t base = i * model.h1_dim;
        for (std::size_t j = 0; j < model.h1_dim; ++j) {
          model.l2_weights[base + j] -= current_lr * d_h2[i] * h1[j];
        }
      }

      for (std::size_t j = 0; j < model.h1_dim; ++j) {
        float sum = 0.0f;
        for (std::size_t i = 0; i < model.h2_dim; ++i) {
          sum += d_h2[i] * model.l2_weights[i * model.h1_dim + j];
        }
        d_h1[j] = (h1[j] > 0.0f ? sum : 0.0f);
      }

      std::fill(d_in.begin(), d_in.end(), 0.0f);
      for (std::size_t i = 0; i < model.h1_dim; ++i) {
        model.l1_bias[i] -= current_lr * d_h1[i];
        const std::size_t base = i * (model.acc_dim * 2);
        for (std::size_t j = 0; j < model.acc_dim * 2; ++j) {
          const float in_v = (j < model.acc_dim)
                               ? ((sample.stm == 0) ? acc_w[j] : acc_b[j])
                               : ((sample.stm == 0) ? acc_b[j - model.acc_dim] : acc_w[j - model.acc_dim]);
          model.l1_weights[base + j] -= current_lr * d_h1[i] * in_v;
          d_in[j] += d_h1[i] * model.l1_weights[base + j];
        }
      }

      for (std::uint16_t idx : sample.white_indices) {
        const std::size_t base = static_cast<std::size_t>(idx) * model.acc_dim;
        for (std::size_t i = 0; i < model.acc_dim; ++i) {
          const float g = (sample.stm == 0) ? d_in[i] : d_in[model.acc_dim + i];
          model.acc_weights[base + i] -= current_lr * g * feat_scale;
        }
      }
      for (std::uint16_t idx : sample.black_indices) {
        const std::size_t base = static_cast<std::size_t>(idx) * model.acc_dim;
        for (std::size_t i = 0; i < model.acc_dim; ++i) {
          const float g = (sample.stm == 0) ? d_in[model.acc_dim + i] : d_in[i];
          model.acc_weights[base + i] -= current_lr * g * feat_scale;
        }
      }
      for (std::size_t i = 0; i < model.acc_dim; ++i) {
        model.acc_bias[i] -= current_lr * (d_in[i] + d_in[model.acc_dim + i]);
      }

      if (weight_decay > 0.0f) {
        const float decay = 1.0f - current_lr * weight_decay;
        model.out_bias *= decay;
        for (std::size_t i = 0; i < model.h2_dim; ++i) {
          model.out_weights[i] *= decay;
          model.l2_bias[i] *= decay;
        }
        for (std::size_t i = 0; i < model.h1_dim; ++i) {
          model.l1_bias[i] *= decay;
        }
        for (std::uint16_t idx : sample.white_indices) {
          const std::size_t base = static_cast<std::size_t>(idx) * model.acc_dim;
          for (std::size_t i = 0; i < model.acc_dim; ++i) {
            model.acc_weights[base + i] *= decay;
          }
        }
        for (std::uint16_t idx : sample.black_indices) {
          const std::size_t base = static_cast<std::size_t>(idx) * model.acc_dim;
          for (std::size_t i = 0; i < model.acc_dim; ++i) {
            model.acc_weights[base + i] *= decay;
          }
        }
        for (std::size_t i = 0; i < model.acc_dim; ++i) {
          model.acc_bias[i] *= decay;
        }
      }
      if (weight_clip > 0.0f) {
        const float wc = weight_clip;
        model.out_bias = std::clamp(model.out_bias, -wc, wc);
        for (std::size_t i = 0; i < model.h2_dim; ++i) {
          model.out_weights[i] = std::clamp(model.out_weights[i], -wc, wc);
          model.l2_bias[i] = std::clamp(model.l2_bias[i], -wc, wc);
        }
        for (std::size_t i = 0; i < model.h1_dim; ++i) {
          model.l1_bias[i] = std::clamp(model.l1_bias[i], -wc, wc);
        }
        for (std::uint16_t idx : sample.white_indices) {
          const std::size_t base = static_cast<std::size_t>(idx) * model.acc_dim;
          for (std::size_t i = 0; i < model.acc_dim; ++i) {
            model.acc_weights[base + i] = std::clamp(model.acc_weights[base + i], -wc, wc);
          }
        }
        for (std::uint16_t idx : sample.black_indices) {
          const std::size_t base = static_cast<std::size_t>(idx) * model.acc_dim;
          for (std::size_t i = 0; i < model.acc_dim; ++i) {
            model.acc_weights[base + i] = std::clamp(model.acc_weights[base + i], -wc, wc);
          }
        }
        for (std::size_t i = 0; i < model.acc_dim; ++i) {
          model.acc_bias[i] = std::clamp(model.acc_bias[i], -wc, wc);
        }
      }

      count++;
    }
    const double mse = count ? loss_sum / static_cast<double>(count) : 0.0;
    std::cout << "epoch " << (epoch + 1) << " mse " << mse << " samples " << count << "\n";
    if (epoch > 0 && mse > prev_mse * 10.0) {
      std::cout << "early stop: mse diverged\n";
      break;
    }
    prev_mse = mse;
    current_lr *= lr_decay;
    if (!checkpoint_prefix.empty()) {
      const std::string path = checkpoint_prefix + "_epoch" + std::to_string(epoch + 1) + ".bin";
      save_model(path, model);
    }
  }

  for (std::size_t i = 0; i < model.h2_dim; ++i) {
    model.out_weights[i] *= label_scale;
  }
  model.out_bias *= label_scale;

  save_model(out_path, model);
  return 0;
}
