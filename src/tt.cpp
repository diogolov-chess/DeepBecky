// Transposition table with coherent 16-byte entries, four entries per cache line.
#include "tt.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <malloc.h>
#include <windows.h>
#endif

// Global transposition table instance
TranspositionTable TT;

namespace {

uint64_t packPayload(const TTData& data) {
    return static_cast<uint64_t>(data.depth8)
         | (static_cast<uint64_t>(data.genBound8) << 8)
         | (static_cast<uint64_t>(data.move.data) << 16)
         | (static_cast<uint64_t>(static_cast<uint16_t>(data.value)) << 32)
         | (static_cast<uint64_t>(static_cast<uint16_t>(data.eval)) << 48);
}

TTData unpackData(uint64_t key, uint64_t payload) {
    TTData data;
    data.key = key;
    data.depth8 = static_cast<uint8_t>(payload);
    data.genBound8 = static_cast<uint8_t>(payload >> 8);
    data.move.data = static_cast<uint16_t>(payload >> 16);
    data.value = static_cast<int16_t>(payload >> 32);
    data.eval = static_cast<int16_t>(payload >> 48);
    if (!data.isOccupied())
        data.eval = EVAL_NONE;
    return data;
}

} // namespace

bool TTEntry::read(TTData& data) const noexcept {
    const uint64_t signature = keyXor_.load(std::memory_order_acquire);
    const uint64_t payload = payload_.load(std::memory_order_relaxed);
    data = unpackData(signature ^ payload, payload);
    return true;
}

void TTEntry::save(uint64_t k, int16_t v, bool pv, TTFlag b, int d,
                   uint16_t mv, int16_t ev,
                   uint8_t generation8) noexcept {
    assert(d >= TT_DEPTH_QS && d <= 254);

    TTData oldData;
    read(oldData);
    TTData newData = oldData;

    // Preserve the previous move when the same position is saved without one.
    if (mv != 0 || k != oldData.key)
        newData.move.data = mv;

    const bool replace = b == TT_EXACT
                      || k != oldData.key
                      || d + 2 * static_cast<int>(pv) > oldData.depth() - 4
                      || oldData.relativeAge(generation8) != 0;

    if (replace) {
        newData.key = k;
        newData.depth8 = static_cast<uint8_t>(
            std::clamp(d + TT_DEPTH_OFFSET, TT_DEPTH_OFFSET, 255));
        newData.genBound8 = static_cast<uint8_t>(
            generation8 | (static_cast<uint8_t>(pv) << 2) | b);
        newData.value = v;
        newData.eval = ev;
    }

    const uint64_t payload = packPayload(newData);
    payload_.store(payload, std::memory_order_relaxed);
    keyXor_.store(newData.key ^ payload, std::memory_order_release);
}

// ========================= Implementation =========================

TranspositionTable::TranspositionTable()
    : table_(nullptr), clusterCount_(0), generation8_(0) {
    resize(64);  // 64 MB default
}

TranspositionTable::~TranspositionTable() {
    if (table_) {
#if defined(_WIN32)
        if (isLargePageAllocated_) {
            VirtualFree(table_, 0, MEM_RELEASE);
        } else {
            _aligned_free(table_);
        }
#else
        free(table_);
#endif
        table_ = nullptr;
    }
}

#if defined(_WIN32)
void enableLargePages() {
    HANDLE hToken;
    TOKEN_PRIVILEGES tp;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &tp.Privileges[0].Luid);
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, &tp, 0, (PTOKEN_PRIVILEGES)NULL, 0);
        CloseHandle(hToken);
    }
}
#endif

