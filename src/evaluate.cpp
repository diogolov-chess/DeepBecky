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

// Position Evaluation
#include "evaluate.h"
#include "position.h"
#include "thread.h"
#include "bitboard.h"
#include "magic.h"
#include <algorithm>
#include <iostream>

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

// ========================= Incremental PSQT Initialization =========================
int Eval::PSQT_MG[PIECE_NB][64];
int Eval::PSQT_EG[PIECE_NB][64];
int Eval::PHASE_WEIGHT[PIECE_NB];

void Eval::init() {
    std::memset(PSQT_MG, 0, sizeof(PSQT_MG));
    std::memset(PSQT_EG, 0, sizeof(PSQT_EG));
    std::memset(PHASE_WEIGHT, 0, sizeof(PHASE_WEIGHT));

    PHASE_WEIGHT[WKNIGHT] = PHASE_WEIGHT[BKNIGHT] = PHASE_KNIGHT;
    PHASE_WEIGHT[WBISHOP] = PHASE_WEIGHT[BBISHOP] = PHASE_BISHOP;
    PHASE_WEIGHT[WROOK]   = PHASE_WEIGHT[BROOK]   = PHASE_ROOK;
    PHASE_WEIGHT[WQUEEN]  = PHASE_WEIGHT[BQUEEN]  = PHASE_QUEEN;

    for (int sq = 0; sq < 64; sq++) {
        int rsq = sq ^ 56; // rank-flipped square for black

        // White pieces: positive values
        PSQT_MG[WPAWN][sq]   = PST_PAWN[sq];
        PSQT_EG[WPAWN][sq]   = PST_PAWN[sq];
        PSQT_MG[WKNIGHT][sq] = PST_KNIGHT[sq];
        PSQT_EG[WKNIGHT][sq] = PST_KNIGHT[sq];
        PSQT_MG[WBISHOP][sq] = PST_BISHOP[sq];
        PSQT_EG[WBISHOP][sq] = PST_BISHOP[sq];
        PSQT_MG[WROOK][sq]   = PST_ROOK[sq];
        PSQT_EG[WROOK][sq]   = PST_ROOK[sq];
        PSQT_MG[WQUEEN][sq]  = PST_QUEEN[sq];
        PSQT_EG[WQUEEN][sq]  = PST_QUEEN[sq];
        PSQT_MG[WKING][sq]   = PST_KING_MG[sq];
        PSQT_EG[WKING][sq]   = PST_KING_EG[sq];

        // Black pieces: negated values at rank-flipped square
        PSQT_MG[BPAWN][sq]   = -PST_PAWN[rsq];
        PSQT_EG[BPAWN][sq]   = -PST_PAWN[rsq];
        PSQT_MG[BKNIGHT][sq] = -PST_KNIGHT[rsq];
        PSQT_EG[BKNIGHT][sq] = -PST_KNIGHT[rsq];
        PSQT_MG[BBISHOP][sq] = -PST_BISHOP[rsq];
        PSQT_EG[BBISHOP][sq] = -PST_BISHOP[rsq];
        PSQT_MG[BROOK][sq]   = -PST_ROOK[rsq];
        PSQT_EG[BROOK][sq]   = -PST_ROOK[rsq];
        PSQT_MG[BQUEEN][sq]  = -PST_QUEEN[rsq];
        PSQT_EG[BQUEEN][sq]  = -PST_QUEEN[rsq];
        PSQT_MG[BKING][sq]   = -PST_KING_MG[rsq];
        PSQT_EG[BKING][sq]   = -PST_KING_EG[rsq];
    }
}

