#include "threats.h"
#include "position.h"
#include "magic.h"
#include <cstring>
#include <algorithm>

namespace Threats {

namespace {

constexpr int PIECE_INTERACTION_MAP[6][6] = {
    {0,  1, -1,  2, -1, -1},  // Pawn: attacks Pawn, Knight, Rook
    {0,  1,  2,  3,  4, -1},  // Knight: attacks P, N, B, R, Q
    {0,  1,  2,  3, -1, -1},  // Bishop: attacks P, N, B, R
    {0,  1,  2,  3, -1, -1},  // Rook: attacks P, N, B, R
    {0,  1,  2,  3,  4, -1},  // Queen: attacks P, N, B, R, Q
    {0,  1,  2,  3, -1, -1},  // King: attacks P, N, B, R
};

constexpr int PIECE_TARGET_COUNT[6] = {6, 10, 8, 8, 10, 8};

struct PiecePair {
    std::uint32_t inner = 0;

    PiecePair() = default;
    PiecePair(bool excluded, bool semiExcluded, int base) {
        inner = (static_cast<std::uint32_t>(semiExcluded && !excluded) << 30)
              | (static_cast<std::uint32_t>(excluded) << 31)
              | (static_cast<std::uint32_t>(base) & 0x3FFFFFFFu);
    }

    bool isExcluded() const { return (inner >> 31) & 1; }

