/*
 * This file is part of Deep Becky 2.0 - A UCI Chess Engine written by AI
 * Copyright © 2025-2026 Diogo de O. Almeida.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

// Lazy SMP Threading
#ifndef DEEPBECKY_THREAD_H
#define DEEPBECKY_THREAD_H

#include "position.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

// ========================= SearchThread =========================
// Each thread owns its own Position copy and per-thread heuristic tables.
// Threads communicate only through the shared TT (lockless).
struct SearchThread {
    Position pos;
    size_t idx;

    // Per-thread heuristic tables (no sharing = no contention)
    KillerTable killers;
    int history_heur[2][64][64]{};
    PawnEntry pawnTable[PAWN_TT_SIZE]{};

    // Null move verification
    // When doing verification search after null move cutoff at high depth,
    // null move is temporarily disabled for `nmpColor` until ply > nmpMinPly.
    int nmpMinPly = 0;
    int nmpColor = 0;  // 0 = WHITE, 1 = BLACK

    // Search results (read by main thread after search completes)
    Move bestMove = MOVE_NONE;
    int bestScore = -INF_SCORE;
    int completedDepth = 0;

    // Thread management
    std::mutex mtx;
    std::condition_variable cv;
    bool searching = false;
    bool exitFlag = false;
    std::thread nativeThread;

    SearchThread(size_t index);
    ~SearchThread();

    void idle_loop();
    void start_searching();
    void wait_for_search_finished();
    void clear();
};

// ========================= ThreadPool =========================
// Manages all search threads. Lazy SMP: all threads search the same
// root position independently; the TT is the only communication channel.
class ThreadPool {
    std::vector<SearchThread*> threads_;

public:
    std::atomic<bool> stop{false};

    // Search parameters (set by cmdGo before waking main thread)
    int searchMaxDepth = 64;
    int searchTimeMs = 0;

    // Ponder support
    std::atomic<bool> ponder{false};   // Currently in ponder mode (don't print bestmove)
    bool ponderEnabled = false;        // UCI option "Ponder" enabled

    ~ThreadPool();

    void set(size_t num);
    void clear();
    void waitForSearchFinished();

    // Setup all threads with root position, then start main thread
    void startThinking(Position& rootPos, int maxDepth, int timeMs, bool ponderMode = false);

    SearchThread* main() const { return threads_.empty() ? nullptr : threads_[0]; }
    SearchThread* at(size_t i) const { return threads_[i]; }
    size_t size() const { return threads_.size(); }
    uint64_t nodes_searched() const;
};

extern ThreadPool Threads;

#endif // DEEPBECKY_THREAD_H
