/*
 * This file is part of Deep Becky 1.2 - A UCI Chess Engine written by AI
 * Copyright (C) 2025-2026 Diogo de Oliveira Almeida.
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

// timeman.cpp - Time Management
// Based on Stockfish's time management algorithm
// Conservative settings to preserve time for endgame
#include "timeman.h"
#include <cfloat>
#include <algorithm>

// Global time manager instance
TimeManagement TimeMgr;

namespace {
    // Constants for time management
    constexpr int MoveHorizon = 50;      // Plan at most this many moves ahead
    constexpr double MaxRatio = 5.0;     // Maximum time ratio (reduced from 7.3)
    constexpr double StealRatio = 0.20;  // Don't steal more than this (reduced from 0.34)
    
    // Minimum thinking time (ms)
    constexpr TimePoint MinThinkingTime = 10;
    
    // Overhead per move (ms) - time needed for I/O, etc.
    constexpr TimePoint MoveOverhead = 30;
}

double TimeManagement::moveImportance(int ply) {
    constexpr double XScale = 6.85;
    constexpr double XShift = 64.5;
    constexpr double Skew = 0.171;
    
    // Skew-logistic function
    return std::pow((1.0 + std::exp((ply - XShift) / XScale)), -Skew) + DBL_MIN;
}

// Calculate time remaining for current move
TimePoint TimeManagement::remaining(TimePoint myTime, int movesToGo, int ply, bool isMaxTime) {
    const double TMaxRatio = isMaxTime ? MaxRatio : 1.0;
    const double TStealRatio = isMaxTime ? StealRatio : 0.0;
    
    double moveImp = moveImportance(ply);
    double otherMovesImp = 0.0;
    
    // Sum importance of future moves
    for (int i = 1; i < movesToGo; ++i) {
        otherMovesImp += moveImportance(ply + 2 * i);
    }
    
    // Two ratio calculations (take the minimum)
    double ratio1 = (TMaxRatio * moveImp) / (TMaxRatio * moveImp + otherMovesImp);
    double ratio2 = (moveImp + TStealRatio * otherMovesImp) / (moveImp + otherMovesImp);
    
    return static_cast<TimePoint>(static_cast<double>(myTime) * std::min(ratio1, ratio2));
}

void TimeManagement::init(const SearchLimits& limits, bool whiteToMove, int gamePly) {
    startTime = limits.startTime > 0 ? limits.startTime : now();
    
    // Handle special cases
    if (limits.infinite) {
        optimumTime = maximumTime = TimePoint(24 * 60 * 60 * 1000); // 24 hours
        baseOptimum = optimumTime;
        return;
    }
    
    if (limits.movetime > 0) {
        // Fixed time per move
        optimumTime = maximumTime = std::max(TimePoint(limits.movetime - MoveOverhead), MinThinkingTime);
        baseOptimum = optimumTime;
        return;
    }
    
    if (limits.depth > 0 && limits.time[0] == 0 && limits.time[1] == 0) {
        // Depth-only search
        optimumTime = maximumTime = TimePoint(24 * 60 * 60 * 1000);
        baseOptimum = optimumTime;
        return;
    }
    
    // Get our time and increment
    int us = whiteToMove ? 0 : 1;  // 0 = WHITE, 1 = BLACK
    TimePoint myTime = limits.time[us];
    TimePoint myInc = limits.inc[us];
    
    // Safety: ensure we have some time
    if (myTime <= 0) myTime = 60000;  // Default 1 minute
    
    // ==========================================================
    // SIMPLE AND CONSERVATIVE TIME ALLOCATION
    // ==========================================================
    
    // Estimate moves remaining based on game phase
    // Early game (ply < 20): expect ~40 moves remaining
    // Middle game (20-60): expect ~25 moves remaining  
    // Late game (60+): expect ~15 moves remaining
    int expectedMoves;
    if (gamePly < 20) {
        expectedMoves = 45;  // More conservative in opening
    } else if (gamePly < 60) {
        expectedMoves = 30;
    } else {
        expectedMoves = 20;
    }
    
    // Use movestogo if provided (tournament time control)
    if (limits.movestogo > 0) {
        expectedMoves = limits.movestogo;
    }
    
    // Base time per move = remaining time / expected moves
    // Add 80% of increment (save 20% for safety)
    double baseTimePerMove = static_cast<double>(myTime) / expectedMoves 
                           + static_cast<double>(myInc) * 0.8;
    
    // Opening factor: use less time in the opening (moves are usually simpler)
    // ply 0-10: 40% of normal, ply 10-20: 60%, ply 20-40: 80%, ply 40+: 100%
    double phaseFactor;
    if (gamePly < 10) {
        phaseFactor = 0.40;
    } else if (gamePly < 20) {
        phaseFactor = 0.60;
    } else if (gamePly < 40) {
        phaseFactor = 0.80;
    } else {
        phaseFactor = 1.0;
    }
    
    // Optimum time: target time to think (will stop iteration after this)
    // Use 50% of base time per move, scaled by phase
    optimumTime = static_cast<TimePoint>(baseTimePerMove * 0.50 * phaseFactor);
    
    // Maximum time: hard limit (will abort search if exceeded)
    // Use 120% of base time (less flexibility than before)
    maximumTime = static_cast<TimePoint>(baseTimePerMove * 1.2 * phaseFactor);
    
    // Never use more than 15% of remaining time on a single move
    TimePoint safetyCap = static_cast<TimePoint>(static_cast<double>(myTime) * 0.15);
    maximumTime = std::min(maximumTime, safetyCap);
    optimumTime = std::min(optimumTime, safetyCap);
    
    // Ensure minimum thinking time
    // IMPORTANT: In endgames we need more time to find mates
    optimumTime = std::max(optimumTime, MinThinkingTime);
    maximumTime = std::max(maximumTime, optimumTime);
    
    // Ensure a reasonable minimum search time, but NOT too much!
    // The engine was losing on time because minimum was set to full increment
    // Use 50ms as absolute minimum, or 20% of increment (whichever is higher)
    // This balances between not blundering (too fast) and not flagging (too slow)
    TimePoint absoluteMinimum = std::max(TimePoint(50), myInc / 5);
    
    // Cap the minimum to prevent time trouble - never use more than 10% of remaining time as minimum
    TimePoint timeCap = myTime / 10;
    absoluteMinimum = std::min(absoluteMinimum, std::max(TimePoint(50), timeCap));
    
    optimumTime = std::max(optimumTime, absoluteMinimum);
    maximumTime = std::max(maximumTime, absoluteMinimum * 2);
    
    // SAFETY: Never let maximum exceed 20% of remaining time
    // This prevents flagging in time trouble
    TimePoint safetyMax = myTime / 5;
    maximumTime = std::min(maximumTime, safetyMax);
    optimumTime = std::min(optimumTime, maximumTime);
    
    baseOptimum = optimumTime;
}

void TimeManagement::adjustForStability(double stability) {
    // If best move is unstable (keeps changing), extend time
    // stability: 1.0 = stable, 0.5 = half the iterations had same best move
    
    // Only extend if VERY unstable (stability < 0.5)
    if (stability < 0.5) {
        // Small extension (up to 30% more time, was 50%)
        double extension = 1.0 + (0.5 - stability) * 0.6;
        optimumTime = std::min(
            static_cast<TimePoint>(static_cast<double>(baseOptimum) * extension),
            maximumTime
        );
    }
}

void TimeManagement::adjustForScoreDrop(int scoreDrop) {
    // If score dropped significantly, extend time
    // Only for LARGE drops (more than 0.5 pawn, was 0.3)
    
    if (scoreDrop > 50) {
        // Moderate extension (up to 50% more time, was 100%)
        double extension = 1.0 + std::min(scoreDrop / 200.0, 0.5);
        optimumTime = std::min(
            static_cast<TimePoint>(static_cast<double>(baseOptimum) * extension),
            maximumTime
        );
    }
}

// Factor for number of legal moves - fewer moves = faster decision
// Balance between saving time and ensuring adequate search depth
double TimeManagement::legalMovesFactor(int numLegalMoves, bool inCheck) {
    if (numLegalMoves <= 1) return 0.0;  // Instant move (only 1 option)
    if (numLegalMoves <= 2) return 0.3;  // Very fast - only 2 choices
    if (numLegalMoves <= 4) return 0.5;  // Fast - few choices
    if (numLegalMoves <= 6) return 0.7;  // Slightly fast
    if (inCheck && numLegalMoves <= 10) return 0.6;  // Fast when in check
    return 1.0;  // Normal time
}

// Check if this is an "obvious" move situation
bool TimeManagement::isObviousMove(int numLegalMoves, bool inCheck, bool isRecapture) {
    // Only one legal move - ALWAYS play instantly
    if (numLegalMoves <= 1) return true;
    
    // In check with very few options - play faster (but still search)
    if (inCheck && numLegalMoves <= 2) return true;
    
    // Simple recapture with few options
    if (isRecapture && numLegalMoves <= 2) return true;
    
    return false;
}
