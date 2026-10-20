#pragma once

#include <array>
#include <string>

namespace aurora {

struct SearchParams {
  int aspiration_window = 25;
  int quiescence_max_depth = 14;
  int delta_margin = 200;
  int futility_margin_per_depth = 250;
  int probcut_margin_base = 200;
  int probcut_margin_scale = 50;
  int null_reduction_base = 3;
  int null_reduction_extra = 1;
  int null_reduction_depth_threshold = 6;
  int lmp_base = 3;
  int lmp_depth_sq_coeff = 1;
  int lmr_min_depth = 3;
  int lmr_move_threshold = 2;
  double lmr_log_base = 1.5;
  int singular_margin = 150;
  int singular_depth_reduction = 3;
  int check_extension = 1;
  int q_check_extension = 1;
  int q_extension_margin = 2;
  int q_promo_extension = 1;
};

struct EvalParams {
  int tempo_bonus = 42;

  // Material values (MG / EG)
  std::array<int, 5> piece_value_mg = {50, 207, 270, 392, 895};
  std::array<int, 5> piece_value_eg = {91, 291, 317, 447, 806};

  // Pawn structure
  int doubled_pawn_penalty = -26;
  int isolated_pawn_penalty = 0;
  int backward_pawn_penalty = -11;
  std::array<int, 8> connected_bonus = {0, 32, 63, 95, 100, 26, 0, 0};

  // Passed pawn bonuses per rank (MG and EG)
  std::array<int, 8> passed_pawn_bonus_mg = {0, 0, 0, 0, 0, 95, 200, 0};
  std::array<int, 8> passed_pawn_bonus_eg = {0, 0, 0, 95, 185, 185, 250, 0};
  int passed_pawn_free_advance = 34;
  int passed_pawn_file_penalty = 0;
  int passed_pawn_king_proximity = 4;
  int rook_behind_passer_mg = 0;
  int rook_behind_passer_eg = 50;

  // Piece coordination
  int rook_open_file = 36;
  int rook_semi_open_file = 14;
  int knight_outpost_bonus = 55;
  int bishop_outpost_bonus = 5;

  // King safety
  int pawn_shield_bonus = 20;
  int king_castle_bonus = 0;
  int king_attack_count_weight = 120;
  int king_weak_sq_weight = 50;
  int king_open_file_weight = 100;
  int king_no_queen_divisor = 2;
  std::array<int, 6> king_attacker_weight = {0, 0, 81, 52, 44, 10};
  std::array<int, 6> safe_check_bonus = {0, 310, 200, 200, 200, 0};

  // Threats
  std::array<int, 6> threat_by_minor = {0, 0, 60, 75, 45, 0};
  std::array<int, 6> threat_by_rook = {0, 0, 95, 0, 100, 0};
  int hanging_penalty = 44;
  int safe_pawn_threat = 173;
  int restricted_piece = 7;

  // Piece-specific bonuses
  int minor_behind_pawn = 30;
  int bishop_pawns_penalty = -10;
  int long_diagonal_bishop = 48;
  int trapped_rook = -47;
  int rook_on_queen_file = 5;
  int king_protector = -3;

  // Space
  int center_control_weight = 0;
  int enemy_territory_weight = 15;

  // Endgame
  int opposition_bonus = 40;
  int endgame_mat_threshold = 120;

  // Initiative
  int initiative_pawn_weight = 1;
  int initiative_both_flanks = 8;
  int initiative_offset = -200;
};

struct EngineParams {
  SearchParams search;
  EvalParams eval;
};

std::string engineParamsToJson(const EngineParams& params);
bool parseEngineParamsJson(const std::string& json, EngineParams& out);

}  // namespace aurora