    int base(int fromSq, int toSq) const {
        const std::uint32_t below = static_cast<std::uint32_t>(fromSq < toSq);
        const std::uint32_t val = (inner + (below << 30)) & 0x80FFFFFFu;
        return static_cast<int>(val);
    }
};

std::int32_t PIECE_OFFSET_LOOKUP[12][64]{};
std::uint8_t ATTACK_INDEX_LOOKUP[12][64][64]{};
PiecePair PIECE_PAIR_LOOKUP[12][12]{};
bool initialized = false;

U64 rayAttacks(int sq, int df, int dr, U64 occ = 0) {
    int f = sq & 7;
    int r = sq >> 3;
    U64 bb = 0;
    while (true) {
        f += df;
        r += dr;
        if (f < 0 || f > 7 || r < 0 || r > 7) break;
        int targetSq = r * 8 + f;
        bb |= (1ULL << targetSq);
        if (occ & (1ULL << targetSq)) break;
    }
    return bb;
}

U64 generatePseudoAttacks(int pieceType, int sq) {
    int f = sq & 7;
    int r = sq >> 3;
    U64 bb = 0;
    if (pieceType == 0) { // Pawn (White forward diagonal)
        if (r + 1 <= 7) {
            if (f - 1 >= 0) bb |= (1ULL << ((r + 1) * 8 + (f - 1)));
            if (f + 1 <= 7) bb |= (1ULL << ((r + 1) * 8 + (f + 1)));
        }
    } else if (pieceType == 1) { // Knight
        bb = KNIGHT_ATK_BB[sq];
    } else if (pieceType == 2) { // Bishop
        bb = rayAttacks(sq, -1, -1) | rayAttacks(sq, -1, 1) | rayAttacks(sq, 1, -1) | rayAttacks(sq, 1, 1);
    } else if (pieceType == 3) { // Rook
        bb = rayAttacks(sq, -1, 0) | rayAttacks(sq, 1, 0) | rayAttacks(sq, 0, -1) | rayAttacks(sq, 0, 1);
    } else if (pieceType == 4) { // Queen
        bb = generatePseudoAttacks(2, sq) | generatePseudoAttacks(3, sq);
    } else if (pieceType == 5) { // King
        bb = KING_ATK_BB[sq];
    }
    return bb;
}

} // namespace

void init() {
    if (initialized) return;

    int offset = 0;
    int pieceOffset[12]{};
    int offsetTable[12]{};

    for (int pieceColor = 0; pieceColor < 2; ++pieceColor) {
        for (int pieceType = 0; pieceType < 6; ++pieceType) {
            int piece = pieceColor * 6 + pieceType;
            int count = 0;
            for (int sq = 0; sq < 64; ++sq) {
                PIECE_OFFSET_LOOKUP[piece][sq] = count;
                if (pieceType != 0 || (sq >= 8 && sq < 56)) {
                    U64 attacksBB = generatePseudoAttacks(pieceType, sq);
                    count += popcount(attacksBB);
                }
            }
            pieceOffset[piece] = count;
            offsetTable[piece] = offset;
            offset += PIECE_TARGET_COUNT[pieceType] * count;
        }
    }

    for (int attacking = 0; attacking < 12; ++attacking) {
        int attkrType = attacking % 6;
        int attkrCol = attacking / 6;
        for (int attacked = 0; attacked < 12; ++attacked) {
            int attkdType = attacked % 6;
            int attkdCol = attacked / 6;

            int map = PIECE_INTERACTION_MAP[attkrType][attkdType];
            int base = offsetTable[attacking]
                     + ((attkdCol * (PIECE_TARGET_COUNT[attkrType] / 2) + map) * pieceOffset[attacking]);
            bool enemy = (attkrCol != attkdCol);
            bool semiExcluded = (attkrType == attkdType) && (enemy || attkrType != 0);
            bool excluded = (map < 0);

            PIECE_PAIR_LOOKUP[attacking][attacked] = PiecePair(excluded, semiExcluded, base);
        }
    }

    for (int piece = 0; piece < 12; ++piece) {
        int pieceType = piece % 6;
        for (int frm = 0; frm < 64; ++frm) {
            U64 attacksBB = generatePseudoAttacks(pieceType, frm);
            for (int to = 0; to < 64; ++to) {
                U64 mask = (to == 63) ? ~0ULL : ((1ULL << to) - 1);
                if (to == 0) mask = 0ULL;
                ATTACK_INDEX_LOOKUP[piece][frm][to] = static_cast<std::uint8_t>(popcount(attacksBB & mask));
            }
        }
    }

    initialized = true;
}

inline int threatIndex(int attackingPiece, int fromSq, int attackedPiece, int toSq, bool mirrored, int pov) {
    if (attackingPiece <= 0 || attackingPiece > 12 || attackedPiece <= 0 || attackedPiece > 12) return -1;

    const int attkrIdx = attackingPiece - 1; // 0..11
    const int attkdIdx = attackedPiece - 1; // 0..11

    const int attkrType = attkrIdx % 6;
    const int attkrCol  = attkrIdx / 6;
    const int attkdType = attkdIdx % 6;
    const int attkdCol  = attkdIdx / 6;

    const int flip = (mirrored ? 7 : 0) ^ (pov == 1 ? 56 : 0);
    const int frm = fromSq ^ flip;
    const int to = toSq ^ flip;

    const int attkr = (attkrCol ^ pov) * 6 + attkrType;
    const int attkd = (attkdCol ^ pov) * 6 + attkdType;

    const PiecePair& pair = PIECE_PAIR_LOOKUP[attkr][attkd];
    if (pair.isExcluded()) return -1;

    return pair.base(frm, to) + PIECE_OFFSET_LOOKUP[attkr][frm] + ATTACK_INDEX_LOOKUP[attkr][frm][to];
}

inline U64 getActualAttacks(int pieceType, int color, int sq, U64 occ) {
    if (pieceType == 0) { // Pawn
        return color == 0 ? WPAWN_ATK_BB[sq] : BPAWN_ATK_BB[sq];
    } else if (pieceType == 1) { // Knight
        return KNIGHT_ATK_BB[sq];
    } else if (pieceType == 2) { // Bishop
        return Magic::bishopAttacks(sq, occ);
    } else if (pieceType == 3) { // Rook
        return Magic::rookAttacks(sq, occ);
    } else if (pieceType == 4) { // Queen
        return Magic::queenAttacks(sq, occ);
    } else if (pieceType == 5) { // King
        return KING_ATK_BB[sq];
    }
    return 0ULL;
}

int extractActiveThreatsFast(const Position& pos, bool mirrored, int pov, int* threatIndices) {
    if (!initialized) init();
    int count = 0;
    const U64 occ = pos.color_bitboards[WHITE] | pos.color_bitboards[BLACK];
    U64 pieces = occ;

    const int flip = (mirrored ? 7 : 0) ^ (pov == 1 ? 56 : 0);

    while (pieces) {
        const int sq = pop_lsb(&pieces);
        const int piece = pos.piece_board[sq];
        if (piece <= 0 || piece > 12) continue;

        const int attkrIdx = piece - 1;
        const int ptype = attkrIdx % 6;
        const int pcol  = attkrIdx / 6;
        const int attkr = (pcol ^ pov) * 6 + ptype;
        const int frm = sq ^ flip;

        U64 attacks = getActualAttacks(ptype, pcol, sq, occ) & occ;
        while (attacks) {
            const int toSq = pop_lsb(&attacks);
            const int targetPiece = pos.piece_board[toSq];
            if (targetPiece > 0 && targetPiece <= 12) {
                const int attkdIdx = targetPiece - 1;
                const int attkdType = attkdIdx % 6;
                const int attkdCol  = attkdIdx / 6;
                const int attkd = (attkdCol ^ pov) * 6 + attkdType;

                const PiecePair& pair = PIECE_PAIR_LOOKUP[attkr][attkd];
                if (!pair.isExcluded()) {
                    const int to = toSq ^ flip;
                    const int tIdx = pair.base(frm, to) + PIECE_OFFSET_LOOKUP[attkr][frm] + ATTACK_INDEX_LOOKUP[attkr][frm][to];
                    if (tIdx >= 0 && tIdx < ThreatFeatureDimensions && count < MaxActiveThreats) {
                        threatIndices[count++] = tIdx;
                    }
                }
            }
        }
    }
    return count;
}

void extractActiveThreatsDual(const Position& pos,
                             bool stmMirrored, int stmPov, int* stmThreats, int& stmCount,
                             bool nstmMirrored, int nstmPov, int* nstmThreats, int& nstmCount) {
    if (!initialized) init();
    stmCount = 0;
    nstmCount = 0;

    const U64 occ = pos.color_bitboards[WHITE] | pos.color_bitboards[BLACK];
    U64 pieces = occ;

    const int stmFlip  = (stmMirrored  ? 7 : 0) ^ (stmPov  == 1 ? 56 : 0);
    const int nstmFlip = (nstmMirrored ? 7 : 0) ^ (nstmPov == 1 ? 56 : 0);

    while (pieces) {
        const int sq = pop_lsb(&pieces);
        const int piece = pos.piece_board[sq];
        if (piece <= 0 || piece > 12) continue;

        const int attkrIdx = piece - 1;
        const int ptype = attkrIdx % 6;
        const int pcol  = attkrIdx / 6;

        const int stmAttkr  = (pcol ^ stmPov)  * 6 + ptype;
        const int nstmAttkr = (pcol ^ nstmPov) * 6 + ptype;

        const int stmFrm  = sq ^ stmFlip;
        const int nstmFrm = sq ^ nstmFlip;

        const int stmOffset  = PIECE_OFFSET_LOOKUP[stmAttkr][stmFrm];
        const int nstmOffset = PIECE_OFFSET_LOOKUP[nstmAttkr][nstmFrm];

        U64 attacks = getActualAttacks(ptype, pcol, sq, occ) & occ;
        while (attacks) {
            const int toSq = pop_lsb(&attacks);
            const int targetPiece = pos.piece_board[toSq];
            if (targetPiece > 0 && targetPiece <= 12) {
                const int attkdIdx = targetPiece - 1;
                const int attkdType = attkdIdx % 6;
                const int attkdCol  = attkdIdx / 6;

                // STM
                const int stmAttkd = (attkdCol ^ stmPov) * 6 + attkdType;
                const PiecePair& stmPair = PIECE_PAIR_LOOKUP[stmAttkr][stmAttkd];
                if (!stmPair.isExcluded()) {
                    const int stmTo = toSq ^ stmFlip;
                    const int tIdx = stmPair.base(stmFrm, stmTo) + stmOffset + ATTACK_INDEX_LOOKUP[stmAttkr][stmFrm][stmTo];
                    if (tIdx >= 0 && tIdx < ThreatFeatureDimensions && stmCount < MaxActiveThreats) {
                        stmThreats[stmCount++] = tIdx;
                    }
                }

                // NSTM
                const int nstmAttkd = (attkdCol ^ nstmPov) * 6 + attkdType;
                const PiecePair& nstmPair = PIECE_PAIR_LOOKUP[nstmAttkr][nstmAttkd];
                if (!nstmPair.isExcluded()) {
                    const int nstmTo = toSq ^ nstmFlip;
                    const int tIdx = nstmPair.base(nstmFrm, nstmTo) + nstmOffset + ATTACK_INDEX_LOOKUP[nstmAttkr][nstmFrm][nstmTo];
                    if (tIdx >= 0 && tIdx < ThreatFeatureDimensions && nstmCount < MaxActiveThreats) {
                        nstmThreats[nstmCount++] = tIdx;
                    }
                }
            }
        }
    }
}

void extractActiveThreats(const Position& pos, bool mirrored, int pov, std::vector<int>& activeThreats) {
    activeThreats.clear();
    int buffer[MaxActiveThreats];
    int count = extractActiveThreatsFast(pos, mirrored, pov, buffer);
    activeThreats.assign(buffer, buffer + count);
}

} // namespace Threats
