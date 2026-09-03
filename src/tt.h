// Transposition table with coherent 16-byte entries, four entries per cache line.
#ifndef DEEPBECKY_TT_H
#define DEEPBECKY_TT_H

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include "types.h"

// ========================= Constants =========================
// genBound8 layout: [gen:5][pv:1][bound:2]
// 3 lower bits are reserved (1 PV + 2 bound), 5 upper bits for generation
static constexpr unsigned GEN_BITS  = 3;                              // reserved bits
static constexpr int GEN_DELTA      = (1 << GEN_BITS);               // increment per search = 8
static constexpr int GEN_CYCLE      = 255 + GEN_DELTA;               // for modular arithmetic
static constexpr int GEN_MASK       = (0xFF << GEN_BITS) & 0xFF;     // 0xF8

// Sentinel value for "no eval stored"
static constexpr int16_t EVAL_NONE = -32001;

// depth8 == 0 is reserved for an empty entry. Occupied entries store logical
// depth + 1, allowing qsearch depth 0 and main-search depth 1 to remain
// distinct without increasing TTEntry size.
static constexpr int TT_DEPTH_OFFSET = 1;
static constexpr int TT_DEPTH_QS = 0;

// ========================= TT Flag (Bound) =========================
enum TTFlag : uint8_t {
    TT_NONE  = 0,   // No bound (empty or eval-only)
    TT_ALPHA = 1,   // Upper bound (fail-low)
    TT_BETA  = 2,   // Lower bound (fail-high)
    TT_EXACT = 3    // Exact value
};

// ========================= Coherent TT snapshots =========================
// The shared TT stores one complete 64-bit payload and a 64-bit signature
// (full Zobrist key XOR payload). Both words are lock-free atomics. A reader
// accepts the payload for a requested position only when XOR reconstructs the
// exact full key; an interleaving between two writes becomes a miss instead of
// a hybrid record. All chess fields reside in the same atomic payload.
struct TTData {
    uint64_t key = 0;
    uint8_t depth8 = 0;
    uint8_t genBound8 = 0;
    Move move = MOVE_NONE;
    int16_t value = 0;
    int16_t eval = EVAL_NONE;

    bool isOccupied() const { return depth8 != 0; }
    int depth() const {
        return isOccupied() ? static_cast<int>(depth8) - TT_DEPTH_OFFSET : -1;
    }
    TTFlag flag() const { return static_cast<TTFlag>(genBound8 & 0x3); }
    bool isPV() const { return (genBound8 & 0x4) != 0; }
    uint8_t relativeAge(uint8_t generation8) const {
        return static_cast<uint8_t>(
            (GEN_CYCLE + generation8 - genBound8) & GEN_MASK);
    }
};

class alignas(16) TTEntry {
public:
    TTEntry() noexcept : keyXor_(0), payload_(0) {}

    bool read(TTData& data) const noexcept;
    void save(uint64_t k, int16_t v, bool pv, TTFlag b, int d,
              uint16_t mv, int16_t ev, uint8_t generation8) noexcept;
    void clear() noexcept {
        payload_.store(0, std::memory_order_relaxed);
        keyXor_.store(0, std::memory_order_relaxed);
    }

    // Convenience accessors are intended for initialization/tests. Search code
    // consumes the single TTData snapshot returned by probe().
    bool isOccupied() const noexcept {
        TTData data;
        return read(data) && data.isOccupied();
    }
    int depth() const noexcept {
        TTData data;
        return read(data) ? data.depth() : -1;
    }

private:
    std::atomic<uint64_t> keyXor_;
    std::atomic<uint64_t> payload_;
};

static_assert(std::atomic<uint64_t>::is_always_lock_free,
              "Coherent TT requires lock-free 64-bit atomics");
static_assert(sizeof(TTEntry) == 16, "TTEntry must be 16 bytes");

class TTWriter {
public:
    explicit TTWriter(TTEntry* entry = nullptr) : entry_(entry) {}
    void save(uint64_t k, int16_t v, bool pv, TTFlag b, int d,
              uint16_t mv, int16_t ev, uint8_t generation8) const noexcept {
        assert(entry_ != nullptr);
        entry_->save(k, v, pv, b, d, mv, ev, generation8);
    }

private:
    TTEntry* entry_;
};

struct TTProbe {
    bool found = false;
    TTData data{};
    TTWriter writer{};
};

// Four 16-byte entries occupy one complete cache line.
static constexpr int CLUSTER_SIZE = 4;

struct alignas(64) TTCluster {
    TTEntry entry[CLUSTER_SIZE];
};

static_assert(sizeof(TTCluster) == 64, "TT cluster must be one cache line");

// ========================= Move Packing for TT =========================
// Pack a Move's squares+flags into 16 bits for TT storage:
//   bits 0-5:   from square
//   bits 6-11:  to square
//   bits 12-13: type (0=normal, 1=en passant, 2=castle, 3=promotion)
//   bits 14-15: promotion piece (0=knight, 1=bishop, 2=rook, 3=queen)
//
// Capture flag and double-push flag are NOT stored — they are
// reconstructed from the position when the TT move is retrieved.

// Compare a generated move with a TT move.
inline bool ttMoveMatch(const Move& generated, uint16_t ttMoveData) {
    return generated.data == ttMoveData;
}

// ========================= TranspositionTable =========================
inline uint64_t mul_hi64(uint64_t a, uint64_t b) {
#if defined(__GNUC__) && defined(__SIZEOF_INT128__)
    __extension__ using uint128 = unsigned __int128;
    return static_cast<uint64_t>((uint128(a) * uint128(b)) >> 64);
#else
    uint64_t aL = uint32_t(a), aH = a >> 32;
    uint64_t bL = uint32_t(b), bH = b >> 32;
    uint64_t c1 = (aL * bL) >> 32;
    uint64_t c2 = aH * bL + c1;
    uint64_t c3 = aL * bH + uint32_t(c2);
    return aH * bH + (c2 >> 32) + (c3 >> 32);
#endif
}

class TranspositionTable {
public:
    TranspositionTable();
    ~TranspositionTable();

    void resize(size_t sizeMB);
    void clear();

    void newSearch() { generation8_ += GEN_DELTA; }
    uint8_t generation() const { return generation8_; }

    // Probe returns an immutable coherent snapshot plus a separate writer for
    // the matching entry or best replacement candidate.
    TTProbe probe(uint64_t key);

    int hashfull() const;

    size_t getClusterCount() const { return clusterCount_; }
    size_t sizeMB() const { return (clusterCount_ * sizeof(TTCluster)) >> 20; }
    void prefetch(uint64_t key) const {
#if defined(__GNUC__) || defined(__clang__)
        __builtin_prefetch(firstEntry(key));
#elif defined(_MSC_VER)
        _mm_prefetch(reinterpret_cast<const char*>(firstEntry(key)), _MM_HINT_T0);
#endif
    }

private:
    TTEntry* firstEntry(uint64_t key) const {
        return &table_[mul_hi64(key, clusterCount_)].entry[0];
    }

    TTCluster* table_ = nullptr;
    size_t clusterCount_ = 0;
    uint8_t generation8_ = 0;
    bool isLargePageAllocated_ = false;
};

extern TranspositionTable TT;

#endif // DEEPBECKY_TT_HTT_H
