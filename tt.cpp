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

// tt.cpp - Transposition Table Implementation
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
    : table_(nullptr), entryCount_(0), generation_(0) {
    resize(64);  // 64 MB by default
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
    if (sizeMB > 32768) sizeMB = 32768;  // Max 32 GB
    
    size_t newEntryCount = (sizeMB * 1024ULL * 1024ULL) / sizeof(TTEntry);
    
    // Round to power of 2 for better distribution
    size_t power2 = 1;
    while (power2 < newEntryCount) power2 <<= 1;
    newEntryCount = power2 >> 1;  // Use smaller power to not exceed sizeMB
    if (newEntryCount < 1024) newEntryCount = 1024;
    
    if (table_) {
#if defined(_WIN32)
        _aligned_free(table_);
#else
        free(table_);
#endif
        table_ = nullptr;
    }
    
    // Allocate with cache alignment
#if defined(_WIN32)
    table_ = static_cast<TTEntry*>(_aligned_malloc(newEntryCount * sizeof(TTEntry), 64));
#else
    table_ = static_cast<TTEntry*>(aligned_alloc(64, newEntryCount * sizeof(TTEntry)));
#endif
    
    if (!table_) {
        std::cerr << "Failed to allocate " << sizeMB << " MB for TT" << std::endl;
        entryCount_ = 0;
        return;
    }
    
    entryCount_ = newEntryCount;
    clear();
}

void TranspositionTable::clear() {
    if (table_ && entryCount_ > 0) {
        // Use value-initialization instead of memset for proper clearing
        for(size_t i = 0; i < entryCount_; ++i){
            table_[i] = TTEntry{};
        }
    }
    generation_ = 0;
}

TTEntry* TranspositionTable::probe(uint64_t key, bool& found) {
    TTEntry* entry = &table_[index(key)];
    found = (entry->key == key);
    return entry;
}

void TranspositionTable::store(uint64_t key, int depth, int score, TTFlag bound, 
                               uint16_t moveData, uint8_t moveFlags) {
    TTEntry* entry = &table_[index(key)];
    entry->save(key, depth, score, bound, moveData, moveFlags, generation_);
}

int TranspositionTable::hashfull() const {
    if (!table_ || entryCount_ == 0) return 0;
    
    int used = 0;
    size_t sampleSize = std::min(entryCount_, size_t(1000));
    
    for (size_t i = 0; i < sampleSize; ++i) {
        if (!table_[i].isEmpty()) {
            uint8_t age = (TT_GEN_CYCLE + generation_ - table_[i].generation()) & (TT_GEN_CYCLE - 1);
            if (age <= 1) ++used;
        }
    }
    
    return (used * 1000) / static_cast<int>(sampleSize);
}
