#ifndef DEEPBECKY_THREATS_H
#define DEEPBECKY_THREATS_H

#include "types.h"
#include "bitboard.h"
#include <array>
#include <vector>
#include <cstdint>

class Position;

namespace Threats {

constexpr int ThreatFeatureDimensions = 66864;
constexpr int MaxActiveThreats = 128;

// Initializes threat lookup tables (idempotent, thread-safe)
void init();

// Computes unique threat index (0..66863) or -1 if excluded
int threatIndex(int attackingPiece, int fromSq, int attackedPiece, int toSq, bool mirrored, int pov);

// Fast stack-allocated extraction of active threats (no heap allocations)
int extractActiveThreatsFast(const Position& pos, bool mirrored, int pov, int* threatIndices);

// Single-pass extraction of active threats for both STM and NSTM simultaneously
void extractActiveThreatsDual(const Position& pos,
                             bool stmMirrored, int stmPov, int* stmThreats, int& stmCount,
                             bool nstmMirrored, int nstmPov, int* nstmThreats, int& nstmCount);

// Extracts all active threats directly from the position into the provided vector
void extractActiveThreats(const Position& pos, bool mirrored, int pov, std::vector<int>& activeThreats);

// Structure representing a single threat delta (+1 or -1)
struct ThreatDelta {
    int attackingPiece = EMPTY;
    int fromSq = 0;
    int attackedPiece = EMPTY;
    int toSq = 0;
    bool add = true;

    ThreatDelta() = default;
    ThreatDelta(int attkr, int from, int attkd, int to, bool isAdd)
        : attackingPiece(attkr), fromSq(from), attackedPiece(attkd), toSq(to), add(isAdd) {}
};

} // namespace Threats

#endif // DEEPBECKY_THREATS_H
