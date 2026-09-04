//
// 1. No locks in hot path — TT is lockless, per-thread tables avoid contention
// 2. Condition variable parking — zero CPU cost when idle
// 3. Relaxed atomics for stop flag — no fence overhead
// 4. Vote-based best thread selection — combines score + depth
// 5. TT is the ONLY communication channel between threads

#include "thread.h"
#include "nnue.h"
#include "tt.h"
#include "search.h"
#include "timeman.h"
#include <algorithm>
#include <map>
#include <iostream>

ThreadPool Threads;

namespace {

constexpr size_t MIN_QUALIFIED_PV_LENGTH = 3;
constexpr int MIN_NORMAL_MOVE_SUPPORT = 2;

struct SelectionCandidate {
    size_t threadId = 0;
    Move bestMove = MOVE_NONE;
    int bestScore = -INF_SCORE;
    int completedDepth = 0;
    size_t pvLength = 0;
    bool pvMatchesBestMove = false;
    bool hasCompletedIteration = false;
    bool legalMove = false;
    bool eligible = false;
};

bool hasUsableResult(const SelectionCandidate& candidate) {
    return candidate.hasCompletedIteration && candidate.completedDepth > 0 &&
           !moveIsNone(candidate.bestMove) && candidate.legalMove &&
           candidate.bestScore > -INF_SCORE && candidate.bestScore < INF_SCORE;
}

bool isWinningMate(const SelectionCandidate& candidate) {
    return candidate.bestScore >= MATE_IN_MAX;
}

bool isLosingMate(const SelectionCandidate& candidate) {
    return candidate.bestScore <= -MATE_IN_MAX;
}

bool isNormalScore(const SelectionCandidate& candidate) {
    return !isWinningMate(candidate) && !isLosingMate(candidate);
}

size_t selectBestCandidate(std::vector<SelectionCandidate>& candidates) {
    if (candidates.empty())
        return 0;

    SelectionCandidate& main = candidates.front();
    const bool mainUsable = hasUsableResult(main);

    // Thread 0 is the anchor. Helpers are considered only when they have a
    // complete, legal result, a non-truncated PV, and at least the depth of
    // the main thread. The comparison is intentionally against Thread 0,
    // never against a global maximum reached by a helper.
    main.eligible = mainUsable;
    for (size_t i = 1; i < candidates.size(); ++i) {
        SelectionCandidate& candidate = candidates[i];
        candidate.eligible = hasUsableResult(candidate) && mainUsable &&
                             candidate.completedDepth >= main.completedDepth &&
                             candidate.pvLength >= MIN_QUALIFIED_PV_LENGTH &&
                             candidate.pvMatchesBestMove;
    }

    // A completed mate score has priority over normal voting. Among winning
    // mates, the larger score is the shorter mate.
    size_t bestWinningMate = candidates.size();
    for (size_t i = 0; i < candidates.size(); ++i) {
        const SelectionCandidate& candidate = candidates[i];
        if (!candidate.eligible || !isWinningMate(candidate))
            continue;
        if (bestWinningMate == candidates.size() ||
            candidate.bestScore > candidates[bestWinningMate].bestScore ||
            (candidate.bestScore == candidates[bestWinningMate].bestScore &&
             candidate.threadId < candidates[bestWinningMate].threadId)) {
            bestWinningMate = i;
        }
    }
    if (bestWinningMate != candidates.size())
        return bestWinningMate;

    // Only normal scores take part in the vote. A proven loss must never
    // defeat a normal root result merely because its numeric weight is large.
    std::map<uint32_t, int64_t> votes;
    std::map<uint32_t, int> support;
    int minScore = INF_SCORE;
    for (const SelectionCandidate& candidate : candidates) {
        if (candidate.eligible && isNormalScore(candidate))
            minScore = std::min(minScore, candidate.bestScore);
    }
    if (minScore != INF_SCORE) {
        for (const SelectionCandidate& candidate : candidates) {
            if (!candidate.eligible || !isNormalScore(candidate))
                continue;

            const uint32_t moveKey = candidate.bestMove.data;
            const int64_t weight =
                static_cast<int64_t>(candidate.bestScore - minScore + 14) *
                candidate.completedDepth;
            votes[moveKey] += weight;
            support[moveKey] += 1;
        }
    }

    // Normal helper moves require independent confirmation. This blocks a
    // single fail-high, including one from an auxiliary that happened to
    // complete a nominally deeper iteration than Thread 0.
    size_t bestNormalHelper = candidates.size();
    const uint32_t mainMoveKey = main.bestMove.data;
    const int64_t mainVote = votes[mainMoveKey];
    for (size_t i = 1; i < candidates.size(); ++i) {
        const SelectionCandidate& candidate = candidates[i];
        if (!candidate.eligible || !isNormalScore(candidate))
            continue;

        const uint32_t moveKey = candidate.bestMove.data;
        if (moveKey == mainMoveKey ||
            support[moveKey] < MIN_NORMAL_MOVE_SUPPORT ||
            votes[moveKey] <= mainVote)
            continue;

        if (bestNormalHelper == candidates.size()) {
            bestNormalHelper = i;
            continue;
        }

        const SelectionCandidate& current = candidates[bestNormalHelper];
        const uint32_t currentMoveKey = current.bestMove.data;
        if (votes[moveKey] > votes[currentMoveKey] ||
            (votes[moveKey] == votes[currentMoveKey] &&
             candidate.completedDepth > current.completedDepth) ||
            (votes[moveKey] == votes[currentMoveKey] &&
             candidate.completedDepth == current.completedDepth &&
             candidate.threadId < current.threadId)) {
            bestNormalHelper = i;
        }
    }
    if (bestNormalHelper != candidates.size())
        return bestNormalHelper;

    // If every usable result is a proven loss, prefer the larger score: it
    // represents the longest defense. Otherwise preserve Thread 0's normal
    // result as the anchor.
    if (mainUsable && isLosingMate(main)) {
        size_t bestLoss = 0;
        for (size_t i = 1; i < candidates.size(); ++i) {
            const SelectionCandidate& candidate = candidates[i];
            if (candidate.eligible && isLosingMate(candidate) &&
                candidate.bestScore > candidates[bestLoss].bestScore) {
                bestLoss = i;
            }
        }
        return bestLoss;
    }

    return 0;
}

SelectionCandidate makeSelectionTestCandidate(size_t id, uint16_t moveData,
                                               int score, int depth,
                                               size_t pvLength = 3,
                                               bool completed = true,
                                               bool legal = true,
                                               bool pvMatchesBestMove = true) {
    SelectionCandidate candidate;
    candidate.threadId = id;
    candidate.bestMove.data = moveData;
    candidate.bestScore = score;
    candidate.completedDepth = depth;
    candidate.pvLength = pvLength;
    candidate.pvMatchesBestMove = pvMatchesBestMove;
    candidate.hasCompletedIteration = completed;
    candidate.legalMove = legal;
    return candidate;
}

bool runSelectionUnitTests() {
    const auto select = [](std::vector<SelectionCandidate> candidates) {
        const size_t selected = selectBestCandidate(candidates);
        return candidates[selected].threadId;
    };
    const auto check = [](bool condition, int testNumber) {
        if (!condition)
            std::cout << "info string Lazy SMP self-test case " << testNumber
                      << " failed" << std::endl;
        return condition;
    };

    // Single-thread behavior and unusable helpers preserve Thread 0.
    if (!check(select({makeSelectionTestCandidate(0, 1, 20, 18)}) == 0, 1))
        return false;
    if (!check(select({makeSelectionTestCandidate(0, 1, 20, 18),
                       makeSelectionTestCandidate(1, 2, 1100, 30, 3, false)}) == 0,
               2))
        return false;

    // A shallow or lone high-scoring helper cannot replace the anchor.
    if (!check(select({makeSelectionTestCandidate(0, 1, -200, 18),
                       makeSelectionTestCandidate(1, 2, 1100, 8)}) == 0,
               3))
        return false;
    if (!check(select({makeSelectionTestCandidate(0, 1, -200, 18),
                       makeSelectionTestCandidate(1, 2, 1100, 19)}) == 0,
               4))
        return false;

    // Two qualified helpers independently supporting the same normal move
    // may replace Thread 0 when their combined vote is stronger.
    if (!check(select({makeSelectionTestCandidate(0, 1, -200, 18),
                       makeSelectionTestCandidate(1, 2, 200, 18),
                       makeSelectionTestCandidate(2, 2, 180, 19)}) == 2,
               5))
        return false;

    // A truncated PV is not eligible, even at a compatible depth.
    if (!check(select({makeSelectionTestCandidate(0, 1, -200, 18),
                       makeSelectionTestCandidate(1, 2, 1100, 20, 2),
                       makeSelectionTestCandidate(2, 2, 1000, 20, 2)}) == 0,
               6))
        return false;
    if (!check(select({makeSelectionTestCandidate(0, 1, -200, 18),
                       makeSelectionTestCandidate(1, 2, 1100, 20, 3, true, true, false),
                       makeSelectionTestCandidate(2, 2, 1000, 20, 3, true, true, false)}) == 0,
               7))
        return false;

    // Mates override normal voting; greater winning score is a shorter mate.
    if (!check(select({makeSelectionTestCandidate(0, 1, 10, 18),
                       makeSelectionTestCandidate(1, 2, MATE_IN_MAX + 10, 18),
                       makeSelectionTestCandidate(2, 3, MATE_IN_MAX + 20, 18)}) == 2,
               8))
        return false;

    // If all usable candidates are losing mates, choose the longest defense.
    if (!check(select({makeSelectionTestCandidate(0, 1, -MATE_IN_MAX - 20, 18),
                       makeSelectionTestCandidate(1, 2, -MATE_IN_MAX - 10, 18)}) == 1,
               9))
        return false;

    // Sentinel and illegal results must never become voters.
    if (!check(select({makeSelectionTestCandidate(0, 1, 10, 18),
                       makeSelectionTestCandidate(1, 2, -INF_SCORE, 20),
                       makeSelectionTestCandidate(2, 2, 1100, 20, 3, true, false)}) == 0,
               10))
        return false;

    return true;
}

bool isLegalRootMove(Position& root, Move move) {
    Move legalMoves[MAX_MOVES];
    const int legalMoveCount = root.generateLegal(legalMoves);
    for (int i = 0; i < legalMoveCount; ++i) {
        if (legalMoves[i] == move)
            return true;
    }
    return false;
}

void printLazySmpDiagnostics(const std::vector<SelectionCandidate>& candidates,
                             size_t selected, const SearchThread& outputThread) {
    for (const SelectionCandidate& candidate : candidates) {
        std::cout << "info string lazy-smp thread=" << candidate.threadId
                  << " completed=" << (candidate.hasCompletedIteration ? 1 : 0)
                  << " eligible=" << (candidate.eligible ? 1 : 0)
                  << " depth=" << candidate.completedDepth
                  << " score=" << candidate.bestScore
                  << " move=" << outputThread.pos.moveToUCI(candidate.bestMove)
                  << " pv_length=" << candidate.pvLength
                  << " pv_matches_move=" << (candidate.pvMatchesBestMove ? 1 : 0)
                  << " selected=" << (candidate.threadId == selected ? 1 : 0)
                  << std::endl;
    }
}

} // namespace

