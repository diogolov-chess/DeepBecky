//
// The init() function computes optimumTime and maximumTime.
// All dynamic adjustments (fallingEval, bestMoveInstability,
// timeReduction, nodesEffort) are handled in search.cpp's
// iterative deepening loop.

#include "timeman.h"
#include <algorithm>
#include <cmath>

// Global time manager instance
TimeManagement TimeMgr;

void TimeManagement::init(const SearchLimits& limits, bool whiteToMove, int ply) {
    startTime = limits.startTime > 0 ? limits.startTime : now();

    fixed = (limits.movetime > 0);

    // Handle special cases
    if (limits.infinite) {
        optimumTime = maximumTime = TimePoint(24 * 60 * 60 * 1000); // 24 hours
        return;
    }

    if (limits.movetime > 0) {
        // Fixed time per move
        optimumTime = maximumTime =
            std::max(TimePoint(1), TimePoint(limits.movetime - moveOverhead));
        return;
    }

    if (limits.depth > 0 && limits.time[0] == 0 && limits.time[1] == 0) {
        // Depth-only search
        optimumTime = maximumTime = TimePoint(24 * 60 * 60 * 1000);
        return;
    }

    // Get our time and increment
    int us = whiteToMove ? 0 : 1;
    TimePoint myTime = limits.time[us];
    TimePoint myInc  = limits.inc[us];

    // If we have no time, nothing to calculate
    if (myTime == 0) {
        optimumTime = maximumTime = TimePoint(1);
        return;
    }

    // ================================================================
    // ================================================================

    double optScale, maxScale;
    int centiMTG = limits.movestogo ? std::min(limits.movestogo * 100, 5000) : 5051;

    if (myTime < 1000)
        centiMTG = int(static_cast<double>(myTime) * 5.051);

    TimePoint timeLeft =
      std::max(TimePoint(1),
               myTime + (myInc * (centiMTG - 100) - moveOverhead * (200 + centiMTG)) / 100);

    // Sudden death
    if (limits.movestogo == 0) {
        // Tuned constant and optScale multiplier for NNUE
        double logTimeInSec = std::log10(std::max(1.0, static_cast<double>(myTime) / 1000.0));
        double optConstant  = std::min(0.002 + 0.0002 * logTimeInSec, 0.004);
        
        // Conservative optScale (capped at 10% of remaining clock time)
        optScale = std::min(0.01 + std::pow(ply + 2.0, 0.45) * optConstant,
                            0.10 * static_cast<double>(myTime) / static_cast<double>(timeLeft));

        // Limit maximum scaling to prevent single-move time overspend
        maxScale = std::min(1.5, 1.2 + ply / 25.0);
    }
    // Movestogo
    else {
        optScale =
          std::min((0.88 + ply / 116.4) / (centiMTG / 100.0),
                   0.88 * static_cast<double>(myTime) / static_cast<double>(timeLeft));
        maxScale = std::min(1.5, 1.1 + 0.1 * (centiMTG / 100.0));
    }

    // Compute final times
    optimumTime = TimePoint(optScale * static_cast<double>(timeLeft));
    
    // Ensure we never exceed the physical time we have left on the clock
    TimePoint hardLimit = TimePoint(std::max(
        1.0, 0.85 * static_cast<double>(myTime)
                 - static_cast<double>(moveOverhead) - 50.0));
    
    maximumTime =
      TimePoint(std::min(static_cast<double>(hardLimit),
                         maxScale * static_cast<double>(optimumTime)));

    // Safety clamps
    optimumTime = std::max(optimumTime, TimePoint(1));
    optimumTime = std::min(optimumTime, hardLimit); // NEVER exceed hard limit
    maximumTime = std::max(maximumTime, optimumTime);
}
