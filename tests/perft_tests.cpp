#include "perft.h"
#include "position.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect_equal(std::uint64_t got, std::uint64_t expected, const std::string& name) {
  if (got != expected) {
    std::cerr << "[FAIL] " << name << " expected " << expected << " got " << got << "\n";
    ++failures;
  } else {
    std::cout << "[OK] " << name << "\n";
  }
}

}  // namespace

int main() {
  aurora::Position position;

  // Placeholder until movegen/makemove are implemented.
  expect_equal(aurora::perft(position, 1), 20, "perft depth 1 startpos");
  expect_equal(aurora::perft(position, 3), 8902, "perft depth 3 startpos");

  {
    aurora::Position kiwipete;
    const char* fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    if (kiwipete.setFromFEN(fen)) {
      expect_equal(aurora::perft(kiwipete, 3), 97862, "perft depth 3 kiwipete");
    } else {
      expect_equal(1, 0, "kiwipete fen parse");
    }
  }

  if (failures == 0) {
    std::cout << "All tests passed.\n";
    return 0;
  }

  std::cout << failures << " test(s) failed.\n";
  return 1;
}