// ========================= SearchThread =========================

SearchThread::SearchThread(size_t index) : idx(index) {
    clear();
    pos.thread = this;
    // Launch the native thread — it will park itself in idle_loop
    nativeThread = std::thread(&SearchThread::idle_loop, this);
    // Wait until the thread is parked (searching == false)
    wait_for_search_finished();
}

SearchThread::~SearchThread() {
    // Signal the thread to exit
    {
        std::lock_guard<std::mutex> lk(mtx);
        exitFlag = true;
        searching = true;
    }
    cv.notify_one();
    if (nativeThread.joinable())
        nativeThread.join();
}

void SearchThread::idle_loop() {
    while (true) {
        {
            std::unique_lock<std::mutex> lk(mtx);
            searching = false;
            cv.notify_one();  // Signal "I'm parked"
            cv.wait(lk, [&] { return searching; });
        }
        if (exitFlag) return;

        if (idx == 0) {
            // === MAIN THREAD ===
            // Starting and joining helpers costs more than the entire budget
            // in an emergency clock situation. Keep configured helpers parked
            // when the hard allocation does not even exceed Move Overhead.
            // Depth/infinite/ponder searches retain their normal SMP behavior.
            const int initialSearchTimeMs =
                Threads.searchTimeMs.load(std::memory_order_acquire);
            const bool startHelpers =
                TimeMgr.allowsHelperThreads(initialSearchTimeMs);
            if (startHelpers)
                for (size_t i = 1; i < Threads.size(); i++)
                    Threads.at(i)->start_searching();

            // Run own search (with time management)
            pos.search(Threads.searchMaxDepth, initialSearchTimeMs);

            // Signal all threads to stop
            Threads.stop.store(true, std::memory_order_relaxed);

            // Wait for helpers to finish
            if (startHelpers)
                for (size_t i = 1; i < Threads.size(); i++)
                    Threads.at(i)->wait_for_search_finished();

            // === DEPTH-QUALIFIED LAZY SMP SELECTION (POLICY C2) ===
            std::vector<SelectionCandidate> candidates;
            candidates.reserve(Threads.size());
            for (size_t i = 0; i < Threads.size(); ++i) {
                SearchThread* thread = Threads.at(i);
                SelectionCandidate candidate;
                candidate.threadId = thread->idx;
                candidate.bestMove = thread->bestMove;
                candidate.bestScore = thread->bestScore;
                candidate.completedDepth = thread->completedDepth;
                candidate.pvLength = thread->completedPV.size();
                candidate.pvMatchesBestMove =
                    !thread->completedPV.empty() &&
                    thread->completedPV.front() == candidate.bestMove;
                candidate.hasCompletedIteration = thread->hasCompletedIteration;
                candidate.legalMove = isLegalRootMove(pos, candidate.bestMove);
                candidates.push_back(candidate);
            }

            const size_t selectedIndex = selectBestCandidate(candidates);
            SearchThread* bestThread = Threads.at(selectedIndex);

            if (Threads.lazySmpDebug)
                printLazySmpDiagnostics(candidates, bestThread->idx, *bestThread);

            // Print bestmove (from main thread context)
            Move bm = bestThread->bestMove;

            // If in ponder mode, wait for ponderhit or stop before outputting
            // (the stop/ponderhit handler already clears ponder flag)
            if (Threads.ponder.load(std::memory_order_relaxed)) {
                // Ponder search finished before ponderhit; wait for it
                std::unique_lock<std::mutex> lk(mtx);
                cv.wait(lk, [&] { return !Threads.ponder.load(std::memory_order_relaxed); });
            }

#ifdef ENABLE_SEARCH_STATS
            if (Threads.searchStatsEnabled) {
                SearchStats aggregate;
                for (size_t i = 0; i < Threads.size(); ++i)
                    aggregate += Threads.at(i)->searchStats;
                std::cout << "info string searchstats "
                          << aggregate.toUciString() << std::endl;
            }
#endif

            if (moveIsNone(bm)) {
                std::cout << "bestmove 0000" << std::endl;
            } else {
                if (NNUE::trainingLogEnabled() && !Threads.ponder.load(std::memory_order_relaxed)) {
                    NNUE::logTrainingSample(bestThread->pos, bm, bestThread->bestScore,
                                            bestThread->completedDepth, Threads.nodes_searched());
                }

                // If best thread is not us, print its info line
                if (bestThread != this && bestThread->completedDepth > 0) {
                    uint64_t totalNodes = Threads.nodes_searched();
                    auto now_t = std::chrono::high_resolution_clock::now();
                    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now_t - pos.start_time).count();
                    if (ms == 0) ms = 1;
                    uint64_t nps = (totalNodes * 1000ULL) / static_cast<uint64_t>(ms);

                    std::cout << "info depth " << bestThread->completedDepth
                              << " seldepth " << bestThread->pos.selDepth;
                    int sc = bestThread->bestScore;
                    if (sc >= MATE_IN_MAX || sc <= -MATE_IN_MAX) {
                        int md = MATE_SCORE - (sc > 0 ? sc : -sc);
                        int mm = std::max(1, (md + 1) / 2);
                        if (sc < 0) mm = -mm;
                        std::cout << " score mate " << mm;
                    } else {
                        std::cout << " score cp " << sc;
                    }
                    std::cout << " time " << ms
                              << " nodes " << totalNodes
                              << " nps " << nps
                              << " hashfull " << TT.hashfull()
                              << " pv " << bestThread->pos.moveToUCI(bm)
                              << std::endl;
                }

                // Extract and validate ponder move from the completed PV (second move).
                Move ponderMove = MOVE_NONE;
                const std::vector<Move>& pvLine = bestThread->completedPV;
                if (pvLine.size() >= 2 && pvLine[0] == bm) {
                    Position childPos = pos;
                    childPos.makeMove(bm);
                    Move legalMoves[MAX_MOVES];
                    int numLegal = childPos.generateLegal(legalMoves);
                    for (int i = 0; i < numLegal; ++i) {
                        if (sameMoveIdentity(legalMoves[i], pvLine[1])) {
                            ponderMove = legalMoves[i];
                            break;
                        }
                    }
                    if (!moveIsNone(ponderMove)) {
                        std::cout << "bestmove " << pos.moveToUCI(bm) << " ponder " << childPos.moveToUCI(ponderMove);
                    } else {
                        std::cout << "bestmove " << pos.moveToUCI(bm);
                    }
                } else {
                    std::cout << "bestmove " << pos.moveToUCI(bm);
                }
                std::cout << std::endl;
            }
            std::cout.flush();

        } else {
            // === HELPER THREAD ===
            // Search until Threads.stop is set
            pos.search(64, 0);
        }
    }
}

