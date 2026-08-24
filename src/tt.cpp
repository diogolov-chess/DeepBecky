// Transposition Table with 3-entry clusters (32-byte cache-line aligned) and static evaluation storage
#include "tt.h"
#include <iostream>
#include <cstdlib>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <malloc.h>
#include <windows.h>
#endif

// Global transposition table instance
TranspositionTable TT;

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
        table_ = static_cast<TTCluster*>(_aligned_malloc(allocSize, 32));
    }
#else
    table_ = static_cast<TTCluster*>(aligned_alloc(32, allocSize));
#endif

    if (!table_) {
        std::cerr << "Failed to allocate " << sizeMB << " MB for TT" << std::endl;
        clusterCount_ = 0;
        return;
    }

    clusterCount_ = newClusterCount;
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

    if (threadCount == 1 || clusterCount_ < 1024) {
        // Small table or single core: just memset directly
        std::memset(static_cast<void*>(table_), 0, clusterCount_ * sizeof(TTCluster));
        return;
    }

    std::vector<std::thread> workers;
    workers.reserve(threadCount);

    const size_t stride = clusterCount_ / threadCount;
    for (size_t i = 0; i < threadCount; ++i) {
        const size_t start = stride * i;
        const size_t len   = (i + 1 != threadCount) ? stride : clusterCount_ - start;
        workers.emplace_back([this, start, len]() {
            std::memset(reinterpret_cast<char*>(table_) + start * sizeof(TTCluster), 0, len * sizeof(TTCluster));
        });
    }

    for (auto& w : workers)
        w.join();
}

// Probe the transposition table.
// Returns a pointer to the matching entry (or replacement candidate).
// Sets 'found' to true if the position's key matches.
// NOTE: This function is read-only — it does NOT write to the TT.
// Generation refresh happens only in save() when the entry is actually stored.
// This is critical for Lazy SMP: writing genBound8 here would cause cache line
// invalidation across all cores on every TT read, destroying performance.
TTEntry* TranspositionTable::probe(uint64_t key, bool& found) {
    TTEntry* const tte = firstEntry(key);
    const uint16_t k16 = uint16_t(key);

    for (int i = 0; i < CLUSTER_SIZE; ++i) {
        if (tte[i].key16 == k16 || !tte[i].depth8) {
            found = tte[i].isOccupied();
            return &tte[i];
        }
    }

    // All 3 entries occupied by different keys — find best replacement candidate
    // Replace the entry with lowest (depth - 8 * relativeAge)
    TTEntry* replace = tte;
    for (int i = 1; i < CLUSTER_SIZE; ++i) {
        if (replace->depth8 - replace->relativeAge(generation8_)
            > tte[i].depth8 - tte[i].relativeAge(generation8_))
            replace = &tte[i];
    }

    found = false;
    return replace;
}

// Returns an approximation of hashtable occupation (permille).
// Samples the first 1000 clusters.
int TranspositionTable::hashfull() const {
    if (!table_ || clusterCount_ == 0) return 0;

    int cnt = 0;
    size_t sampleClusters = std::min(clusterCount_, size_t(1000));
    for (size_t i = 0; i < sampleClusters; ++i) {
        for (int j = 0; j < CLUSTER_SIZE; ++j) {
            cnt += table_[i].entry[j].isOccupied()
                && table_[i].entry[j].relativeAge(generation8_) == 0;
        }
    }

    return cnt * 1000 / (static_cast<int>(sampleClusters) * CLUSTER_SIZE);
}