// ========================= Endgame Mating Evaluation =======================
// Table: how far a square is from the center (Manhattan distance from center)
// Used to drive the losing king to the corner/edge
static const int CENTER_DISTANCE[64] = {
    6, 5, 4, 3, 3, 4, 5, 6,
    5, 4, 3, 2, 2, 3, 4, 5,
    4, 3, 2, 1, 1, 2, 3, 4,
    3, 2, 1, 0, 0, 1, 2, 3,
    3, 2, 1, 0, 0, 1, 2, 3,
    4, 3, 2, 1, 1, 2, 3, 4,
    5, 4, 3, 2, 2, 3, 4, 5,
    6, 5, 4, 3, 3, 4, 5, 6
};

// Manhattan distance between two squares
static inline int manhattanDistance(int sq1, int sq2) {
    int f1 = sq1 & 7, r1 = sq1 >> 3;
    int f2 = sq2 & 7, r2 = sq2 >> 3;
    return std::abs(f1 - f2) + std::abs(r1 - r2);
}

// Chebyshev distance (king distance) between two squares  
static inline int chebyshevDistance(int sq1, int sq2) {
    int f1 = sq1 & 7, r1 = sq1 >> 3;
    int f2 = sq2 & 7, r2 = sq2 >> 3;
    return std::max(std::abs(f1 - f2), std::abs(r1 - r2));
}

// Evaluate KQ vs K or KR vs K: guide the winning side to checkmate
// Returns score from WHITE's perspective, or 0 if not applicable
static int evaluateKXK(const Position& pos) {
    // Count pieces
    int wQueens  = popcount(pos.bitboards[WQUEEN]);
    int wRooks   = popcount(pos.bitboards[WROOK]);
    int wBishops = popcount(pos.bitboards[WBISHOP]);
    int wKnights = popcount(pos.bitboards[WKNIGHT]);
    int wPawns   = popcount(pos.bitboards[WPAWN]);
    int bQueens  = popcount(pos.bitboards[BQUEEN]);
    int bRooks   = popcount(pos.bitboards[BROOK]);
    int bBishops = popcount(pos.bitboards[BBISHOP]);
    int bKnights = popcount(pos.bitboards[BKNIGHT]);
    int bPawns   = popcount(pos.bitboards[BPAWN]);

    int wPieceCount = wQueens + wRooks + wBishops + wKnights + wPawns;
    int bPieceCount = bQueens + bRooks + bBishops + bKnights + bPawns;

    // White has mating material, Black has only king
    if (bPieceCount == 0 && wPieceCount >= 1 && (wQueens > 0 || wRooks > 0)) {
        int wKingSq = lsb_index(pos.bitboards[WKING]);
        int bKingSq = lsb_index(pos.bitboards[BKING]);

        int score = 5000;  // Large base score to ensure we know we're winning

        // Bonus for pushing enemy king to edge/corner (CRITICAL)
        score += CENTER_DISTANCE[bKingSq] * 50;

        // Bonus for bringing our king closer to enemy king
        int kingDist = chebyshevDistance(wKingSq, bKingSq);
        score += (7 - kingDist) * 30;

        // Extra bonus when enemy king is on the edge
        int bFile = bKingSq & 7;
        int bRank = bKingSq >> 3;
        bool onEdge = (bFile == 0 || bFile == 7 || bRank == 0 || bRank == 7);
        if (onEdge) score += 80;

        // Extra bonus when enemy king is in a corner
        bool inCorner = ((bFile == 0 || bFile == 7) && (bRank == 0 || bRank == 7));
        if (inCorner) score += 120;

        // Add actual material on top (queen=900, rook=500, etc.)
        score += wQueens * 900 + wRooks * 500 + wBishops * 330 + wKnights * 320 + wPawns * 100;

        return score;
    }

    // Black has mating material, White has only king  
    if (wPieceCount == 0 && bPieceCount >= 1 && (bQueens > 0 || bRooks > 0)) {
        int wKingSq = lsb_index(pos.bitboards[WKING]);
        int bKingSq = lsb_index(pos.bitboards[BKING]);

        int score = 5000;

        score += CENTER_DISTANCE[wKingSq] * 50;

        int kingDist = chebyshevDistance(wKingSq, bKingSq);
        score += (7 - kingDist) * 30;

        int wFile = wKingSq & 7;
        int wRank = wKingSq >> 3;
        bool onEdge = (wFile == 0 || wFile == 7 || wRank == 0 || wRank == 7);
        if (onEdge) score += 80;

        bool inCorner = ((wFile == 0 || wFile == 7) && (wRank == 0 || wRank == 7));
        if (inCorner) score += 120;

        score += bQueens * 900 + bRooks * 500 + bBishops * 330 + bKnights * 320 + bPawns * 100;

        return -score;  // Negative because Black is winning
    }

    return 0;  // Not a recognized endgame pattern
}

