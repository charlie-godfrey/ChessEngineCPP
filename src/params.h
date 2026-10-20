#pragma once

#include <array>
#include <string>

namespace aurora {

struct SearchParams {
  int aspiration_window = 28;
  int quiescence_max_depth = 14;
  int delta_margin = 200;
  int futility_margin_per_depth = 250;
  int probcut_margin_base = 200;
  int probcut_margin_scale = 50;
  int null_reduction_base = 4;
  int null_reduction_extra = 1;
  int null_reduction_depth_threshold = 6;
  int lmp_base = 3;
  int lmp_depth_sq_coeff = 1;
  int lmr_min_depth = 3;
  int lmr_move_threshold = 1;
  double lmr_log_base = 1.5;
  int singular_margin = 150;
  int singular_depth_reduction = 3;
  int check_extension = 1;
  int q_check_extension = 1;
  int q_extension_margin = 2;
  int q_promo_extension = 1;
};

struct EvalParams {
  int tempo_bonus = 28;

  // Material values (MG / EG)
  std::array<int, 5> piece_value_mg = {82, 337, 365, 477, 1025};
  std::array<int, 5> piece_value_eg = {94, 281, 297, 512, 936};

  // Pawn structure
  int doubled_pawn_penalty = -11;
  int isolated_pawn_penalty = -5;
  int backward_pawn_penalty = -9;
  std::array<int, 8> connected_bonus = {0, 7, 8, 12, 29, 48, 86, 0};

  // Passed pawn bonuses per rank (MG and EG)
  std::array<int, 8> passed_pawn_bonus_mg = {0, 5, 10, 17, 32, 70, 128, 0};
  std::array<int, 8> passed_pawn_bonus_eg = {0, 7, 13, 25, 45, 95, 175, 0};
  int passed_pawn_free_advance = 18;
  int passed_pawn_file_penalty = -7;
  int passed_pawn_king_proximity = 5;
  int rook_behind_passer_mg = 15;
  int rook_behind_passer_eg = 25;

  // Piece coordination
  int rook_open_file = 47;
  int rook_semi_open_file = 25;
  int knight_outpost_bonus = 54;
  int bishop_outpost_bonus = 35;

  // King safety
  int pawn_shield_bonus = 8;
  int king_castle_bonus = 5;
  int king_attack_count_weight = 69;
  int king_weak_sq_weight = 185;
  int king_open_file_weight = 42;
  int king_no_queen_divisor = 2;
  std::array<int, 6> king_attacker_weight = {0, 0, 81, 52, 44, 10};
  std::array<int, 6> safe_check_bonus = {0, 790, 635, 1080, 780, 0};

  // Threats
  std::array<int, 6> threat_by_minor = {0, 0, 30, 55, 80, 0};
  std::array<int, 6> threat_by_rook = {0, 0, 35, 0, 55, 0};
  int hanging_penalty = 69;
  int safe_pawn_threat = 173;
  int restricted_piece = 7;

  // Piece-specific bonuses
  int minor_behind_pawn = 16;
  int bishop_pawns_penalty = -3;
  int long_diagonal_bishop = 45;
  int trapped_rook = -47;
  int rook_on_queen_file = 7;
  int king_protector = -8;

  // Space
  int center_control_weight = 5;
  int enemy_territory_weight = 5;

  // Endgame
  int opposition_bonus = 15;
  int endgame_mat_threshold = 200;

  // Initiative
  int initiative_pawn_weight = 9;
  int initiative_both_flanks = 11;
  int initiative_offset = -110;
};

struct EngineParams {
  SearchParams search;
  EvalParams eval;
};

std::string engineParamsToJson(const EngineParams& params);
bool parseEngineParamsJson(const std::string& json, EngineParams& out);

}  // namespace aurora
