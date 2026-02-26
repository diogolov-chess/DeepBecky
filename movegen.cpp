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

// movegen.cpp - Move generation
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

bool Position::legalMove(const Move& m) {
    makeMove(m);
    bool ok = !inCheck(!white_to_move);
    undoMove(m);
    return ok;
}

// ========================= Pseudo-legal Generation =========================

int Position::generatePseudo(Move* mv, bool capturesOnly) {
    int count = 0;

    int us = white_to_move ? WHITE : BLACK;
    int them = us ^ 1;

    U64 my_pieces = color_bitboards[us];
    U64 opp_pieces = color_bitboards[them];
    U64 all_pieces = my_pieces | opp_pieces;
    U64 empty_squares = ~all_pieces;

    auto push_move = [&](int from_sq, int to_sq, bool capture, bool enpassant,
                         bool castle, bool doublepush, int promotion) {
        if (count >= MAX_MOVES) return;
        if (capturesOnly && !capture && promotion == 0) return;
        Move m;
        m.squares = static_cast<uint16_t>((to_sq << 6) | from_sq);
        uint8_t flags = 0;
        if (capture) flags |= MOVE_FLAG_CAPTURE;
        if (enpassant) flags |= MOVE_FLAG_ENPASSANT;
        if (castle) flags |= MOVE_FLAG_CASTLE;
        if (doublepush) flags |= MOVE_FLAG_DOUBLEPUSH;
        if (promotion != 0) flags |= static_cast<uint8_t>(promotion << MOVE_PROMO_SHIFT);
        m.flags = flags;
        m.score = 0;
        mv[count++] = m;
    };

    // Pieces
    const int piece_types[] = {
        us == WHITE ? WKNIGHT : BKNIGHT,
        us == WHITE ? WBISHOP : BBISHOP,
        us == WHITE ? WROOK : BROOK,
        us == WHITE ? WQUEEN : BQUEEN,
        us == WHITE ? WKING : BKING
    };

    for (int piece : piece_types) {
        U64 bb = bitboards[piece];
        while (bb) {
            int from_sq = pop_lsb(&bb);
            U64 attacks = 0;
            switch (piece) {
                case WKNIGHT: case BKNIGHT:
                    attacks = KNIGHT_ATK_BB[from_sq];
                    break;
                case WBISHOP: case BBISHOP:
                    attacks = Magic::bishopAttacks(from_sq, all_pieces);
                    break;
                case WROOK: case BROOK:
                    attacks = Magic::rookAttacks(from_sq, all_pieces);
                    break;
                case WQUEEN: case BQUEEN:
                    attacks = Magic::bishopAttacks(from_sq, all_pieces) |
                              Magic::rookAttacks(from_sq, all_pieces);
                    break;
                case WKING: case BKING:
                    attacks = KING_ATK_BB[from_sq];
                    break;
            }
            U64 targets = capturesOnly ? (attacks & opp_pieces) : (attacks & ~my_pieces);
            while (targets) {
                int to_sq = pop_lsb(&targets);
                bool capture = ((opp_pieces >> to_sq) & 1ULL) != 0;
                push_move(from_sq, to_sq, capture, false, false, false, 0);
            }
        }
    }

    // Pawns
    const int pawn_piece = us == WHITE ? WPAWN : BPAWN;
    U64 pawns = bitboards[pawn_piece];
    const int promoPieces[4] = {
        us == WHITE ? WQUEEN : BQUEEN,
        us == WHITE ? WROOK : BROOK,
        us == WHITE ? WBISHOP : BBISHOP,
        us == WHITE ? WKNIGHT : BKNIGHT
    };

    if (us == WHITE) {
        if (!capturesOnly) {
            U64 oneStep = (pawns << 8) & empty_squares;
            U64 quietPush = oneStep & ~Rank8BB;
            while (quietPush) {
                int to = pop_lsb(&quietPush);
                push_move(to - 8, to, false, false, false, false, 0);
            }
            U64 promoPush = oneStep & Rank8BB;
            while (promoPush) {
                int to = pop_lsb(&promoPush);
                for (int p : promoPieces)
                    push_move(to - 8, to, false, false, false, false, p);
            }
            U64 mid = ((pawns & Rank2BB) << 8) & empty_squares;
            U64 twoStep = (mid << 8) & empty_squares;
            while (twoStep) {
                int to = pop_lsb(&twoStep);
                push_move(to - 16, to, false, false, false, true, 0);
            }
        }

        U64 capL = ((pawns & ~FileABB) << 7) & opp_pieces;
        U64 capR = ((pawns & ~FileHBB) << 9) & opp_pieces;

        U64 nl = capL & ~Rank8BB;
        while (nl) {
            int to = pop_lsb(&nl);
            push_move(to - 7, to, true, false, false, false, 0);
        }
        U64 nr = capR & ~Rank8BB;
        while (nr) {
            int to = pop_lsb(&nr);
            push_move(to - 9, to, true, false, false, false, 0);
        }

        U64 pl = capL & Rank8BB;
        while (pl) {
            int to = pop_lsb(&pl);
            for (int p : promoPieces)
                push_move(to - 7, to, true, false, false, false, p);
        }
        U64 pr = capR & Rank8BB;
        while (pr) {
            int to = pop_lsb(&pr);
            for (int p : promoPieces)
                push_move(to - 9, to, true, false, false, false, p);
        }

        if (ep_file > 0) {
            int ep_sq = sq(ep_file - 1, 5);
            U64 ep = 1ULL << ep_sq;
            U64 fromL = (pawns & ~FileABB) & (ep >> 7);
            U64 fromR = (pawns & ~FileHBB) & (ep >> 9);
            U64 f = fromL | fromR;
            while (f) {
                int from = pop_lsb(&f);
                push_move(from, ep_sq, true, true, false, false, 0);
            }
        }
    } else {
        if (!capturesOnly) {
            U64 oneStep = (pawns >> 8) & empty_squares;
            U64 quietPush = oneStep & ~Rank1BB;
            while (quietPush) {
                int to = pop_lsb(&quietPush);
                push_move(to + 8, to, false, false, false, false, 0);
            }
            U64 promoPush = oneStep & Rank1BB;
            while (promoPush) {
                int to = pop_lsb(&promoPush);
                for (int p : promoPieces)
                    push_move(to + 8, to, false, false, false, false, p);
            }
            U64 mid = ((pawns & Rank7BB) >> 8) & empty_squares;
            U64 twoStep = (mid >> 8) & empty_squares;
            while (twoStep) {
                int to = pop_lsb(&twoStep);
                push_move(to + 16, to, false, false, false, true, 0);
            }
        }

        U64 capR = ((pawns & ~FileHBB) >> 7) & opp_pieces;
        U64 capL = ((pawns & ~FileABB) >> 9) & opp_pieces;

        U64 nr = capR & ~Rank1BB;
        while (nr) {
            int to = pop_lsb(&nr);
            push_move(to + 7, to, true, false, false, false, 0);
        }
        U64 nl = capL & ~Rank1BB;
        while (nl) {
            int to = pop_lsb(&nl);
            push_move(to + 9, to, true, false, false, false, 0);
        }

        U64 pr = capR & Rank1BB;
        while (pr) {
            int to = pop_lsb(&pr);
            for (int p : promoPieces)
                push_move(to + 7, to, true, false, false, false, p);
        }
        U64 pl = capL & Rank1BB;
        while (pl) {
            int to = pop_lsb(&pl);
            for (int p : promoPieces)
                push_move(to + 9, to, true, false, false, false, p);
        }

        if (ep_file > 0) {
            int ep_sq = sq(ep_file - 1, 2);
            U64 ep = 1ULL << ep_sq;
            U64 fromR = (pawns & ~FileHBB) & (ep << 7);
            U64 fromL = (pawns & ~FileABB) & (ep << 9);
            U64 f = fromR | fromL;
            while (f) {
                int from = pop_lsb(&f);
                push_move(from, ep_sq, true, true, false, false, 0);
            }
        }
    }

    // Castling
    if (!capturesOnly && !inCheck(white_to_move)) {
        if (us == WHITE) {
            if ((castling & 8) && !(all_pieces & 0x60ULL) &&
                !isAttacked(5, false) && !isAttacked(6, false)) {
                push_move(sq(4, 0), sq(6, 0), false, false, true, false, 0);
            }
            if ((castling & 4) && !(all_pieces & 0xEULL) &&
                !isAttacked(3, false) && !isAttacked(2, false)) {
                push_move(sq(4, 0), sq(2, 0), false, false, true, false, 0);
            }
        } else {
            if ((castling & 2) && !(all_pieces & 0x6000000000000000ULL) &&
                !isAttacked(61, true) && !isAttacked(62, true)) {
                push_move(sq(4, 7), sq(6, 7), false, false, true, false, 0);
            }
            if ((castling & 1) && !(all_pieces & 0x0E00000000000000ULL) &&
                !isAttacked(59, true) && !isAttacked(58, true)) {
                push_move(sq(4, 7), sq(2, 7), false, false, true, false, 0);
            }
        }
    }

    return count;
}

