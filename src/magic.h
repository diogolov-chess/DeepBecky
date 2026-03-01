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

// Magic Bitboards Header
#ifndef MAGIC_H
#define MAGIC_H

#include "types.h"
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace Magic {

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

// Initialize magic bitboard tables
void init();

// Rook attacks
inline U64 rookAttacks(int sq, U64 occ) {
    U64 key = ((occ & rookMasks[sq]) * rookMagics[sq]) >> rookShifts[sq];
    return rookTable[rookOffsets[sq] + static_cast<int>(key)];
}

// Bishop attacks
inline U64 bishopAttacks(int sq, U64 occ) {
    U64 key = ((occ & bishopMasks[sq]) * bishopMagics[sq]) >> bishopShifts[sq];
    return bishopTable[bishopOffsets[sq] + static_cast<int>(key)];
}

// Queen attacks = rook + bishop
inline U64 queenAttacks(int sq, U64 occ) {
    return rookAttacks(sq, occ) | bishopAttacks(sq, occ);
}

} // namespace Magic

#endif // MAGIC_H
