#include "bitboard.h"
#include "magic.h"

// ============================================================================
// Global Attack and Ray Tables
// ============================================================================
U64 KNIGHT_ATK_BB[64];
U64 KING_ATK_BB[64];
U64 WPAWN_ATK_BB[64];
U64 BPAWN_ATK_BB[64];
U64 BETWEEN_BB[64][64];
U64 LINE_BB[64][64];
U64 RAY_BB[64][8];

// ============================================================================
// Initialization
// ============================================================================
void initAttackTables() {
    for (int s = 0; s < 64; ++s) {
        KNIGHT_ATK_BB[s] = 0;
        KING_ATK_BB[s] = 0;
        WPAWN_ATK_BB[s] = 0;
        BPAWN_ATK_BB[s] = 0;
        
        int x = sq_x(s), y = sq_y(s);

        // Knight attacks
        const int KNDX[8] = {1, 2, 2, 1, -1, -2, -2, -1};
        const int KNDY[8] = {2, 1, -1, -2, -2, -1, 1, 2};
        for (int i = 0; i < 8; i++) {
            int nx = x + KNDX[i], ny = y + KNDY[i];
            if (onBoard(nx, ny)) set_bit(KNIGHT_ATK_BB[s], sq(nx, ny));
        }

        // King attacks
        const int KGDX[8] = {1, 1, 1, 0, 0, -1, -1, -1};
        const int KGDY[8] = {1, 0, -1, 1, -1, 1, 0, -1};
        for (int i = 0; i < 8; i++) {
            int nx = x + KGDX[i], ny = y + KGDY[i];
            if (onBoard(nx, ny)) set_bit(KING_ATK_BB[s], sq(nx, ny));
        }

        // Pawn attacks
        if (onBoard(x - 1, y + 1)) set_bit(WPAWN_ATK_BB[s], sq(x - 1, y + 1));
        if (onBoard(x + 1, y + 1)) set_bit(WPAWN_ATK_BB[s], sq(x + 1, y + 1));
        if (onBoard(x - 1, y - 1)) set_bit(BPAWN_ATK_BB[s], sq(x - 1, y - 1));
        if (onBoard(x + 1, y - 1)) set_bit(BPAWN_ATK_BB[s], sq(x + 1, y - 1));
    }
}

void initRayTables() {
    std::memset(BETWEEN_BB, 0, sizeof(BETWEEN_BB));
    std::memset(LINE_BB, 0, sizeof(LINE_BB));
    std::memset(RAY_BB, 0, sizeof(RAY_BB));

    // Build RAY_BB for each square and direction
    for (int s = 0; s < 64; ++s) {
        int x = sq_x(s), y = sq_y(s);
        for (int d = 0; d < 8; ++d) {
            U64 ray = 0;
            int nx = x + DIR_DX[d];
            int ny = y + DIR_DY[d];
            while (onBoard(nx, ny)) {
                set_bit(ray, sq(nx, ny));
                nx += DIR_DX[d];
                ny += DIR_DY[d];
            }
            RAY_BB[s][d] = ray;
        }
    }

    // Build BETWEEN_BB and LINE_BB for all square pairs
    for (int s1 = 0; s1 < 64; ++s1) {
        for (int s2 = 0; s2 < 64; ++s2) {
            if (s1 == s2) continue;

            int x1 = sq_x(s1), y1 = sq_y(s1);
            int x2 = sq_x(s2), y2 = sq_y(s2);
            int dx = x2 - x1;
            int dy = y2 - y1;

            bool sameLine = false;
            int stepX = 0, stepY = 0;

            if (dx == 0 && dy != 0) {  // Vertical line
                sameLine = true;
                stepY = (dy > 0) ? 1 : -1;
            } else if (dy == 0 && dx != 0) {  // Horizontal line
                sameLine = true;
                stepX = (dx > 0) ? 1 : -1;
            } else if (std::abs(dx) == std::abs(dy)) {  // Diagonal line
                sameLine = true;
                stepX = (dx > 0) ? 1 : -1;
                stepY = (dy > 0) ? 1 : -1;
            }

            if (sameLine) {
                // Squares strictly between s1 and s2 (exclusive)
                U64 between = 0;
                int cx = x1 + stepX;
                int cy = y1 + stepY;
                while (cx != x2 || cy != y2) {
                    set_bit(between, sq(cx, cy));
                    cx += stepX;
                    cy += stepY;
                }
                BETWEEN_BB[s1][s2] = between;

                // Complete line through s1 and s2
                int dir = -1;
                for (int d = 0; d < 8; ++d) {
                    if (DIR_DX[d] == stepX && DIR_DY[d] == stepY) {
                        dir = d;
                        break;
                    }
                }
                if (dir >= 0) {
                    int oppDir = (dir + 4) % 8;
                    LINE_BB[s1][s2] = RAY_BB[s1][dir] | RAY_BB[s1][oppDir] | (1ULL << s1);
                }
            }
        }
    }
}

void initBitboards() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    initAttackTables();
    initRayTables();
}
