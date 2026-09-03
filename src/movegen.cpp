#include "movegen.h"
#include "position.h"
#include "magic.h"

// ========================= Attack Detection =========================

bool Position::isAttacked(int s, bool byWhite) const {
    U64 pawns   = byWhite ? bitboards[WPAWN]   : bitboards[BPAWN];
    U64 knights = byWhite ? bitboards[WKNIGHT] : bitboards[BKNIGHT];
    U64 bishops = byWhite ? bitboards[WBISHOP] : bitboards[BBISHOP];
    U64 rooks   = byWhite ? bitboards[WROOK]   : bitboards[BROOK];
    U64 queens  = byWhite ? bitboards[WQUEEN]  : bitboards[BQUEEN];
    U64 king    = byWhite ? bitboards[WKING]   : bitboards[BKING];
    U64 opp_pawn_atk = byWhite ? BPAWN_ATK_BB[s] : WPAWN_ATK_BB[s];

    if (opp_pawn_atk & pawns) return true;
    if (KNIGHT_ATK_BB[s] & knights) return true;
    if (KING_ATK_BB[s] & king) return true;

    U64 occupancy = color_bitboards[WHITE] | color_bitboards[BLACK];
    if (Magic::bishopAttacks(s, occupancy) & (bishops | queens)) return true;
    if (Magic::rookAttacks(s, occupancy) & (rooks | queens)) return true;

    return false;
}

bool Position::inCheck(bool whiteSide) const {
    return isAttacked(king_sq[whiteSide ? WHITE : BLACK], !whiteSide);
}

U64 Position::attackersTo(int sq, U64 occ) const {
    U64 attackers = 0;
    attackers |= (BPAWN_ATK_BB[sq] & bitboards[WPAWN]);
    attackers |= (WPAWN_ATK_BB[sq] & bitboards[BPAWN]);
    attackers |= KNIGHT_ATK_BB[sq] & (bitboards[WKNIGHT] | bitboards[BKNIGHT]);
    attackers |= KING_ATK_BB[sq] & (bitboards[WKING] | bitboards[BKING]);
    U64 diagSliders = bitboards[WBISHOP] | bitboards[BBISHOP] | bitboards[WQUEEN] | bitboards[BQUEEN];
    attackers |= Magic::bishopAttacks(sq, occ) & diagSliders;
    U64 orthSliders = bitboards[WROOK] | bitboards[BROOK] | bitboards[WQUEEN] | bitboards[BQUEEN];
    attackers |= Magic::rookAttacks(sq, occ) & orthSliders;
    return attackers;
}

U64 Position::checkersBB(bool whiteSide) const {
    int ksq = king_sq[whiteSide ? WHITE : BLACK];
    U64 occ = color_bitboards[WHITE] | color_bitboards[BLACK];
    U64 enemyColor = color_bitboards[whiteSide ? BLACK : WHITE];
    return attackersTo(ksq, occ) & enemyColor;
}

U64 Position::blockersForKing(bool whiteSide, U64& pinners) const {
    int ksq = king_sq[whiteSide ? WHITE : BLACK];
    U64 occ = color_bitboards[WHITE] | color_bitboards[BLACK];
    U64 us = color_bitboards[whiteSide ? WHITE : BLACK];

    U64 enemyRooksQueens = whiteSide
        ? (bitboards[BROOK] | bitboards[BQUEEN])
        : (bitboards[WROOK] | bitboards[WQUEEN]);
    U64 enemyBishopsQueens = whiteSide
        ? (bitboards[BBISHOP] | bitboards[BQUEEN])
        : (bitboards[WBISHOP] | bitboards[WQUEEN]);

    U64 snipers = (Magic::rookAttacks(ksq, 0) & enemyRooksQueens) |
                  (Magic::bishopAttacks(ksq, 0) & enemyBishopsQueens);

    U64 blockers = 0;
    pinners = 0;

    while (snipers) {
        int sniperSq = pop_lsb(&snipers);
        U64 between = BETWEEN_BB[ksq][sniperSq] & occ;

        if (between && !(between & (between - 1))) {
            blockers |= between;
            if (between & us) {
                pinners |= (1ULL << sniperSq);
            }
        }
    }

    return blockers;
}

