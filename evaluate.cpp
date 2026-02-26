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

// evaluate.cpp - Position evaluation
#include "evaluate.h"
#include "position.h"
#include "bitboard.h"
#include "magic.h"
#include <algorithm>

// ========================= PST Tables =========================
static const int PST_PAWN[64] = {
     0,  5,  5, -5, -5,  5,  5,  0, 0, 10, -5,  0,  0, -5, 10,  0,
     0, 10, 10, 20, 20, 10, 10,  0, 5, 15, 20, 25, 25, 20, 15,  5,
    10, 20, 25, 30, 30, 25, 20, 10, 15, 25, 30, 35, 35, 30, 25, 15,
    30, 40, 45, 50, 50, 45, 40, 30,  0,  0,  0,  0,  0,  0,  0,  0
};

static const int PST_KNIGHT[64] = {
   -30,-10,-10,-10,-10,-10,-10,-30, -10,  0,  5,  0,  0,  5,  0,-10,
   -10,  5, 10, 10, 10, 10,  5,-10, -10,  0, 10, 15, 15, 10,  0,-10,
   -10,  0, 10, 15, 15, 10,  0,-10, -10,  5, 10, 10, 10, 10,  5,-10,
   -10,  0,  5,  0,  0,  5,  0,-10, -30,-10,-10,-10,-10,-10,-10,-30
};

static const int PST_BISHOP[64] = {
   -20,-10,-10,-10,-10,-10,-10,-20, -10, 10,  0,  5,  5,  0, 10,-10,
   -10,  5, 10, 10, 10, 10,  5,-10, -10,  0, 10, 10, 10, 10,  0,-10,
   -10,  0, 10, 10, 10, 10,  0,-10, -10,  5, 10, 10, 10, 10,  5,-10,
   -10, 10,  0,  5,  5,  0, 10,-10, -20,-10,-10,-10,-10,-10,-10,-20
};

static const int PST_ROOK[64] = {
     0,  0,  5, 10, 10,  5,  0,  0, -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5, -5,  0,  0,  5,  5,  0,  0, -5,
    -5,  0,  0,  5,  5,  0,  0, -5, -5,  0,  0,  0,  0,  0,  0, -5,
     5, 10, 10, 10, 10, 10, 10,  5,  0,  0,  0,  0,  0,  0,  0,  0
};

static const int PST_QUEEN[64] = {
   -20,-10,-10, -5, -5,-10,-10,-20, -10,  0,  5,  0,  0,  0,  0,-10,
   -10,  5,  5,  5,  5,  5,  0,-10,  -5,  0,  5,  5,  5,  5,  0, -5,
    -5,  0,  5,  5,  5,  5,  0, -5, -10,  0,  5,  5,  5,  5,  0,-10,
   -10,  0,  0,  0,  0,  0,  0,-10, -20,-10,-10, -5, -5,-10,-10,-20
};

static const int PST_KING_MG[64] = {
   -30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30,
   -20,-30,-30,-40,-40,-30,-30,-20, -10,-20,-20,-20,-20,-20,-20,-10,
    20, 20,  0,  0,  0,  0, 20, 20,  20, 30, 10,  0,  0, 10, 30, 20
};

static const int PST_KING_EG[64] = {
   -50,-30,-30,-30,-30,-30,-30,-50, -30,-20,-10,  0,  0,-10,-20,-30,
   -30,-10, 20, 30, 30, 20,-10,-30, -30,-10, 30, 40, 40, 30,-10,-30,
   -30,-10, 30, 40, 40, 30,-10,-30, -30,-10, 20, 30, 30, 20,-10,-30,
   -30,-30,-10,  0,  0,-10,-30,-30, -50,-30,-30,-30,-30,-30,-30,-50
};

