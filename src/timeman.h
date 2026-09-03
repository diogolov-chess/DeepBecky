#ifndef DEEPBECKY_TIMEMAN_H
#define DEEPBECKY_TIMEMAN_H

// ============================================================
//
// The TM computes two values at the start of each search:
//   optimumTime  - "soft limit": target thinking time
//   maximumTime  - "hard limit": absolute maximum
//
// Dynamic adjustments (fallingEval, bestMoveInstability,
// timeReduction, nodesEffort) are computed in the iterative
// deepening loop in search.cpp, NOT here.
// ============================================================

#include <chrono>
#include <cstdint>
#include <algorithm>
#include <cmath>

using TimePoint = int64_t;

// Move overhead range (ms) — reserve for GUI, I/O and thread shutdown.
constexpr TimePoint DEFAULT_MOVE_OVERHEAD = 30;
constexpr TimePoint MAX_MOVE_OVERHEAD = 5000;

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
    void init(const SearchLimits& limits, bool whiteToMove, int ply);

    // Get optimum thinking time (soft limit — target)
    TimePoint optimum() const { return optimumTime; }

    // Get maximum thinking time (hard limit — absolute max)
    TimePoint maximum() const { return maximumTime; }

    // Get elapsed time since search start
    TimePoint elapsed() const { return now() - startTime; }

    // Reset start time (used when transitioning from ponder to real search on ponderhit)
    void restartTimer() { startTime = now(); }

    // Check if search uses fixed move time
    bool isFixedTime() const { return fixed; }

    void setMoveOverhead(TimePoint value) {
        moveOverhead = std::clamp(value, TimePoint(0), MAX_MOVE_OVERHEAD);
    }
    TimePoint getMoveOverhead() const { return moveOverhead; }

    // Starting and joining helpers is counterproductive when their lifecycle
    // can consume the complete hard allocation.
    bool allowsHelperThreads(TimePoint searchTimeMs) const {
        return searchTimeMs <= 0 || searchTimeMs > moveOverhead;
    }

private:
    TimePoint startTime    = 0;
    TimePoint optimumTime  = 0;
    TimePoint maximumTime  = 0;
    TimePoint moveOverhead = DEFAULT_MOVE_OVERHEAD;
    bool fixed             = false;
};

// Global time manager
extern TimeManagement TimeMgr;

#endif // DEEPBECKY_TIMEMAN_H