U64 Position::pinnedBB(bool whiteSide) const {
    U64 pinners;
    U64 blockers = blockersForKing(whiteSide, pinners);
    U64 us = color_bitboards[whiteSide ? WHITE : BLACK];
    return blockers & us;
}

static bool isEnPassantLegal(int ksq, int from_sq, int epCapturedSq, int epSq,
                             U64 occ, U64 enemyRooksQueens, U64 enemyBishopsQueens) {
    U64 occAfter = (occ ^ (1ULL << from_sq) ^ (1ULL << epCapturedSq)) | (1ULL << epSq);
    if (Magic::rookAttacks(ksq, occAfter) & enemyRooksQueens) return false;
    if (Magic::bishopAttacks(ksq, occAfter) & enemyBishopsQueens) return false;
    return true;
}

bool Position::legalMove(const Move& m) {
    if (moveIsNone(m)) return false;

    int from_sq = moveFrom(m);
    int to_sq = moveTo(m);
    int us = white_to_move ? WHITE : BLACK;
    int them = us ^ 1;
    int ksq = king_sq[us];

    int piece = piece_board[from_sq];
    if (piece == EMPTY) return false;
    if ((us == WHITE && piece > WKING) || (us == BLACK && piece <= WKING)) return false;

    // Destination must not be occupied by friendly piece
    if (color_bitboards[us] & (1ULL << to_sq)) return false;

    U64 opp_pieces = color_bitboards[them];
    U64 all_pieces = color_bitboards[WHITE] | color_bitboards[BLACK];

    // 1. King Moves (Castling or Normal King Move)
    if (piece == WKING || piece == BKING) {
        if (moveIsCastle(m)) {
            return isPseudoLegal(m);
        }
        U64 occWithoutKing = all_pieces ^ (1ULL << from_sq);
        return !(attackersTo(to_sq, occWithoutKing) & opp_pieces);
    }

    // 2. En Passant
    if (moveIsEnPassant(m)) {
        if (ep_file == 0) return false;
        int ep_sq = sq(ep_file - 1, us == WHITE ? 5 : 2);
        if (to_sq != ep_sq) return false;
        int epCapturedSq = ep_sq + (us == WHITE ? -8 : 8);

        U64 enemyRooksQueens = (us == WHITE)
            ? (bitboards[BROOK] | bitboards[BQUEEN])
            : (bitboards[WROOK] | bitboards[WQUEEN]);
        U64 enemyBishopsQueens = (us == WHITE)
            ? (bitboards[BBISHOP] | bitboards[BQUEEN])
            : (bitboards[WBISHOP] | bitboards[WQUEEN]);

        return isEnPassantLegal(ksq, from_sq, epCapturedSq, to_sq, all_pieces, enemyRooksQueens, enemyBishopsQueens);
    }

    // 3. In Check / Checkers
    U64 checkers = checkersBB(white_to_move);
    int numCheckers = popcount(checkers);

    if (numCheckers >= 2) return false;

    if (numCheckers == 1) {
        int checkerSq = lsb_index(checkers);
        U64 targetMask = BETWEEN_BB[ksq][checkerSq] | checkers;
        if (!((1ULL << to_sq) & targetMask)) return false;
    }

    // 4. Absolute Pins
    U64 pinners_unused;
    U64 blockers = blockersForKing(white_to_move, pinners_unused);
    U64 pinned = blockers & color_bitboards[us];

    if ((pinned & (1ULL << from_sq)) && !((1ULL << to_sq) & LINE_BB[ksq][from_sq])) {
        return false;
    }

    return true;
}

