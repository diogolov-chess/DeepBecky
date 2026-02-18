/*
 * This file is part of Deep Becky 1.0 - A UCI Chess Engine written by AI
 * Copyright (C) 2025-2026 Diogo de Oliveira Almeida
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

#pragma once
#include <cstdint>
#include <vector>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace Magic {

using U64 = uint64_t;

extern const U64 rookMagics[64];
extern const U64 bishopMagics[64];
extern U64 rookMasks[64];
extern U64 bishopMasks[64];
extern int rookShifts[64];
extern int bishopShifts[64];
extern int rookOffsets[64];
extern int bishopOffsets[64];
extern std::vector<U64> rookTable;
extern std::vector<U64> bishopTable;

void init();

inline int popcount64(U64 v){
#if defined(_MSC_VER)
    return (int)__popcnt64(v);
#else
    return __builtin_popcountll(v);
#endif
}

inline int lsb_index(U64 b){
#if defined(_MSC_VER)
    unsigned long idx; _BitScanForward64(&idx, b); return (int)idx;
#else
    return __builtin_ctzll(b);
#endif
}

inline U64 rookAttacks(int sq, U64 occ){
    U64 key = ((occ & rookMasks[sq]) * rookMagics[sq]) >> rookShifts[sq];
    return rookTable[rookOffsets[sq] + (int)key];
}
inline U64 bishopAttacks(int sq, U64 occ){
    U64 key = ((occ & bishopMasks[sq]) * bishopMagics[sq]) >> bishopShifts[sq];
    return bishopTable[bishopOffsets[sq] + (int)key];
}

} // namespace Magic
