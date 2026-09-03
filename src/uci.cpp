#include "uci.h"
#include "evaluate.h"
#include "movegen.h"
#include "nnue.h"
#include "search.h"
#include "thread.h"
#include "timeman.h"
#include "tt.h"
#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>

namespace UCI {

// ============================================================================
// Utilities
// ============================================================================
std::string toLower(const std::string &str) {
  std::string result = str;
  for (char &c : result) {
    if (c >= 'A' && c <= 'Z')
      c = c - 'A' + 'a';
  }
  return result;
}

#ifndef ENGINE_VERSION
#define ENGINE_VERSION "Deep Becky 3.0"
#endif

// ============================================================================
// UCI Protocol Handlers
// ============================================================================
void cmdUci() {
  std::cout << "id name " << ENGINE_VERSION << std::endl;
  std::cout << "id author Diogo de Oliveira Almeida" << std::endl;
  std::cout << "option name Hash type spin default 256 min 1 max 4096"
            << std::endl;
  std::cout << "option name Threads type spin default 4 min 1 max 256"
            << std::endl;
  std::cout << "option name Ponder type check default true" << std::endl;
  std::cout << "option name Move Overhead type spin default "
            << DEFAULT_MOVE_OVERHEAD << " min 0 max " << MAX_MOVE_OVERHEAD
            << std::endl;
  std::cout << "option name EvalFile type string default "
            << NNUE::DEFAULT_MODEL_FILE << std::endl;
  std::cout << "option name TrainingLog type check default false" << std::endl;
  std::cout
      << "option name TrainingLogFile type string default deepbecky_train.jsonl"
      << std::endl;
  std::cout << "option name LazySmpDebug type check default false" << std::endl;
  std::cout << "option name LazySmpSelfTest type button" << std::endl;
#ifdef ENABLE_SEARCH_STATS
  std::cout << "option name Search Stats type check default false" << std::endl;
#endif

  // Tuning parameters
  std::cout << "option name LmrBaseBase type spin default "
            << Search::Tune::LmrBaseBase << " min 20 max 150" << std::endl;
  std::cout << "option name LmrMultBase type spin default "
            << Search::Tune::LmrMultBase << " min 50 max 350" << std::endl;
  std::cout << "option name HistoryBonusMax type spin default "
            << Search::Tune::HistoryBonusMax << " min 100 max 8000"
            << std::endl;
  std::cout << "option name HistoryDivisor type spin default "
            << Search::Tune::HistoryDivisor << " min 4096 max 32768"
            << std::endl;
  std::cout << "option name CaptureHistoryDivisor type spin default "
            << Search::Tune::CaptureHistoryDivisor << " min 4096 max 32768"
            << std::endl;
  std::cout << "option name AspWindowBase type spin default "
            << Search::Tune::AspWindowBase << " min 10 max 100" << std::endl;
  std::cout << "option name AspWindowThreadMult type spin default "
            << Search::Tune::AspWindowThreadMult << " min 2 max 20"
            << std::endl;
  std::cout << "option name FutilityChildBase type spin default "
            << Search::Tune::FutilityChildBase << " min 0 max 300" << std::endl;
  std::cout << "option name FutilityChildMult type spin default "
            << Search::Tune::FutilityChildMult << " min 20 max 250"
            << std::endl;
  std::cout << "option name HistoryPruningMargin type spin default "
            << Search::Tune::HistoryPruningMargin << " min 1000 max 8000"
            << std::endl;
  std::cout << "option name NmpEvalMarginDepth type spin default "
            << Search::Tune::NmpEvalMarginDepth << " min 0 max 50" << std::endl;
  std::cout << "option name NmpEvalMarginBase type spin default "
            << Search::Tune::NmpEvalMarginBase << " min 0 max 800"
            << std::endl;
  std::cout << "option name RfpDepthLimit type spin default "
            << Search::Tune::RfpDepthLimit << " min 5 max 25" << std::endl;
  std::cout << "option name NmpDepthLimit type spin default "
            << Search::Tune::NmpDepthLimit << " min 1 max 10" << std::endl;
  std::cout << "option name IirDepthLimit type spin default "
            << Search::Tune::IirDepthLimit << " min 2 max 12" << std::endl;
  std::cout << "option name ProbCutDepthLimit type spin default "
            << Search::Tune::ProbCutDepthLimit << " min 2 max 10" << std::endl;
  std::cout << "option name CaptureLmrBadBase type spin default "
            << Search::Tune::CaptureLmrBadBase << " min 2000 max 10000"
            << std::endl;
  std::cout << "option name CaptureLmrGoodBase type spin default "
            << Search::Tune::CaptureLmrGoodBase << " min 2000 max 10000"
            << std::endl;
  std::cout << "option name CorHistDivisor type spin default "
            << Search::Tune::CorHistDivisor << " min 64 max 1024" << std::endl;
  std::cout << "option name CorHistWeightBase type spin default "
            << Search::Tune::CorHistWeightBase << " min 4 max 64" << std::endl;
  std::cout << "option name CorHistWeightMax type spin default "
            << Search::Tune::CorHistWeightMax << " min 128 max 1024"
            << std::endl;
  std::cout << "option name CorHistBonusMax type spin default "
            << Search::Tune::CorHistBonusMax << " min 1000 max 8000"
            << std::endl;
  std::cout << "option name SingularDepthLimit type spin default "
            << Search::Tune::SingularDepthLimit << " min 3 max 10" << std::endl;
  std::cout << "option name DoubleExtMargin type spin default "
            << Search::Tune::DoubleExtMargin << " min 4 max 64" << std::endl;
  std::cout << "option name TripleExtMargin type spin default "
            << Search::Tune::TripleExtMargin << " min 32 max 256" << std::endl;
  std::cout << "option name FutilityDepthLimit type spin default "
            << Search::Tune::FutilityDepthLimit << " min 2 max 8" << std::endl;

  std::cout << "uciok" << std::endl;
  std::cout.flush();
}

void cmdIsReady() {
  Threads.waitForSearchFinished();
  std::cout << "readyok" << std::endl;
  std::cout.flush();
}

void cmdSetOption(Position &engine, std::istringstream &is) {
  std::string token;
  is >> token; // "name"

  std::string optName;
  while (is >> token && toLower(token) != "value") {
    if (!optName.empty())
      optName += " ";
    optName += token;
  }

  std::string optValue;
  while (is >> token) {
    if (!optValue.empty())
      optValue += " ";
    optValue += token;
  }

  std::string optNameLower = toLower(optName);

  if (optNameLower == "hash") {
    int mb = std::stoi(optValue);
    mb = std::max(1, std::min(mb, 4096));
    TT.resize(static_cast<size_t>(mb));
  } else if (optNameLower == "threads") {
    int n = std::stoi(optValue);
    n = std::max(1, std::min(n, 256));
    Threads.set(static_cast<size_t>(n));
  } else if (optNameLower == "ponder") {
    Threads.ponderEnabled = (toLower(optValue) == "true");
  } else if (optNameLower == "move overhead") {
    TimeMgr.setMoveOverhead(static_cast<TimePoint>(std::stoll(optValue)));
  } else if (optNameLower == "evalfile") {
    const std::string loweredValue = toLower(optValue);
    if (optValue.empty() || loweredValue == NNUE::DEFAULT_MODEL_FILE) {
      if (NNUE::loadModel()) {
        std::cout << "info string NNUE file loaded: "
                  << NNUE::currentModelPath() << std::endl;
        std::cout << "info string NNUE architecture: "
                  << NNUE::architectureSummary() << std::endl;
      } else {
        std::cout << "info string NNUE file not loaded; engine stays NNUE-only "
                     "with neutral evaluation"
                  << std::endl;
      }
    } else if (NNUE::loadModel(optValue)) {
      std::cout << "info string NNUE file loaded: " << NNUE::currentModelPath()
                << std::endl;
      std::cout << "info string NNUE architecture: "
                << NNUE::architectureSummary() << std::endl;
    } else {
      std::cout << "info string NNUE file not loaded; engine stays NNUE-only "
                   "with neutral evaluation"
                << std::endl;
    }
  } else if (optNameLower == "traininglog") {
    NNUE::setTrainingLogEnabled(toLower(optValue) == "true");
    std::cout << "info string training log "
              << (NNUE::trainingLogEnabled() ? "enabled" : "disabled")
              << " file=" << NNUE::trainingLogFile() << std::endl;
  } else if (optNameLower == "traininglogfile") {
    NNUE::setTrainingLogFile(
        optValue.empty() ? std::string("deepbecky_train.jsonl") : optValue);
    std::cout << "info string training log file set to "
              << NNUE::trainingLogFile() << std::endl;
  } else if (optNameLower == "lazysmpdebug") {
    Threads.lazySmpDebug = (toLower(optValue) == "true");
    std::cout << "info string Lazy SMP diagnostics "
              << (Threads.lazySmpDebug ? "enabled" : "disabled") << std::endl;
  } else if (optNameLower == "lazysmpselftest") {
    const bool passed = Threads.runLazySmpSelectionTests();
    std::cout << "info string Lazy SMP selection self-test "
              << (passed ? "passed" : "FAILED") << std::endl;
#ifdef ENABLE_SEARCH_STATS
  } else if (optNameLower == "search stats") {
    Threads.searchStatsEnabled = (toLower(optValue) == "true");
#endif
  } else if (optNameLower == "lmrbasebase")
    Search::Tune::LmrBaseBase = std::stoi(optValue);
  else if (optNameLower == "lmrmultbase")
    Search::Tune::LmrMultBase = std::stoi(optValue);
  else if (optNameLower == "historybonusmax")
    Search::Tune::HistoryBonusMax = std::stoi(optValue);
  else if (optNameLower == "historydivisor")
    Search::Tune::HistoryDivisor = std::max(1, std::stoi(optValue));
  else if (optNameLower == "capturehistorydivisor")
    Search::Tune::CaptureHistoryDivisor = std::max(1, std::stoi(optValue));
  else if (optNameLower == "aspwindowbase")
    Search::Tune::AspWindowBase = std::clamp(std::stoi(optValue), 10, 100);
  else if (optNameLower == "aspwindowthreadmult")
    Search::Tune::AspWindowThreadMult = std::max(1, std::stoi(optValue));
  else if (optNameLower == "futilitychildbase")
    Search::Tune::FutilityChildBase = std::stoi(optValue);
  else if (optNameLower == "futilitychildmult")
    Search::Tune::FutilityChildMult = std::stoi(optValue);
  else if (optNameLower == "historypruningmargin")
    Search::Tune::HistoryPruningMargin = std::stoi(optValue);
  else if (optNameLower == "nmpevalmargindepth")
    Search::Tune::NmpEvalMarginDepth = std::stoi(optValue);
  else if (optNameLower == "nmpevalmarginbase")
    Search::Tune::NmpEvalMarginBase = std::stoi(optValue);
  else if (optNameLower == "rfpdepthlimit")
    Search::Tune::RfpDepthLimit = std::stoi(optValue);
  else if (optNameLower == "nmpdepthlimit")
    Search::Tune::NmpDepthLimit = std::stoi(optValue);
  else if (optNameLower == "iirdepthlimit")
    Search::Tune::IirDepthLimit = std::stoi(optValue);
  else if (optNameLower == "probcutdepthlimit")
    Search::Tune::ProbCutDepthLimit = std::stoi(optValue);
  else if (optNameLower == "capturelmrbadbase")
    Search::Tune::CaptureLmrBadBase = std::stoi(optValue);
  else if (optNameLower == "capturelmrgoodbase")
    Search::Tune::CaptureLmrGoodBase = std::stoi(optValue);
  else if (optNameLower == "corhistdivisor")
    Search::Tune::CorHistDivisor = std::max(1, std::stoi(optValue));
  else if (optNameLower == "corhistweightbase")
    Search::Tune::CorHistWeightBase = std::stoi(optValue);
  else if (optNameLower == "corhistweightmax")
    Search::Tune::CorHistWeightMax = std::stoi(optValue);
  else if (optNameLower == "corhistbonusmax")
    Search::Tune::CorHistBonusMax = std::stoi(optValue);
  else if (optNameLower == "singulardepthlimit")
    Search::Tune::SingularDepthLimit = std::stoi(optValue);
  else if (optNameLower == "doubleextmargin")
    Search::Tune::DoubleExtMargin = std::stoi(optValue);
  else if (optNameLower == "tripleextmargin")
    Search::Tune::TripleExtMargin = std::stoi(optValue);
  else if (optNameLower == "futilitydepthlimit")
    Search::Tune::FutilityDepthLimit = std::stoi(optValue); // avoid div by 0
}

void cmdNewGame(Position &engine) {
  Threads.waitForSearchFinished();
  TT.clear();
  TT.newSearch();
  engine.setStartPos();
  Threads.clear();
}

void cmdPosition(Position &engine, std::istringstream &is) {
  Threads.waitForSearchFinished();
  std::string token;
  is >> token;
  std::string tokenLower = toLower(token);

  if (tokenLower == "startpos") {
    engine.setStartPos();
    is >> token; // Consume "moves" token if present
  } else if (tokenLower == "fen") {
    std::vector<std::string> fenParts;
    std::string fenToken;
    bool sawMoves = false;

    while (is >> fenToken) {
      if (toLower(fenToken) == "moves") {
        sawMoves = true;
        break;
      }
      fenParts.push_back(fenToken);
    }

    if (fenParts.empty()) {
      std::cout << "info string Missing FEN in position command" << std::endl;
      return;
    }

    std::string fen;
    fen.reserve(fenParts.size() * 8);
    for (size_t i = 0; i < fenParts.size(); ++i) {
      if (i)
        fen.push_back(' ');
      fen += fenParts[i];
    }

    if (!engine.setFEN(fen)) {
      std::cout << "info string Invalid FEN: " << fen << std::endl;
      return;
    }

    if (!sawMoves)
      return;
  }

  // Parse and apply moves
  std::string moveStr;
  while (is >> moveStr) {
    Move move = engine.uciToMove(moveStr);
    if (moveIsNone(move) && moveStr != "0000") {
      std::cout << "info string illegal move from GUI: " << moveStr
                << std::endl;
      break;
    }
    engine.makeMove(move);
  }
}

void cmdGo(Position &engine, std::istringstream &is) {
  // Wait for any previous search to finish
  Threads.waitForSearchFinished();

  SearchLimits limits;
  limits.startTime = now();
  bool ponderMode = false;

  std::string token;

  while (is >> token) {
    std::string key = toLower(token);
    if (key == "wtime")
      is >> limits.time[0];
    else if (key == "btime")
      is >> limits.time[1];
    else if (key == "winc")
      is >> limits.inc[0];
    else if (key == "binc")
      is >> limits.inc[1];
    else if (key == "movestogo")
      is >> limits.movestogo;
    else if (key == "movetime")
      is >> limits.movetime;
    else if (key == "depth")
      is >> limits.depth;
    else if (key == "infinite")
      limits.infinite = true;
    else if (key == "ponder")
      ponderMode = true;
    else if (key == "perft") {
      int perftDepth = 1;
      is >> perftDepth;
      std::istringstream perftIs(std::to_string(perftDepth));
      cmdPerft(engine, perftIs);
      return;
    }
  }

  // Calculate game ply
  int gamePly = (engine.fullmove - 1) * 2 + (engine.white_to_move ? 0 : 1);

  // Initialize time management
  TimeMgr.init(limits, engine.white_to_move, gamePly);

  // Set search parameters
  int maxDepth = (limits.depth > 0 ? limits.depth : MAX_PLY);
  int searchTime = static_cast<int>(TimeMgr.maximum());

  // In ponder mode, search without time limit until ponderhit or stop
  if (ponderMode) {
    searchTime = 0; // No time limit during pondering
    maxDepth = MAX_PLY;
  }

  // Start async search via ThreadPool
  // The main search thread will print bestmove when done
  Threads.startThinking(engine, maxDepth, searchTime, ponderMode);
}

void cmdStop() {
  Threads.ponder.store(false, std::memory_order_relaxed);
  Threads.stop.store(true, std::memory_order_relaxed);
  // Wake main thread if it's waiting for ponderhit
  if (Threads.main()) {
    std::lock_guard<std::mutex> lk(Threads.main()->mtx);
    Threads.main()->cv.notify_one();
  }
  Threads.waitForSearchFinished();
}

void cmdPonderHit(Position &engine) {
  // Opponent played the expected move - switch from ponder to normal search
  Threads.ponder.store(false, std::memory_order_relaxed);
  TimeMgr.restartTimer();

  // Reinitialize time management now that real clock starts
  // The search will pick up time checking on the next node
  // We need to set a proper time limit for the main thread
  if (Threads.main()) {
    // Set the start time to NOW (pondering time doesn't count)
    auto now_time = std::chrono::high_resolution_clock::now();
    Threads.main()->pos.start_time = now_time;

    // Calculate proper time allocation from the limits stored earlier
    int timeMs = static_cast<int>(TimeMgr.optimum());
    Threads.main()->pos.time_limit_ms = timeMs;
    Threads.searchTimeMs = timeMs;

    // Wake main thread if it's waiting for ponderhit after search completed
    std::lock_guard<std::mutex> lk(Threads.main()->mtx);
    Threads.main()->cv.notify_one();
  }
}

void cmdPerft(Position &engine, std::istringstream &is) {
  int perftDepth = 1;
  is >> perftDepth;
  if (perftDepth < 1)
    perftDepth = 1;

  auto startTime = std::chrono::high_resolution_clock::now();

  // Perft divide: show node counts per root move
  Move moves[MAX_MOVES];
  int count = engine.generateLegal(moves);
  uint64_t totalNodes = 0;

  for (int i = 0; i < count; ++i) {
    engine.makeMove(moves[i]);
    uint64_t n = ::perft(engine, perftDepth - 1);
    engine.undoMove(moves[i]);
    std::cout << engine.moveToUCI(moves[i]) << ": " << n << std::endl;
    totalNodes += n;
  }

  auto endTime = std::chrono::high_resolution_clock::now();
  long long ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime)
          .count();
  if (ms == 0)
    ms = 1;
  uint64_t nps = (totalNodes * 1000ULL) / static_cast<uint64_t>(ms);