// ========================= Evaluate Implementation =========================
int Position::evaluate() {
    auto file_of = [](int s) { return s & 7; };
    auto rank_of = [](int s) { return s >> 3; };
    auto make_sq = [](int f, int r) { return (r << 3) | f; };
    auto file_mask = [](int f) -> U64 { return 0x0101010101010101ULL << f; };
    auto in_front_mask_white = [&](int s) -> U64 { return (~0ULL) << ((rank_of(s) + 1) * 8); };
    auto in_front_mask_black = [&](int s) -> U64 { return ((rank_of(s) * 8) == 0 ? 0ULL : ((1ULL << (rank_of(s) * 8)) - 1ULL)); };

    // ========== ENDGAME MATING PATTERNS ==========
    // Check for KQ vs K, KR vs K, etc. BEFORE full evaluation
    // These endgames need specialized evaluation to guide the engine toward mate
    int endgameScore = evaluateKXK(*this);
    if (endgameScore != 0) {
        // Apply 50-move damping but MUCH weaker for clearly won endgames
        // Only start damping significantly above halfmove 80 (close to actual draw)
        if (halfmove > 80) {
            endgameScore -= endgameScore * (halfmove - 80) / 40;
        }
        return white_to_move ? endgameScore : -endgameScore;
    }

    // Use incremental material + PST values (computed in makeMove/setFEN)
    int matW = materialW;
    int matB = materialB;
    int pstMG = psqtMG;
    int pstEG = psqtEG;
    int phase_count = this->phaseCount;

#ifndef NDEBUG
    // Debug verification: recompute from scratch and compare
    {
        int dbg_matW = 0, dbg_matB = 0, dbg_pstMG = 0, dbg_pstEG = 0, dbg_phase = 0;
        for (int p = WPAWN; p <= BKING; ++p) {
            U64 bb = bitboards[p];
            while (bb) {
                int sqi = pop_lsb(&bb);
                dbg_pstMG += Eval::PSQT_MG[p][sqi];
                dbg_pstEG += Eval::PSQT_EG[p][sqi];
                if (p != WKING && p != BKING) {
                    if (isWhitePiece(p)) dbg_matW += PIECE_VALUE[p];
                    else                 dbg_matB += PIECE_VALUE[p];
                }
                dbg_phase += Eval::PHASE_WEIGHT[p];
            }
        }
        if (dbg_matW != matW || dbg_matB != matB ||
            dbg_pstMG != pstMG || dbg_pstEG != pstEG || dbg_phase != phase_count) {
            std::cout << "info string INCREMENTAL BUG! matW=" << matW << " expected=" << dbg_matW
                      << " matB=" << matB << " expected=" << dbg_matB
                      << " pstMG=" << pstMG << " expected=" << dbg_pstMG
                      << " pstEG=" << pstEG << " expected=" << dbg_pstEG
                      << " phase=" << phase_count << " expected=" << dbg_phase << std::endl;
        }
    }
#endif

    U64 occupancy = color_bitboards[WHITE] | color_bitboards[BLACK];

    // ========== LAZY EVALUATION ==========
    // If material advantage is VERY large (>= 20 pawns), skip detailed eval
    // Only skip when position is truly decided - endgames need precision!
    constexpr int LAZY_THRESHOLD = 2000;  // ~20 pawns (very conservative)
    int matDiff = matW - matB;
    
    // NEVER do lazy eval in endgames - they need precise evaluation
    bool isEndgame = (phase_count <= 8);

    if (!isEndgame) {
        int lazyScore = matDiff + (pstMG + pstEG) / 2;  // rough estimate
        int lazyScoreSide = white_to_move ? lazyScore : -lazyScore;
        if (std::abs(lazyScoreSide) > LAZY_THRESHOLD) {
            // Quick return with basic score
            int phase = phase_count;
            if (phase > 24) phase = 24;
            if (phase < 0) phase = 0;
            int score = (matDiff + pstMG) * phase / 24 + (matDiff + pstEG) * (24 - phase) / 24;
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
    PawnEntry& pe = thread->pawnTable[pawnIdx];

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
                sc += have_pawn(f, r + 1) ? 15 : -18;
                sc += have_pawn(f - 1, r + 1) ? 10 : -12;
                sc += have_pawn(f + 1, r + 1) ? 10 : -12;
                if (r <= 5) sc += have_pawn(f, r + 2) ? 6 : -6;
            }
        } else {
            if (r >= 1) {
                sc += have_pawn(f, r - 1) ? 15 : -18;
                sc += have_pawn(f - 1, r - 1) ? 10 : -12;
                sc += have_pawn(f + 1, r - 1) ? 10 : -12;
                if (r >= 2) sc += have_pawn(f, r - 2) ? 6 : -6;
            }
        }
        return sc;
    };

    // =================== NON-LINEAR KING DANGER ===================
    // The penalty grows quadratically with the total "attack weight."
    // This is essential: a single piece near the king is a minor nuisance,
    // but 2-3 pieces attacking the king zone is a MAJOR threat.
    // The old linear system gave ~19cp for knight+queen attacking the king,
    // which is invisible compared to a pawn advantage. The new system
    // gives ~200+cp for the same scenario.
    auto king_danger = [&](bool kingSide) -> int {
        int ks = king_square(kingSide);
        if (ks < 0) return 0;
        U64 ring = KING_ATK_BB[ks] | (1ULL << ks);
        int attackUnits = 0;
        int attackerCount = 0;

        // Enemy pieces
        U64 eKnights = kingSide ? bitboards[BKNIGHT] : bitboards[WKNIGHT];
        U64 eBishops = kingSide ? bitboards[BBISHOP] : bitboards[WBISHOP];
        U64 eRooks   = kingSide ? bitboards[BROOK]   : bitboards[WROOK];
        U64 eQueens  = kingSide ? bitboards[BQUEEN]  : bitboards[WQUEEN];
        U64 ePawns   = kingSide ? bitboards[BPAWN]   : bitboards[WPAWN];

        // Knight attacks on ring (knights are very dangerous near the king)
        {
            U64 knights = eKnights;
            while (knights) {
                int s = pop_lsb(&knights);
                if (KNIGHT_ATK_BB[s] & ring) { attackUnits += 8; attackerCount++; }
            }
        }

        // Bishop attacks on ring
        {
            U64 bishops = eBishops;
            while (bishops) {
                int s = pop_lsb(&bishops);
                if (Magic::bishopAttacks(s, occupancy) & ring) { attackUnits += 6; attackerCount++; }
            }
        }

        // Rook attacks on ring
        {
            U64 rooks = eRooks;
            while (rooks) {
                int s = pop_lsb(&rooks);
                if (Magic::rookAttacks(s, occupancy) & ring) { attackUnits += 7; attackerCount++; }
            }
        }

        // Queen attacks on ring (the most dangerous attacker)
        bool enemyHasQueen = false;
        {
            U64 queens = eQueens;
            while (queens) {
                int s = pop_lsb(&queens);
                U64 qatk = Magic::bishopAttacks(s, occupancy) | Magic::rookAttacks(s, occupancy);
                if (qatk & ring) { attackUnits += 12; attackerCount++; }
                enemyHasQueen = true;
            }
        }

        // Pawn attacks on ring
        {
            U64 pawnAtk = kingSide
                ? ((ePawns >> 7) & ~0x0101010101010101ULL) | ((ePawns >> 9) & ~0x8080808080808080ULL)
                : ((ePawns << 7) & ~0x8080808080808080ULL) | ((ePawns << 9) & ~0x0101010101010101ULL);
            int pawnPress = popcount(pawnAtk & ring);
            if (pawnPress > 0) { attackUnits += pawnPress * 3; attackerCount++; }
        }

        // Multiple attacker synergy bonus:
        // 2 attackers = +6 bonus, 3 attackers = +16, 4+ = +30
        // This is CRITICAL: a single piece isn't very dangerous,
        // but coordinated attackers are devastating.
        if (attackerCount >= 2) attackUnits += (attackerCount - 1) * 6;
        if (attackerCount >= 3) attackUnits += (attackerCount - 2) * 4;

        // If no queen involvement, reduce danger significantly
        // (attacks without a queen are rarely decisive)
        if (!enemyHasQueen && attackerCount > 0) {
            attackUnits = attackUnits * 2 / 3;
        }

        // Weak pawn shelter adds to attack weight
        // Check the 3 files around the king for missing shelter pawns
        {
            int kf = file_of(ks);
            int kr = rank_of(ks);
            U64 myPawns = kingSide ? wp : bp;
            int shelterMissing = 0;

            for (int ff = std::max(0, kf - 1); ff <= std::min(7, kf + 1); ff++) {
                U64 filePawns = myPawns & file_mask(ff);
                bool hasShelter = false;
                if (kingSide) {
                    // White: look for pawns on ranks above king (shield)
                    for (int rr = kr + 1; rr <= std::min(kr + 2, 7); rr++) {
                        if (filePawns & (1ULL << make_sq(ff, rr))) { hasShelter = true; break; }
                    }
                } else {
                    // Black: look for pawns on ranks below king (shield)
                    for (int rr = kr - 1; rr >= std::max(kr - 2, 0); rr--) {
                        if (filePawns & (1ULL << make_sq(ff, rr))) { hasShelter = true; break; }
                    }
                }
                if (!hasShelter) shelterMissing++;
            }
            // Each missing shelter pawn adds attack weight (more openings = more danger)
            attackUnits += shelterMissing * 3;
        }

        // NON-LINEAR penalty: quadratic scaling
        // Examples with this formula (attackUnits^2 / 3):
        //   1 minor piece near king (units ~8):  penalty = 21cp  -- small, as expected
        //   1 queen near king (units ~12):       penalty = 48cp  -- moderate
        //   knight + queen (units ~26):          penalty = 225cp -- SERIOUS danger
        //   3 pieces (units ~35):                penalty = 408cp -- overwhelming
        int penalty = std::min(700, (attackUnits * attackUnits) / 3);

        return penalty;
    };

    // Pawn shield: linear component (separate from non-linear danger)
    ksMG += pawn_shield_score(true);
    ksMG -= pawn_shield_score(false);

    // King danger: non-linear, quadratic penalty for concentrated attacks
    ksMG -= king_danger(true);   // Penalty on White's king (bad for White)
    ksMG += king_danger(false);  // Penalty on Black's king (bad for Black = good for White)

    // Endgame king mobility (active king is important in endgames)
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

    int phase = phase_count;
    if (phase > 24) phase = 24;
    if (phase < 0) phase = 0;
    int score = (scMG * phase + scEG * (24 - phase)) / 24;

    // 50-move rule damping
    score -= score * halfmove / 212;

    return white_to_move ? score : -score;
}
