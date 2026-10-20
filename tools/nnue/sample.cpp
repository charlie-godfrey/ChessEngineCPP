#include "nnue_dataset.h"

#include "nnue.h"
#include "position.h"
#include "bitboard.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace aurora;
using namespace aurora::nnue_tools;

namespace {

struct JsonValue {
  enum class Type { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  double number = 0.0;
  bool boolean = false;
  std::string str;
  std::vector<JsonValue> array;
  std::map<std::string, JsonValue> object;
};

struct PvEntry {
  std::optional<int> cp;
  std::optional<int> mate;
  std::string line;
};

struct EvalEntry {
  int knodes = 0;
  int depth = 0;
  std::vector<PvEntry> pvs;
};

struct DatasetEntry {
  std::string fen;
  std::vector<EvalEntry> evals;
};

struct Config {
  int target_depth = 0;
  int target_knodes = 0;
  int max_positions = 0;
  int mate_plies_max = 4;
  int cp_abs_max = 1200;
  int mate_cp = 2000;
  int min_nonpawn = 6;
  int max_nonpawn = 12;
  int pv_swing_max = 400;
  bool use_stm = true;
};

void skip_ws(const std::string& s, std::size_t& i) {
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
    ++i;
  }
}

bool match(const std::string& s, std::size_t& i, char c) {
  skip_ws(s, i);
  if (i < s.size() && s[i] == c) {
    ++i;
    return true;
  }
  return false;
}

JsonValue parse_value(const std::string& s, std::size_t& i);

std::string parse_string(const std::string& s, std::size_t& i) {
  std::string out;
  if (!match(s, i, '"')) {
    return out;
  }
  while (i < s.size()) {
    char c = s[i++];
    if (c == '"') {
      break;
    }
    if (c == '\\' && i < s.size()) {
      char esc = s[i++];
      if (esc == '"' || esc == '\\' || esc == '/') {
        out.push_back(esc);
      } else if (esc == 'b') {
        out.push_back('\b');
      } else if (esc == 'f') {
        out.push_back('\f');
      } else if (esc == 'n') {
        out.push_back('\n');
      } else if (esc == 'r') {
        out.push_back('\r');
      } else if (esc == 't') {
        out.push_back('\t');
      }
    } else {
      out.push_back(c);
    }
  }
  return out;
}

JsonValue parse_number(const std::string& s, std::size_t& i) {
  JsonValue v;
  v.type = JsonValue::Type::Number;
  skip_ws(s, i);
  std::size_t start = i;
  while (i < s.size() &&
         (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '+' ||
          s[i] == '.' || s[i] == 'e' || s[i] == 'E')) {
    ++i;
  }
  v.number = std::strtod(s.c_str() + start, nullptr);
  return v;
}

JsonValue parse_array(const std::string& s, std::size_t& i) {
  JsonValue v;
  v.type = JsonValue::Type::Array;
  match(s, i, '[');
  skip_ws(s, i);
  if (match(s, i, ']')) {
    return v;
  }
  while (i < s.size()) {
    v.array.push_back(parse_value(s, i));
    skip_ws(s, i);
    if (match(s, i, ']')) {
      break;
    }
    match(s, i, ',');
  }
  return v;
}

JsonValue parse_object(const std::string& s, std::size_t& i) {
  JsonValue v;
  v.type = JsonValue::Type::Object;
  match(s, i, '{');
  skip_ws(s, i);
  if (match(s, i, '}')) {
    return v;
  }
  while (i < s.size()) {
    std::string key = parse_string(s, i);
    match(s, i, ':');
    JsonValue value = parse_value(s, i);
    v.object.emplace(std::move(key), std::move(value));
    skip_ws(s, i);
    if (match(s, i, '}')) {
      break;
    }
    match(s, i, ',');
  }
  return v;
}

JsonValue parse_value(const std::string& s, std::size_t& i) {
  skip_ws(s, i);
  if (i >= s.size()) {
    return {};
  }
  char c = s[i];
  if (c == '"') {
    JsonValue v;
    v.type = JsonValue::Type::String;
    v.str = parse_string(s, i);
    return v;
  }
  if (c == '{') {
    return parse_object(s, i);
  }
  if (c == '[') {
    return parse_array(s, i);
  }
  if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+') {
    return parse_number(s, i);
  }
  if (s.compare(i, 4, "true") == 0) {
    i += 4;
    JsonValue v;
    v.type = JsonValue::Type::Bool;
    v.boolean = true;
    return v;
  }
  if (s.compare(i, 5, "false") == 0) {
    i += 5;
    JsonValue v;
    v.type = JsonValue::Type::Bool;
    v.boolean = false;
    return v;
  }
  if (s.compare(i, 4, "null") == 0) {
    i += 4;
    JsonValue v;
    v.type = JsonValue::Type::Null;
    return v;
  }
  return {};
}