void SearchThread::start_searching() {
    {
        std::lock_guard<std::mutex> lk(mtx);
        searching = true;
    }
    cv.notify_one();
}

void SearchThread::wait_for_search_finished() {
    std::unique_lock<std::mutex> lk(mtx);
    cv.wait(lk, [&] { return !searching; });
}

void SearchThread::clear() {
    killers.clear();
    std::memset(history_heur, 0, sizeof(history_heur));
    std::memset(contHistory, 0, sizeof(contHistory));
    std::memset(pawnHistory, 0, sizeof(pawnHistory));
    std::memset(corHist, 0, sizeof(corHist));
    std::memset(nonPawnCorHist, 0, sizeof(nonPawnCorHist));
    std::memset(captureHistory, 0, sizeof(captureHistory));
    for (int i = 0; i < PAWN_TT_SIZE; ++i)
        pawnTable[i] = PawnEntry{};
    std::memset(counterMoves, 0, sizeof(counterMoves));
    bestMove = MOVE_NONE;
    bestScore = -INF_SCORE;
    completedDepth = 0;
    completedPV.clear();
    hasCompletedIteration = false;
    bestMoveChanges.store(0, std::memory_order_relaxed);
    previousTimeReduction = 1.0;
    bestPreviousScore = -INF_SCORE;
    bestPreviousAverageScore = -INF_SCORE;
    nmpMinPly = 0;
    nmpColor = WHITE;
#ifdef ENABLE_SEARCH_STATS
    searchStats.clear();
    collectSearchStats = false;
#endif
}

