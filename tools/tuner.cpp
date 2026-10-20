#include "evaluate.h"
#include "move.h"
#include "params.h"
#include "position.h"
#include "search.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <numeric>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace aurora {
namespace {

// ---------------------------------------------------------------------------
// Minimal JSON parser (same as before, needed for JSONL dataset)
// ---------------------------------------------------------------------------
struct JsonValue {
  enum class Type { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  double number = 0.0;
  bool boolean = false;
  std::string str;
  std::vector<JsonValue> array;
  std::map<std::string, JsonValue> object;
};

void skip_ws(const std::string& s, std::size_t& i) {
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

bool match(const std::string& s, std::size_t& i, char c) {
  skip_ws(s, i);
  if (i < s.size() && s[i] == c) { ++i; return true; }
  return false;
}

JsonValue parse_value(const std::string& s, std::size_t& i);

std::string parse_string(const std::string& s, std::size_t& i) {
  std::string out;
  if (!match(s, i, '"')) return out;
  while (i < s.size()) {
    char c = s[i++];
    if (c == '"') break;
    if (c == '\\' && i < s.size()) {
      char esc = s[i++];
      switch (esc) {
        case '"': case '\\': case '/': out.push_back(esc); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        default: break;
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
         (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' ||
          s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E'))
    ++i;
  v.number = std::strtod(s.c_str() + start, nullptr);
  return v;
}

JsonValue parse_array(const std::string& s, std::size_t& i) {
  JsonValue v;
  v.type = JsonValue::Type::Array;
  match(s, i, '[');
  skip_ws(s, i);
  if (match(s, i, ']')) return v;
  while (i < s.size()) {
    v.array.push_back(parse_value(s, i));
    skip_ws(s, i);
    if (match(s, i, ']')) break;
    match(s, i, ',');
  }
  return v;
}

JsonValue parse_object(const std::string& s, std::size_t& i) {
  JsonValue v;
  v.type = JsonValue::Type::Object;
  match(s, i, '{');
  skip_ws(s, i);
  if (match(s, i, '}')) return v;
  while (i < s.size()) {
    std::string key = parse_string(s, i);
    match(s, i, ':');
    JsonValue value = parse_value(s, i);
    v.object.emplace(std::move(key), std::move(value));
    skip_ws(s, i);
    if (match(s, i, '}')) break;
    match(s, i, ',');
  }
  return v;
}

JsonValue parse_value(const std::string& s, std::size_t& i) {
  skip_ws(s, i);
  if (i >= s.size()) return {};
  char c = s[i];
  if (c == '"') {
    JsonValue v;
    v.type = JsonValue::Type::String;
    v.str = parse_string(s, i);
    return v;
  }
  if (c == '{') return parse_object(s, i);
  if (c == '[') return parse_array(s, i);
  if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+')
    return parse_number(s, i);
  if (s.compare(i, 4, "true") == 0) {
    i += 4;
    JsonValue v; v.type = JsonValue::Type::Bool; v.boolean = true; return v;
  }
  if (s.compare(i, 5, "false") == 0) {
    i += 5;
    JsonValue v; v.type = JsonValue::Type::Bool; v.boolean = false; return v;
  }
  if (s.compare(i, 4, "null") == 0) { i += 4; return {}; }
  return {};
}

// ---------------------------------------------------------------------------
// Dataset types & parsing
// ---------------------------------------------------------------------------
struct PVCandidate {
  std::string move_uci;  // first move of this PV
  int cp = 0;            // eval in centipawns from white's POV
  bool is_mate = false;
};

struct TuningPosition {
  std::string fen;
  int reference_cp = 0;       // best PV's eval, from white's POV
  std::string best_move_uci;  // best PV's first move
  bool is_mate = false;
  std::vector<PVCandidate> candidates;  // all PV candidates with evals
};

constexpr int kMateCpEquiv = 10000;

int pv_score_to_cp(const JsonValue& pv_obj) {
  auto cp_it = pv_obj.object.find("cp");
  if (cp_it != pv_obj.object.end() && cp_it->second.type == JsonValue::Type::Number)
    return static_cast<int>(cp_it->second.number);
  auto mate_it = pv_obj.object.find("mate");
  if (mate_it != pv_obj.object.end() && mate_it->second.type == JsonValue::Type::Number) {
    int m = static_cast<int>(mate_it->second.number);
    return (m > 0) ? kMateCpEquiv : -kMateCpEquiv;
  }
  return 0;
}

bool pv_is_mate(const JsonValue& pv_obj) {
  return pv_obj.object.find("mate") != pv_obj.object.end() &&
         pv_obj.object.at("mate").type == JsonValue::Type::Number;
}

std::string pv_first_move(const JsonValue& pv_obj) {
  auto line_it = pv_obj.object.find("line");
  if (line_it == pv_obj.object.end() || line_it->second.type != JsonValue::Type::String)
    return {};
  const auto& pv_line = line_it->second.str;
  auto space = pv_line.find(' ');
  return (space != std::string::npos) ? pv_line.substr(0, space) : pv_line;
}

std::optional<TuningPosition> parse_line(const std::string& line) {
  std::size_t i = 0;
  JsonValue root = parse_value(line, i);
  if (root.type != JsonValue::Type::Object) return std::nullopt;

  auto fen_it = root.object.find("fen");
  if (fen_it == root.object.end() || fen_it->second.type != JsonValue::Type::String)
    return std::nullopt;

  auto evals_it = root.object.find("evals");
  if (evals_it == root.object.end() || evals_it->second.type != JsonValue::Type::Array)
    return std::nullopt;
  if (evals_it->second.array.empty()) return std::nullopt;

  // Pick the deepest eval entry
  const JsonValue* best_eval = nullptr;
  int best_depth = -1;
  for (const auto& ev : evals_it->second.array) {
    if (ev.type != JsonValue::Type::Object) continue;
    int d = 0;
    auto d_it = ev.object.find("depth");
    if (d_it != ev.object.end() && d_it->second.type == JsonValue::Type::Number)
      d = static_cast<int>(d_it->second.number);
    if (d > best_depth) { best_depth = d; best_eval = &ev; }
  }
  if (!best_eval) return std::nullopt;

  auto pvs_it = best_eval->object.find("pvs");
  if (pvs_it == best_eval->object.end() || pvs_it->second.type != JsonValue::Type::Array)
    return std::nullopt;
  if (pvs_it->second.array.empty()) return std::nullopt;

  const auto& pv0 = pvs_it->second.array.front();
  if (pv0.type != JsonValue::Type::Object) return std::nullopt;

  TuningPosition pos;
  pos.fen = fen_it->second.str;
  pos.reference_cp = pv_score_to_cp(pv0);
  pos.is_mate = pv_is_mate(pv0);
  pos.best_move_uci = pv_first_move(pv0);

  if (pos.best_move_uci.empty()) return std::nullopt;

  // Collect all PV candidates
  for (const auto& pv_val : pvs_it->second.array) {
    if (pv_val.type != JsonValue::Type::Object) continue;
    std::string move = pv_first_move(pv_val);
    if (move.empty()) continue;
    PVCandidate cand;
    cand.move_uci = std::move(move);
    cand.cp = pv_score_to_cp(pv_val);
    cand.is_mate = pv_is_mate(pv_val);
    pos.candidates.push_back(std::move(cand));
  }

  return pos;
}

// ---------------------------------------------------------------------------
// Reservoir sampling: stream through a huge file and collect N random positions
// ---------------------------------------------------------------------------
std::vector<TuningPosition> load_dataset(const std::string& path, int max_positions,
                                         unsigned int seed, bool skip_mates,
                                         bool skip_extreme_eval, int extreme_cp_threshold,
                                         bool first_n_only) {
  std::ifstream file(path);
  if (!file) {
    std::cerr << "Error: cannot open dataset " << path << "\n";
    return {};
  }

  std::mt19937 rng(seed);
  std::vector<TuningPosition> reservoir;
  reservoir.reserve(static_cast<std::size_t>(max_positions));
  std::uint64_t seen = 0;
  std::string line;

  auto t0 = std::chrono::steady_clock::now();

  while (std::getline(file, line)) {
    if (line.empty()) continue;
    auto parsed = parse_line(line);
    if (!parsed) continue;

    if (skip_mates && parsed->is_mate) continue;
    if (skip_extreme_eval && std::abs(parsed->reference_cp) > extreme_cp_threshold) continue;

    ++seen;

    if (first_n_only) {
      reservoir.push_back(std::move(*parsed));
      if (static_cast<int>(reservoir.size()) >= max_positions) break;
    } else {
      if (static_cast<int>(reservoir.size()) < max_positions) {
        reservoir.push_back(std::move(*parsed));
      } else {
        std::uniform_int_distribution<std::uint64_t> dist(0, seen - 1);
        std::uint64_t j = dist(rng);
        if (j < static_cast<std::uint64_t>(max_positions)) {
          reservoir[static_cast<std::size_t>(j)] = std::move(*parsed);
        }
      }
    }

    if ((seen & 0xFFFFF) == 0) {
      auto elapsed = std::chrono::steady_clock::now() - t0;
      double secs = std::chrono::duration<double>(elapsed).count();
      std::cerr << "\r  Scanned " << seen << " positions ("
                << std::fixed << std::setprecision(1) << secs << "s)..." << std::flush;
    }
  }

  auto elapsed = std::chrono::steady_clock::now() - t0;
  double secs = std::chrono::duration<double>(elapsed).count();
  std::cerr << "\r  Loaded " << reservoir.size() << " positions from " << seen
            << " scanned (" << std::fixed << std::setprecision(1) << secs << "s)\n";

  return reservoir;
}

// ---------------------------------------------------------------------------
// Texel sigmoid: maps centipawn score to win probability [0, 1]
// wp = 1 / (1 + 10^(-cp / K))
// ---------------------------------------------------------------------------
constexpr double kDefaultK = 400.0;

double sigmoid(int cp, double K) {
  return 1.0 / (1.0 + std::pow(10.0, -static_cast<double>(cp) / K));
}

// ---------------------------------------------------------------------------
// Tunable parameter specification
// ---------------------------------------------------------------------------
enum class ParamType { Int, Double };

struct ParamSpec {
  std::string name;
  ParamType type = ParamType::Int;
  int* int_ptr = nullptr;
  double* double_ptr = nullptr;
  double min_val = 0.0;
  double max_val = 0.0;
  double step = 1.0;
};

double get_value(const ParamSpec& spec) {
  return (spec.type == ParamType::Int) ? static_cast<double>(*spec.int_ptr) : *spec.double_ptr;
}

void set_value(ParamSpec& spec, double value) {
  value = std::clamp(value, spec.min_val, spec.max_val);
  if (spec.type == ParamType::Int)
    *spec.int_ptr = static_cast<int>(std::round(value));
  else
    *spec.double_ptr = value;
}

std::vector<ParamSpec> build_eval_specs(EvalParams& ep) {
  static const char* piece_names[] = {"pawn", "knight", "bishop", "rook", "queen"};
  std::vector<ParamSpec> specs;

  specs.push_back({"tempo_bonus", ParamType::Int, &ep.tempo_bonus, nullptr, 0, 50, 2});

  // Material values MG/EG
  for (int p = 0; p < 5; ++p) {
    int lo_mg = (p == 0) ? 50 : ep.piece_value_mg[p] - 80;
    int hi_mg = (p == 0) ? 150 : ep.piece_value_mg[p] + 80;
    specs.push_back({
      std::string("piece_value_mg_") + piece_names[p],
      ParamType::Int, &ep.piece_value_mg[static_cast<std::size_t>(p)], nullptr,
      static_cast<double>(lo_mg), static_cast<double>(hi_mg), (p == 0) ? 3.0 : 5.0
    });
  }
  for (int p = 0; p < 5; ++p) {
    int lo_eg = (p == 0) ? 60 : ep.piece_value_eg[p] - 80;
    int hi_eg = (p == 0) ? 160 : ep.piece_value_eg[p] + 80;
    specs.push_back({
      std::string("piece_value_eg_") + piece_names[p],
      ParamType::Int, &ep.piece_value_eg[static_cast<std::size_t>(p)], nullptr,
      static_cast<double>(lo_eg), static_cast<double>(hi_eg), (p == 0) ? 3.0 : 5.0
    });
  }

  // Pawn structure
  specs.push_back({"doubled_pawn_penalty",  ParamType::Int, &ep.doubled_pawn_penalty,  nullptr, -50, 0, 2});
  specs.push_back({"isolated_pawn_penalty", ParamType::Int, &ep.isolated_pawn_penalty, nullptr, -30, 0, 2});
  specs.push_back({"backward_pawn_penalty", ParamType::Int, &ep.backward_pawn_penalty, nullptr, -30, 0, 2});

  // Connected pawn bonus (ranks 1-6)
  for (int r = 1; r <= 6; ++r) {
    specs.push_back({
      "connected_bonus_" + std::to_string(r),
      ParamType::Int, &ep.connected_bonus[static_cast<std::size_t>(r)], nullptr,
      0, 120, 3
    });
  }

  // Passed pawn MG/EG per rank
  for (int r = 1; r <= 6; ++r) {
    specs.push_back({
      "passed_pawn_bonus_mg_" + std::to_string(r),
      ParamType::Int, &ep.passed_pawn_bonus_mg[static_cast<std::size_t>(r)], nullptr,
      0, 200, 5
    });
  }
  for (int r = 1; r <= 6; ++r) {
    specs.push_back({
      "passed_pawn_bonus_eg_" + std::to_string(r),
      ParamType::Int, &ep.passed_pawn_bonus_eg[static_cast<std::size_t>(r)], nullptr,
      0, 250, 5
    });
  }
  specs.push_back({"passed_pawn_free_advance",    ParamType::Int, &ep.passed_pawn_free_advance,    nullptr, 0, 40,  2});
  specs.push_back({"passed_pawn_file_penalty",    ParamType::Int, &ep.passed_pawn_file_penalty,    nullptr,-20, 0,  2});
  specs.push_back({"passed_pawn_king_proximity",  ParamType::Int, &ep.passed_pawn_king_proximity,  nullptr, 1, 12,  1});
  specs.push_back({"rook_behind_passer_mg",       ParamType::Int, &ep.rook_behind_passer_mg,       nullptr, 0, 40,  2});
  specs.push_back({"rook_behind_passer_eg",       ParamType::Int, &ep.rook_behind_passer_eg,       nullptr, 0, 50,  3});

  // Piece coordination
  specs.push_back({"rook_open_file",       ParamType::Int, &ep.rook_open_file,       nullptr, 10, 80, 3});
  specs.push_back({"rook_semi_open_file",  ParamType::Int, &ep.rook_semi_open_file,  nullptr,  5, 50, 2});
  specs.push_back({"knight_outpost_bonus", ParamType::Int, &ep.knight_outpost_bonus, nullptr, 10, 80, 3});
  specs.push_back({"bishop_outpost_bonus", ParamType::Int, &ep.bishop_outpost_bonus, nullptr,  5, 60, 3});

  // King safety
  specs.push_back({"pawn_shield_bonus",        ParamType::Int, &ep.pawn_shield_bonus,        nullptr, 0, 20,   1});
  specs.push_back({"king_castle_bonus",        ParamType::Int, &ep.king_castle_bonus,        nullptr, 0, 20,   2});
  specs.push_back({"king_attack_count_weight", ParamType::Int, &ep.king_attack_count_weight, nullptr, 20,120,  5});
  specs.push_back({"king_weak_sq_weight",      ParamType::Int, &ep.king_weak_sq_weight,      nullptr, 50,300, 10});
  specs.push_back({"king_open_file_weight",    ParamType::Int, &ep.king_open_file_weight,    nullptr, 10,100,  5});
  specs.push_back({"king_no_queen_divisor",    ParamType::Int, &ep.king_no_queen_divisor,    nullptr,  1,  4,  1});

  // King attacker weights (indices 1-4: knight, bishop, rook, queen)
  static const char* king_att_names[] = {"", "knight", "bishop", "rook", "queen"};
  for (int pt = 1; pt <= 4; ++pt) {
    specs.push_back({
      std::string("king_attacker_weight_") + king_att_names[pt],
      ParamType::Int, &ep.king_attacker_weight[static_cast<std::size_t>(pt)], nullptr,
      0, 150, 5
    });
  }

  // Safe check bonuses (indices 1-4)
  for (int pt = 1; pt <= 4; ++pt) {
    specs.push_back({
      std::string("safe_check_bonus_") + king_att_names[pt],
      ParamType::Int, &ep.safe_check_bonus[static_cast<std::size_t>(pt)], nullptr,
      200, 1500, 30
    });
  }

  // Threats
  for (int pt = 2; pt <= 4; ++pt) {
    specs.push_back({
      "threat_by_minor_" + std::to_string(pt),
      ParamType::Int, &ep.threat_by_minor[static_cast<std::size_t>(pt)], nullptr,
      0, 120, 5
    });
  }
  for (int pt : {2, 4}) {
    specs.push_back({
      "threat_by_rook_" + std::to_string(pt),
      ParamType::Int, &ep.threat_by_rook[static_cast<std::size_t>(pt)], nullptr,
      0, 100, 5
    });
  }
  specs.push_back({"hanging_penalty",  ParamType::Int, &ep.hanging_penalty,  nullptr, 20, 120, 5});
  specs.push_back({"safe_pawn_threat", ParamType::Int, &ep.safe_pawn_threat, nullptr, 50, 300, 10});
  specs.push_back({"restricted_piece", ParamType::Int, &ep.restricted_piece, nullptr,  0,  20, 1});

  // Piece-specific bonuses
  specs.push_back({"minor_behind_pawn",    ParamType::Int, &ep.minor_behind_pawn,    nullptr,  0, 30,  2});
  specs.push_back({"bishop_pawns_penalty", ParamType::Int, &ep.bishop_pawns_penalty, nullptr,-10,  0,  1});
  specs.push_back({"long_diagonal_bishop", ParamType::Int, &ep.long_diagonal_bishop, nullptr, 10, 80,  3});
  specs.push_back({"trapped_rook",         ParamType::Int, &ep.trapped_rook,         nullptr,-100, 0,  5});
  specs.push_back({"rook_on_queen_file",   ParamType::Int, &ep.rook_on_queen_file,   nullptr,  0, 20,  1});
  specs.push_back({"king_protector",       ParamType::Int, &ep.king_protector,       nullptr,-15,  0,  1});

  // Space
  specs.push_back({"center_control_weight",  ParamType::Int, &ep.center_control_weight,  nullptr, 0, 15, 1});
  specs.push_back({"enemy_territory_weight", ParamType::Int, &ep.enemy_territory_weight, nullptr, 0, 15, 1});

  // Endgame
  specs.push_back({"opposition_bonus",       ParamType::Int, &ep.opposition_bonus,       nullptr, 0, 40,  2});
  specs.push_back({"endgame_mat_threshold",  ParamType::Int, &ep.endgame_mat_threshold,  nullptr, 50, 500, 20});

  // Initiative
  specs.push_back({"initiative_pawn_weight", ParamType::Int, &ep.initiative_pawn_weight, nullptr,  1, 20,  1});
  specs.push_back({"initiative_both_flanks", ParamType::Int, &ep.initiative_both_flanks, nullptr,  0, 30,  2});
  specs.push_back({"initiative_offset",      ParamType::Int, &ep.initiative_offset,      nullptr,-200, 0, 10});

  return specs;
}

std::vector<ParamSpec> build_search_specs(SearchParams& sp) {
  return {
    {"aspiration_window",         ParamType::Int,    &sp.aspiration_window,         nullptr, 5,   100,  3},
    {"delta_margin",              ParamType::Int,    &sp.delta_margin,              nullptr, 50,  500, 15},
    {"futility_margin_per_depth", ParamType::Int,    &sp.futility_margin_per_depth, nullptr, 50,  400, 15},
    {"probcut_margin_base",       ParamType::Int,    &sp.probcut_margin_base,       nullptr, 50,  500, 15},
    {"probcut_margin_scale",      ParamType::Int,    &sp.probcut_margin_scale,      nullptr, 10,  150,  5},
    {"null_reduction_base",       ParamType::Int,    &sp.null_reduction_base,       nullptr, 1,    6,  1},
    {"null_reduction_extra",      ParamType::Int,    &sp.null_reduction_extra,      nullptr, 0,    3,  1},
    {"lmp_base",                  ParamType::Int,    &sp.lmp_base,                  nullptr, 0,    8,  1},
    {"lmp_depth_sq_coeff",        ParamType::Int,    &sp.lmp_depth_sq_coeff,        nullptr, 0,    3,  1},
    {"lmr_log_base",              ParamType::Double, nullptr, &sp.lmr_log_base,                1.0, 3.0, 0.1},
    {"singular_margin",           ParamType::Int,    &sp.singular_margin,           nullptr, 30,  500, 15},
  };
}

// ---------------------------------------------------------------------------
// EVAL MODE: Texel tuning
//   Loss = mean( (sigmoid(engine_eval) - sigmoid(reference_cp))^2 )
//   Only uses static eval, no search. Very fast.
// ---------------------------------------------------------------------------
struct EvalLossResult {
  double total_loss = 0.0;
  std::size_t count = 0;
  double mean_loss() const { return (count > 0) ? total_loss / static_cast<double>(count) : 0.0; }
};

EvalLossResult compute_eval_loss(const std::vector<TuningPosition>& positions,
                                 const EvalParams& params, double K,
                                 std::size_t begin, std::size_t end) {
  setEvalParams(params);
  EvalLossResult result;
  Position pos;

  for (std::size_t i = begin; i < end; ++i) {
    if (!pos.setFromFEN(positions[i].fen)) continue;
    int engine_eval = evaluate(pos);
    double wp_engine = sigmoid(engine_eval, K);
    double wp_ref = sigmoid(positions[i].reference_cp, K);
    double err = wp_engine - wp_ref;
    result.total_loss += err * err;
    result.count++;
  }
  return result;
}

EvalLossResult compute_eval_loss_mt(const std::vector<TuningPosition>& positions,
                                    const EvalParams& params, double K, int num_threads) {
  setEvalParams(params);

  if (num_threads <= 1) {
    return compute_eval_loss(positions, params, K, 0, positions.size());
  }

  std::vector<std::thread> threads;
  std::vector<EvalLossResult> results(static_cast<std::size_t>(num_threads));
  std::size_t chunk = positions.size() / static_cast<std::size_t>(num_threads);

  for (int t = 0; t < num_threads; ++t) {
    std::size_t b = static_cast<std::size_t>(t) * chunk;
    std::size_t e = (t == num_threads - 1) ? positions.size() : b + chunk;
    threads.emplace_back([&, t, b, e]() {
      results[static_cast<std::size_t>(t)] = compute_eval_loss(positions, params, K, b, e);
    });
  }
  for (auto& thr : threads) thr.join();

  EvalLossResult combined;
  for (const auto& r : results) {
    combined.total_loss += r.total_loss;
    combined.count += r.count;
  }
  return combined;
}

void run_eval_tuning(std::vector<TuningPosition>& all_positions, EvalParams& params,
                     int iterations, int num_threads, double K_value,
                     const std::string& out_path) {
  // Split train/validation 80/20 by hash
  std::vector<TuningPosition> train, val;
  for (auto& p : all_positions) {
    std::uint64_t h = 1469598103934665603ULL;
    for (char c : p.fen) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ULL; }
    if ((h % 5) == 0)
      val.push_back(std::move(p));
    else
      train.push_back(std::move(p));
  }
  all_positions.clear();

  std::cout << "Train: " << train.size() << " positions, Validation: " << val.size() << "\n" << std::flush;

  double K = K_value;

  auto specs = build_eval_specs(params);
  double best_loss = compute_eval_loss_mt(train, params, K, num_threads).mean_loss();

  std::cout << "\n=== Texel Tuning (Eval Parameters) ===\n";
  std::cout << "Initial loss: " << std::fixed << std::setprecision(8) << best_loss << "\n\n" << std::flush;

  for (int iter = 0; iter < iterations; ++iter) {
    auto iter_start = std::chrono::steady_clock::now();
    int improved_count = 0;

    for (auto& spec : specs) {
      double original = get_value(spec);
      double step = spec.step;

      // Try +step
      set_value(spec, original + step);
      double loss_plus = compute_eval_loss_mt(train, params, K, num_threads).mean_loss();

      if (loss_plus < best_loss) {
        best_loss = loss_plus;
        ++improved_count;
        continue;
      }

      // Try -step
      set_value(spec, original - step);
      double loss_minus = compute_eval_loss_mt(train, params, K, num_threads).mean_loss();

      if (loss_minus < best_loss) {
        best_loss = loss_minus;
        ++improved_count;
        continue;
      }

      // No improvement, revert
      set_value(spec, original);
    }

    auto iter_elapsed = std::chrono::steady_clock::now() - iter_start;
    double iter_secs = std::chrono::duration<double>(iter_elapsed).count();

    double val_loss = compute_eval_loss_mt(val, params, K, num_threads).mean_loss();

    std::cout << "Iter " << std::setw(3) << (iter + 1)
              << ": train_loss=" << std::fixed << std::setprecision(8) << best_loss
              << "  val_loss=" << val_loss
              << "  improved=" << improved_count
              << "  (" << std::setprecision(1) << iter_secs << "s)\n";

    // Print current values
    if ((iter + 1) % 5 == 0 || improved_count == 0) {
      std::cout << "  Current params:\n";
      for (const auto& s : specs) {
        std::cout << "    " << std::setw(24) << std::left << s.name << " = ";
        if (s.type == ParamType::Int)
          std::cout << *s.int_ptr;
        else
          std::cout << std::fixed << std::setprecision(3) << *s.double_ptr;
        std::cout << "\n";
      }
      std::cout << "\n";
    }

    if (improved_count == 0) {
      // Halve all step sizes
      bool any_above_min = false;
      for (auto& spec : specs) {
        spec.step = std::max(spec.step * 0.5, (spec.type == ParamType::Int) ? 1.0 : 0.01);
        if (spec.step > ((spec.type == ParamType::Int) ? 1.0 : 0.01))
          any_above_min = true;
      }
      if (!any_above_min) {
        std::cout << "All step sizes at minimum, converged.\n";
        break;
      }
      std::cout << "  No improvement — halved step sizes.\n\n";
    }
  }

  // Final report
  double final_val = compute_eval_loss_mt(val, params, K, num_threads).mean_loss();
  std::cout << "\n=== Final Results ===\n";
  std::cout << "Train loss: " << std::fixed << std::setprecision(8) << best_loss << "\n";
  std::cout << "Val   loss: " << final_val << "\n";
  std::cout << "K = " << K << "\n\n";

  // Save output
  EngineParams full_params;
  full_params.eval = params;
  std::ofstream out(out_path);
  if (out) {
    out << "{\n";
    out << "  \"mode\": \"eval\",\n";
    out << "  \"K\": " << K << ",\n";
    out << "  \"train_loss\": " << best_loss << ",\n";
    out << "  \"val_loss\": " << final_val << ",\n";
    out << "  \"params\": " << engineParamsToJson(full_params) << "\n";
    out << "}\n";
    std::cout << "Results saved to " << out_path << "\n";
  }
}

// ---------------------------------------------------------------------------
// SEARCH MODE: Multi-signal loss using full multi-PV reference data
//
// For each position we compute three loss components:
//
// 1. Move quality loss (continuous):
//    The engine picks a best move. We look up that move in the reference PVs.
//    If it matches PV_i, the quality loss = sigmoid(best_ref_cp) - sigmoid(pv_i_cp).
//    This is 0 when the engine finds THE best move, and smoothly increases
//    for weaker choices. If the move isn't in any PV, we use the engine's own
//    eval vs the best reference eval.
//
// 2. Eval accuracy loss:
//    (sigmoid(engine_cp) - sigmoid(best_ref_cp))^2
//    Gives continuous gradient even when the best move doesn't change.
//
// 3. Best-move match (for reporting only, not in the loss):
//    Whether engine's move == PV_0's move.
// ---------------------------------------------------------------------------
struct SearchResult {
  double move_quality_loss = 0.0;  // 0 = found best move, higher = worse
  double eval_loss = 0.0;          // sigmoid MSE vs reference
  bool best_move_match = false;
  bool valid = false;
};

int engine_score_to_cp(int score, Color stm) {
  constexpr int kScoreMate = 32000;
  constexpr int kMateThreshold = kScoreMate - 64;
  int cp = score;
  if (score >= kMateThreshold)
    cp = kMateCpEquiv;
  else if (score <= -kMateThreshold)
    cp = -kMateCpEquiv;
  if (stm == Color::Black) cp = -cp;
  return cp;
}

SearchResult eval_position_search(const TuningPosition& tp, Search& search,
                                  const EvalParams& eparams, int depth, double K) {
  SearchResult sr;
  Position pos;
  if (!pos.setFromFEN(tp.fen)) return sr;

  setEvalParams(eparams);

  SearchLimits limits;
  limits.max_depth = depth;
  limits.hard_time_ms = 5000;
  Move best = search.start(pos, limits);
  if (best == kNullMove) return sr;

  sr.valid = true;

  std::string engine_move = toUci(best);
  int engine_cp = engine_score_to_cp(search.getScore(pos), pos.side_to_move());
  double wp_best_ref = sigmoid(tp.reference_cp, K);
  double wp_engine = sigmoid(engine_cp, K);

  // 1. Move quality: check if engine's move matches any reference PV
  sr.best_move_match = (engine_move == tp.best_move_uci);

  bool found_in_pvs = false;
  for (const auto& cand : tp.candidates) {
    if (engine_move == cand.move_uci) {
      double wp_cand = sigmoid(cand.cp, K);
      sr.move_quality_loss = std::max(0.0, wp_best_ref - wp_cand);
      found_in_pvs = true;
      break;
    }
  }

  if (!found_in_pvs) {
    // Engine chose a move not in any reference PV.
    // Use the engine's own eval as the quality estimate.
    // Loss = max(0, wp_best_ref - wp_engine), so no penalty if engine
    // thinks its move is better (could be a legitimate alternative).
    sr.move_quality_loss = std::max(0.0, wp_best_ref - wp_engine);
  }

  // 2. Eval accuracy
  double eval_err = wp_engine - wp_best_ref;
  sr.eval_loss = eval_err * eval_err;

  return sr;
}

struct SearchLossResult {
  double mean_move_quality_loss = 0.0;
  double mean_eval_loss = 0.0;
  double move_match_rate = 0.0;
  std::size_t count = 0;
};

constexpr std::size_t kTunerTTMB = 4;

SearchLossResult compute_search_loss(const std::vector<TuningPosition>& positions,
                                     const SearchParams& sparams, const EvalParams& eparams,
                                     int depth, double K, int num_threads) {
  struct Accum {
    std::uint64_t move_quality_x1e12 = 0;
    std::uint64_t eval_loss_x1e12 = 0;
    std::size_t matches = 0;
    std::size_t valid = 0;
  };

  std::vector<Accum> accums(static_cast<std::size_t>(num_threads));

  auto worker = [&](int tid, std::size_t begin, std::size_t end) {
    Search search;
    search.resizeTT(kTunerTTMB);
    search.setParams(sparams);

    auto& acc = accums[static_cast<std::size_t>(tid)];
    for (std::size_t i = begin; i < end; ++i) {
      auto sr = eval_position_search(positions[i], search, eparams, depth, K);
      if (!sr.valid) continue;
      ++acc.valid;
      if (sr.best_move_match) ++acc.matches;
      acc.move_quality_x1e12 += static_cast<std::uint64_t>(sr.move_quality_loss * 1e12);
      acc.eval_loss_x1e12 += static_cast<std::uint64_t>(sr.eval_loss * 1e12);
    }
  };

  std::vector<std::thread> threads;
  std::size_t chunk = positions.size() / static_cast<std::size_t>(num_threads);
  for (int t = 0; t < num_threads; ++t) {
    std::size_t b = static_cast<std::size_t>(t) * chunk;
    std::size_t e = (t == num_threads - 1) ? positions.size() : b + chunk;
    threads.emplace_back(worker, t, b, e);
  }
  for (auto& thr : threads) thr.join();

  Accum total;
  for (const auto& a : accums) {
    total.move_quality_x1e12 += a.move_quality_x1e12;
    total.eval_loss_x1e12 += a.eval_loss_x1e12;
    total.matches += a.matches;
    total.valid += a.valid;
  }

  SearchLossResult result;
  result.count = total.valid;
  if (result.count > 0) {
    double n = static_cast<double>(result.count);
    result.mean_move_quality_loss = static_cast<double>(total.move_quality_x1e12) / 1e12 / n;
    result.mean_eval_loss = static_cast<double>(total.eval_loss_x1e12) / 1e12 / n;
    result.move_match_rate = static_cast<double>(total.matches) / n;
  }
  return result;
}

void run_search_tuning(std::vector<TuningPosition>& all_positions, SearchParams& sparams,
                       EvalParams& eparams, int iterations, int search_depth,
                       int num_threads, double K, double w_eval,
                       const std::string& out_path) {
  // Split train/val
  std::vector<TuningPosition> train, val;
  for (auto& p : all_positions) {
    std::uint64_t h = 1469598103934665603ULL;
    for (char c : p.fen) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ULL; }
    if ((h % 5) == 0)
      val.push_back(std::move(p));
    else
      train.push_back(std::move(p));
  }
  all_positions.clear();

  std::cout << "Train: " << train.size() << " positions, Validation: " << val.size() << "\n";

  auto specs = build_search_specs(sparams);

  auto combined_loss = [&](const std::vector<TuningPosition>& positions) {
    auto r = compute_search_loss(positions, sparams, eparams, search_depth, K, num_threads);
    return r.mean_move_quality_loss + w_eval * r.mean_eval_loss;
  };

  double best_loss = combined_loss(train);

  std::cout << "\n=== Search Parameter Tuning ===\n";
  std::cout << "Depth: " << search_depth << ", K: " << K << ", w_eval: " << w_eval << "\n";

  {
    auto r = compute_search_loss(train, sparams, eparams, search_depth, K, num_threads);
    std::cout << "Initial: move_quality=" << std::fixed << std::setprecision(6)
              << r.mean_move_quality_loss
              << "  eval_err=" << r.mean_eval_loss
              << "  best_move=" << std::setprecision(2) << (r.move_match_rate * 100.0) << "%"
              << "  combined=" << std::setprecision(6) << best_loss << "\n\n";
  }

  for (int iter = 0; iter < iterations; ++iter) {
    auto iter_start = std::chrono::steady_clock::now();
    int improved_count = 0;

    for (std::size_t pi = 0; pi < specs.size(); ++pi) {
      auto& spec = specs[pi];
      double original = get_value(spec);
      double step = spec.step;

      std::cout << "  [" << (pi + 1) << "/" << specs.size() << "] "
                << spec.name << " = " << original << " ..." << std::flush;

      set_value(spec, original + step);
      double loss_plus = combined_loss(train);
      if (loss_plus < best_loss) {
        best_loss = loss_plus;
        ++improved_count;
        std::cout << " -> " << get_value(spec) << " (+" << step << ")\n" << std::flush;
        continue;
      }

      set_value(spec, original - step);
      double loss_minus = combined_loss(train);
      if (loss_minus < best_loss) {
        best_loss = loss_minus;
        ++improved_count;
        std::cout << " -> " << get_value(spec) << " (-" << step << ")\n" << std::flush;
        continue;
      }

      set_value(spec, original);
      std::cout << " unchanged\n" << std::flush;
    }

    auto iter_elapsed = std::chrono::steady_clock::now() - iter_start;
    double iter_secs = std::chrono::duration<double>(iter_elapsed).count();

    auto val_r = compute_search_loss(val, sparams, eparams, search_depth, K, num_threads);
    double val_combined = val_r.mean_move_quality_loss + w_eval * val_r.mean_eval_loss;

    std::cout << "Iter " << std::setw(3) << (iter + 1)
              << ": loss=" << std::fixed << std::setprecision(6) << best_loss
              << "  val=" << val_combined
              << "  best_move=" << std::setprecision(2) << (val_r.move_match_rate * 100.0) << "%"
              << "  improved=" << improved_count
              << "  (" << std::setprecision(1) << iter_secs << "s)\n" << std::flush;

    if ((iter + 1) % 3 == 0 || improved_count == 0) {
      std::cout << "  Current params:\n";
      for (const auto& s : specs) {
        std::cout << "    " << std::setw(28) << std::left << s.name << " = ";
        if (s.type == ParamType::Int)
          std::cout << *s.int_ptr;
        else
          std::cout << std::fixed << std::setprecision(3) << *s.double_ptr;
        std::cout << "\n";
      }
      std::cout << "\n";
    }

    if (improved_count == 0) {
      bool any_above_min = false;
      for (auto& spec : specs) {
        spec.step = std::max(spec.step * 0.5, (spec.type == ParamType::Int) ? 1.0 : 0.01);
        if (spec.step > ((spec.type == ParamType::Int) ? 1.0 : 0.01))
          any_above_min = true;
      }
      if (!any_above_min) {
        std::cout << "All step sizes at minimum, converged.\n";
        break;
      }
      std::cout << "  No improvement — halved step sizes.\n\n";
    }
  }

  // Final
  auto final_train = compute_search_loss(train, sparams, eparams, search_depth, K, num_threads);
  auto final_val = compute_search_loss(val, sparams, eparams, search_depth, K, num_threads);

  std::cout << "\n=== Final Results ===\n";
  std::cout << "Train: move_quality=" << std::fixed << std::setprecision(6)
            << final_train.mean_move_quality_loss
            << "  eval_err=" << final_train.mean_eval_loss
            << "  best_move=" << std::setprecision(2) << (final_train.move_match_rate * 100.0) << "%\n";
  std::cout << "Val:   move_quality=" << std::setprecision(6) << final_val.mean_move_quality_loss
            << "  eval_err=" << final_val.mean_eval_loss
            << "  best_move=" << std::setprecision(2) << (final_val.move_match_rate * 100.0) << "%\n\n";

  EngineParams full_params;
  full_params.search = sparams;
  full_params.eval = eparams;
  std::ofstream out(out_path);
  if (out) {
    out << "{\n";
    out << "  \"mode\": \"search\",\n";
    out << "  \"search_depth\": " << search_depth << ",\n";
    out << "  \"K\": " << K << ",\n";
    out << "  \"train_move_quality\": " << final_train.mean_move_quality_loss << ",\n";
    out << "  \"train_eval_error\": " << final_train.mean_eval_loss << ",\n";
    out << "  \"train_best_move_match\": " << final_train.move_match_rate << ",\n";
    out << "  \"val_move_quality\": " << final_val.mean_move_quality_loss << ",\n";
    out << "  \"val_eval_error\": " << final_val.mean_eval_loss << ",\n";
    out << "  \"val_best_move_match\": " << final_val.move_match_rate << ",\n";
    out << "  \"params\": " << engineParamsToJson(full_params) << "\n";
    out << "}\n";
    std::cout << "Results saved to " << out_path << "\n";
  }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
void print_usage() {
  std::cout <<
R"(Aurora Parameter Tuner
Usage: tuner --mode <eval|search> --dataset <path> [options]

Common options:
  --dataset <path>       Path to lichess_db_eval.jsonl (required)
  --mode <eval|search>   Tuning mode (required)
  --params <path>        Load initial params from JSON file
  --out <path>           Output file (default: tuning_result.json)
  --max-positions N      Max positions to sample (default: 100000)
  --iterations N         Max tuning iterations (default: 50)
  --threads N            Worker threads (default: 1)
  --seed N               RNG seed (default: 42)
  --skip-mates           Skip positions with mate scores
  --skip-extreme N       Skip positions with |cp| > N (default: 0 = disabled)
  --first-n              Take first N positions (fast) instead of reservoir sampling (slow)
  --K <value>            Sigmoid scaling factor (default: 400)

Search mode options:
  --depth N              Search depth for move-match (default: 6)
  --w-eval <value>       Weight for eval error in combined loss (default: 1.0)
)";
}

}  // namespace
}  // namespace aurora

int main(int argc, char** argv) {
  using namespace aurora;

  std::string mode;
  std::string dataset_path;
  std::string params_path;
  std::string out_path = "tuning_result.json";
  int max_positions = 100000;
  int iterations = 50;
  int num_threads = 1;
  unsigned int seed = 42;
  bool skip_mates = false;
  int skip_extreme = 0;
  bool first_n = false;
  double K = -1.0;
  int search_depth = 6;
  double w_eval = 1.0;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") { print_usage(); return 0; }
    else if (arg == "--mode" && i + 1 < argc) mode = argv[++i];
    else if (arg == "--dataset" && i + 1 < argc) dataset_path = argv[++i];
    else if (arg == "--params" && i + 1 < argc) params_path = argv[++i];
    else if (arg == "--out" && i + 1 < argc) out_path = argv[++i];
    else if (arg == "--max-positions" && i + 1 < argc) max_positions = std::stoi(argv[++i]);
    else if (arg == "--iterations" && i + 1 < argc) iterations = std::stoi(argv[++i]);
    else if (arg == "--threads" && i + 1 < argc) num_threads = std::stoi(argv[++i]);
    else if (arg == "--seed" && i + 1 < argc) seed = static_cast<unsigned int>(std::stoul(argv[++i]));
    else if (arg == "--skip-mates") skip_mates = true;
    else if (arg == "--skip-extreme" && i + 1 < argc) skip_extreme = std::stoi(argv[++i]);
    else if (arg == "--first-n") first_n = true;
    else if (arg == "--K" && i + 1 < argc) K = std::stod(argv[++i]);
    else if (arg == "--depth" && i + 1 < argc) search_depth = std::stoi(argv[++i]);
    else if (arg == "--w-eval" && i + 1 < argc) w_eval = std::stod(argv[++i]);
    else {
      std::cerr << "Unknown option: " << arg << "\n";
      print_usage();
      return 1;
    }
  }

  if (mode.empty() || dataset_path.empty()) {
    print_usage();
    return 1;
  }

  EngineParams params;
  if (!params_path.empty()) {
    std::ifstream pf(params_path);
    if (pf) {
      std::ostringstream buf;
      buf << pf.rdbuf();
      parseEngineParamsJson(buf.str(), params);
      std::cout << "Loaded params from " << params_path << "\n";
    }
  }

  std::cout << "Loading dataset (sampling " << max_positions << " positions)...\n";
  auto positions = load_dataset(dataset_path, max_positions, seed, skip_mates,
                                skip_extreme > 0, skip_extreme, first_n);
  if (positions.empty()) {
    std::cerr << "No positions loaded.\n";
    return 1;
  }

  if (mode == "eval") {
    if (K <= 0.0) K = kDefaultK;
    run_eval_tuning(positions, params.eval, iterations, num_threads, K, out_path);
  } else if (mode == "search") {
    if (K <= 0.0) K = kDefaultK;
    run_search_tuning(positions, params.search, params.eval, iterations,
                      search_depth, num_threads, K, w_eval, out_path);
  } else {
    std::cerr << "Unknown mode: " << mode << " (use 'eval' or 'search')\n";
    return 1;
  }

  return 0;
}
