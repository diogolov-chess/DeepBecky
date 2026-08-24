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
            // Start all helper threads
            for (size_t i = 1; i < Threads.size(); i++)
                Threads.at(i)->start_searching();

            // Run own search (with time management)
            pos.search(Threads.searchMaxDepth, Threads.searchTimeMs);

            // Signal all threads to stop
            Threads.stop.store(true, std::memory_order_relaxed);

            // Wait for helpers to finish
            for (size_t i = 1; i < Threads.size(); i++)
                Threads.at(i)->wait_for_search_finished();

            // === VOTE-BASED BEST THREAD SELECTION ===
            SearchThread* bestThread = this;

            if (Threads.size() > 1) {
                std::map<uint32_t, int64_t> votes;
                int minScore = bestScore;

                // Find minimum score across all threads
                for (size_t i = 0; i < Threads.size(); i++) {
                    auto* t = Threads.at(i);
                    if (t->completedDepth > 0 && t->bestScore < minScore)
                        minScore = t->bestScore;
                }

                // Vote: weight = (score - minScore + 14) * completedDepth
                for (size_t i = 0; i < Threads.size(); i++) {
                    auto* t = Threads.at(i);
                    if (t->completedDepth <= 0 || moveIsNone(t->bestMove))
                        continue;

                    uint32_t moveKey = t->bestMove.data;
                    int64_t weight = static_cast<int64_t>(t->bestScore - minScore + 14)
                                   * t->completedDepth;
                    votes[moveKey] += weight;

                    uint32_t bestKey = bestThread->bestMove.data;

                    // Prefer shortest mate
                    if (bestThread->bestScore >= MATE_IN_MAX) {
                        if (t->bestScore > bestThread->bestScore)
                            bestThread = t;
                    } else if (t->bestScore >= MATE_IN_MAX
                            || votes[moveKey] > votes[bestKey]) {
                        bestThread = t;
                    }
                }
            }

            // Print bestmove (from main thread context)
            Move bm = bestThread->bestMove;

            // If in ponder mode, wait for ponderhit or stop before outputting
            // (the stop/ponderhit handler already clears ponder flag)
            if (Threads.ponder.load(std::memory_order_relaxed)) {
                // Ponder search finished before ponderhit; wait for it
                std::unique_lock<std::mutex> lk(mtx);
                cv.wait(lk, [&] { return !Threads.ponder.load(std::memory_order_relaxed); });
            }

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

                // Extract ponder move from PV (second move in the PV)
                Move ponderMove = MOVE_NONE;
                // Get PV from TT to find ponder move
                std::vector<Move> pvLine = bestThread->pos.rootPV;
                if (pvLine.size() >= 2) {
                    ponderMove = pvLine[1];
                }

                std::cout << "bestmove " << pos.moveToUCI(bm);
                if (!moveIsNone(ponderMove)) {
                    std::cout << " ponder " << pos.moveToUCI(ponderMove);
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
}

#ifdef __AVX2__
#include <immintrin.h>

static void ageInt16Array(int16_t* data, size_t count) {
    size_t chunks = count / 16;
    __m256i* vptr = reinterpret_cast<__m256i*>(data);
    for (size_t i = 0; i < chunks; ++i) {
        __m256i val = _mm256_loadu_si256(&vptr[i]);
        __m256i shifted = _mm256_srai_epi16(val, 3);
        _mm256_storeu_si256(&vptr[i], _mm256_sub_epi16(val, shifted));
    }
    for (size_t i = chunks * 16; i < count; ++i) {
        data[i] = static_cast<int16_t>(data[i] - data[i] / 8);
    }
}

static void ageInt32Array(int* data, size_t count) {
    size_t chunks = count / 8;
    __m256i* vptr = reinterpret_cast<__m256i*>(data);
    for (size_t i = 0; i < chunks; ++i) {
        __m256i val = _mm256_loadu_si256(&vptr[i]);
        __m256i shifted = _mm256_srai_epi32(val, 3);
        _mm256_storeu_si256(&vptr[i], _mm256_sub_epi32(val, shifted));
    }
    for (size_t i = chunks * 8; i < count; ++i) {
        data[i] = data[i] - data[i] / 8;
    }
}
#endif

void SearchThread::ageHistory() {
#ifdef __AVX2__
    ageInt32Array(&history_heur[0][0][0], sizeof(history_heur) / sizeof(int));
    ageInt16Array(&contHistory[0][0][0][0][0], sizeof(contHistory) / sizeof(int16_t));
    ageInt16Array(&pawnHistory[0][0][0], sizeof(pawnHistory) / sizeof(int16_t));
    ageInt16Array(&captureHistory[0][0][0], sizeof(captureHistory) / sizeof(int16_t));
#else
    // Keep cross-move learning but prevent score saturation over long games.
    for (int c = 0; c < 2; ++c)
        for (int from = 0; from < 64; ++from)
            for (int to = 0; to < 64; ++to)
                history_heur[c][from][to] = (history_heur[c][from][to] * 7) / 8;

    for (int layer = 0; layer < 4; ++layer)
        for (int prevPiece = 0; prevPiece < PIECE_NB; ++prevPiece)
            for (int prevTo = 0; prevTo < 64; ++prevTo)
                for (int piece = 0; piece < PIECE_NB; ++piece)
                    for (int to = 0; to < 64; ++to)
                        contHistory[layer][prevPiece][prevTo][piece][to] =
                            static_cast<int16_t>((contHistory[layer][prevPiece][prevTo][piece][to] * 7) / 8);

    for (int pawnHash = 0; pawnHash < 8192; ++pawnHash)
        for (int piece = 0; piece < PIECE_NB; ++piece)
            for (int to = 0; to < 64; ++to)
                pawnHistory[pawnHash][piece][to] =
                    static_cast<int16_t>((pawnHistory[pawnHash][piece][to] * 7) / 8);

    for (int piece = 0; piece < PIECE_NB; ++piece)
        for (int to = 0; to < 64; ++to)
            for (int capturedType = 0; capturedType < 6; ++capturedType)
                captureHistory[piece][to][capturedType] =
                    static_cast<int16_t>((captureHistory[piece][to][capturedType] * 7) / 8);
#endif
}

// ========================= ThreadPool =========================

ThreadPool::~ThreadPool() {
    set(0);
}

void ThreadPool::set(size_t num) {
    // Destroy existing threads
    if (!threads_.empty()) {
        waitForSearchFinished();
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

uint64_t ThreadPool::nodes_searched() const {
    uint64_t total = 0;
    for (auto* t : threads_)
        total += static_cast<uint64_t>(t->pos.nodes);
    return total;
}

void ThreadPool::startThinking(Position& rootPos, int maxDepth, int timeMs, bool ponderMode) {
    // Wait for any previous search to complete
    waitForSearchFinished();

    // Store search parameters
    searchMaxDepth = maxDepth;
    searchTimeMs = timeMs;
    stop.store(false, std::memory_order_relaxed);
    ponder.store(ponderMode, std::memory_order_relaxed);
    increaseDepth.store(true, std::memory_order_relaxed);

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
        t->bestMoveChanges.store(0, std::memory_order_relaxed);
    }

    TT.newSearch();

    // Wake main thread — it will start helpers from its idle_loop
    main()->start_searching();
    // Returns immediately: search runs asynchronously
}
