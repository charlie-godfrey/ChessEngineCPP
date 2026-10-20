#pragma once

#include "position.h"
#include "search.h"

#include <iosfwd>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

namespace aurora {

class Uci {
 public:
  Uci();
  ~Uci();
  void run();

 private:
  bool handleCommand(const std::string& line);
  void handleUci();
  void handleIsReady();
  void handleUciNewGame();
  void handlePosition(std::istringstream& iss);
  void handleGo(std::istringstream& iss);
  void handleStop();
  void handleSetOption(std::istringstream& iss);
  void handleDebug(std::istringstream& iss);
  void handlePrint();
  void waitForSearch();

  Position position_;
  Search search_;
  bool debug_ = false;
  std::unique_ptr<std::thread> search_thread_;
  std::vector<ZobristKey> game_keys_;
};

}  // namespace aurora
