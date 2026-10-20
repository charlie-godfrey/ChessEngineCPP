#include "play.h"
#include "position.h"
#include "uci.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
  // Check for UCI mode
  if (argc > 1) {
    std::string arg = argv[1];
    if (arg == "uci" || arg == "--uci") {
      // Run UCI loop
      aurora::Uci uci;
      uci.run();
      return 0;
    } else if (arg == "play" || arg == "--play") {
      // Run interactive play mode
      aurora::PlayMode play;
      play.run();
      return 0;
    }
  }

  // Default CLI mode (simple)
  aurora::Position position;
  std::cout << "Aurora CLI\n";
  std::cout << "Modes:\n";
  std::cout << "  aurora_cli uci   - UCI protocol mode\n";
  std::cout << "  aurora_cli play  - Interactive play mode\n";
  std::cout << "\n";
  std::cout << "FEN: " << position.toFEN() << "\n";
  return 0;
}