  std::cout << std::endl;
  std::cout << "Nodes searched: " << totalNodes << std::endl;
  std::cout << "Time: " << ms << " ms" << std::endl;
  std::cout << "NPS: " << nps << std::endl;
}

// ============================================================================
// Main UCI Loop
// ============================================================================
void loop(Position &engine) {
  std::string line;

  // Initialize TT with default size (256 MB)
  TT.resize(256);

  engine.setStartPos();

  while (std::getline(std::cin, line)) {
    while (!line.empty() &&
           (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
      line.pop_back();
    }
    if (line.empty())
      continue;

    std::istringstream is(line);
    std::string cmd;
    if (!(is >> cmd))
      continue;
    std::string cmdLower = toLower(cmd);

    if (cmdLower == "uci") {
      cmdUci();
    } else if (cmdLower == "isready") {
      cmdIsReady();
    } else if (cmdLower == "eval") {
      std::cout << "info string Eval: " << Eval::evaluate(engine) << std::endl;
    } else if (cmdLower == "setoption") {
      Threads.waitForSearchFinished();
      cmdSetOption(engine, is);
    } else if (cmdLower == "ucinewgame") {
      cmdNewGame(engine);
    } else if (cmdLower == "position") {
      Threads.waitForSearchFinished();
      cmdPosition(engine, is);
    } else if (cmdLower == "go") {
      cmdGo(engine, is);
    } else if (cmdLower == "stop") {
      cmdStop();
    } else if (cmdLower == "ponderhit") {
      cmdPonderHit(engine);
    } else if (cmdLower == "perft") {
      Threads.waitForSearchFinished();
      cmdPerft(engine, is);
    } else if (cmdLower == "quit") {
      Threads.stop.store(true, std::memory_order_relaxed);
      Threads.waitForSearchFinished();
      break;
    } else if (cmdLower == "d" || cmdLower == "display") {
      std::cout << "info string Display board not implemented yet" << std::endl;
    }
  }
}

} // namespace UCI
