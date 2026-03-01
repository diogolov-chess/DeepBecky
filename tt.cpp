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

// Transposition Table with Clusters Implementation
#include "tt.h"
#include <iostream>
#include <cstdlib>

#if defined(_WIN32)
#include <malloc.h>
#endif

// Global instance
TranspositionTable TT;

// ========================= Implementation =========================

TranspositionTable::TranspositionTable()
    : table_(nullptr), clusterCount_(0), generation8_(0) {
    resize(64);  // 64 MB default
}

TranspositionTable::~TranspositionTable() {
    if (table_) {
#if defined(_WIN32)
        _aligned_free(table_);
#else
        free(table_);
#endif
        table_ = nullptr;
    }
}

void TranspositionTable::resize(size_t sizeMB) {
    if (sizeMB < 1) sizeMB = 1;
    if (sizeMB > 32768) sizeMB = 32768;

    size_t newClusterCount = sizeMB * 1024ULL * 1024ULL / sizeof(TTCluster);
    if (newClusterCount < 1024) newClusterCount = 1024;

    if (table_) {
#if defined(_WIN32)
        _aligned_free(table_);
#else
        free(table_);
#endif
        table_ = nullptr;
    }

    // Allocate cache-aligned memory (32 bytes = cluster size)
#if defined(_WIN32)
    table_ = static_cast<TTCluster*>(
        _aligned_malloc(newClusterCount * sizeof(TTCluster), 32));
#else
    table_ = static_cast<TTCluster*>(
        aligned_alloc(32, newClusterCount * sizeof(TTCluster)));
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
    if (table_ && clusterCount_ > 0) {
        // Cast to void* to clear raw memory safely (all zeros = valid empty state)
        std::memset(static_cast<void*>(table_), 0, clusterCount_ * sizeof(TTCluster));
    }
    generation8_ = 0;
}

// Probe the transposition table.
// Returns a pointer to the matching entry (or replacement candidate).
// Sets 'found' to true if the position's key matches.
TTEntry* TranspositionTable::probe(uint64_t key, bool& found) {
    TTEntry* const tte = firstEntry(key);
    const uint16_t k16 = uint16_t(key);

    // Search the cluster for a matching key
    for (int i = 0; i < CLUSTER_SIZE; ++i) {
        if (tte[i].key16 == k16) {
            found = tte[i].isOccupied();
            return &tte[i];
        }
    }

    // Not found — find the best replacement candidate
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

// Store a position in the TT
void TranspositionTable::store(uint64_t key, int depth, int16_t score, TTFlag bound,
                               uint16_t moveData, uint8_t moveFlags, int16_t eval, bool pvNode) {
    // Probe to find the right entry (matching key or replacement candidate)
    bool found;
    TTEntry* tte = probe(key, found);

    // Pack the move for storage
    uint16_t packedMove = packTTMove(moveData, moveFlags);

    tte->save(key, score, pvNode, bound, depth, packedMove, eval, generation8_);
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