bool Position::isPseudoLegal(const Move& m) const {
    if (moveIsNone(m)) return false;

    int from_sq = moveFrom(m);
    int to_sq = moveTo(m);
    int us = white_to_move ? WHITE : BLACK;

    // Must move our piece
    int piece = piece_board[from_sq];
    if (piece == EMPTY) return false;
    if ((white_to_move && piece > WKING) || (!white_to_move && piece <= WKING)) return false;

    // Destination must not be occupied by our piece
    if (color_bitboards[us] & (1ULL << to_sq)) return false;

    U64 occ = color_bitboards[WHITE] | color_bitboards[BLACK];
    int promo = movePromotionType(m);

    // Validate based on piece type
    switch (piece) {
        case WPAWN:
        case BPAWN: {
            int dir = (us == WHITE) ? 8 : -8;
            bool isCapture = moveIsCapture(m);
            bool isEnPassant = moveIsEnPassant(m);
            bool isDoublePush = moveIsDoublePush(m);

            if (promo) {
                if ((us == WHITE && to_sq < 56) || (us == BLACK && to_sq > 7)) return false;
                if (!isValidPromotionType(promo)) return false;
            } else {
                if ((us == WHITE && to_sq >= 56) || (us == BLACK && to_sq <= 7)) return false;
            }

            if (isEnPassant) {
                if (!isCapture) return false;
                if (ep_file == 0) return false;
                int ep_sq = sq(ep_file - 1, us == WHITE ? 5 : 2);
                if (to_sq != ep_sq) return false;
                if (abs(sq_x(from_sq) - sq_x(to_sq)) != 1) return false;
                return true;
            }

            if (isCapture) {
                if (!(color_bitboards[us ^ 1] & (1ULL << to_sq))) return false;
                if (abs(sq_x(from_sq) - sq_x(to_sq)) != 1) return false;
                if (to_sq - from_sq != dir + 1 && to_sq - from_sq != dir - 1) return false;
                return true;
            }

            // Normal push
            if (from_sq + dir == to_sq) {
                if (occ & (1ULL << to_sq)) return false;
                if (isDoublePush) return false;
                return true;
            }

            // Double push
            if (isDoublePush) {
                if (from_sq + 2 * dir != to_sq) return false;
                if ((us == WHITE && from_sq >= 16) || (us == BLACK && from_sq <= 47)) return false;
                if (occ & (1ULL << (from_sq + dir))) return false;
                if (occ & (1ULL << to_sq)) return false;
                return true;
            }
            return false;
        }
        case WKNIGHT:
        case BKNIGHT:
            return (KNIGHT_ATK_BB[from_sq] & (1ULL << to_sq)) != 0;
        case WBISHOP:
        case BBISHOP:
            return (Magic::bishopAttacks(from_sq, occ) & (1ULL << to_sq)) != 0;
        case WROOK:
        case BROOK:
            return (Magic::rookAttacks(from_sq, occ) & (1ULL << to_sq)) != 0;
        case WQUEEN:
        case BQUEEN:
            return ((Magic::bishopAttacks(from_sq, occ) | Magic::rookAttacks(from_sq, occ)) & (1ULL << to_sq)) != 0;
        case WKING:
        case BKING:
            if (moveIsCastle(m)) {
                if (inCheck(white_to_move)) return false;
                if (us == WHITE) {
                    if (to_sq == sq(6, 0)) {
                        if (!(castling & 8)) return false;
                        if (occ & 0x60ULL) return false;
                        if (isAttacked(5, false) || isAttacked(6, false)) return false;
                        return true;
                    }
                    if (to_sq == sq(2, 0)) {
                        if (!(castling & 4)) return false;
                        if (occ & 0xEULL) return false;
                        if (isAttacked(3, false) || isAttacked(2, false)) return false;
                        return true;
                    }
                } else {
                    if (to_sq == sq(6, 7)) {
                        if (!(castling & 2)) return false;
                        if (occ & 0x6000000000000000ULL) return false;
                        if (isAttacked(61, true) || isAttacked(62, true)) return false;
                        return true;
                    }
                    if (to_sq == sq(2, 7)) {
                        if (!(castling & 1)) return false;
                        if (occ & 0x0E00000000000000ULL) return false;
                        if (isAttacked(59, true) || isAttacked(58, true)) return false;
                        return true;
                    }
                }
                return false;
            }
            return (KING_ATK_BB[from_sq] & (1ULL << to_sq)) != 0;
    }
    return false;
}

// ========================= Legal Generation =========================

static inline void addMove(Move* mv, int& count, int from_sq, int to_sq, MoveType type) {
    mv[count].data = static_cast<uint16_t>(from_sq | (to_sq << 6) | (type << 12));
    count++;
}

