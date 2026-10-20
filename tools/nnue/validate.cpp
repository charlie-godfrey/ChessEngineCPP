#include "evaluate.h"
#include "nnue.h"
#include "position.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace aurora;

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
  int mate_cp = 2000;
  int symmetry_checks = 0;
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

std::string flip_fen(const std::string& fen) {
  std::string board;
  std::string stm;
  std::string castling;
  std::string ep;
  std::string halfmove;
  std::string fullmove;
  std::istringstream iss(fen);
  if (!(iss >> board >> stm >> castling >> ep >> halfmove >> fullmove)) {
    return fen;
  }
  std::vector<std::string> ranks;
  std::string rank;
  std::stringstream bss(board);
  while (std::getline(bss, rank, '/')) {
    ranks.push_back(rank);
  }
  if (ranks.size() != 8) {
    return fen;
  }
  std::string flipped_board;
  for (int r = 7; r >= 0; --r) {
    std::string expanded;
    for (char c : ranks[r]) {
      if (std::isdigit(static_cast<unsigned char>(c))) {
        expanded.append(static_cast<std::size_t>(c - '0'), '.');
      } else {
        expanded.push_back(c);
      }
    }
    std::reverse(expanded.begin(), expanded.end());
    for (char& c : expanded) {
      if (std::isalpha(static_cast<unsigned char>(c))) {
        c = std::islower(static_cast<unsigned char>(c)) ? std::toupper(c) : std::tolower(c);
      }
    }
    int empty = 0;
    for (char c : expanded) {
      if (c == '.') {
        empty++;
      } else {
        if (empty > 0) {
          flipped_board.push_back(static_cast<char>('0' + empty));
          empty = 0;
        }
        flipped_board.push_back(c);
      }
    }
    if (empty > 0) {
      flipped_board.push_back(static_cast<char>('0' + empty));
    }
    if (r > 0) {
      flipped_board.push_back('/');
    }
  }
  std::string flipped_stm = (stm == "w") ? "b" : "w";
  std::string flipped_castling;
  if (castling == "-") {
    flipped_castling = "-";
  } else {
    for (char c : castling) {
      if (c == 'K') flipped_castling.push_back('k');
      else if (c == 'Q') flipped_castling.push_back('q');
      else if (c == 'k') flipped_castling.push_back('K');
      else if (c == 'q') flipped_castling.push_back('Q');
    }
    if (flipped_castling.empty()) {
      flipped_castling = "-";
    }
  }
  std::string flipped_ep = "-";
  if (ep != "-" && ep.size() == 2) {
    char file = ep[0];
    char rankc = ep[1];
    char flipped_file = static_cast<char>('h' - (file - 'a'));
    char flipped_rank = static_cast<char>('9' - (rankc - '0'));
    flipped_ep = std::string{flipped_file, flipped_rank};
  }
  std::ostringstream out;
  out << flipped_board << ' ' << flipped_stm << ' ' << flipped_castling << ' '
      << flipped_ep << ' ' << halfmove << ' ' << fullmove;
  return out.str();
}

}  // namespace

int main(int argc, char** argv) {
  std::string dataset_path;
  std::string nnue_path;
  Config config;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--dataset" && i + 1 < argc) {
      dataset_path = argv[++i];
    } else if (arg == "--nnue" && i + 1 < argc) {
      nnue_path = argv[++i];
    } else if (arg == "--target-depth" && i + 1 < argc) {
      config.target_depth = std::stoi(argv[++i]);
    } else if (arg == "--target-knodes" && i + 1 < argc) {
      config.target_knodes = std::stoi(argv[++i]);
    } else if (arg == "--max-positions" && i + 1 < argc) {
      config.max_positions = std::stoi(argv[++i]);
    } else if (arg == "--symmetry" && i + 1 < argc) {
      config.symmetry_checks = std::stoi(argv[++i]);
    }
  }

  if (dataset_path.empty() || nnue_path.empty()) {
    std::cerr << "Usage: validate --dataset <path> --nnue <path> [--target-depth D] [--target-knodes K]\n";
    return 1;
  }

  std::ifstream dataset_file(dataset_path);
  if (!dataset_file) {
    std::cerr << "Failed to open dataset\n";
    return 1;
  }

  if (!nnue::load(nnue_path)) {
    std::cerr << "Failed to load nnue file\n";
    return 1;
  }

  double mse_nnue = 0.0;
  double mse_classic = 0.0;
  std::size_t count = 0;
  std::size_t sym_count = 0;
  double sym_error = 0.0;
  std::string line;
  while (std::getline(dataset_file, line)) {
    if (line.empty()) {
      continue;
    }
    auto parsed = parse_dataset_line(line);
    if (!parsed) {
      continue;
    }
    const auto* eval_entry = select_eval_entry(parsed->evals, config);
    if (!eval_entry || eval_entry->pvs.empty()) {
      continue;
    }
    const PvEntry& pv = eval_entry->pvs.front();
    int target = 0;
    if (pv.mate.has_value()) {
      target = (pv.mate.value() > 0) ? config.mate_cp : -config.mate_cp;
    } else if (pv.cp.has_value()) {
      target = pv.cp.value();
    } else {
      continue;
    }

    Position position;
    if (!position.setFromFEN(parsed->fen)) {
      continue;
    }
    if (position.side_to_move() == Color::Black) {
      target = -target;
    }

    nnue::setEnabled(true);
    const int nnue_eval = evaluate(position);
    nnue::setEnabled(false);
    const int classic_eval = evaluate(position);

    const double e1 = static_cast<double>(nnue_eval - target);
    const double e2 = static_cast<double>(classic_eval - target);
    mse_nnue += e1 * e1;
    mse_classic += e2 * e2;
    count++;
    if (config.max_positions > 0 && static_cast<int>(count) >= config.max_positions) {
      break;
    }

    if (config.symmetry_checks > 0 && sym_count < static_cast<std::size_t>(config.symmetry_checks)) {
      Position flipped;
      const std::string flipped_fen = flip_fen(parsed->fen);
      if (flipped.setFromFEN(flipped_fen)) {
        nnue::setEnabled(true);
        const int flipped_eval = evaluate(flipped);
        nnue::setEnabled(false);
        sym_error += std::abs(nnue_eval + flipped_eval);
        sym_count++;
      }
    }
  }

  if (count > 0) {
    mse_nnue /= static_cast<double>(count);
    mse_classic /= static_cast<double>(count);
  }
  std::cout << "samples " << count << " nnue_mse " << mse_nnue
            << " classic_mse " << mse_classic;
  if (sym_count > 0) {
    std::cout << " symmetry_abs " << (sym_error / static_cast<double>(sym_count));
  }
  std::cout << "\n";
  return 0;
}