// ========================= Legal Generation =========================

static bool isEnPassantLegal(int ksq, int from_sq, int epCapturedSq, U64 occ,
                              U64 enemyRooksQueens) {
    U64 occAfter = occ ^ (1ULL << from_sq) ^ (1ULL << epCapturedSq);
    U64 rookAttacks = Magic::rookAttacks(ksq, occAfter);
    return !(rookAttacks & enemyRooksQueens);
}

int Position::generateLegal(Move* moves) {
    Move pseudo[MAX_MOVES];
    int pseudoCount = generatePseudo(pseudo, false);
    int legalCount = 0;

    int us = white_to_move ? WHITE : BLACK;
    int them = us ^ 1;
    int ksq = king_sq[us];
    U64 occ = color_bitboards[WHITE] | color_bitboards[BLACK];
    U64 enemyPieces = color_bitboards[them];

    U64 enemyRooksQueens = (us == WHITE)
        ? (bitboards[BROOK] | bitboards[BQUEEN])
        : (bitboards[WROOK] | bitboards[WQUEEN]);

    U64 checkers = checkersBB(white_to_move);
    U64 pinned = pinnedBB(white_to_move);

    int numCheckers = popcount(checkers);

    U64 checkMask = ~0ULL;
    if (numCheckers == 1) {
        int checkerSq = lsb_index(checkers);
        checkMask = BETWEEN_BB[ksq][checkerSq] | checkers;
    }

    for (int i = 0; i < pseudoCount; ++i) {
        Move& m = pseudo[i];
        int from_sq = moveFrom(m);
        int to_sq = moveTo(m);
        int piece = piece_board[from_sq];
        bool isPinned = (pinned >> from_sq) & 1;
        bool isKingMove = (piece == WKING || piece == BKING);

        if (numCheckers >= 2) {
            if (!isKingMove) continue;
            U64 occWithoutKing = occ ^ (1ULL << ksq);
            if (attackersTo(to_sq, occWithoutKing) & enemyPieces) continue;
            moves[legalCount++] = m;
            continue;
        }

        if (isKingMove) {
            if (moveIsCastle(m)) {
                U64 occWithoutKing = occ ^ (1ULL << ksq);
                if (attackersTo(to_sq, occWithoutKing) & enemyPieces) continue;
                moves[legalCount++] = m;
            } else {
                U64 occWithoutKing = occ ^ (1ULL << ksq);
                if (attackersTo(to_sq, occWithoutKing) & enemyPieces) continue;
                moves[legalCount++] = m;
            }
            continue;
        }

        if (numCheckers == 1) {
            if (moveIsEnPassant(m)) {
                int epCapturedSq = to_sq + (us == WHITE ? -8 : 8);
                if (!((1ULL << epCapturedSq) & checkers)) continue;
                if (!isEnPassantLegal(ksq, from_sq, epCapturedSq, occ, enemyRooksQueens)) continue;
                moves[legalCount++] = m;
                continue;
            }

            if (!((1ULL << to_sq) & checkMask)) continue;
            if (isPinned && !((1ULL << to_sq) & LINE_BB[ksq][from_sq])) continue;
            moves[legalCount++] = m;
            continue;
        }

        if (moveIsEnPassant(m)) {
            int epCapturedSq = to_sq + (us == WHITE ? -8 : 8);
            if (isPinned && !((1ULL << to_sq) & LINE_BB[ksq][from_sq])) continue;
            if (!isEnPassantLegal(ksq, from_sq, epCapturedSq, occ, enemyRooksQueens)) continue;
            moves[legalCount++] = m;
            continue;
        }

        if (isPinned && !((1ULL << to_sq) & LINE_BB[ksq][from_sq])) continue;
        moves[legalCount++] = m;
    }

    return legalCount;
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
