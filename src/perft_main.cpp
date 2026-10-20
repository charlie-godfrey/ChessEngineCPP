#include "perft.h"
#include "position.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
  int depth = 1;
  bool divide = false;
  std::string fen;

  if (argc > 1) {
    depth = std::max(1, std::atoi(argv[1]));
  }

  for (int i = 2; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "divide") {
      divide = true;
      continue;
    }
    if (arg == "fen" && i + 1 < argc) {
      ++i;
      fen = argv[i];
      for (int j = i + 1; j < argc; ++j) {
        if (std::string_view(argv[j]) == "divide") {
          divide = true;
          continue;
        }
        fen += ' ';
        fen += argv[j];
      }
      break;
    }
  }

  aurora::Position position;
  if (!fen.empty()) {
    if (!position.setFromFEN(fen)) {
      std::cerr << "Invalid FEN\n";
      return 1;
    }
  }

  if (divide) {
    aurora::perft_divide(position, depth);
  } else {
    auto nodes = aurora::perft(position, depth);
    std::cout << "perft depth " << depth << ": " << nodes << "\n";
  }
  return 0;
}
