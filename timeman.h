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

#ifndef DEEPBECKY_TIMEMAN_H
#define DEEPBECKY_TIMEMAN_H

// ============================================================
// TimeManagement - Dynamic Time Control
//
// Features:
// - Optimum time (target) vs Maximum time (hard limit)
// - Move importance based on game phase
// - Support for increment, movestogo, sudden death
// - Instant move for obvious situations (one legal move, recapture)
// - Dynamic time extension only for unstable positions
// ============================================================

#include <chrono>
#include <cstdint>
#include <algorithm>
#include <cmath>

using TimePoint = int64_t;

// Get current time in milliseconds
inline TimePoint now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

// Search limits passed from UCI
struct SearchLimits {
    int time[2] = {0, 0};      // Time left for WHITE and BLACK (ms)
    int inc[2] = {0, 0};       // Increment for WHITE and BLACK (ms)
    int movestogo = 0;         // Moves to next time control (0 = sudden death)
    int movetime = 0;          // Exact time per move (ms)
    int depth = 0;             // Maximum depth (0 = unlimited)
    bool infinite = false;     // Infinite analysis mode
    TimePoint startTime = 0;   // When search started
};

class TimeManagement {
public:
    // Initialize time management for a new search
    void init(const SearchLimits& limits, bool whiteToMove, int gamePly);
    
    // Get optimum thinking time (target)
    TimePoint optimum() const { return optimumTime; }
    
    // Set optimum thinking time (for mate extension)
    void setOptimum(TimePoint t) { optimumTime = t; }
    
    // Get maximum thinking time (hard limit)
    TimePoint maximum() const { return maximumTime; }
    
    // Get elapsed time since search start
    TimePoint elapsed() const { return now() - startTime; }
    
    // Check if we should stop searching
    bool shouldStop() const { return elapsed() >= maximumTime; }
    
    // Check if we've used enough time (can stop after completing iteration)
    bool shouldStopIteration() const { return elapsed() >= optimumTime; }
    
    // Scale time based on best move stability (call after each iteration)
    // stability: 1.0 = very stable, <1.0 = unstable (need more time)
    void adjustForStability(double stability);
    
    // Scale time based on score drop (if score drops, think longer)
    void adjustForScoreDrop(int scoreDrop);
    
    // Extend time for winning endgames that need deeper search for mate
    void adjustForWinningEndgame(int eval, int phaseCount);
    
    // Scale time for number of legal moves - fewer moves = less time
    // Returns multiplier: 0.0 = instant move, 1.0 = normal time
    static double legalMovesFactor(int numLegalMoves, bool inCheck);
    
    // Check if this is an "obvious" move situation
    static bool isObviousMove(int numLegalMoves, bool inCheck, bool isRecapture);

private:
    TimePoint startTime = 0;
    TimePoint optimumTime = 0;
    TimePoint maximumTime = 0;
    TimePoint baseOptimum = 0;  // Original optimum before adjustments
    
    // Move importance function (based on game phase)
    static double moveImportance(int ply);
    
    // Calculate remaining time allocation
    static TimePoint remaining(TimePoint myTime, int movesToGo, int ply, bool isMaxTime);
};

// Global time manager
extern TimeManagement TimeMgr;

#endif // DEEPBECKY_TIMEMAN_H
