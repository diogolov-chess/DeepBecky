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

// tt.h - Transposition Table
#ifndef DEEPBECKY_TT_H
#define DEEPBECKY_TT_H

#include <cstdint>
#include <cstring>
#include <algorithm>

// ========================= Constantes TT =========================
constexpr uint8_t TT_GEN_BITS = 6;
constexpr uint8_t TT_FLAG_BITS = 2;
constexpr uint8_t TT_FLAG_MASK = (1u << TT_FLAG_BITS) - 1u;
constexpr uint8_t TT_GEN_MASK  = ((1u << TT_GEN_BITS) - 1u) << TT_FLAG_BITS;
constexpr uint8_t TT_GEN_CYCLE = 1u << TT_GEN_BITS;  // 64 generations

// ========================= TT Flag =========================
enum TTFlag : uint8_t { 
    TT_EXACT = 0,   // Exact value
    TT_ALPHA = 1,   // Upper bound (fail-low)
    TT_BETA  = 2,   // Lower bound (fail-high)
    TT_NONE  = 3    // Invalid entry
};

// ========================= TTEntry =========================
// Compact 16-byte structure for better cache performance
#pragma pack(push, 1)
struct TTEntry {
    uint64_t key = 0;        // Full Zobrist key for verification (8 bytes)
    int16_t  score = 0;      // Position score (2 bytes)
    uint16_t moveData = 0;   // Move packed data (from/to squares) (2 bytes)
    int8_t   depth = 0;      // Search depth (1 byte)
    uint8_t  moveFlags = 0;  // Move flags (capture, ep, castle, promo) (1 byte)
    uint8_t  genBound = 0;   // Generation (6 bits) + Bound type (2 bits) (1 byte)
    uint8_t  pad = 0;        // Padding (1 byte) - Total: 16 bytes

    TTFlag flag() const { 
        return static_cast<TTFlag>(genBound & TT_FLAG_MASK); 
    }
    
    uint8_t generation() const { 
        return genBound >> TT_FLAG_BITS; 
    }
    
    void save(uint64_t k, int d, int s, TTFlag f, uint16_t md, uint8_t mf, uint8_t gen) {
        // Replacement policy: depth-preferred with aging
        uint8_t relativeAge = (TT_GEN_CYCLE + gen - generation()) & (TT_GEN_CYCLE - 1);
        
        // Replace if: new entry, greater depth, or old entry
        if (k != key || d >= depth - 3 || relativeAge > 4) {
            key = k;
            depth = static_cast<int8_t>(d);
            score = static_cast<int16_t>(s);
            moveData = md;
            moveFlags = mf;
            genBound = static_cast<uint8_t>((gen << TT_FLAG_BITS) | (f & TT_FLAG_MASK));
        }
    }
    
    bool isEmpty() const { return key == 0; }
};
#pragma pack(pop)

static_assert(sizeof(TTEntry) == 16, "TTEntry must be 16 bytes for cache alignment");

// ========================= TranspositionTable =========================
class TranspositionTable {
public:
    TranspositionTable();
    ~TranspositionTable();
    
    void resize(size_t sizeMB);
    void clear();
    
    void newSearch() { 
        generation_ = (generation_ + 1) & ((1u << TT_GEN_BITS) - 1); 
    }
    
    TTEntry* probe(uint64_t key, bool& found);
    void store(uint64_t key, int depth, int score, TTFlag bound, uint16_t moveData, uint8_t moveFlags);
    
    int hashfull() const;
    uint8_t generation() const { return generation_; }
    size_t entryCount() const { return entryCount_; }
    size_t sizeMB() const { return (entryCount_ * sizeof(TTEntry)) >> 20; }
    
    void prefetch(uint64_t key) const {
#if defined(__GNUC__) || defined(__clang__)
        __builtin_prefetch(&table_[index(key)]);
#endif
    }
    
private:
    TTEntry* table_;
    size_t entryCount_;
    uint8_t generation_;
    
    size_t index(uint64_t key) const { 
        return key % entryCount_;
    }
};

extern TranspositionTable TT;

#endif // DEEPBECKY_TT_H
