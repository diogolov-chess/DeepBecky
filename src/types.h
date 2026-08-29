#ifndef DEEPBECKY_TYPES_H
#define DEEPBECKY_TYPES_H

#include <cstdint>
#include <cstring>
#include <string>
#include <limits>
#include <algorithm>

// ============================================================================
// Basic Types
// ============================================================================
using U64 = uint64_t;

// ============================================================================
// Global Search Constants
// ============================================================================
constexpr int INF_SCORE   = 30000;
constexpr int MATE_SCORE  = 29000;
constexpr int MATE_IN_MAX = 28000;
constexpr int MAX_PLY     = 64;
constexpr int MAX_MOVES   = 256;
constexpr int MAX_STACK   = 4096;

// ============================================================================
// Pieces & Colors
// ============================================================================
enum Piece {
    EMPTY = 0,
    WPAWN = 1, WKNIGHT = 2, WBISHOP = 3, WROOK = 4, WQUEEN = 5, WKING = 6,
    BPAWN = 7, BKNIGHT = 8, BBISHOP = 9, BROOK = 10, BQUEEN = 11, BKING = 12,
    PIECE_NB = 13
};

enum Color { WHITE = 0, BLACK = 1, COLOR_NB = 2 };

inline constexpr Color operator~(Color c) { return Color(c ^ 1); }

inline bool isWhitePiece(int p) { return p >= WPAWN && p <= WKING; }
inline bool isBlackPiece(int p) { return p >= BPAWN && p <= BKING; }
inline int  pieceColor(int p) { 
    if (p == EMPTY) return -1; 
    return isWhitePiece(p) ? WHITE : BLACK; 
}
inline Piece makePiece(Color c, int type) {
    return Piece(type + (c == BLACK ? 6 : 0));
}

// ============================================================================
// Squares & Coordinates
// ============================================================================
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
inline int sq_x(int s) { return s & 7; }
inline int sq_y(int s) { return s >> 3; }
inline bool isLightSquare(int s) { return ((sq_file(s) + sq_rank(s)) & 1) == 0; }
inline bool onBoard(int x, int y) { return x >= 0 && x < 8 && y >= 0 && y < 8; }
inline int flipRank(int s) { return s ^ 56; }

// ============================================================================
// Move Representation
// ============================================================================
enum MoveType : uint16_t {
    MOVE_TYPE_QUIET = 0,
    MOVE_TYPE_DOUBLE_PUSH = 1,
    MOVE_TYPE_CASTLE = 2,
    MOVE_TYPE_EP = 3,
    MOVE_TYPE_CAPTURE = 4,

    // Promotions
    MOVE_TYPE_PROMO_KNIGHT = 8,
    MOVE_TYPE_PROMO_BISHOP = 9,
    MOVE_TYPE_PROMO_ROOK = 10,
    MOVE_TYPE_PROMO_QUEEN = 11,

    // Promo Captures
    MOVE_TYPE_PROMO_CAPT_KNIGHT = 12,
    MOVE_TYPE_PROMO_CAPT_BISHOP = 13,
    MOVE_TYPE_PROMO_CAPT_ROOK = 14,
    MOVE_TYPE_PROMO_CAPT_QUEEN = 15
};

struct Move {
    uint16_t data = 0; // bits 0-5: from, 6-11: to, 12-15: MoveType

    bool operator==(const Move& o) const { return data == o.data; }
    bool operator!=(const Move& o) const { return data != o.data; }
};

inline constexpr Move MOVE_NONE{};

constexpr MoveType MOVE_FLAG_CAPTURE    = MOVE_TYPE_CAPTURE;
constexpr MoveType MOVE_FLAG_ENPASSANT  = MOVE_TYPE_EP;
constexpr MoveType MOVE_FLAG_CASTLE     = MOVE_TYPE_CASTLE;
constexpr MoveType MOVE_FLAG_DOUBLEPUSH = MOVE_TYPE_DOUBLE_PUSH;

inline MoveType makePromoMoveType(int pt) { return static_cast<MoveType>(8 + (pt % 6) - 2); }
inline MoveType makePromoCaptureMoveType(int pt) { return static_cast<MoveType>(12 + (pt % 6) - 2); }

inline int moveFrom(const Move& m) { return m.data & 0x3F; }
inline int moveTo(const Move& m) { return (m.data >> 6) & 0x3F; }
inline MoveType moveTypeOf(const Move& m) { return static_cast<MoveType>((m.data >> 12) & 0xF); }

inline bool moveIsCapture(const Move& m) { 
    MoveType t = moveTypeOf(m);
    return t == MOVE_TYPE_CAPTURE || t == MOVE_TYPE_EP || t >= MOVE_TYPE_PROMO_CAPT_KNIGHT;
}
inline bool moveIsEnPassant(const Move& m) { return moveTypeOf(m) == MOVE_TYPE_EP; }
inline bool moveIsCastle(const Move& m) { return moveTypeOf(m) == MOVE_TYPE_CASTLE; }
inline bool moveIsDoublePush(const Move& m) { return moveTypeOf(m) == MOVE_TYPE_DOUBLE_PUSH; }
inline bool moveIsPromotion(const Move& m) { return moveTypeOf(m) >= MOVE_TYPE_PROMO_KNIGHT; }
inline int movePromotionType(const Move& m) {
    if (!moveIsPromotion(m)) return 0;
    return (moveTypeOf(m) & 3) + 2; 
}
inline bool moveIsNone(const Move& m) { return m.data == 0; }

inline Move makeMove(int from, int to, MoveType type = MOVE_TYPE_QUIET) {
    Move m;
    m.data = static_cast<uint16_t>(from | (to << 6) | (type << 12));
    return m;
}

// ============================================================================
// Piece Values & Geometry
// ============================================================================
constexpr int PIECE_VALUE[PIECE_NB] = {
    0, 100, 320, 330, 500, 900, 20000, 100, 320, 330, 500, 900, 20000
};

constexpr int PIECE_TYPE_VALUE[6] = { 100, 320, 330, 500, 900, 20000 };

constexpr int NORTH = 8;
constexpr int SOUTH = -8;
constexpr int EAST = 1;
constexpr int WEST = -1;
constexpr int NORTH_EAST = 9;
constexpr int NORTH_WEST = 7;
constexpr int SOUTH_EAST = -7;
constexpr int SOUTH_WEST = -9;

constexpr int DIR_DX[8] = { 0,  1,  1,  1,  0, -1, -1, -1};
constexpr int DIR_DY[8] = { 1,  1,  0, -1, -1, -1,  0,  1};

// Castling Rights Bitmasks
constexpr int CASTLING_K = 8;  // White kingside
constexpr int CASTLING_Q = 4;  // White queenside
constexpr int CASTLING_k = 2;  // Black kingside
constexpr int CASTLING_q = 1;  // Black queenside

// Move Generation Types
enum GenType {
    GEN_ALL,       // All legal moves
    GEN_CAPTURES,  // Captures and promotions only
    GEN_QUIETS,    // Non-captures only
    GEN_EVASIONS   // Check evasions
};

#endif // DEEPBECKY_TYPES_H