std::optional<DatasetEntry> parse_dataset_line(const std::string& line) {
  std::size_t i = 0;
  JsonValue root = parse_value(line, i);
  if (root.type != JsonValue::Type::Object) {
    return std::nullopt;
  }
  DatasetEntry entry;
  auto fen_it = root.object.find("fen");
  if (fen_it == root.object.end() || fen_it->second.type != JsonValue::Type::String) {
    return std::nullopt;
  }
  entry.fen = fen_it->second.str;

  auto evals_it = root.object.find("evals");
  if (evals_it == root.object.end() || evals_it->second.type != JsonValue::Type::Array) {
    return std::nullopt;
  }
  for (const auto& eval_val : evals_it->second.array) {
    if (eval_val.type != JsonValue::Type::Object) {
      continue;
    }
    EvalEntry eval_entry;
    auto kn_it = eval_val.object.find("knodes");
    if (kn_it != eval_val.object.end() && kn_it->second.type == JsonValue::Type::Number) {
      eval_entry.knodes = static_cast<int>(kn_it->second.number);
    }
    auto depth_it = eval_val.object.find("depth");
    if (depth_it != eval_val.object.end() && depth_it->second.type == JsonValue::Type::Number) {
      eval_entry.depth = static_cast<int>(depth_it->second.number);
    }
    auto pvs_it = eval_val.object.find("pvs");
    if (pvs_it != eval_val.object.end() && pvs_it->second.type == JsonValue::Type::Array) {
      for (const auto& pv_val : pvs_it->second.array) {
        if (pv_val.type != JsonValue::Type::Object) {
          continue;
        }
        PvEntry pv;
        auto cp_it = pv_val.object.find("cp");
        if (cp_it != pv_val.object.end() && cp_it->second.type == JsonValue::Type::Number) {
          pv.cp = static_cast<int>(cp_it->second.number);
        }
        auto mate_it = pv_val.object.find("mate");
        if (mate_it != pv_val.object.end() && mate_it->second.type == JsonValue::Type::Number) {
          pv.mate = static_cast<int>(mate_it->second.number);
        }
        auto line_it = pv_val.object.find("line");
        if (line_it != pv_val.object.end() && line_it->second.type == JsonValue::Type::String) {
          pv.line = line_it->second.str;
        }
        eval_entry.pvs.push_back(std::move(pv));
      }
    }
    entry.evals.push_back(std::move(eval_entry));
  }
  return entry;
}

const EvalEntry* select_eval_entry(const std::vector<EvalEntry>& evals, const Config& config) {
  if (evals.empty()) {
    return nullptr;
  }
  const EvalEntry* best = &evals.front();
  int best_depth = -1;
  int best_knodes = -1;
  if (config.target_depth > 0) {
    for (const auto& e : evals) {
      if (e.depth <= config.target_depth && e.depth > best_depth) {
        best = &e;
        best_depth = e.depth;
      }
    }
  } else if (config.target_knodes > 0) {
    for (const auto& e : evals) {
      if (e.knodes <= config.target_knodes && e.knodes > best_knodes) {
        best = &e;
        best_knodes = e.knodes;
      }
    }
  }
  return best;
}

int count_nonpawn(const Position& position) {
  int count = 0;
  for (int color = 0; color < 2; ++color) {
    count += popcount(position.pieces(static_cast<Color>(color), PieceType::Knight));
    count += popcount(position.pieces(static_cast<Color>(color), PieceType::Bishop));
    count += popcount(position.pieces(static_cast<Color>(color), PieceType::Rook));
    count += popcount(position.pieces(static_cast<Color>(color), PieceType::Queen));
  }
  return count;
}

}  // namespace