void TranspositionTable::resize(size_t sizeMB) {
    if (sizeMB < 1) sizeMB = 1;
    if (sizeMB > 32768) sizeMB = 32768;

    size_t newClusterCount = sizeMB * 1024ULL * 1024ULL / sizeof(TTCluster);
    if (newClusterCount < 1024) newClusterCount = 1024;
    size_t allocSize = newClusterCount * sizeof(TTCluster);

    if (table_) {
#if defined(_WIN32)
        if (isLargePageAllocated_) {
            VirtualFree(table_, 0, MEM_RELEASE);
        } else {
            _aligned_free(table_);
        }
#else
        free(table_);
#endif
        table_ = nullptr;
        isLargePageAllocated_ = false;
    }

#if defined(_WIN32)
    enableLargePages();
    SIZE_T largePageMin = GetLargePageMinimum();
    if (largePageMin > 0) {
        SIZE_T lpSize = (allocSize + largePageMin - 1) & ~(largePageMin - 1);
        table_ = static_cast<TTCluster*>(VirtualAlloc(NULL, lpSize, MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES, PAGE_READWRITE));
        if (table_) {
            isLargePageAllocated_ = true;
            newClusterCount = lpSize / sizeof(TTCluster);
            std::cout << "info string Hash allocated " << (lpSize >> 20) << " MB with Large Pages" << std::endl;
        }
    }
    
    if (!table_) {
        table_ = static_cast<TTCluster*>(
            _aligned_malloc(allocSize, alignof(TTCluster)));
    }
#else
    table_ = static_cast<TTCluster*>(
        aligned_alloc(alignof(TTCluster), allocSize));
#endif

    if (!table_) {
        std::cerr << "Failed to allocate " << sizeMB << " MB for TT" << std::endl;
        clusterCount_ = 0;
        return;
    }

    clusterCount_ = newClusterCount;
    for (size_t i = 0; i < clusterCount_; ++i)
        ::new (static_cast<void*>(table_ + i)) TTCluster;
    clear();
}

void TranspositionTable::clear() {
    if (!table_ || clusterCount_ == 0) {
        generation8_ = 0;
        return;
    }

    generation8_ = 0;

    // Multi-threaded clear: split memset across available hardware threads.
    // With 256MB+ hash, single-threaded memset takes hundreds of ms.
    // Splitting across N threads makes this near-instantaneous.
    size_t threadCount = std::max(size_t(1), size_t(std::thread::hardware_concurrency()));
    if (threadCount > 16) threadCount = 16;  // cap to avoid over-subscription

    auto clearRange = [this](size_t start, size_t len) {
        for (size_t i = start; i < start + len; ++i)
            for (int entry = 0; entry < CLUSTER_SIZE; ++entry)
                table_[i].entry[entry].clear();
    };

    if (threadCount == 1 || clusterCount_ < 1024) {
        clearRange(0, clusterCount_);
        return;
    }

    std::vector<std::thread> workers;
    workers.reserve(threadCount);

    const size_t stride = clusterCount_ / threadCount;
    for (size_t i = 0; i < threadCount; ++i) {
        const size_t start = stride * i;
        const size_t len   = (i + 1 != threadCount) ? stride : clusterCount_ - start;
        workers.emplace_back(clearRange, start, len);
    }

    for (auto& w : workers)
        w.join();
}

TTProbe TranspositionTable::probe(uint64_t key) {
    TTEntry* const tte = firstEntry(key);
    TTEntry* replace = nullptr;
    int replaceValue = std::numeric_limits<int>::max();

    for (int i = 0; i < CLUSTER_SIZE; ++i) {
        TTData data;
        if (!tte[i].read(data))
            continue;

        if (data.isOccupied() && data.key == key)
            return TTProbe{true, data, TTWriter(&tte[i])};

        if (!data.isOccupied())
            return TTProbe{false, TTData{}, TTWriter(&tte[i])};

        const int value = static_cast<int>(data.depth8)
                        - static_cast<int>(data.relativeAge(generation8_));
        if (value < replaceValue) {
            replaceValue = value;
            replace = &tte[i];
        }
    }

    // Defensive fallback. read() always returns a snapshot, so normal probes
    // select either an empty slot or the lowest replacement-value slot.
    if (!replace)
        replace = tte;
    return TTProbe{false, TTData{}, TTWriter(replace)};
}

// Returns an approximation of hashtable occupation (permille).
// Samples the first 1000 clusters.
int TranspositionTable::hashfull() const {
    if (!table_ || clusterCount_ == 0) return 0;

    int cnt = 0;
    size_t sampleClusters = std::min(clusterCount_, size_t(1000));
    for (size_t i = 0; i < sampleClusters; ++i) {
        for (int j = 0; j < CLUSTER_SIZE; ++j) {
            TTData data;
            cnt += table_[i].entry[j].read(data) && data.isOccupied()
                && data.relativeAge(generation8_) == 0;
        }
    }

    return cnt * 1000 / (static_cast<int>(sampleClusters) * CLUSTER_SIZE);
}
