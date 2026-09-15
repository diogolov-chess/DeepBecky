#include "evaluate.h"
#include "bitboard.h"
#include "nnue.h"
#include "position.h"
#include <algorithm>
#include <cmath>

namespace Eval {

void init() {}

inline int centerDistance(int sq) {
    int file = sq & 7;
    int rank = sq >> 3;
    int distFile = std::max(3 - file, file - 4);
    int distRank = std::max(3 - rank, rank - 4);
    return distFile + distRank; // ranges from 0 (center e4/d4/e5/d5) to 6 (corners a1/a8/h1/h8)
}

inline int cornerDistance(int sq) {
    int file = sq & 7;
    int rank = sq >> 3;
    int distFile = std::min(file, 7 - file);
    int distRank = std::min(rank, 7 - rank);
    return distFile + distRank; // 0 at corners (a1, a8, h1, h8), up to 6 in center
}

inline int kingDistance(int sq1, int sq2) {
    int file1 = sq1 & 7, rank1 = sq1 >> 3;
    int file2 = sq2 & 7, rank2 = sq2 >> 3;
    return std::max(std::abs(file1 - file2), std::abs(rank1 - rank2)); // ranges from 1 to 7
}

int evaluateKXK(const Position& pos) {
    // 1. Check if pawns exist (early exit for 98% of positions)
    if (pos.bitboards[WPAWN] != 0 || pos.bitboards[BPAWN] != 0)
        return 0;

    // 2. Identify if one side is a bare king
    bool whiteBare = (pos.color_bitboards[WHITE] == (1ULL << pos.king_sq[WHITE]));
    bool blackBare = (pos.color_bitboards[BLACK] == (1ULL << pos.king_sq[BLACK]));

    if (whiteBare == blackBare) // either both bare or neither bare
        return 0;

    Color winner = blackBare ? WHITE : BLACK;
    Color loser  = blackBare ? BLACK : WHITE;

    // 3. Verify that the winning side has mating material
    int wQueens   = popcount(pos.bitboards[makePiece(winner, WQUEEN)]);
    int wRooks    = popcount(pos.bitboards[makePiece(winner, WROOK)]);
    int wBishops  = popcount(pos.bitboards[makePiece(winner, WBISHOP)]);
    int wKnights  = popcount(pos.bitboards[makePiece(winner, WKNIGHT)]);

    bool isMajor = (wQueens >= 1 || wRooks >= 1);
    U64 bishops = pos.bitboards[makePiece(winner, WBISHOP)];
    bool light = false, dark = false;
    for (U64 remaining = bishops; remaining;) {
        const int square = pop_lsb(&remaining);
        if (isLightSquare(square)) light = true;
        else dark = true;
    }
    bool isMinors = ((light && dark) || (wBishops >= 1 && wKnights >= 1));

    if (!isMajor && !isMinors)
        return 0;

    int winnerKingSq = pos.king_sq[winner];
    int loserKingSq  = pos.king_sq[loser];

    int totalBonus = 0;

    if (isMajor) {
        // For Queen/Rook: push to any edge and bring king close
        int edgeBonus  = centerDistance(loserKingSq) * 45;
        int closeBonus = (7 - kingDistance(winnerKingSq, loserKingSq)) * 30;
        totalBonus = edgeBonus + closeBonus;
    } else {
        // A single-colour bishop team plus knight must steer toward that colour.
        // Two opposite-coloured bishops do not require a particular corner.
        int distance = cornerDistance(loserKingSq);
        if (!(light && dark) && wKnights) {
            // isLightSquare() names even file+rank parity (including a1).
            const int first = light ? 0 : 7;
            const int second = first ^ 63;
            auto manhattan = [](int a, int b) {
                return std::abs((a & 7) - (b & 7)) + std::abs((a >> 3) - (b >> 3));
            };
            distance = std::min(manhattan(loserKingSq, first), manhattan(loserKingSq, second));
        }
        int cornerBonus = (6 - distance) * 60;
        int closeBonus  = (7 - kingDistance(winnerKingSq, loserKingSq)) * 40;
        totalBonus = cornerBonus + closeBonus;
    }

    return (pos.white_to_move == (winner == WHITE)) ? totalBonus : -totalBonus;
}

int evaluate(Position& pos) {
    if (pos.isInsufficientMaterial()) return 0;
    // Exact KBNK: keep a known-win material value and an unambiguous geometric
    // gradient. A generic NNUE score can swamp the bishop-colour corner bonus.
    // This is an evaluator, not a tablebase result or a mate-distance claim.
    if (popcount(pos.pieces()) == 4) {
        for (Color strong : {WHITE, BLACK}) {
            if (popcount(pos.bitboards[makePiece(strong, WBISHOP)]) == 1 &&
                popcount(pos.bitboards[makePiece(strong, WKNIGHT)]) == 1) {
                const int sign = pos.white_to_move == (strong == WHITE) ? 1 : -1;
                return clampStaticScore(sign * 10000 + evaluateKXK(pos));
            }
        }
    }
    int score = NNUE::evaluate(pos);
    score += evaluateKXK(pos);
    return clampStaticScore(score);
}

} // namespace Eval

int Position::evaluate() {
    return Eval::evaluate(*this);
}