int main(int argc, char** argv) {
  Config config;
  std::string dataset_path;
  std::string out_path = "nnue_shard.bin";

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--dataset" && i + 1 < argc) {
      dataset_path = argv[++i];
    } else if (arg == "--out" && i + 1 < argc) {
      out_path = argv[++i];
    } else if (arg == "--target-depth" && i + 1 < argc) {
      config.target_depth = std::stoi(argv[++i]);
    } else if (arg == "--target-knodes" && i + 1 < argc) {
      config.target_knodes = std::stoi(argv[++i]);
    } else if (arg == "--max-positions" && i + 1 < argc) {
      config.max_positions = std::stoi(argv[++i]);
    } else if (arg == "--mate-plies-max" && i + 1 < argc) {
      config.mate_plies_max = std::stoi(argv[++i]);
    } else if (arg == "--cp-abs-max" && i + 1 < argc) {
      config.cp_abs_max = std::stoi(argv[++i]);
    } else if (arg == "--mate-cp" && i + 1 < argc) {
      config.mate_cp = std::stoi(argv[++i]);
    } else if (arg == "--min-nonpawn" && i + 1 < argc) {
      config.min_nonpawn = std::stoi(argv[++i]);
    } else if (arg == "--max-nonpawn" && i + 1 < argc) {
      config.max_nonpawn = std::stoi(argv[++i]);
    } else if (arg == "--pv-swing-max" && i + 1 < argc) {
      config.pv_swing_max = std::stoi(argv[++i]);
    } else if (arg == "--no-stm") {
      config.use_stm = false;
    }
  }

  if (dataset_path.empty()) {
    std::cerr << "Usage: sample --dataset <path> [--out <path>] [--target-depth D] [--target-knodes K]\n"
                 "             [--max-positions N] [--mate-plies-max N] [--cp-abs-max N] [--mate-cp N]\n"
                 "             [--min-nonpawn N] [--max-nonpawn N] [--pv-swing-max N] [--no-stm]\n";
    return 1;
  }

  std::ifstream dataset_file(dataset_path);
  if (!dataset_file) {
    std::cerr << "Failed to open dataset: " << dataset_path << "\n";
    return 1;
  }

  ShardWriter writer(out_path, nnue::kFeatureDim);
  if (!writer.ok()) {
    std::cerr << "Failed to open output: " << out_path << "\n";
    return 1;
  }

  std::string line;
  int kept = 0;
  int seen = 0;
  while (std::getline(dataset_file, line)) {
    if (line.empty()) {
      continue;
    }
    seen++;
    auto parsed = parse_dataset_line(line);
    if (!parsed) {
      continue;
    }
    const auto* eval_entry = select_eval_entry(parsed->evals, config);
    if (!eval_entry || eval_entry->pvs.empty()) {
      continue;
    }
    const PvEntry& pv = eval_entry->pvs.front();

    Position position;
    if (!position.setFromFEN(parsed->fen)) {
      continue;
    }

    const int nonpawn = count_nonpawn(position);
    if (nonpawn < config.min_nonpawn || nonpawn > config.max_nonpawn) {
      continue;
    }

    int label_cp = 0;
    if (pv.mate.has_value()) {
      const int mate = *pv.mate;
      if (std::abs(mate) <= config.mate_plies_max) {
        continue;
      }
      label_cp = (mate > 0 ? config.mate_cp : -config.mate_cp);
    } else if (pv.cp.has_value()) {
      label_cp = *pv.cp;
      if (std::abs(label_cp) > config.cp_abs_max) {
        continue;
      }
    } else {
      continue;
    }

    if (eval_entry->pvs.size() > 1 && pv.cp.has_value()) {
      const auto& alt = eval_entry->pvs[1];
      if (alt.cp.has_value()) {
        const int swing = std::abs(*pv.cp - *alt.cp);
        if (swing > config.pv_swing_max) {
          continue;
        }
      }
    }

    if (config.use_stm && position.side_to_move() == Color::Black) {
      label_cp = -label_cp;
    }

    Sample sample;
    sample.label = static_cast<std::int16_t>(
      std::clamp(label_cp, -config.mate_cp, config.mate_cp));
    sample.stm = (position.side_to_move() == Color::White) ? 0 : 1;
    nnue::extractFeatures(position, Color::White, sample.white_indices);
    nnue::extractFeatures(position, Color::Black, sample.black_indices);
    writer.writeSample(sample);
    kept++;
    if (config.max_positions > 0 && kept >= config.max_positions) {
      break;
    }
  }

  writer.close();
  std::cout << "Samples kept: " << kept << " (seen " << seen << ")\n";
  return 0;
}
