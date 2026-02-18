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
    int from_x=0, from_y=0, to_x=0, to_y=0;
    int promotion=0;
    bool is_capture=false, is_enpassant=false, is_castle=false, is_doublepush=false;
    int score=0;

    // Adicionado para facilitar o makeMove/undoMove
    int piece_moved=EMPTY;
    int captured_piece=EMPTY;

    bool operator==(const Move& o) const {
        return from_x==o.from_x && from_y==o.from_y && to_x==o.to_x && to_y==o.to_y &&
               promotion==o.promotion;
    }
};
static const Move MOVE_NONE;

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
    uint64_t key;
    int16_t  score;
    int8_t   depth;
    int8_t   flag;
    Move     best;
};
extern TTEntry TT[TT_SIZE];

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
    // ==== NOVA REPRESENTAÇÃO DE TABULEIRO (FULL BITBOARD) ====
    U64 bitboards[13]{};        // Bitboard para cada tipo de peça
    U64 color_bitboards[2]{};   // Bitboard para WHITE e BLACK
    int piece_board[64]{};      // Mailbox para O(1) lookup de peça

    bool white_to_move=true;
    int castling=0b1111; // KQkq
    int ep_file=0;       // 1..8 se existe EP
    int king_sq[2]{4,60}; // posições dos reis: sq(e1)=4, sq(e8)=60
    int halfmove=0, fullmove=1;
    uint64_t hash=0;

    // Search
    long long nodes=0; // Alterado para long long para evitar overflow
    bool stop=false;
    chrono::high_resolution_clock::time_point start_time;
    int time_limit_ms=0;

    vector<string> uci_history;
    unordered_map<string, vector<string>> opening_book;

    struct Undo {
        int castling_before, ep_before, half_before;
        uint64_t hash_before;
    };
    vector<Undo> undo;

    DeepBeckyEngine();

    void run();
    void setStartPos();
    void setFEN(const string &fen);

    vector<Move> generateLegal();
    vector<Move> generatePseudo(bool capturesOnly=false);
    bool isAttacked(int s, bool byWhite);
    bool inCheck(bool whiteSide);
    void makeMove(const Move& m);
    void undoMove(const Move& m);
    void makeNullMove();
    void undoNullMove();
    bool legalMove(const Move& m);

    Move search(int maxDepth, int timeMs);
    int pvs(int depth, int ply, int alpha, int beta);
    int qsearch(int alpha, int beta, int ply);

    void scoreMoves(vector<Move>& mv, const Move& ttMove, int ply);
    int evaluate();

    std::vector<Move> getPV(int maxDepth);
    std::string pvToString(const std::vector<Move>& pv);

    string moveToUCI(const Move& m) const;
    Move uciToMove(const string& s);
    uint64_t computeHash() const;
    void clearTT(){ for(int i=0;i<TT_SIZE;i++) TT[i]=TTEntry(); }
    void clearHeuristics(){ memset(history_heur,0,sizeof(history_heur)); killers.clear(); }
    string bookKey() const;
    bool timeUp() const;
    void initBook();
};

#endif // DeepBecky_ENGINE_H