// ========================= ThreadPool =========================

ThreadPool::~ThreadPool() {
    set(0);
}

void ThreadPool::set(size_t num) {
    // Destroy existing threads
    if (!threads_.empty()) {
        stopAndWait();
        for (auto* t : threads_)
            delete t;
        threads_.clear();
    }

    if (num > 0) {
        threads_.reserve(num);
        for (size_t i = 0; i < num; i++)
            threads_.push_back(new SearchThread(i));
    }
}

void ThreadPool::clear() {
    for (auto* t : threads_)
        t->clear();
}

void ThreadPool::waitForSearchFinished() {
    if (!threads_.empty() && main())
        main()->wait_for_search_finished();
}

void ThreadPool::stopAndWait() {
    ponder.store(false, std::memory_order_release);
    stop.store(true, std::memory_order_release);

    // A completed ponder search can be parked waiting to publish bestmove.
    // Wake it before joining; otherwise quit/setoption/position can deadlock.
    if (main())
        main()->cv.notify_one();

    waitForSearchFinished();
}

void ThreadPool::ponderHit() {
    if (!ponder.load(std::memory_order_acquire))
        return;

    // Publish the real-search clock before clearing ponder. Search-side
    // acquire loads then observe a complete, coherent transition.
    TimeMgr.restartTimer();
    searchTimeMs.store(static_cast<int>(TimeMgr.maximum()),
                       std::memory_order_release);
    ponder.store(false, std::memory_order_release);

    if (main())
        main()->cv.notify_one();
}

