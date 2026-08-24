#ifndef DEEPBECKY_BITBOARD_H
#define DEEPBECKY_BITBOARD_H

#include "types.h"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

// ============================================================================
// Bitboard Constants (Files & Ranks)
// ============================================================================
constexpr U64 FileABB = 0x0101010101010101ULL;
constexpr U64 FileBBB = FileABB << 1;
constexpr U64 FileCBB = FileABB << 2;
constexpr U64 FileDBB = FileABB << 3;
constexpr U64 FileEBB = FileABB << 4;
constexpr U64 FileFBB = FileABB << 5;
constexpr U64 FileGBB = FileABB << 6;
constexpr U64 FileHBB = FileABB << 7;

constexpr U64 Rank1BB = 0xFFULL;
constexpr U64 Rank2BB = Rank1BB << (8 * 1);
constexpr U64 Rank3BB = Rank1BB << (8 * 2);
constexpr U64 Rank4BB = Rank1BB << (8 * 3);
constexpr U64 Rank5BB = Rank1BB << (8 * 4);
constexpr U64 Rank6BB = Rank1BB << (8 * 5);
constexpr U64 Rank7BB = Rank1BB << (8 * 6);
constexpr U64 Rank8BB = Rank1BB << (8 * 7);

// ============================================================================
// Bit Manipulation Primitives
// ============================================================================
inline void set_bit(U64& bb, int sq) { bb |= (1ULL << sq); }
inline void pop_bit(U64& bb, int sq) { bb &= ~(1ULL << sq); }
inline bool get_bit(U64 bb, int sq) { return (bb >> sq) & 1; }
inline U64 square_bb(int sq) { return 1ULL << sq; }

inline int lsb_index(U64 b) {
#if defined(_MSC_VER)
    unsigned long idx;
    _BitScanForward64(&idx, b);
    return static_cast<int>(idx);
#else
    return __builtin_ctzll(b);
#endif
}

inline int msb_index(U64 b) {
#if defined(_MSC_VER)
    unsigned long idx;
    _BitScanReverse64(&idx, b);
    return static_cast<int>(idx);
#else
    return 63 - __builtin_clzll(b);
#endif
}

inline int pop_lsb(U64* bb) {
    int sq = lsb_index(*bb);
    *bb &= *bb - 1;
    return sq;
}

inline int popcount(U64 b) {
#if defined(_MSC_VER)
    return static_cast<int>(__popcnt64(b));
#else
    return __builtin_popcountll(b);
#endif
}

// Directional Shifts
template<int D>
inline U64 shift(U64 b) {
    return D == NORTH      ? b << 8
         : D == SOUTH      ? b >> 8
         : D == EAST       ? (b & ~FileHBB) << 1
         : D == WEST       ? (b & ~FileABB) >> 1
         : D == NORTH_EAST ? (b & ~FileHBB) << 9
         : D == NORTH_WEST ? (b & ~FileABB) << 7
         : D == SOUTH_EAST ? (b & ~FileHBB) >> 7
         : D == SOUTH_WEST ? (b & ~FileABB) >> 9
         : 0;
}

// ============================================================================
// Precomputed Attack and Ray Tables
// ============================================================================
extern U64 KNIGHT_ATK_BB[64];
extern U64 KING_ATK_BB[64];
extern U64 WPAWN_ATK_BB[64];
extern U64 BPAWN_ATK_BB[64];

extern U64 BETWEEN_BB[64][64];  // Squares strictly between two squares (exclusive)
extern U64 LINE_BB[64][64];     // Entire line through two squares
extern U64 RAY_BB[64][8];       // Directional rays (N, NE, E, SE, S, SW, W, NW)

void initBitboards();
void initAttackTables();
void initRayTables();

// ============================================================================
// Attack Lookup Helpers
// ============================================================================
inline U64 pawnAttacks(Color c, int sq) {
    return c == WHITE ? WPAWN_ATK_BB[sq] : BPAWN_ATK_BB[sq];
}

inline U64 knightAttacks(int sq) { return KNIGHT_ATK_BB[sq]; }
inline U64 kingAttacks(int sq) { return KING_ATK_BB[sq]; }

namespace Magic {
    U64 bishopAttacks(int sq, U64 occ);
    U64 rookAttacks(int sq, U64 occ);
}

inline U64 bishopAttacks(int sq, U64 occ) { return Magic::bishopAttacks(sq, occ); }
inline U64 rookAttacks(int sq, U64 occ) { return Magic::rookAttacks(sq, occ); }
inline U64 queenAttacks(int sq, U64 occ) { 
    return Magic::bishopAttacks(sq, occ) | Magic::rookAttacks(sq, occ); 
}

#endif // DEEPBECKY_BITBOARD_H
