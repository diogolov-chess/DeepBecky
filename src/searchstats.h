#ifndef DEEPBECKY_SEARCHSTATS_H
#define DEEPBECKY_SEARCHSTATS_H

#include <array>
#include <cstdint>
#include <string>

struct SearchStats {
    static constexpr int DepthBuckets = 64;
    static constexpr int LmrDepthBands = 4;     // 1-3, 4-6, 7-10, 11+
    static constexpr int LmrReductionBands = 4; // R=1, R=2, R=3, R>=4

    uint64_t rfpCandidates = 0;
    uint64_t rfpCuts = 0;
    uint64_t rfpVetoTtQuiet = 0;
    uint64_t rfpVetoEndgame = 0;
    uint64_t rfpVetoMate = 0;
    std::array<uint64_t, DepthBuckets> rfpCandidatesByDepth{};
    std::array<uint64_t, DepthBuckets> rfpCutsByDepth{};

    uint64_t nmpAttempts = 0;
    uint64_t nmpCuts = 0;
    uint64_t nmpVerifications = 0;
    uint64_t nmpVerificationFails = 0;

    uint64_t lmrReductions = 0;
    uint64_t lmrFailHighs = 0;
    uint64_t lmrResearches = 0;
    std::array<uint64_t, LmrDepthBands> lmrReductionsByDepth{};
    std::array<uint64_t, LmrReductionBands> lmrReductionsByAmount{};

    uint64_t checkExtensions = 0;
    uint64_t singularExtensions = 0;
    uint64_t doubleExtensions = 0;
    uint64_t tripleExtensions = 0;
    uint64_t extensionNodes = 0;

    uint64_t aspirationAttempts = 0;
    uint64_t aspirationFailLow = 0;
    uint64_t aspirationFailHigh = 0;
    uint64_t aspirationResearches = 0;
    uint64_t aspirationDiscardedNodes = 0;

    uint64_t probCutAttempts = 0;
    uint64_t probCutCuts = 0;
    uint64_t razorCuts = 0;
    uint64_t futilityCuts = 0;
    uint64_t lmpTriggers = 0;
    uint64_t historyCuts = 0;
    uint64_t seeCaptureCuts = 0;
    uint64_t seeQuietCuts = 0;

    void clear() { *this = SearchStats{}; }
    SearchStats& operator+=(const SearchStats& other);
    bool invariantsHold() const;
    std::string toUciString() const;
};

#endif // DEEPBECKY_SEARCHSTATS_H
