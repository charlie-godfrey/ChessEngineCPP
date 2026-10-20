#pragma once

#include "position.h"
#include "search.h"

#include <string>

namespace aurora {

class PlayMode {
 public:
  PlayMode();
  void run();

 private:
  void printBoard() const;
  void printMovePrompt() const;
  bool handleUserMove(const std::string& move_str);
  void makeEngineMove();
  void printGameResult() const;
  bool isGameOver() const;
  
  Position position_;
  Search search_;
  int search_depth_ = 14;
  bool user_plays_white_ = true;
  std::string nnue_path_;
  std::vector<ZobristKey> game_keys_;
};

}  // namespace aurora
