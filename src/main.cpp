#include "bitboard.h"
#include "magic.h"
#include "nnue.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include <iostream>

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  // Unbuffer I/O streams for reliable UCI communication
  setbuf(stdout, NULL);
  setbuf(stdin, NULL);
  setbuf(stderr, NULL);
  std::ios_base::sync_with_stdio(false);
  std::cin.tie(nullptr);

  // Initialize tables and subsystems
  Magic::init();
  initBitboards();
  NNUE::init();
  NNUE::loadModel();
  Search::init();

  // Initialize thread pool (4 threads default)
  Threads.set(4);

  // Create engine instance and enter UCI loop
  Position engine;
  UCI::loop(engine);

  // Shutdown thread pool
  Threads.set(0);

  return 0;
}
