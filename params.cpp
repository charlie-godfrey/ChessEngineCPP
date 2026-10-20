#include "params.h"

#include <cctype>
#include <charconv>
#include <sstream>

namespace aurora {
namespace {

bool find_key(const std::string& json, const std::string& key, std::size_t& pos) {
  const std::string token = "\"" + key + "\"";
  pos = json.find(token);
  return pos != std::string::npos;
}

bool parse_int_at(const std::string& json, std::size_t start, int& value) {
  while (start < json.size() && std::isspace(static_cast<unsigned char>(json[start]))) {
    ++start;
  }
  if (start >= json.size() || (json[start] != '-' && !std::isdigit(static_cast<unsigned char>(json[start])))) {
    return false;
  }
  const char* begin = json.data() + start;
  const char* end = json.data() + json.size();
  int out = 0;
  auto res = std::from_chars(begin, end, out);
  if (res.ec != std::errc()) {
    return false;
  }
  value = out;
  return true;
}

bool parse_double_at(const std::string& json, std::size_t start, double& value) {
  while (start < json.size() && std::isspace(static_cast<unsigned char>(json[start]))) {
    ++start;
  }
  if (start >= json.size() || (json[start] != '-' && !std::isdigit(static_cast<unsigned char>(json[start])))) {
    return false;
  }
  char* endptr = nullptr;
  value = std::strtod(json.c_str() + start, &endptr);
  return endptr != nullptr;
}

bool extract_int(const std::string& json, const std::string& key, int& out) {
  std::size_t pos = 0;
  if (!find_key(json, key, pos)) {
    return false;
  }
  pos = json.find(':', pos);
  if (pos == std::string::npos) {
    return false;
  }
  return parse_int_at(json, pos + 1, out);
}

bool extract_double(const std::string& json, const std::string& key, double& out) {
  std::size_t pos = 0;
  if (!find_key(json, key, pos)) {
    return false;
  }
  pos = json.find(':', pos);
  if (pos == std::string::npos) {
    return false;
  }
  return parse_double_at(json, pos + 1, out);
}

template <std::size_t N>
bool extract_int_array(const std::string& json, const std::string& key, std::array<int, N>& out) {
  std::size_t pos = 0;
  if (!find_key(json, key, pos)) {
    return false;
  }
  pos = json.find('[', pos);
  if (pos == std::string::npos) {
    return false;
  }
  std::size_t idx = 0;
  std::size_t i = pos + 1;
  while (i < json.size() && idx < out.size()) {
    while (i < json.size() && (std::isspace(static_cast<unsigned char>(json[i])) || json[i] == ',')) {
      ++i;
    }
    if (i >= json.size() || json[i] == ']') {
      break;
    }
    int value = 0;
    if (!parse_int_at(json, i, value)) {
      return false;
    }
    out[idx++] = value;
    while (i < json.size() && json[i] != ',' && json[i] != ']') {
      ++i;
    }
  }
  return idx == out.size();
}

template <std::size_t N>
void write_int_array(std::ostringstream& out, const std::string& key, const std::array<int, N>& arr) {
  out << "\"" << key << "\":[";
  for (std::size_t i = 0; i < N; ++i) {
    if (i > 0) out << ",";
    out << arr[i];
  }
  out << "]";
}

}  // namespace

std::string engineParamsToJson(const EngineParams& params) {
  std::ostringstream out;
  out << "{";
  out << "\"search\":{";
  out << "\"aspiration_window\":" << params.search.aspiration_window << ",";
  out << "\"quiescence_max_depth\":" << params.search.quiescence_max_depth << ",";
  out << "\"delta_margin\":" << params.search.delta_margin << ",";
  out << "\"futility_margin_per_depth\":" << params.search.futility_margin_per_depth << ",";
  out << "\"probcut_margin_base\":" << params.search.probcut_margin_base << ",";
  out << "\"probcut_margin_scale\":" << params.search.probcut_margin_scale << ",";
  out << "\"null_reduction_base\":" << params.search.null_reduction_base << ",";
  out << "\"null_reduction_extra\":" << params.search.null_reduction_extra << ",";
  out << "\"null_reduction_depth_threshold\":" << params.search.null_reduction_depth_threshold << ",";
  out << "\"lmp_base\":" << params.search.lmp_base << ",";
  out << "\"lmp_depth_sq_coeff\":" << params.search.lmp_depth_sq_coeff << ",";
  out << "\"lmr_min_depth\":" << params.search.lmr_min_depth << ",";
  out << "\"lmr_move_threshold\":" << params.search.lmr_move_threshold << ",";
  out << "\"lmr_log_base\":" << params.search.lmr_log_base << ",";
  out << "\"singular_margin\":" << params.search.singular_margin << ",";
  out << "\"singular_depth_reduction\":" << params.search.singular_depth_reduction << ",";
  out << "\"check_extension\":" << params.search.check_extension << ",";
  out << "\"q_check_extension\":" << params.search.q_check_extension << ",";
  out << "\"q_extension_margin\":" << params.search.q_extension_margin << ",";
  out << "\"q_promo_extension\":" << params.search.q_promo_extension;
  out << "},\"eval\":{";
  out << "\"tempo_bonus\":" << params.eval.tempo_bonus << ",";
  write_int_array(out, "piece_value_mg", params.eval.piece_value_mg); out << ",";
  write_int_array(out, "piece_value_eg", params.eval.piece_value_eg); out << ",";
  out << "\"doubled_pawn_penalty\":" << params.eval.doubled_pawn_penalty << ",";
  out << "\"isolated_pawn_penalty\":" << params.eval.isolated_pawn_penalty << ",";
  out << "\"backward_pawn_penalty\":" << params.eval.backward_pawn_penalty << ",";
  write_int_array(out, "connected_bonus", params.eval.connected_bonus); out << ",";
  write_int_array(out, "passed_pawn_bonus_mg", params.eval.passed_pawn_bonus_mg); out << ",";
  write_int_array(out, "passed_pawn_bonus_eg", params.eval.passed_pawn_bonus_eg); out << ",";
  out << "\"passed_pawn_free_advance\":" << params.eval.passed_pawn_free_advance << ",";
  out << "\"passed_pawn_file_penalty\":" << params.eval.passed_pawn_file_penalty << ",";
  out << "\"passed_pawn_king_proximity\":" << params.eval.passed_pawn_king_proximity << ",";
  out << "\"rook_behind_passer_mg\":" << params.eval.rook_behind_passer_mg << ",";
  out << "\"rook_behind_passer_eg\":" << params.eval.rook_behind_passer_eg << ",";
  out << "\"rook_open_file\":" << params.eval.rook_open_file << ",";
  out << "\"rook_semi_open_file\":" << params.eval.rook_semi_open_file << ",";
  out << "\"knight_outpost_bonus\":" << params.eval.knight_outpost_bonus << ",";
  out << "\"bishop_outpost_bonus\":" << params.eval.bishop_outpost_bonus << ",";
  out << "\"pawn_shield_bonus\":" << params.eval.pawn_shield_bonus << ",";
  out << "\"king_castle_bonus\":" << params.eval.king_castle_bonus << ",";
  out << "\"king_attack_count_weight\":" << params.eval.king_attack_count_weight << ",";
  out << "\"king_weak_sq_weight\":" << params.eval.king_weak_sq_weight << ",";
  out << "\"king_open_file_weight\":" << params.eval.king_open_file_weight << ",";
  out << "\"king_no_queen_divisor\":" << params.eval.king_no_queen_divisor << ",";
  write_int_array(out, "king_attacker_weight", params.eval.king_attacker_weight); out << ",";
  write_int_array(out, "safe_check_bonus", params.eval.safe_check_bonus); out << ",";
  write_int_array(out, "threat_by_minor", params.eval.threat_by_minor); out << ",";
  write_int_array(out, "threat_by_rook", params.eval.threat_by_rook); out << ",";
  out << "\"hanging_penalty\":" << params.eval.hanging_penalty << ",";
  out << "\"safe_pawn_threat\":" << params.eval.safe_pawn_threat << ",";
  out << "\"restricted_piece\":" << params.eval.restricted_piece << ",";
  out << "\"minor_behind_pawn\":" << params.eval.minor_behind_pawn << ",";
  out << "\"bishop_pawns_penalty\":" << params.eval.bishop_pawns_penalty << ",";
  out << "\"long_diagonal_bishop\":" << params.eval.long_diagonal_bishop << ",";
  out << "\"trapped_rook\":" << params.eval.trapped_rook << ",";
  out << "\"rook_on_queen_file\":" << params.eval.rook_on_queen_file << ",";
  out << "\"king_protector\":" << params.eval.king_protector << ",";
  out << "\"center_control_weight\":" << params.eval.center_control_weight << ",";
  out << "\"enemy_territory_weight\":" << params.eval.enemy_territory_weight << ",";
  out << "\"opposition_bonus\":" << params.eval.opposition_bonus << ",";
  out << "\"endgame_mat_threshold\":" << params.eval.endgame_mat_threshold << ",";
  out << "\"initiative_pawn_weight\":" << params.eval.initiative_pawn_weight << ",";
  out << "\"initiative_both_flanks\":" << params.eval.initiative_both_flanks << ",";
  out << "\"initiative_offset\":" << params.eval.initiative_offset;
  out << "}}";
  return out.str();
}

bool parseEngineParamsJson(const std::string& json, EngineParams& out) {
  bool any = false;
  auto& sp = out.search;
  auto& ep = out.eval;

  any |= extract_int(json, "aspiration_window", sp.aspiration_window);
  any |= extract_int(json, "quiescence_max_depth", sp.quiescence_max_depth);
  any |= extract_int(json, "delta_margin", sp.delta_margin);
  any |= extract_int(json, "futility_margin_per_depth", sp.futility_margin_per_depth);
  any |= extract_int(json, "probcut_margin_base", sp.probcut_margin_base);
  any |= extract_int(json, "probcut_margin_scale", sp.probcut_margin_scale);
  any |= extract_int(json, "null_reduction_base", sp.null_reduction_base);
  any |= extract_int(json, "null_reduction_extra", sp.null_reduction_extra);
  any |= extract_int(json, "null_reduction_depth_threshold", sp.null_reduction_depth_threshold);
  any |= extract_int(json, "lmp_base", sp.lmp_base);
  any |= extract_int(json, "lmp_depth_sq_coeff", sp.lmp_depth_sq_coeff);
  any |= extract_int(json, "lmr_min_depth", sp.lmr_min_depth);
  any |= extract_int(json, "lmr_move_threshold", sp.lmr_move_threshold);
  any |= extract_double(json, "lmr_log_base", sp.lmr_log_base);
  any |= extract_int(json, "singular_margin", sp.singular_margin);
  any |= extract_int(json, "singular_depth_reduction", sp.singular_depth_reduction);
  any |= extract_int(json, "check_extension", sp.check_extension);
  any |= extract_int(json, "q_check_extension", sp.q_check_extension);
  any |= extract_int(json, "q_extension_margin", sp.q_extension_margin);
  any |= extract_int(json, "q_promo_extension", sp.q_promo_extension);

  any |= extract_int(json, "tempo_bonus", ep.tempo_bonus);
  any |= extract_int_array(json, "piece_value_mg", ep.piece_value_mg);
  any |= extract_int_array(json, "piece_value_eg", ep.piece_value_eg);
  any |= extract_int(json, "doubled_pawn_penalty", ep.doubled_pawn_penalty);
  any |= extract_int(json, "isolated_pawn_penalty", ep.isolated_pawn_penalty);
  any |= extract_int(json, "backward_pawn_penalty", ep.backward_pawn_penalty);
  any |= extract_int_array(json, "connected_bonus", ep.connected_bonus);
  any |= extract_int_array(json, "passed_pawn_bonus_mg", ep.passed_pawn_bonus_mg);
  any |= extract_int_array(json, "passed_pawn_bonus_eg", ep.passed_pawn_bonus_eg);
  any |= extract_int(json, "passed_pawn_free_advance", ep.passed_pawn_free_advance);
  any |= extract_int(json, "passed_pawn_file_penalty", ep.passed_pawn_file_penalty);
  any |= extract_int(json, "passed_pawn_king_proximity", ep.passed_pawn_king_proximity);
  any |= extract_int(json, "rook_behind_passer_mg", ep.rook_behind_passer_mg);
  any |= extract_int(json, "rook_behind_passer_eg", ep.rook_behind_passer_eg);
  any |= extract_int(json, "rook_open_file", ep.rook_open_file);
  any |= extract_int(json, "rook_semi_open_file", ep.rook_semi_open_file);
  any |= extract_int(json, "knight_outpost_bonus", ep.knight_outpost_bonus);
  any |= extract_int(json, "bishop_outpost_bonus", ep.bishop_outpost_bonus);
  any |= extract_int(json, "pawn_shield_bonus", ep.pawn_shield_bonus);
  any |= extract_int(json, "king_castle_bonus", ep.king_castle_bonus);
  any |= extract_int(json, "king_attack_count_weight", ep.king_attack_count_weight);
  any |= extract_int(json, "king_weak_sq_weight", ep.king_weak_sq_weight);
  any |= extract_int(json, "king_open_file_weight", ep.king_open_file_weight);
  any |= extract_int(json, "king_no_queen_divisor", ep.king_no_queen_divisor);
  any |= extract_int_array(json, "king_attacker_weight", ep.king_attacker_weight);
  any |= extract_int_array(json, "safe_check_bonus", ep.safe_check_bonus);
  any |= extract_int_array(json, "threat_by_minor", ep.threat_by_minor);
  any |= extract_int_array(json, "threat_by_rook", ep.threat_by_rook);
  any |= extract_int(json, "hanging_penalty", ep.hanging_penalty);
  any |= extract_int(json, "safe_pawn_threat", ep.safe_pawn_threat);
  any |= extract_int(json, "restricted_piece", ep.restricted_piece);
  any |= extract_int(json, "minor_behind_pawn", ep.minor_behind_pawn);
  any |= extract_int(json, "bishop_pawns_penalty", ep.bishop_pawns_penalty);
  any |= extract_int(json, "long_diagonal_bishop", ep.long_diagonal_bishop);
  any |= extract_int(json, "trapped_rook", ep.trapped_rook);
  any |= extract_int(json, "rook_on_queen_file", ep.rook_on_queen_file);
  any |= extract_int(json, "king_protector", ep.king_protector);
  any |= extract_int(json, "center_control_weight", ep.center_control_weight);
  any |= extract_int(json, "enemy_territory_weight", ep.enemy_territory_weight);
  any |= extract_int(json, "opposition_bonus", ep.opposition_bonus);
  any |= extract_int(json, "endgame_mat_threshold", ep.endgame_mat_threshold);
  any |= extract_int(json, "initiative_pawn_weight", ep.initiative_pawn_weight);
  any |= extract_int(json, "initiative_both_flanks", ep.initiative_both_flanks);
  any |= extract_int(json, "initiative_offset", ep.initiative_offset);

  return any;
}

}  // namespace aurora
