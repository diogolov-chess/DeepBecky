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

// types.h - Fundamental types and constants
#ifndef DEEPBECKY_TYPES_H
#define DEEPBECKY_TYPES_H

#include <cstdint>
#include <cstring>
#include <string>
#include <limits>

// ========================= Basic types =========================
using U64 = uint64_t;

// ========================= Global constants =========================
constexpr int INF_SCORE   = 30000;
constexpr int MATE_SCORE  = 29000;
constexpr int MATE_IN_MAX = 28000;
constexpr int MAX_PLY     = 64;
constexpr int MAX_MOVES   = 256;
constexpr int MAX_STACK   = 4096;
constexpr int DRAW_REJECT_MARGIN = 50;      // centipawns threshold to decline draw when better
constexpr int DRAW_DECLINE_PENALTY = 10000; // MASSIVE penalty for unwanted draw

// ========================= Pieces =========================
enum Piece {
    EMPTY = 0,
    WPAWN = 1, WKNIGHT = 2, WBISHOP = 3, WROOK = 4, WQUEEN = 5, WKING = 6,
    BPAWN = 7, BKNIGHT = 8, BBISHOP = 9, BROOK = 10, BQUEEN = 11, BKING = 12,
    PIECE_NB = 13
};

// ========================= Colors =========================
enum Color { WHITE = 0, BLACK = 1, COLOR_NB = 2 };

inline constexpr Color operator~(Color c) { return Color(c ^ 1); }

// ========================= Piece functions =========================
inline bool isWhitePiece(int p) { return p >= WPAWN && p <= WKING; }
inline bool isBlackPiece(int p) { return p >= BPAWN && p <= BKING; }
inline int  pieceColor(int p) { 
    if (p == EMPTY) return -1; 
    return isWhitePiece(p) ? WHITE : BLACK; 
}
inline Piece makePiece(Color c, int type) {
    return Piece(type + (c == BLACK ? 6 : 0));
}

// ========================= Squares =========================
enum Square : int {
    SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
    SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
    SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
    SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
    SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
    SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
    SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
    SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
    SQ_NONE = 64, SQUARE_NB = 64
};

inline int sq(int file, int rank) { return rank * 8 + file; }
inline int sq_file(int s) { return s & 7; }
inline int sq_rank(int s) { return s >> 3; }
inline int sq_x(int s) { return s & 7; }  // Alias
inline int sq_y(int s) { return s >> 3; }  // Alias
inline bool isLightSquare(int s) { return ((sq_file(s) + sq_rank(s)) & 1) == 0; }
inline bool onBoard(int x, int y) { return x >= 0 && x < 8 && y >= 0 && y < 8; }
inline int flipRank(int s) { return s ^ 56; }  // Flip vertically

// ========================= Moves =========================
struct Move {
    uint16_t squares = 0;  // bits 0-5: from, 6-11: to
    uint8_t  flags = 0;    // bits 0-3: flags, 4-7: promotion
    int score = 0;

    bool operator==(const Move& o) const {
        return squares == o.squares && flags == o.flags;
    }
    bool operator!=(const Move& o) const {
        return !(*this == o);
    }
};

inline constexpr Move MOVE_NONE{};

// Move flags
constexpr uint8_t MOVE_FLAG_CAPTURE    = 1u << 0;
constexpr uint8_t MOVE_FLAG_ENPASSANT  = 1u << 1;
constexpr uint8_t MOVE_FLAG_CASTLE     = 1u << 2;
constexpr uint8_t MOVE_FLAG_DOUBLEPUSH = 1u << 3;
constexpr uint8_t MOVE_PROMO_SHIFT     = 4u;
constexpr uint8_t MOVE_PROMO_MASK      = 0xF0u;

// Move functions
inline int moveFrom(const Move& m) { return m.squares & 63; }
inline int moveTo(const Move& m) { return (m.squares >> 6) & 63; }
inline bool moveIsCapture(const Move& m) { return (m.flags & MOVE_FLAG_CAPTURE) != 0; }
inline bool moveIsEnPassant(const Move& m) { return (m.flags & MOVE_FLAG_ENPASSANT) != 0; }
inline bool moveIsCastle(const Move& m) { return (m.flags & MOVE_FLAG_CASTLE) != 0; }
inline bool moveIsDoublePush(const Move& m) { return (m.flags & MOVE_FLAG_DOUBLEPUSH) != 0; }
inline int movePromotion(const Move& m) { return (m.flags & MOVE_PROMO_MASK) >> MOVE_PROMO_SHIFT; }
inline bool moveIsNone(const Move& m) { return m.squares == 0 && m.flags == 0; }

inline Move makeMove(int from, int to, uint8_t flags = 0) {
    Move m;
    m.squares = static_cast<uint16_t>(from | (to << 6));
    m.flags = flags;
    m.score = 0;
    return m;
}

inline Move makeMovePacked(uint16_t data, uint8_t flags) {
    Move m;
    m.squares = data;
    m.flags = flags;
    m.score = 0;
    return m;
}

// ========================= Piece values =========================
constexpr int PIECE_VALUE[PIECE_NB] = {
    0, 100, 320, 330, 500, 900, 20000, 100, 320, 330, 500, 900, 20000
};

// Simplified values by type (0=pawn, 1=knight, etc.)
constexpr int PIECE_TYPE_VALUE[6] = { 100, 320, 330, 500, 900, 20000 };

// ========================= Directions =========================
constexpr int NORTH = 8;
constexpr int SOUTH = -8;
constexpr int EAST = 1;
constexpr int WEST = -1;
constexpr int NORTH_EAST = 9;
constexpr int NORTH_WEST = 7;
constexpr int SOUTH_EAST = -7;
constexpr int SOUTH_WEST = -9;

// Directions: N, NE, E, SE, S, SW, W, NW
constexpr int DIR_DX[8] = { 0,  1,  1,  1,  0, -1, -1, -1};
constexpr int DIR_DY[8] = { 1,  1,  0, -1, -1, -1,  0,  1};

// ========================= Castling =========================
constexpr int CASTLING_K = 8;  // White kingside
constexpr int CASTLING_Q = 4;  // White queenside
constexpr int CASTLING_k = 2;  // Black kingside
constexpr int CASTLING_q = 1;  // Black queenside

// TT flags moved to tt.h

#endif // DEEPBECKY_TYPES_H
