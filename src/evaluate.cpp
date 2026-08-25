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
    bool isMinors = (wBishops >= 2 || (wBishops >= 1 && wKnights >= 1));

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
        // For 2 Bishops / Bishop+Knight: mate ONLY happens in a corner!
        // Push directly into the corner and bring winning king into close opposition
        int cornerBonus = (6 - cornerDistance(loserKingSq)) * 60;
        int closeBonus  = (7 - kingDistance(winnerKingSq, loserKingSq)) * 40;
        totalBonus = cornerBonus + closeBonus;
    }

    return (pos.white_to_move == (winner == WHITE)) ? totalBonus : -totalBonus;
}

int evaluate(Position& pos) {
    int score = NNUE::evaluate(pos);
    score += evaluateKXK(pos);
    return score;
}

} // namespace Eval

int Position::evaluate() {
    return Eval::evaluate(*this);
}