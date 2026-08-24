// Transposition Table with 3-entry clusters (32-byte cache-line aligned) and static evaluation storage
#ifndef DEEPBECKY_TT_H
#define DEEPBECKY_TT_H

#include <cstdint>
#include <cstring>
#include <algorithm>
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

// ========================= TT Flag (Bound) =========================
enum TTFlag : uint8_t {
    TT_NONE  = 0,   // No bound (empty or eval-only)
    TT_ALPHA = 1,   // Upper bound (fail-low)
    TT_BETA  = 2,   // Lower bound (fail-high)
    TT_EXACT = 3    // Exact value
};

// ========================= TTEntry =========================
// 10-byte transposition table entry, optimized for cache efficiency.
// Field order matches access pattern in probe() for best sequential reads.
//
// key16       16 bit
// depth8       8 bit
// genBound8    8 bit  (gen:5 + pv:1 + bound:2)
// move16      16 bit  (from:6 + to:6 + type:2 + promo:2)
// value16     16 bit
// eval16      16 bit  (static eval, NEW in Phase 2)
//
#pragma pack(push, 1)
struct TTEntry {
    // --- Data fields ---
    uint16_t key16     = 0;
    uint8_t  depth8    = 0;
    uint8_t  genBound8 = 0;
    uint16_t move16    = 0;  // packed: from(6)+to(6)+type(2)+promo(2)
    int16_t  value16   = 0;
    int16_t  eval16    = EVAL_NONE;

    // --- Accessors ---
    TTFlag flag() const {
        return static_cast<TTFlag>(genBound8 & 0x3);
    }

    bool isPV() const {
        return (genBound8 & 0x4) != 0;
    }

    bool isOccupied() const {
        return depth8 != 0;
    }

    int depth() const {
        return static_cast<int>(depth8);
    }

    uint8_t relativeAge(uint8_t generation8) const {
        return static_cast<uint8_t>((GEN_CYCLE + generation8 - genBound8) & GEN_MASK);
    }

    // Overwrite if exact bound, different position, deeper+PV, or older generation.
    void save(uint64_t k, int16_t v, bool pv, TTFlag b, int d,
              uint16_t mv, int16_t ev, uint8_t generation8)
    {
        // Preserve old move if we don't have a new one
        if (mv || uint16_t(k) != key16)
            move16 = mv;

        // Overwrite less valuable entries (cheapest checks first)
        if (b == TT_EXACT
            || uint16_t(k) != key16
            || d + 2 * pv > depth8 - 4
            || relativeAge(generation8))
        {
            key16     = uint16_t(k);
            depth8    = uint8_t(std::max(d, 1)); // min depth 1 to keep isOccupied
            genBound8 = uint8_t(generation8 | (uint8_t(pv) << 2) | b);
            value16   = v;
            eval16    = ev;
        }
    }
};
#pragma pack(pop)

static_assert(sizeof(TTEntry) == 10, "TTEntry must be 10 bytes");

// ========================= Cluster =========================
// 3 entries per cluster = 30 bytes + 2 padding = 32 bytes (cache-aligned)
static constexpr int CLUSTER_SIZE = 3;

struct alignas(32) TTCluster {
    TTEntry entry[CLUSTER_SIZE];
    char padding[2];
};

static_assert(sizeof(TTCluster) == 32, "Cluster must be 32 bytes");

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

    // Probe: returns pointer to matching entry (found=true) or
    // to the best replacement candidate (found=false).
    // Read-only: does NOT write to the TT. Use tte->save() to store data.
    TTEntry* probe(uint64_t key, bool& found);

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
