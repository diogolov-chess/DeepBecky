/*
 * This file is part of Deep Becky 1.1 - A UCI Chess Engine written by AI
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

#ifndef DeepBecky_ENGINE_H
#define DeepBecky_ENGINE_H

#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <array>
#include <unordered_map>
#include <chrono>
#include <random>
#include <cstdint>
#include <cstring>
#include <limits>
#include <cctype>

// ===== Bitboards helpers (runtime magic) =====
#include "magic.h"
using U64 = uint64_t;

// Funções utilitárias para bitboards
inline void set_bit(U64 &bb, int sq) { bb |= (1ULL << sq); }
inline void pop_bit(U64 &bb, int sq) { bb &= ~(1ULL << sq); }
inline int get_bit(U64 bb, int sq) { return (bb >> sq) & 1; }

inline int lsb_index(U64 b){
#if defined(_MSC_VER)
    unsigned long idx; _BitScanForward64(&idx, b); return (int)idx;
#else
    return __builtin_ctzll(b);
#endif
}

// Pop LSB e retorna o índice
static inline int pop_lsb(U64 *bb) {
    int sq = lsb_index(*bb);
    *bb &= *bb - 1;
    return sq;
}

using namespace std;

// ========================= Identidade =========================
extern const string ENGINE_NAME;
extern const string ENGINE_VERSION;
extern const string ENGINE_AUTHOR;

// ========================= Constantes globais =========================
constexpr int INF_SCORE   = 30000;
constexpr int MATE_SCORE  = 29000;
constexpr int MATE_IN_MAX = 28000;
constexpr int MAX_PLY     = 64;
constexpr int TT_SIZE     = 1 << 22; // ~4M entradas
constexpr int MAX_MOVES   = 256;
constexpr int MAX_STACK   = 4096;
constexpr int DRAW_REJECT_MARGIN = 50; // centipawns threshold to decline draw when better
constexpr int DRAW_DECLINE_PENALTY = 10000; // MASSIVE penalty for unwanted draw

// ========================= Peças =========================
enum Piece {
    EMPTY=0,
    WPAWN=1, WKNIGHT=2, WBISHOP=3, WROOK=4, WQUEEN=5, WKING=6,
    BPAWN=7, BKNIGHT=8, BBISHOP=9, BROOK=10, BQUEEN=11, BKING=12
};

// Cores para indexação
enum Color { WHITE=0, BLACK=1 };

inline bool isWhitePiece(int p){ return p>=WPAWN && p<=WKING; }
inline bool isBlackPiece(int p){ return p>=BPAWN && p<=BKING; }
inline int  pieceColor(int p){ if(p==EMPTY) return -1; return isWhitePiece(p)?WHITE:BLACK; }

// ========================= Movimentos =========================
struct Move {
    uint16_t squares = 0; // bits 0-5: origem, 6-11: destino
    uint8_t  flags   = 0; // bits 0-3: flags, 4-7: promoção (piece code)
    int score = 0;

    bool operator==(const Move& o) const {
        return squares == o.squares && flags == o.flags;
    }
};
inline constexpr Move MOVE_NONE{};

constexpr uint8_t MOVE_FLAG_CAPTURE    = 1u << 0;
constexpr uint8_t MOVE_FLAG_ENPASSANT  = 1u << 1;
constexpr uint8_t MOVE_FLAG_CASTLE     = 1u << 2;
constexpr uint8_t MOVE_FLAG_DOUBLEPUSH = 1u << 3;
constexpr uint8_t MOVE_PROMO_SHIFT     = 4u;
constexpr uint8_t MOVE_PROMO_MASK      = 0xF0u;

inline int moveFrom(const Move& m){ return m.squares & 63; }
inline int moveTo(const Move& m){ return (m.squares >> 6) & 63; }
inline bool moveIsCapture(const Move& m){ return (m.flags & MOVE_FLAG_CAPTURE) != 0; }
inline bool moveIsEnPassant(const Move& m){ return (m.flags & MOVE_FLAG_ENPASSANT) != 0; }
inline bool moveIsCastle(const Move& m){ return (m.flags & MOVE_FLAG_CASTLE) != 0; }
inline bool moveIsDoublePush(const Move& m){ return (m.flags & MOVE_FLAG_DOUBLEPUSH) != 0; }
inline int movePromotion(const Move& m){ return (m.flags & MOVE_PROMO_MASK) >> MOVE_PROMO_SHIFT; }
inline bool moveIsNone(const Move& m){ return m.squares == 0 && m.flags == 0; }
inline Move makeMovePacked(uint16_t data, uint8_t flags){ Move m; m.squares=data; m.flags=flags; m.score=0; return m; }

constexpr uint8_t TT_GEN_BITS = 6;
constexpr uint8_t TT_FLAG_BITS = 2;
constexpr uint8_t TT_FLAG_MASK = (1u << TT_FLAG_BITS) - 1u;
constexpr uint8_t TT_GEN_MASK  = ((1u << TT_GEN_BITS) - 1u) << TT_FLAG_BITS;

// ========================= Zobrist =========================
struct Zobrist {
    uint64_t piece[13][64]{};
    uint64_t side=0, castling[16]{}, ep[9]{};
    Zobrist();
};
extern Zobrist ZOB;

// ========================= TT =========================
enum TTFlag { TT_EXACT=0, TT_ALPHA=1, TT_BETA=2 };
struct TTEntry {
    uint64_t key=0;
    uint16_t moveData=0;
    uint8_t  moveFlags=0;
    int16_t  score=0;
    int8_t   depth=0;
    uint8_t  genBound=0;
    uint8_t  pad=0;

    Move bestMove() const { return makeMovePacked(moveData, moveFlags); }
    uint8_t flag() const { return genBound & TT_FLAG_MASK; }
    uint8_t generation() const { return genBound >> TT_FLAG_BITS; }
    void store(uint64_t newKey, int newDepth, int newScore, uint8_t newFlag, const Move& move, uint8_t generation) {
        key = newKey;
        depth = static_cast<int8_t>(newDepth);
        score = static_cast<int16_t>(newScore);
        moveData = move.squares;
        moveFlags = move.flags;
        genBound = static_cast<uint8_t>((generation << TT_FLAG_BITS) | (newFlag & TT_FLAG_MASK));
    }
};
extern TTEntry TT[TT_SIZE];
extern uint8_t TTGeneration;

// ========================= Heurísticas =========================
struct KillerTable {
    Move killer[2][MAX_PLY];
    void clear(){ memset(killer,0,sizeof(killer)); }
};
extern KillerTable killers;

extern int history_heur[2][64][64];

// ========================= Utilidades =========================
inline int sq(int x,int y){ return y*8 + x; }
inline int sq_x(int s){ return s % 8; }
inline int sq_y(int s){ return s / 8; }
inline bool isLightSquare(int s){ return ((sq_x(s) + sq_y(s)) & 1) == 0; }
inline bool onBoard(int x,int y){ return x>=0 && x<8 && y>=0 && y<8; }

// ========================= Avaliação =========================
constexpr int PIECE_VALUE[13] = {
    0, 100, 320, 330, 500, 900, 20000, 100, 320, 330, 500, 900, 20000
};

// ===== Pré-computação de ataques (não-deslizantes) =====
extern U64 KNIGHT_ATK_BB[64];
extern U64 KING_ATK_BB[64];
extern U64 WPAWN_ATK_BB[64];
extern U64 BPAWN_ATK_BB[64];
void initAttackTables();


// ========================= Engine principal =========================
class DeepBeckyEngine {
public:
    U64 bitboards[13]{};        // Bitboard para cada tipo de peça
    U64 color_bitboards[2]{};   // Bitboard para WHITE e BLACK
    int piece_board[64]{};      // Mailbox para O(1) lookup de peça

    bool white_to_move=true;
    int castling=0b1111; // KQkq
    int ep_file=0;       // 1..8 se existe EP
    int king_sq[2]{4,60}; // posições dos reis: sq(e1)=4, sq(e8)=60
    int halfmove=0, fullmove=1;
    uint64_t hash=0;

    struct RepState {
        uint64_t key = 0;
        int repetition = 0;
    };
    std::vector<RepState> repetitionHistory;
    int plies_since_null = 0;

    // Search state
    int contempt = 0;  // Contempt value (from White's perspective)
    bool rootSideIsWhite = true;  // Side to move at root of search

    // Search
    long long nodes=0; // Alterado para long long para evitar overflow
    bool stop=false;
    chrono::high_resolution_clock::time_point start_time;
    int time_limit_ms=0;

    vector<string> uci_history;
    unordered_map<string, vector<string>> opening_book;

    struct Undo {
        int castling_before = 0;
        int ep_before = 0;
        int half_before = 0;
        int fullmove_before = 0;
        uint64_t hash_before = 0;
        int captured_piece = EMPTY;
        int moved_piece = EMPTY;
        size_t repIndexBefore = 0;
        int repetition_before = 0;
        int plies_from_null_before = 0;
        bool was_null = false;
    };
    Undo undoStack[MAX_STACK];
    int undoTop = 0;

    DeepBeckyEngine();

    void run();
    void setStartPos();
    bool setFEN(const string &fen);

    int generateLegal(Move* moves);
    int generatePseudo(Move* moves, bool capturesOnly=false);
    bool isAttacked(int s, bool byWhite) const;
    bool inCheck(bool whiteSide) const;
    void makeMove(const Move& m);
    void undoMove(const Move& m);
    void makeNullMove();
    void undoNullMove();
    bool legalMove(const Move& m);

    Move search(int maxDepth, int timeMs);
    int pvs(int depth, int ply, int alpha, int beta);
    int qsearch(int alpha, int beta, int ply);

    int evaluate();
    int see(const Move& m) const;

    std::vector<Move> getPV(int maxDepth);
    std::string pvToString(const std::vector<Move>& pv);

    string moveToUCI(const Move& m) const;
    Move uciToMove(const string& s);
    uint64_t computeHash() const;
    void clearTT();
    void clearHeuristics(){ memset(history_heur,0,sizeof(history_heur)); killers.clear(); }
    string bookKey() const;
    bool timeUp() const;
    void initBook();
    bool isFiftyMoveDraw() const;
    bool isThreefoldRepetition() const;
    bool isThreefoldRepetition(int ply) const;
    bool isInsufficientMaterial() const;
    bool isDraw(int ply);
    bool hasGameCycle(int ply) const;
};

#endif // DeepBecky_ENGINE_H