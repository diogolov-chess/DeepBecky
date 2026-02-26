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

// position.h - Board representation
#ifndef DEEPBECKY_POSITION_H
#define DEEPBECKY_POSITION_H

#include "types.h"
#include "bitboard.h"
#include <vector>
#include <string>
#include <chrono>
#include <unordered_map>

// Forward declarations
struct TTEntry;

// ========================= Zobrist =========================
struct Zobrist {
    uint64_t piece[PIECE_NB][64]{};
    uint64_t side = 0;
    uint64_t castling[16]{};
    uint64_t ep[9]{};
    Zobrist();
};
extern Zobrist ZOB;

// ========================= Hash Tables =========================
constexpr int PAWN_TT_SIZE = 1 << 16;      // 64K entries for pawn structure cache
constexpr int MATERIAL_TT_SIZE = 1 << 14;  // 16K entries for material config cache

struct PawnEntry {
    uint64_t key = 0;
    int16_t scoreMG = 0;
    int16_t scoreEG = 0;
};
extern PawnEntry pawnTable[PAWN_TT_SIZE];

struct MaterialEntry {
    uint64_t key = 0;
    int16_t scoreMG = 0;
    int16_t scoreEG = 0;
    int8_t phase = 0;
    int8_t flags = 0;  // bit 0: white bishop pair, bit 1: black bishop pair
};
extern MaterialEntry materialTable[MATERIAL_TT_SIZE];

// ========================= Heuristics =========================
struct KillerTable {
    Move killer[2][MAX_PLY];
    void clear() { std::memset(killer, 0, sizeof(killer)); }
};
extern KillerTable killers;
extern int history_heur[2][64][64];

// ========================= Position =========================
class Position {
public:
    // Bitboards
    U64 bitboards[PIECE_NB]{};      // Bitboard per piece type
    U64 color_bitboards[COLOR_NB]{}; // Bitboard per color
    int piece_board[64]{};           // Mailbox lookup

    // Game state
    bool white_to_move = true;
    int castling = 0b1111;  // KQkq
    int ep_file = 0;        // 1..8 if EP exists
    int king_sq[COLOR_NB]{4, 60};
    int halfmove = 0;
    int fullmove = 1;
    uint64_t hash = 0;
    uint64_t pawnKey = 0;
    uint64_t materialKey = 0;

    // Repetition history
    struct RepState {
        uint64_t key = 0;
        int repetition = 0;
    };
    std::vector<RepState> repetitionHistory;
    int plies_since_null = 0;

    // Search state
    int contempt = 0;
    bool rootSideIsWhite = true;
    long long nodes = 0;
    int selDepth = 0;
    bool stopSearching = false;
    std::chrono::high_resolution_clock::time_point start_time;
    int time_limit_ms = 0;
    
    // Search tables
    int evalStack[MAX_PLY]{};
    std::unordered_map<uint32_t, Move> counterMoves;

    // Book/History
    std::vector<std::string> uci_history;
    std::unordered_map<std::string, std::vector<std::string>> opening_book;

    // Undo stack
    struct Undo {
        int castling_before = 0;
        int ep_before = 0;
        int half_before = 0;
        int fullmove_before = 0;
        uint64_t hash_before = 0;
        uint64_t pawnKey_before = 0;
        int captured_piece = EMPTY;
        int moved_piece = EMPTY;
        size_t repIndexBefore = 0;
        int repetition_before = 0;
        int plies_from_null_before = 0;
        bool was_null = false;
    };
    Undo undoStack[MAX_STACK];
    int undoTop = 0;

    // Constructor
    Position();

    // Setup
    void setStartPos();
    bool setFEN(const std::string& fen);
    uint64_t computeHash() const;

    // Move generation
    int generateLegal(Move* moves);
    int generatePseudo(Move* moves, bool capturesOnly = false);

    // Attack detection
    bool isAttacked(int s, bool byWhite) const;
    bool inCheck(bool whiteSide) const;
    U64 attackersTo(int sq, U64 occ) const;
    U64 checkersBB(bool whiteSide) const;
    U64 pinnedBB(bool whiteSide) const;
    U64 blockersForKing(bool whiteSide, U64& pinners) const;

    // Move application
    void makeMove(const Move& m);
    void undoMove(const Move& m);
    void makeNullMove();
    void undoNullMove();
    bool legalMove(const Move& m);

    // Search interface
    Move search(int maxDepth, int timeMs);
    int pvs(int depth, int alpha, int beta, int ply);
    int qsearch(int alpha, int beta, int ply);

    // Evaluation
    int evaluate();
    int see(const Move& m) const;
    bool SEE(const Move& m, int threshold) const;
    bool hasNonPawnMaterial(bool white) const;

    // PV extraction
    std::vector<Move> getPV(int maxDepth);
    std::string pvToString(const std::vector<Move>& pv);

    // UCI helpers
    std::string moveToUCI(const Move& m) const;
    Move uciToMove(const std::string& s);

    // TT/Heuristics
    void clearTT();
    void clearHeuristics() {
        std::memset(history_heur, 0, sizeof(history_heur));
        killers.clear();
    }

    // Time management
    bool timeUp() const;

    // Draw detection
    bool isFiftyMoveDraw() const;
    bool isThreefoldRepetition() const;
    bool isThreefoldRepetition(int ply) const;
    bool isInsufficientMaterial() const;
    bool isDraw(int ply);
    bool hasGameCycle(int ply) const;

    // Book
    void initBook();
    std::string bookKey() const;

    // Helpers
    Color sideToMove() const { return white_to_move ? WHITE : BLACK; }
    U64 pieces() const { return color_bitboards[WHITE] | color_bitboards[BLACK]; }
    U64 pieces(Color c) const { return color_bitboards[c]; }
    U64 pieces(Piece p) const { return bitboards[p]; }
    int pieceOn(int sq) const { return piece_board[sq]; }
};

// Alias for compatibility
using DeepBeckyEngine = Position;

#endif // DEEPBECKY_POSITION_H