// ========================= Evaluate Implementation =========================
int Position::evaluate() {
    auto file_of = [](int s) { return s & 7; };
    auto rank_of = [](int s) { return s >> 3; };
    auto make_sq = [](int f, int r) { return (r << 3) | f; };
    auto file_mask = [](int f) -> U64 { return 0x0101010101010101ULL << f; };
    auto in_front_mask_white = [&](int s) -> U64 { return (~0ULL) << ((rank_of(s) + 1) * 8); };
    auto in_front_mask_black = [&](int s) -> U64 { return ((rank_of(s) * 8) == 0 ? 0ULL : ((1ULL << (rank_of(s) * 8)) - 1ULL)); };

    int matW = 0, matB = 0;
    int pstMG = 0, pstEG = 0;
    int phaseCount = 0;

    U64 occupancy = color_bitboards[WHITE] | color_bitboards[BLACK];

    // Material + PST using efficient switch
    for (int p = WPAWN; p <= BKING; ++p) {
        U64 bb = bitboards[p];
        while (bb) {
            int sqi = pop_lsb(&bb);
            switch (p) {
                case WPAWN:
                    matW += 100;
                    pstMG += PST_PAWN[sqi];
                    pstEG += PST_PAWN[sqi];
                    break;
                case WKNIGHT:
                    matW += 320;
                    pstMG += PST_KNIGHT[sqi];
                    pstEG += PST_KNIGHT[sqi];
                    phaseCount += 1;
                    break;
                case WBISHOP:
                    matW += 330;
                    pstMG += PST_BISHOP[sqi];
                    pstEG += PST_BISHOP[sqi];
                    phaseCount += 1;
                    break;
                case WROOK:
                    matW += 500;
                    pstMG += PST_ROOK[sqi];
                    pstEG += PST_ROOK[sqi];
                    phaseCount += 2;
                    break;
                case WQUEEN:
                    matW += 900;
                    pstMG += PST_QUEEN[sqi];
                    pstEG += PST_QUEEN[sqi];
                    phaseCount += 4;
                    break;
                case WKING:
                    pstMG += PST_KING_MG[sqi];
                    pstEG += PST_KING_EG[sqi];
                    break;
                case BPAWN: {
                    int r_sqi = 56 ^ sqi;
                    matB += 100;
                    pstMG -= PST_PAWN[r_sqi];
                    pstEG -= PST_PAWN[r_sqi];
                    break;
                }
                case BKNIGHT: {
                    int r_sqi = 56 ^ sqi;
                    matB += 320;
                    pstMG -= PST_KNIGHT[r_sqi];
                    pstEG -= PST_KNIGHT[r_sqi];
                    phaseCount += 1;
                    break;
                }
                case BBISHOP: {
                    int r_sqi = 56 ^ sqi;
                    matB += 330;
                    pstMG -= PST_BISHOP[r_sqi];
                    pstEG -= PST_BISHOP[r_sqi];
                    phaseCount += 1;
                    break;
                }
                case BROOK: {
                    int r_sqi = 56 ^ sqi;
                    matB += 500;
                    pstMG -= PST_ROOK[r_sqi];
                    pstEG -= PST_ROOK[r_sqi];
                    phaseCount += 2;
                    break;
                }
                case BQUEEN: {
                    int r_sqi = 56 ^ sqi;
                    matB += 900;
                    pstMG -= PST_QUEEN[r_sqi];
                    pstEG -= PST_QUEEN[r_sqi];
                    phaseCount += 4;
                    break;
                }
                case BKING: {
                    int r_sqi = 56 ^ sqi;
                    pstMG -= PST_KING_MG[r_sqi];
                    pstEG -= PST_KING_EG[r_sqi];
                    break;
                }
            }
        }
    }

    // ========== LAZY EVALUATION ==========
    // If material advantage is VERY large (>= 20 pawns), skip detailed eval
    // Only skip when position is truly decided - endgames need precision!
    constexpr int LAZY_THRESHOLD = 2000;  // ~20 pawns (very conservative)
    int matDiff = matW - matB;
    
    // NEVER do lazy eval in endgames - they need precise evaluation
    bool isEndgame = (phaseCount <= 8);

    if (!isEndgame) {
        int lazyScore = matDiff + (pstMG + pstEG) / 2;  // rough estimate
        int lazyScoreSide = white_to_move ? lazyScore : -lazyScore;
        if (std::abs(lazyScoreSide) > LAZY_THRESHOLD) {
            // Quick return with basic score
            int phase = phaseCount;
            if (phase > 24) phase = 24;
            if (phase < 0) phase = 0;
            int score = (matDiff + pstMG) * phase / 24 + (matDiff + pstEG) * (24 - phase) / 24;
            score += contempt;
            return white_to_move ? score : -score;
        }
    }

    // Bishop pair
    if (popcount(bitboards[WBISHOP]) >= 2) { pstMG += 25; pstEG += 25; }
    if (popcount(bitboards[BBISHOP]) >= 2) { pstMG -= 25; pstEG -= 25; }

    // Mobility
    int mobMG = 0, mobEG = 0;
    U64 whitePieces = color_bitboards[WHITE];
    U64 blackPieces = color_bitboards[BLACK];

    // Knights
    {
        U64 bb = bitboards[WKNIGHT];
        while (bb) {
            int s = pop_lsb(&bb);
            int m = popcount(KNIGHT_ATK_BB[s] & ~whitePieces);
            mobMG += 4 * m; mobEG += 3 * m;
        }
        bb = bitboards[BKNIGHT];
        while (bb) {
            int s = pop_lsb(&bb);
            int m = popcount(KNIGHT_ATK_BB[s] & ~blackPieces);
            mobMG -= 4 * m; mobEG -= 3 * m;
        }
    }
    // Bishops
    {
        U64 bb = bitboards[WBISHOP];
        while (bb) {
            int s = pop_lsb(&bb);
            int m = popcount(Magic::bishopAttacks(s, occupancy) & ~whitePieces);
            mobMG += 3 * m; mobEG += 4 * m;
        }
        bb = bitboards[BBISHOP];
        while (bb) {
            int s = pop_lsb(&bb);
            int m = popcount(Magic::bishopAttacks(s, occupancy) & ~blackPieces);
            mobMG -= 3 * m; mobEG -= 4 * m;
        }
    }
    // Rooks
    {
        U64 bb = bitboards[WROOK];
        while (bb) {
            int s = pop_lsb(&bb);
            int m = popcount(Magic::rookAttacks(s, occupancy) & ~whitePieces);
            mobMG += 2 * m; mobEG += 2 * m;
        }
        bb = bitboards[BROOK];
        while (bb) {
            int s = pop_lsb(&bb);
            int m = popcount(Magic::rookAttacks(s, occupancy) & ~blackPieces);
            mobMG -= 2 * m; mobEG -= 2 * m;
        }
    }
    // Queens
    {
        U64 bb = bitboards[WQUEEN];
        while (bb) {
            int s = pop_lsb(&bb);
            int m = popcount((Magic::rookAttacks(s, occupancy) | Magic::bishopAttacks(s, occupancy)) & ~whitePieces);
            mobMG += 1 * m; mobEG += 2 * m;
        }
        bb = bitboards[BQUEEN];
        while (bb) {
            int s = pop_lsb(&bb);
            int m = popcount((Magic::rookAttacks(s, occupancy) | Magic::bishopAttacks(s, occupancy)) & ~blackPieces);
            mobMG -= 1 * m; mobEG -= 2 * m;
        }
    }

    // Pawn structure with hash
    int pawnMG = 0, pawnEG = 0;
    U64 wp = bitboards[WPAWN];
    U64 bp = bitboards[BPAWN];

    size_t pawnIdx = pawnKey & (PAWN_TT_SIZE - 1);
    PawnEntry& pe = pawnTable[pawnIdx];

    if (pe.key == pawnKey) {
        pawnMG = pe.scoreMG;
        pawnEG = pe.scoreEG;
    } else {
        // Doubled pawns
        for (int f = 0; f < 8; ++f) {
            int wc = popcount(wp & file_mask(f));
            if (wc > 1) { pawnMG -= 12 * (wc - 1); pawnEG -= 8 * (wc - 1); }
            int bc = popcount(bp & file_mask(f));
            if (bc > 1) { pawnMG += 12 * (bc - 1); pawnEG += 8 * (bc - 1); }
        }

        // Isolated pawns
        auto is_isolated = [&](bool white, int sq) -> bool {
            int f = file_of(sq);
            U64 my = white ? wp : bp;
            U64 left = (f > 0) ? (my & file_mask(f - 1)) : 0ULL;
            U64 right = (f < 7) ? (my & file_mask(f + 1)) : 0ULL;
            return (left | right) == 0ULL;
        };

        {
            U64 bb = wp;
            while (bb) {
                int s = pop_lsb(&bb);
                if (is_isolated(true, s)) { pawnMG -= 15; pawnEG -= 10; }
            }
            bb = bp;
            while (bb) {
                int s = pop_lsb(&bb);
                if (is_isolated(false, s)) { pawnMG += 15; pawnEG += 10; }
            }
        }

        // Passed pawns
        static const int PASSED_MG[8] = {0, 5, 10, 20, 35, 60, 90, 0};
        static const int PASSED_EG[8] = {0, 10, 20, 40, 70, 110, 180, 0};

        auto is_passed_white = [&](int s) -> bool {
            int f = file_of(s);
            U64 files = file_mask(f) | (f > 0 ? file_mask(f - 1) : 0ULL) | (f < 7 ? file_mask(f + 1) : 0ULL);
            U64 infront = in_front_mask_white(s);
            return (bp & files & infront) == 0ULL;
        };
        auto is_passed_black = [&](int s) -> bool {
            int f = file_of(s);
            U64 files = file_mask(f) | (f > 0 ? file_mask(f - 1) : 0ULL) | (f < 7 ? file_mask(f + 1) : 0ULL);
            U64 infront = in_front_mask_black(s);
            return (wp & files & infront) == 0ULL;
        };

        {
            U64 bb = wp;
            while (bb) {
                int s = pop_lsb(&bb);
                if (is_passed_white(s)) {
                    int r = rank_of(s);
                    pawnMG += PASSED_MG[r];
                    pawnEG += PASSED_EG[r];
                }
            }
            bb = bp;
            while (bb) {
                int s = pop_lsb(&bb);
                if (is_passed_black(s)) {
                    int r = 7 - rank_of(s);
                    pawnMG -= PASSED_MG[r];
                    pawnEG -= PASSED_EG[r];
                }
            }
        }

        pe.key = pawnKey;
        pe.scoreMG = static_cast<int16_t>(pawnMG);
        pe.scoreEG = static_cast<int16_t>(pawnEG);
    }

    // Rooks on open/semi-open files
    int rookMG = 0, rookEG = 0;
    auto is_open_file_for = [&](bool white, int f) -> int {
        U64 myPawns = white ? wp : bp;
        U64 oppPawns = white ? bp : wp;
        bool my = (myPawns & file_mask(f)) == 0ULL;
        bool op = (oppPawns & file_mask(f)) == 0ULL;
        return my && op ? 2 : (my ? 1 : 0);
    };
    {
        U64 bb = bitboards[WROOK];
        while (bb) {
            int s = pop_lsb(&bb);
            int ty = is_open_file_for(true, file_of(s));
            if (ty == 1) { rookMG += 12; rookEG += 8; }
            else if (ty == 2) { rookMG += 24; rookEG += 12; }
        }
        bb = bitboards[BROOK];
        while (bb) {
            int s = pop_lsb(&bb);
            int ty = is_open_file_for(false, file_of(s));
            if (ty == 1) { rookMG -= 12; rookEG -= 8; }
            else if (ty == 2) { rookMG -= 24; rookEG -= 12; }
        }
    }

    // Knights on outposts
    int outMG = 0, outEG = 0;
    auto supported_by_pawn = [&](bool white, int s) -> bool {
        return white ? ((WPAWN_ATK_BB[s] & wp) != 0ULL) : ((BPAWN_ATK_BB[s] & bp) != 0ULL);
    };
    {
        U64 bb = bitboards[WKNIGHT];
        while (bb) {
            int s = pop_lsb(&bb);
            if ((BPAWN_ATK_BB[s] & bp) == 0ULL && rank_of(s) >= 3) {
                outMG += supported_by_pawn(true, s) ? 20 : 10;
                outEG += 10;
            }
        }
        bb = bitboards[BKNIGHT];
        while (bb) {
            int s = pop_lsb(&bb);
            if ((WPAWN_ATK_BB[s] & wp) == 0ULL && rank_of(s) <= 4) {
                outMG -= supported_by_pawn(false, s) ? 20 : 10;
                outEG -= 10;
            }
        }
    }

    // King safety
    int ksMG = 0, ksEG = 0;

    auto king_square = [&](bool white) -> int {
        U64 bb = white ? bitboards[WKING] : bitboards[BKING];
        return bb ? lsb_index(bb) : -1;
    };

    auto pawn_shield_score = [&](bool white) -> int {
        int ks = king_square(white);
        if (ks < 0) return 0;
        int f = file_of(ks);
        int r = rank_of(ks);
        int sc = 0;
        auto have_pawn = [&](int ff, int rr) -> bool {
            if (ff < 0 || ff > 7 || rr < 0 || rr > 7) return false;
            int s = make_sq(ff, rr);
            U64 bb = white ? wp : bp;
            return (bb >> s) & 1ULL;
        };
        if (white) {
            if (r <= 6) {
                sc += have_pawn(f, r + 1) ? 10 : -12;
                sc += have_pawn(f - 1, r + 1) ? 6 : -8;
                sc += have_pawn(f + 1, r + 1) ? 6 : -8;
                if (r <= 5) sc += have_pawn(f, r + 2) ? 4 : -4;
            }
        } else {
            if (r >= 1) {
                sc += have_pawn(f, r - 1) ? 10 : -12;
                sc += have_pawn(f - 1, r - 1) ? 6 : -8;
                sc += have_pawn(f + 1, r - 1) ? 6 : -8;
                if (r >= 2) sc += have_pawn(f, r - 2) ? 4 : -4;
            }
        }
        return sc;
    };

    auto king_ring_pressure = [&](bool white) -> int {
        int ks = king_square(white);
        if (ks < 0) return 0;
        U64 ring = KING_ATK_BB[ks] | (1ULL << ks);  // Include king square
        int sc = 0;
        
        // Get enemy pieces
        U64 eKnights = white ? bitboards[BKNIGHT] : bitboards[WKNIGHT];
        U64 eBishops = white ? bitboards[BBISHOP] : bitboards[WBISHOP];
        U64 eRooks = white ? bitboards[BROOK] : bitboards[WROOK];
        U64 eQueens = white ? bitboards[BQUEEN] : bitboards[WQUEEN];
        U64 ePawns = white ? bitboards[BPAWN] : bitboards[WPAWN];
        
        // EFFICIENT: Calculate each piece's attacks ONCE, then AND with ring
        // Instead of iterating over ring and calculating attacks for each square
        
        // Knight attacks on ring
        U64 knights = eKnights;
        while (knights) {
            int s = pop_lsb(&knights);
            if (KNIGHT_ATK_BB[s] & ring) sc += 9;
        }
        
        // Pawn attacks on ring (using bitboard shift - very fast)
        U64 pawnAtk = white 
            ? ((ePawns >> 7) & ~0x0101010101010101ULL) | ((ePawns >> 9) & ~0x8080808080808080ULL)
            : ((ePawns << 7) & ~0x8080808080808080ULL) | ((ePawns << 9) & ~0x0101010101010101ULL);
        sc += 5 * popcount(pawnAtk & ring);
        
        // Bishop attacks on ring
        U64 bishops = eBishops;
        while (bishops) {
            int s = pop_lsb(&bishops);
            if (Magic::bishopAttacks(s, occupancy) & ring) sc += 7;
        }
        
        // Rook attacks on ring
        U64 rooks = eRooks;
        while (rooks) {
            int s = pop_lsb(&rooks);
            if (Magic::rookAttacks(s, occupancy) & ring) sc += 6;
        }
        
        // Queen attacks on ring (bishop + rook)
        U64 queens = eQueens;
        while (queens) {
            int s = pop_lsb(&queens);
            U64 qatk = Magic::bishopAttacks(s, occupancy) | Magic::rookAttacks(s, occupancy);
            if (qatk & ring) sc += 10;
        }
        
        return sc;
    };

    ksMG += pawn_shield_score(true);
    ksMG -= pawn_shield_score(false);
    ksMG -= king_ring_pressure(true);
    ksMG += king_ring_pressure(false);

    {
        int ws = king_square(true);
        if (ws >= 0) ksEG += 6 * popcount(KING_ATK_BB[ws] & ~whitePieces);
        int bs = king_square(false);
        if (bs >= 0) ksEG -= 6 * popcount(KING_ATK_BB[bs] & ~blackPieces);
    }

    // Tempo
    int tempoMG = white_to_move ? 10 : -10;
    int tempoEG = white_to_move ? 5 : -5;

    // Blend MG/EG
    int mat = matW - matB;
    int scMG = mat + pstMG + mobMG + pawnMG + rookMG + outMG + ksMG + tempoMG;
    int scEG = mat + pstEG + mobEG + pawnEG + rookEG + outEG + ksEG + tempoEG;

    int phase = phaseCount;
    if (phase > 24) phase = 24;
    if (phase < 0) phase = 0;
    int score = (scMG * phase + scEG * (24 - phase)) / 24;

    // 50-move rule damping
    score -= score * halfmove / 212;

    // Contempt
    score += contempt;

    return white_to_move ? score : -score;
}