static inline void addMoveIfOnLine(Move* mv, int& count, int from_sq, int to_sq,
                                    MoveType type, U64 pinned, int ksq) {
    if ((pinned & (1ULL << from_sq)) && !((1ULL << to_sq) & LINE_BB[ksq][from_sq]))
        return;
    mv[count].data = static_cast<uint16_t>(from_sq | (to_sq << 6) | (type << 12));
    count++;
}

int Position::generateLegal(Move* moves, bool capturesOnly) {
    return generateLegal(moves, capturesOnly ? GEN_CAPTURES : GEN_ALL);
}

int Position::generateLegal(Move* moves, GenType type) {
    int count = 0;

    int us = white_to_move ? WHITE : BLACK;
    int them = us ^ 1;
    int ksq = king_sq[us];

    bool capturesOnly = (type == GEN_CAPTURES);
    bool quietsOnly   = (type == GEN_QUIETS);

    U64 my_pieces = color_bitboards[us];
    U64 opp_pieces = color_bitboards[them];
    U64 all_pieces = my_pieces | opp_pieces;
    U64 empty_squares = ~all_pieces;

    // Destination mask for pieces: captures only, quiets only, or all non-friendly squares
    U64 pieceDest;
    if (capturesOnly) pieceDest = opp_pieces;
    else if (quietsOnly) pieceDest = empty_squares;
    else pieceDest = ~my_pieces;

    U64 enemyRooksQueens = (us == WHITE)
        ? (bitboards[BROOK] | bitboards[BQUEEN])
        : (bitboards[WROOK] | bitboards[WQUEEN]);

    U64 enemyBishopsQueens = (us == WHITE)
        ? (bitboards[BBISHOP] | bitboards[BQUEEN])
        : (bitboards[WBISHOP] | bitboards[WQUEEN]);

    // Compute checkers and pin info
    U64 checkers = checkersBB(white_to_move);
    U64 pinners_unused;
    U64 blockers = blockersForKing(white_to_move, pinners_unused);
    U64 pinned = blockers & my_pieces;

    int numCheckers = popcount(checkers);

    // === King moves (always generated, regardless of check count) ===
    {
        U64 attacks = KING_ATK_BB[ksq] & pieceDest;
        U64 occWithoutKing = all_pieces ^ (1ULL << ksq);
        while (attacks) {
            int to_sq = pop_lsb(&attacks);
            // King move: check that to_sq is not attacked with king removed
            if (!(attackersTo(to_sq, occWithoutKing) & opp_pieces)) {
                MoveType flags = (opp_pieces & (1ULL << to_sq)) ? MOVE_FLAG_CAPTURE : MOVE_TYPE_QUIET;
                addMove(moves, count, ksq, to_sq, flags);
            }
        }
    }

    // Double check: only king moves are legal
    if (numCheckers >= 2) return count;

    // Compute target mask: if single check, non-king pieces must block or capture checker
    U64 targetMask = ~0ULL;
    if (numCheckers == 1) {
        int checkerSq = lsb_index(checkers);
        targetMask = BETWEEN_BB[ksq][checkerSq] | checkers;
    }

    // === Knights ===
    {
        U64 bb = bitboards[us == WHITE ? WKNIGHT : BKNIGHT] & ~pinned;
        // Pinned knights can never move legally (they can't stay on the pin line)
        while (bb) {
            int from_sq = pop_lsb(&bb);
            U64 targets = KNIGHT_ATK_BB[from_sq] & pieceDest & targetMask;
            while (targets) {
                int to_sq = pop_lsb(&targets);
                MoveType flags = (opp_pieces & (1ULL << to_sq)) ? MOVE_FLAG_CAPTURE : MOVE_TYPE_QUIET;
                addMove(moves, count, from_sq, to_sq, flags);
            }
        }
    }

    // === Bishops ===
    {
        U64 bb = bitboards[us == WHITE ? WBISHOP : BBISHOP];
        while (bb) {
            int from_sq = pop_lsb(&bb);
            U64 attacks = Magic::bishopAttacks(from_sq, all_pieces) & pieceDest & targetMask;
            // If pinned, restrict to pin line
            if (pinned & (1ULL << from_sq))
                attacks &= LINE_BB[ksq][from_sq];
            while (attacks) {
                int to_sq = pop_lsb(&attacks);
                MoveType flags = (opp_pieces & (1ULL << to_sq)) ? MOVE_FLAG_CAPTURE : MOVE_TYPE_QUIET;
                addMove(moves, count, from_sq, to_sq, flags);
            }
        }
    }

    // === Rooks ===
    {
        U64 bb = bitboards[us == WHITE ? WROOK : BROOK];
        while (bb) {
            int from_sq = pop_lsb(&bb);
            U64 attacks = Magic::rookAttacks(from_sq, all_pieces) & pieceDest & targetMask;
            if (pinned & (1ULL << from_sq))
                attacks &= LINE_BB[ksq][from_sq];
            while (attacks) {
                int to_sq = pop_lsb(&attacks);
                MoveType flags = (opp_pieces & (1ULL << to_sq)) ? MOVE_FLAG_CAPTURE : MOVE_TYPE_QUIET;
                addMove(moves, count, from_sq, to_sq, flags);
            }
        }
    }

    // === Queens ===
    {
        U64 bb = bitboards[us == WHITE ? WQUEEN : BQUEEN];
        while (bb) {
            int from_sq = pop_lsb(&bb);
            U64 attacks = (Magic::bishopAttacks(from_sq, all_pieces) |
                           Magic::rookAttacks(from_sq, all_pieces)) & pieceDest & targetMask;
            if (pinned & (1ULL << from_sq))
                attacks &= LINE_BB[ksq][from_sq];
            while (attacks) {
                int to_sq = pop_lsb(&attacks);
                MoveType flags = (opp_pieces & (1ULL << to_sq)) ? MOVE_FLAG_CAPTURE : MOVE_TYPE_QUIET;
                addMove(moves, count, from_sq, to_sq, flags);
            }
        }
    }

    // === Pawns ===
    U64 pawns = bitboards[us == WHITE ? WPAWN : BPAWN];
    const int promoPieces[4] = {
        us == WHITE ? WQUEEN : BQUEEN,
        us == WHITE ? WROOK : BROOK,
        us == WHITE ? WBISHOP : BBISHOP,
        us == WHITE ? WKNIGHT : BKNIGHT
    };

    if (us == WHITE) {
        // Single pushes & Double pushes
        if (!capturesOnly) {
            U64 oneStep = (pawns << 8) & empty_squares;
            // Push-promotions: only if not quietsOnly
            if (!quietsOnly) {
                U64 promoPush = oneStep & Rank8BB & targetMask;
                while (promoPush) {
                    int to = pop_lsb(&promoPush);
                    int from = to - 8;
                    if (!(pinned & (1ULL << from)) || ((1ULL << to) & LINE_BB[ksq][from])) {
                        for (int p : promoPieces)
                            addMove(moves, count, from, to, makePromoMoveType(p));
                    }
                }
            }
            U64 quietPush = oneStep & ~Rank8BB & targetMask;
            while (quietPush) {
                int to = pop_lsb(&quietPush);
                addMoveIfOnLine(moves, count, to - 8, to, MOVE_TYPE_QUIET, pinned, ksq);
            }
            // Double pushes: intermediate square must also be empty
            U64 mid = ((pawns & Rank2BB) << 8) & empty_squares;
            U64 twoStep = (mid << 8) & empty_squares & targetMask;
            while (twoStep) {
                int to = pop_lsb(&twoStep);
                addMoveIfOnLine(moves, count, to - 16, to, MOVE_FLAG_DOUBLEPUSH, pinned, ksq);
            }
        } else {
            // Push-promotions in capturesOnly (qsearch)
            U64 oneStep = (pawns << 8) & empty_squares;
            U64 promoPush = oneStep & Rank8BB & targetMask;
            while (promoPush) {
                int to = pop_lsb(&promoPush);
                int from = to - 8;
                if (!(pinned & (1ULL << from)) || ((1ULL << to) & LINE_BB[ksq][from])) {
                    for (int p : promoPieces)
                        addMove(moves, count, from, to, makePromoMoveType(p));
                }
            }
        }

        // Captures
        if (!quietsOnly) {
            U64 capL = ((pawns & ~FileABB) << 7) & opp_pieces & targetMask;
            while (capL) {
                int to = pop_lsb(&capL);
                int from = to - 7;
                if (to >= 56) {
                    if (!(pinned & (1ULL << from)) || ((1ULL << to) & LINE_BB[ksq][from])) {
                        for (int p : promoPieces)
                            addMove(moves, count, from, to, makePromoCaptureMoveType(p));
                    }
                } else {
                    addMoveIfOnLine(moves, count, from, to, MOVE_FLAG_CAPTURE, pinned, ksq);
                }
            }
            U64 capR = ((pawns & ~FileHBB) << 9) & opp_pieces & targetMask;
            while (capR) {
                int to = pop_lsb(&capR);
                int from = to - 9;
                if (to >= 56) {
                    if (!(pinned & (1ULL << from)) || ((1ULL << to) & LINE_BB[ksq][from])) {
                        for (int p : promoPieces)
                            addMove(moves, count, from, to, makePromoCaptureMoveType(p));
                    }
                } else {
                    addMoveIfOnLine(moves, count, from, to, MOVE_FLAG_CAPTURE, pinned, ksq);
                }
            }

            // En passant
            if (ep_file > 0) {
                int ep_sq = sq(ep_file - 1, 5);
                int epCapturedSq = ep_sq - 8; // the pawn being captured
                U64 ep = 1ULL << ep_sq;
                U64 fromL = (pawns & ~FileABB) & (ep >> 7);
                U64 fromR = (pawns & ~FileHBB) & (ep >> 9);
                U64 f = fromL | fromR;
                while (f) {
                    int from = pop_lsb(&f);
                    bool epLegal = false;
                    if (numCheckers == 1) {
                        if ((1ULL << epCapturedSq) & checkers)
                            epLegal = true;
                        else if ((1ULL << ep_sq) & targetMask)
                            epLegal = true;
                    } else {
                        epLegal = true; // not in check
                    }
                    if (!epLegal) continue;
                    if ((pinned & (1ULL << from)) && !((1ULL << ep_sq) & LINE_BB[ksq][from]))
                        continue;
                    if (!isEnPassantLegal(ksq, from, epCapturedSq, ep_sq, all_pieces, enemyRooksQueens, enemyBishopsQueens))
                        continue;
                    addMove(moves, count, from, ep_sq, MOVE_TYPE_EP);
                }
            }
        }
    } else {
        // BLACK pawns
        // Single & Double pushes
        if (!capturesOnly) {
            U64 oneStep = (pawns >> 8) & empty_squares;
            if (!quietsOnly) {
                U64 promoPush = oneStep & Rank1BB & targetMask;
                while (promoPush) {
                    int to = pop_lsb(&promoPush);
                    int from = to + 8;
                    if (!(pinned & (1ULL << from)) || ((1ULL << to) & LINE_BB[ksq][from])) {
                        for (int p : promoPieces)
                            addMove(moves, count, from, to, makePromoMoveType(p));
                    }
                }
            }
            U64 quietPush = oneStep & ~Rank1BB & targetMask;
            while (quietPush) {
                int to = pop_lsb(&quietPush);
                addMoveIfOnLine(moves, count, to + 8, to, MOVE_TYPE_QUIET, pinned, ksq);
            }
            U64 mid = ((pawns & Rank7BB) >> 8) & empty_squares;
            U64 twoStep = (mid >> 8) & empty_squares & targetMask;
            while (twoStep) {
                int to = pop_lsb(&twoStep);
                addMoveIfOnLine(moves, count, to + 16, to, MOVE_FLAG_DOUBLEPUSH, pinned, ksq);
            }
        } else {
            U64 oneStep = (pawns >> 8) & empty_squares;
            U64 promoPush = oneStep & Rank1BB & targetMask;
            while (promoPush) {
                int to = pop_lsb(&promoPush);
                int from = to + 8;
                if (!(pinned & (1ULL << from)) || ((1ULL << to) & LINE_BB[ksq][from])) {
                    for (int p : promoPieces)
                        addMove(moves, count, from, to, makePromoMoveType(p));
                }
            }
        }

        // Captures
        if (!quietsOnly) {
            U64 capR = ((pawns & ~FileHBB) >> 7) & opp_pieces & targetMask;
            while (capR) {
                int to = pop_lsb(&capR);
                int from = to + 7;
                if (to <= 7) { // Rank1
                    if (!(pinned & (1ULL << from)) || ((1ULL << to) & LINE_BB[ksq][from])) {
                        for (int p : promoPieces)
                            addMove(moves, count, from, to, makePromoCaptureMoveType(p));
                    }
                } else {
                    addMoveIfOnLine(moves, count, from, to, MOVE_FLAG_CAPTURE, pinned, ksq);
                }
            }
            U64 capL = ((pawns & ~FileABB) >> 9) & opp_pieces & targetMask;
            while (capL) {
                int to = pop_lsb(&capL);
                int from = to + 9;
                if (to <= 7) { // Rank1
                    if (!(pinned & (1ULL << from)) || ((1ULL << to) & LINE_BB[ksq][from])) {
                        for (int p : promoPieces)
                            addMove(moves, count, from, to, makePromoCaptureMoveType(p));
                    }
                } else {
                    addMoveIfOnLine(moves, count, from, to, MOVE_FLAG_CAPTURE, pinned, ksq);
                }
            }

            // En passant
            if (ep_file > 0) {
                int ep_sq = sq(ep_file - 1, 2);
                int epCapturedSq = ep_sq + 8;
                U64 ep = 1ULL << ep_sq;
                U64 fromR = (pawns & ~FileHBB) & (ep << 7);
                U64 fromL = (pawns & ~FileABB) & (ep << 9);
                U64 f = fromR | fromL;
                while (f) {
                    int from = pop_lsb(&f);
                    bool epLegal = false;
                    if (numCheckers == 1) {
                        if ((1ULL << epCapturedSq) & checkers)
                            epLegal = true;
                        else if ((1ULL << ep_sq) & targetMask)
                            epLegal = true;
                    } else {
                        epLegal = true;
                    }
                    if (!epLegal) continue;
                    if ((pinned & (1ULL << from)) && !((1ULL << ep_sq) & LINE_BB[ksq][from]))
                        continue;
                    if (!isEnPassantLegal(ksq, from, epCapturedSq, ep_sq, all_pieces, enemyRooksQueens, enemyBishopsQueens))
                        continue;
                    addMove(moves, count, from, ep_sq, MOVE_TYPE_EP);
                }
            }
        }
    }

    // === Castling (only when not in check and not captures-only) ===
    if (!capturesOnly && numCheckers == 0) {
        if (us == WHITE) {
            if ((castling & 8) && !(all_pieces & 0x60ULL) &&
                !isAttacked(5, false) && !isAttacked(6, false)) {
                addMove(moves, count, sq(4, 0), sq(6, 0), MOVE_FLAG_CASTLE);
            }
            if ((castling & 4) && !(all_pieces & 0xEULL) &&
                !isAttacked(3, false) && !isAttacked(2, false)) {
                addMove(moves, count, sq(4, 0), sq(2, 0), MOVE_FLAG_CASTLE);
            }
        } else {
            if ((castling & 2) && !(all_pieces & 0x6000000000000000ULL) &&
                !isAttacked(61, true) && !isAttacked(62, true)) {
                addMove(moves, count, sq(4, 7), sq(6, 7), MOVE_FLAG_CASTLE);
            }
            if ((castling & 1) && !(all_pieces & 0x0E00000000000000ULL) &&
                !isAttacked(59, true) && !isAttacked(58, true)) {
                addMove(moves, count, sq(4, 7), sq(2, 7), MOVE_FLAG_CASTLE);
            }
        }
    }

    return count;
}

// ========================= Perft =========================

uint64_t perft(Position& pos, int depth) {
    if (depth == 0) return 1;
    
    Move moves[MAX_MOVES];
    int moveCount = pos.generateLegal(moves);
    
    if (depth == 1) return static_cast<uint64_t>(moveCount);
    
    uint64_t nodes = 0;
    for (int i = 0; i < moveCount; ++i) {
        pos.makeMove(moves[i]);
        nodes += perft(pos, depth - 1);
        pos.undoMove(moves[i]);
    }
    return nodes;
}
