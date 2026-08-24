#ifndef MAGIC_H
#define MAGIC_H

#include "types.h"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#ifdef USE_PEXT
#include <immintrin.h>
#endif

namespace Magic {

// Table sizes (deterministic from mask generation)
static constexpr int ROOK_TABLE_SIZE   = 102400;
static constexpr int BISHOP_TABLE_SIZE = 5248;

extern const U64 rookMagics[64];
extern const U64 bishopMagics[64];
extern U64 rookMasks[64];
extern U64 bishopMasks[64];
extern int rookShifts[64];
extern int bishopShifts[64];
extern int rookOffsets[64];
extern int bishopOffsets[64];
extern U64 rookTable[ROOK_TABLE_SIZE];
extern U64 bishopTable[BISHOP_TABLE_SIZE];

// Initialize magic bitboard attack tables
void init();

// Rook attacks lookup
inline U64 rookAttacks(int sq, U64 occ) {
#ifdef USE_PEXT
    return rookTable[rookOffsets[sq] + static_cast<int>(_pext_u64(occ, rookMasks[sq]))];
#else
    U64 key = ((occ & rookMasks[sq]) * rookMagics[sq]) >> rookShifts[sq];
    return rookTable[rookOffsets[sq] + static_cast<int>(key)];
#endif
}

// Bishop attacks lookup
inline U64 bishopAttacks(int sq, U64 occ) {
#ifdef USE_PEXT
    return bishopTable[bishopOffsets[sq] + static_cast<int>(_pext_u64(occ, bishopMasks[sq]))];
#else
    U64 key = ((occ & bishopMasks[sq]) * bishopMagics[sq]) >> bishopShifts[sq];
    return bishopTable[bishopOffsets[sq] + static_cast<int>(key)];
#endif
}

// Queen attacks = rook attacks | bishop attacks
inline U64 queenAttacks(int sq, U64 occ) {
    return rookAttacks(sq, occ) | bishopAttacks(sq, occ);
}

} // namespace Magic

#endif // MAGIC_H