uint64_t ThreadPool::nodes_searched() const {
    uint64_t total = 0;
    for (auto* t : threads_)
        total += static_cast<uint64_t>(t->pos.nodes);
    return total;
}

void ThreadPool::startThinking(Position& rootPos, int maxDepth, int timeMs, bool ponderMode) {
    // Wait for any previous search to complete
    stopAndWait();

    // Store search parameters
    searchMaxDepth = maxDepth;
    searchTimeMs.store(timeMs, std::memory_order_release);
    stop.store(false, std::memory_order_relaxed);
    ponder.store(ponderMode, std::memory_order_relaxed);

    auto startTime = std::chrono::high_resolution_clock::now();

    // Copy root position to all threads
    for (auto* t : threads_) {
        t->pos = rootPos;      // Copy position state
        t->pos.thread = t;     // Set thread ownership
        t->pos.stopSearching = false;
        t->pos.nodes = 0;
        t->pos.selDepth = 0;
        t->pos.start_time = startTime;
        t->pos.time_limit_ms = (t->idx == 0) ? timeMs : 0;
        t->bestMove = MOVE_NONE;
        t->bestScore = -INF_SCORE;
        t->completedDepth = 0;
        t->completedPV.clear();
        t->hasCompletedIteration = false;
        t->bestMoveChanges.store(0, std::memory_order_relaxed);
#ifdef ENABLE_SEARCH_STATS
        t->searchStats.clear();
        t->collectSearchStats = searchStatsEnabled;
#endif
    }

    TT.newSearch();

    // Wake main thread — it will start helpers from its idle_loop
    main()->start_searching();
    // Returns immediately: search runs asynchronously
}

bool ThreadPool::runLazySmpSelectionTests() const {
    return runSelectionUnitTests();
}
