#include "uci.h"
#include "ucioutput.h"
#include "input.h"
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
#include <limits>
#if __has_include("buildinfo.h")
#include "buildinfo.h"
#else
#define DEEPBECKY_BUILD_ID "unidentified-manual-build"
#endif

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
  OutputLock outputLock(outputMutex());
  std::cout << "id name " << ENGINE_VERSION << std::endl;
  std::cout << "id author Diogo de Oliveira Almeida" << std::endl;
  std::cout << "info string Build SHA256: " << DEEPBECKY_BUILD_ID << std::endl;
  if (NNUE::isReady())
    std::cout << "info string NNUE SHA256: " << NNUE::currentModelSha256() << std::endl;
  if (NNUE::isReady())
    std::cout << "info string NNUE ready: " << NNUE::currentModelPath()
              << std::endl;
  else
    std::cout << "info string ERROR: NNUE is not loaded; search is disabled"
              << std::endl;
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
  OutputLock outputLock(outputMutex());
  std::cout << "readyok" << std::endl;
  std::cout.flush();
}

void cmdSetOption(Position &engine, std::istringstream &is) {
  std::string token;
  if (!(is >> token) || toLower(token) != "name") {
    std::cout << "info string ERROR: setoption requires name" << std::endl;
    return;
  }

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

  struct SpinRange { const char* name; int minimum; int maximum; };
  static constexpr SpinRange ranges[] = {
    {"hash", 1, 4096}, {"threads", 1, 256},
    {"move overhead", 0, MAX_MOVE_OVERHEAD},
    {"lmrbasebase", 20, 150},
    {"lmrmultbase", 50, 350},
    {"historybonusmax", 100, 8000},
    {"historydivisor", 4096, 32768},
    {"capturehistorydivisor", 4096, 32768},
    {"aspwindowbase", 10, 100},
    {"aspwindowthreadmult", 2, 20},
    {"futilitychildbase", 0, 300},
    {"futilitychildmult", 20, 250},
    {"historypruningmargin", 1000, 8000},
    {"nmpevalmargindepth", 0, 50},
    {"nmpevalmarginbase", 0, 800},
    {"rfpdepthlimit", 5, 25},
    {"nmpdepthlimit", 1, 10},
    {"iirdepthlimit", 2, 12},
    {"probcutdepthlimit", 2, 10},
    {"capturelmrbadbase", 2000, 10000},
    {"capturelmrgoodbase", 2000, 10000},
    {"corhistdivisor", 64, 1024},
    {"corhistweightbase", 4, 64},
    {"corhistweightmax", 128, 1024},
    {"corhistbonusmax", 1000, 8000},
    {"singulardepthlimit", 3, 10},
    {"doubleextmargin", 4, 64},
    {"tripleextmargin", 32, 256},
    {"futilitydepthlimit", 2, 8},
  };
  int numericValue = 0;
  for (const auto& range : ranges) {
    if (optNameLower == range.name
        && !parseInteger(optValue, range.minimum, range.maximum, numericValue)) {
      std::cout << "info string ERROR: invalid value for " << optName << std::endl;
      return;
    }
  }
  if (optNameLower == "ponder" || optNameLower == "traininglog"
      || optNameLower == "lazysmpdebug" || optNameLower == "search stats") {
    const auto boolean = toLower(optValue);
    if (boolean != "true" && boolean != "false") {
      std::cout << "info string ERROR: expected true or false for " << optName << std::endl;
      return;
    }
  }

  if (optNameLower == "hash") {
    int mb = numericValue;
    mb = std::max(1, std::min(mb, 4096));
    if (!TT.resize(static_cast<size_t>(mb)))
      std::cout << "info string ERROR: Hash allocation failed; previous table retained" << std::endl;
  } else if (optNameLower == "threads") {
    int n = numericValue;
    n = std::max(1, std::min(n, 256));
    Threads.set(static_cast<size_t>(n));
  } else if (optNameLower == "ponder") {
    Threads.ponderEnabled = (toLower(optValue) == "true");
  } else if (optNameLower == "move overhead") {
    TimeMgr.setMoveOverhead(static_cast<TimePoint>(numericValue));
  } else if (optNameLower == "evalfile") {
    if (NNUE::loadModel(optValue.empty() ? NNUE::DEFAULT_MODEL_FILE : optValue)) {
      // NNUE generation invalidates accumulators; TT/correction histories also
      // depend on the old evaluator and must not survive a successful reload.
      TT.clear();
      Threads.clear();
      std::cout << "info string NNUE file loaded: " << NNUE::currentModelPath() << std::endl;
      std::cout << "info string NNUE SHA256: " << NNUE::currentModelSha256() << std::endl;
    } else {
      std::cout << "info string ERROR: NNUE replacement rejected; "
                << (NNUE::isReady() ? "previous model retained" : "search is disabled") << std::endl;
    }
  } else if (optNameLower == "traininglog" || optNameLower == "traininglogfile") {
    std::cout << "info string TrainingLog unavailable: legacy option had no implemented writer"
              << std::endl;
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
    Search::Tune::LmrBaseBase = numericValue;
  else if (optNameLower == "lmrmultbase")
    Search::Tune::LmrMultBase = numericValue;
  else if (optNameLower == "historybonusmax")
    Search::Tune::HistoryBonusMax = numericValue;
  else if (optNameLower == "historydivisor")
    Search::Tune::HistoryDivisor = std::max(1, numericValue);
  else if (optNameLower == "capturehistorydivisor")
    Search::Tune::CaptureHistoryDivisor = std::max(1, numericValue);
  else if (optNameLower == "aspwindowbase")
    Search::Tune::AspWindowBase = std::clamp(numericValue, 10, 100);
  else if (optNameLower == "aspwindowthreadmult")
    Search::Tune::AspWindowThreadMult = std::max(1, numericValue);
  else if (optNameLower == "futilitychildbase")
    Search::Tune::FutilityChildBase = numericValue;
  else if (optNameLower == "futilitychildmult")
    Search::Tune::FutilityChildMult = numericValue;
  else if (optNameLower == "historypruningmargin")
    Search::Tune::HistoryPruningMargin = numericValue;
  else if (optNameLower == "nmpevalmargindepth")
    Search::Tune::NmpEvalMarginDepth = numericValue;
  else if (optNameLower == "nmpevalmarginbase")
    Search::Tune::NmpEvalMarginBase = numericValue;
  else if (optNameLower == "rfpdepthlimit")
    Search::Tune::RfpDepthLimit = numericValue;
  else if (optNameLower == "nmpdepthlimit")
    Search::Tune::NmpDepthLimit = numericValue;
  else if (optNameLower == "iirdepthlimit")
    Search::Tune::IirDepthLimit = numericValue;
  else if (optNameLower == "probcutdepthlimit")
    Search::Tune::ProbCutDepthLimit = numericValue;
  else if (optNameLower == "capturelmrbadbase")
    Search::Tune::CaptureLmrBadBase = numericValue;
  else if (optNameLower == "capturelmrgoodbase")
    Search::Tune::CaptureLmrGoodBase = numericValue;
  else if (optNameLower == "corhistdivisor")
    Search::Tune::CorHistDivisor = std::max(1, numericValue);
  else if (optNameLower == "corhistweightbase")
    Search::Tune::CorHistWeightBase = numericValue;
  else if (optNameLower == "corhistweightmax")
    Search::Tune::CorHistWeightMax = numericValue;
  else if (optNameLower == "corhistbonusmax")
    Search::Tune::CorHistBonusMax = numericValue;
  else if (optNameLower == "singulardepthlimit")
    Search::Tune::SingularDepthLimit = numericValue;
  else if (optNameLower == "doubleextmargin")
    Search::Tune::DoubleExtMargin = numericValue;
  else if (optNameLower == "tripleextmargin")
    Search::Tune::TripleExtMargin = numericValue;
  else if (optNameLower == "futilitydepthlimit")
    Search::Tune::FutilityDepthLimit = numericValue; // avoid div by 0
}

void cmdNewGame(Position &engine) {
  Threads.stopAndWait();
  TT.clear();
  TT.newSearch();
  engine.setStartPos();
  Threads.clear();
}

void cmdPosition(Position &engine, std::istringstream &is) {
  Threads.stopAndWait();
  std::string token;
  is >> token;
  std::string tokenLower = toLower(token);

  if (tokenLower != "startpos" && tokenLower != "fen") {
    std::cout << "info string ERROR: position requires startpos or fen" << std::endl;
    return;
  }

  if (tokenLower == "startpos") {
    engine.setStartPos();
    if (is >> token) {
      if (toLower(token) != "moves") {
        std::cout << "info string ERROR: expected moves" << std::endl;
        return;
      }
    }
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
    if (moveIsNone(move)) {
      std::cout << "info string illegal move from GUI: " << moveStr
                << std::endl;
      break;
    }
    if (engine.repHistSize >= MAX_STACK - MAX_PLY - 1) {
      std::cout << "info string ERROR: game history capacity exceeded" << std::endl;
      return;
    }
    engine.makeMove(move);
    engine.consolidateGameRoot();
  }
}

void cmdGo(Position &engine, std::istringstream &is) {
  // Wait for any previous search to finish
  Threads.stopAndWait();

  // Deep Becky is NNUE-only. Searching with the former neutral fallback can
  // produce legal but effectively random moves when deployment forgot or
  // rejected the network. Fail closed so the integration error is explicit.
  if (!NNUE::isReady()) {
    std::cout << "info string ERROR: cannot search without a valid NNUE model"
              << std::endl;
    std::cout << "bestmove 0000" << std::endl;
    std::cout.flush();
    return;
  }

  SearchLimits limits;
  limits.startTime = now();
  bool ponderMode = false;

  std::string token;

  while (is >> token) {
    std::string key = toLower(token);
    if (key == "infinite") {
      limits.infinite = true;
    } else if (key == "ponder") {
      ponderMode = true;
    } else if (key == "wtime" || key == "btime" || key == "winc" || key == "binc"
               || key == "movestogo" || key == "movetime" || key == "depth" || key == "perft") {
      std::string value;
      int parsed = 0;
      const int minimum = (key == "depth" || key == "perft" || key == "movestogo" || key == "movetime") ? 1 : 0;
      const int maximum = (key == "depth" || key == "perft") ? MAX_PLY
                        : key == "movestogo" ? 1000 : std::numeric_limits<int>::max();
      if (!(is >> value) || !parseInteger(value, minimum, maximum, parsed)) {
        std::cout << "info string ERROR: invalid go " << key << std::endl;
        return;
      }
      if (key == "wtime") limits.time[0] = parsed;
      else if (key == "btime") limits.time[1] = parsed;
      else if (key == "winc") limits.inc[0] = parsed;
      else if (key == "binc") limits.inc[1] = parsed;
      else if (key == "movestogo") limits.movestogo = parsed;
      else if (key == "movetime") limits.movetime = parsed;
      else if (key == "depth") limits.depth = parsed;
      else {
        std::istringstream perftIs(value);
        cmdPerft(engine, perftIs);
        return;
      }
    } else {
      std::cout << "info string ERROR: unsupported go token " << token << std::endl;
      return;
    }
  }

  // Calculate game ply
  int gamePly = static_cast<int>(std::min<int64_t>(
      (int64_t(engine.fullmove) - 1) * 2 + (engine.white_to_move ? 0 : 1),
      std::numeric_limits<int>::max()));

  // Initialize time management
  TimeMgr.init(limits, engine.white_to_move, gamePly);

  // Set search parameters
  int maxDepth = (limits.depth > 0 ? limits.depth : MAX_PLY);
  int searchTime = static_cast<int>(TimeMgr.maximum());
  if (limits.infinite) searchTime = 0;

  // In ponder mode, search without time limit until ponderhit or stop
  if (ponderMode) {
    searchTime = 0; // No time limit during pondering
  }

  // Start async search via ThreadPool
  // The main search thread will print bestmove when done
  Threads.startThinking(engine, maxDepth, searchTime, ponderMode, limits.infinite);
}

void cmdStop() {
  Threads.stopAndWait();
}

void cmdPonderHit() {
  Threads.ponderHit();
}

void cmdPerft(Position &engine, std::istringstream &is) {
  int perftDepth = 1;
  std::string depthToken;
  if (!(is >> depthToken) || !parseInteger(depthToken, 1, MAX_PLY, perftDepth)) {
    std::cout << "info string ERROR: invalid perft depth" << std::endl;
    return;
  }

  auto startTime = std::chrono::steady_clock::now();

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

  auto endTime = std::chrono::steady_clock::now();
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
  if (TT.sizeMB() != 256) TT.resize(256);

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
      OutputLock outputLock(outputMutex());
      std::cout << "info string Eval: " << Eval::evaluate(engine) << std::endl;
    } else if (cmdLower == "setoption") {
      Threads.stopAndWait();
      cmdSetOption(engine, is);
    } else if (cmdLower == "ucinewgame") {
      cmdNewGame(engine);
    } else if (cmdLower == "position") {
      cmdPosition(engine, is);
    } else if (cmdLower == "go") {
      cmdGo(engine, is);
    } else if (cmdLower == "stop") {
      cmdStop();
    } else if (cmdLower == "ponderhit") {
      cmdPonderHit();
    } else if (cmdLower == "perft") {
      Threads.stopAndWait();
      cmdPerft(engine, is);
    } else if (cmdLower == "quit") {
      Threads.stopAndWait();
      break;
    } else if (cmdLower == "d" || cmdLower == "display") {
      OutputLock outputLock(outputMutex());
      std::cout << "info string Display board not implemented yet" << std::endl;
    }
  }
}

} // namespace UCI
