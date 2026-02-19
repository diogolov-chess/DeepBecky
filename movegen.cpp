/*
 * This file is part of Deep Becky 1.1 - A UCI Chess Engine written by AI
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

#include "engine.h"

// ============ Cheque/ataque ============
bool DeepBeckyEngine::isAttacked(int s, bool byWhite) const{
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

bool DeepBeckyEngine::inCheck(bool whiteSide) const{
    return isAttacked(king_sq[whiteSide ? WHITE : BLACK], !whiteSide);
}

// ============ Legalidade ============
bool DeepBeckyEngine::legalMove(const Move& m){
    makeMove(m);
    bool ok = !inCheck(!white_to_move); // Lado que acabou de jogar
    undoMove(m);
    return ok;
}

// ============ Gerar movimentos ============
int DeepBeckyEngine::generatePseudo(Move* mv, bool capturesOnly){
    int count = 0;

    int us = white_to_move ? WHITE : BLACK;
    int them = us ^ 1;

    U64 my_pieces = color_bitboards[us];
    U64 opp_pieces = color_bitboards[them];
    U64 all_pieces = my_pieces | opp_pieces;
    U64 empty_squares = ~all_pieces;

    auto push_move = [&](int from_sq, int to_sq, bool capture, bool enpassant, bool castle, bool doublepush, int promotion){
        if(count >= MAX_MOVES) return;
        if(capturesOnly && !capture && promotion == 0) return;
        Move m;
        m.squares = static_cast<uint16_t>((to_sq << 6) | from_sq);
        uint8_t flags = 0;
        if(capture) flags |= MOVE_FLAG_CAPTURE;
        if(enpassant) flags |= MOVE_FLAG_ENPASSANT;
        if(castle) flags |= MOVE_FLAG_CASTLE;
        if(doublepush) flags |= MOVE_FLAG_DOUBLEPUSH;
        if(promotion != 0) flags |= static_cast<uint8_t>(promotion << MOVE_PROMO_SHIFT);
        m.flags = flags;
        m.score = 0;
        mv[count++] = m;
    };

    // Peças deslizantes e saltadoras
    const int piece_types[] = {
        us==WHITE?WKNIGHT:BKNIGHT, us==WHITE?WBISHOP:BBISHOP, us==WHITE?WROOK:BROOK,
        us==WHITE?WQUEEN:BQUEEN, us==WHITE?WKING:BKING
    };

    for(int piece : piece_types) {
        U64 bb = bitboards[piece];
        while(bb){
            int from_sq = pop_lsb(&bb);
            U64 attacks = 0;
            switch(piece){
                case WKNIGHT: case BKNIGHT: attacks = KNIGHT_ATK_BB[from_sq]; break;
                case WBISHOP: case BBISHOP: attacks = Magic::bishopAttacks(from_sq, all_pieces); break;
                case WROOK:   case BROOK:   attacks = Magic::rookAttacks(from_sq, all_pieces); break;
                case WQUEEN:  case BQUEEN:  attacks = Magic::bishopAttacks(from_sq, all_pieces) | Magic::rookAttacks(from_sq, all_pieces); break;
                case WKING:   case BKING:   attacks = KING_ATK_BB[from_sq]; break;
            }
            U64 targets = capturesOnly ? (attacks & opp_pieces) : (attacks & ~my_pieces);
            while(targets){
                int to_sq = pop_lsb(&targets);
                bool capture = ((opp_pieces >> to_sq) & 1ULL) != 0;
                push_move(from_sq, to_sq, capture, false, false, false, 0);
            }
        }
    }

    // Peões
    constexpr U64 FILE_A = 0x0101010101010101ULL;
    constexpr U64 FILE_H = 0x8080808080808080ULL;
    constexpr U64 RANK_2 = 0x000000000000FF00ULL;
    constexpr U64 RANK_1 = 0x00000000000000FFULL;
    constexpr U64 RANK_8 = 0xFF00000000000000ULL;
    constexpr U64 RANK_7 = 0x00FF000000000000ULL;

    const int pawn_piece = us == WHITE ? WPAWN : BPAWN;
    U64 pawns = bitboards[pawn_piece];
    const int promoPieces[4] = {
        us==WHITE ? WQUEEN : BQUEEN,
        us==WHITE ? WROOK  : BROOK,
        us==WHITE ? WBISHOP: BBISHOP,
        us==WHITE ? WKNIGHT: BKNIGHT
    };

    if (us == WHITE){
        if(!capturesOnly){
            U64 oneStep = (pawns << 8) & empty_squares;
            U64 quietPush = oneStep & ~RANK_8;
            U64 q = quietPush;
            while(q){
                int to = pop_lsb(&q);
                int from = to - 8;
                push_move(from, to, false, false, false, false, 0);
            }
            U64 promoPush = oneStep & RANK_8;
            U64 pp = promoPush;
            while(pp){
                int to = pop_lsb(&pp);
                int from = to - 8;
                for(int p : promoPieces){
                    push_move(from, to, false, false, false, false, p);
                }
            }
            U64 mid = ((pawns & RANK_2) << 8) & empty_squares;
            U64 twoStep = (mid << 8) & empty_squares;
            U64 t = twoStep;
            while(t){
                int to = pop_lsb(&t);
                int from = to - 16;
                push_move(from, to, false, false, false, true, 0);
            }
        }

        U64 capL = ((pawns & ~FILE_A) << 7) & opp_pieces;
        U64 capR = ((pawns & ~FILE_H) << 9) & opp_pieces;

        U64 nl = capL & ~RANK_8;
        while(nl){
            int to = pop_lsb(&nl);
            int from = to - 7;
            push_move(from, to, true, false, false, false, 0);
        }
        U64 nr = capR & ~RANK_8;
        while(nr){
            int to = pop_lsb(&nr);
            int from = to - 9;
            push_move(from, to, true, false, false, false, 0);
        }

        U64 pl = capL & RANK_8;
        while(pl){
            int to = pop_lsb(&pl);
            int from = to - 7;
            for(int p : promoPieces){
                push_move(from, to, true, false, false, false, p);
            }
        }
        U64 pr = capR & RANK_8;
        while(pr){
            int to = pop_lsb(&pr);
            int from = to - 9;
            for(int p : promoPieces){
                push_move(from, to, true, false, false, false, p);
            }
        }

        if(ep_file > 0){
            int ep_sq = sq(ep_file - 1, 5);
            U64 ep = 1ULL << ep_sq;
            U64 fromL = (pawns & ~FILE_A) & (ep >> 7);
            U64 fromR = (pawns & ~FILE_H) & (ep >> 9);
            U64 f = fromL | fromR;
            while(f){
                int from = pop_lsb(&f);
                push_move(from, ep_sq, true, true, false, false, 0);
            }
        }
    } else {
        if(!capturesOnly){
            U64 oneStep = (pawns >> 8) & empty_squares;
            U64 quietPush = oneStep & ~RANK_1;
            U64 q = quietPush;
            while(q){
                int to = pop_lsb(&q);
                int from = to + 8;
                push_move(from, to, false, false, false, false, 0);
            }
            U64 promoPush = oneStep & RANK_1;
            U64 pp = promoPush;
            while(pp){
                int to = pop_lsb(&pp);
                int from = to + 8;
                for(int p : promoPieces){
                    push_move(from, to, false, false, false, false, p);
                }
            }
            U64 mid = ((pawns & RANK_7) >> 8) & empty_squares;
            U64 twoStep = (mid >> 8) & empty_squares;
            U64 t = twoStep;
            while(t){
                int to = pop_lsb(&t);
                int from = to + 16;
                push_move(from, to, false, false, false, true, 0);
            }
        }

        U64 capR = ((pawns & ~FILE_H) >> 7) & opp_pieces;
        U64 capL = ((pawns & ~FILE_A) >> 9) & opp_pieces;

        U64 nr = capR & ~RANK_1;
        while(nr){
            int to = pop_lsb(&nr);
            int from = to + 7;
            push_move(from, to, true, false, false, false, 0);
        }
        U64 nl = capL & ~RANK_1;
        while(nl){
            int to = pop_lsb(&nl);
            int from = to + 9;
            push_move(from, to, true, false, false, false, 0);
        }

        U64 pr = capR & RANK_1;
        while(pr){
            int to = pop_lsb(&pr);
            int from = to + 7;
            for(int p : promoPieces){
                push_move(from, to, true, false, false, false, p);
            }
        }
        U64 pl = capL & RANK_1;
        while(pl){
            int to = pop_lsb(&pl);
            int from = to + 9;
            for(int p : promoPieces){
                push_move(from, to, true, false, false, false, p);
            }
        }

        if(ep_file > 0){
            int ep_sq = sq(ep_file - 1, 2);
            U64 ep = 1ULL << ep_sq;
            U64 fromR = (pawns & ~FILE_H) & (ep << 7);
            U64 fromL = (pawns & ~FILE_A) & (ep << 9);
            U64 f = fromR | fromL;
            while(f){
                int from = pop_lsb(&f);
                push_move(from, ep_sq, true, true, false, false, 0);
            }
        }
    }

    if(!capturesOnly && !inCheck(white_to_move)){
        if(us==WHITE){
            if((castling & 8) && !(all_pieces & 0x60ULL) && !isAttacked(5, them==WHITE) && !isAttacked(6, them==WHITE)){
                int from_sq = sq(4,0);
                int to_sq = sq(6,0);
                push_move(from_sq, to_sq, false, false, true, false, 0);
            }
            if((castling & 4) && !(all_pieces & 0xEULL) && !isAttacked(3, them==WHITE) && !isAttacked(2, them==WHITE)){
                int from_sq = sq(4,0);
                int to_sq = sq(2,0);
                push_move(from_sq, to_sq, false, false, true, false, 0);
            }
        } else {
            if((castling & 2) && !(all_pieces & 0x6000000000000000ULL) && !isAttacked(61, them==WHITE) && !isAttacked(62, them==WHITE)){
                int from_sq = sq(4,7);
                int to_sq = sq(6,7);
                push_move(from_sq, to_sq, false, false, true, false, 0);
            }
            if((castling & 1) && !(all_pieces & 0x0E00000000000000ULL) && !isAttacked(59, them==WHITE) && !isAttacked(58, them==WHITE)){
                int from_sq = sq(4,7);
                int to_sq = sq(2,7);
                push_move(from_sq, to_sq, false, false, true, false, 0);
            }
        }
    }

    return count;
}


int DeepBeckyEngine::generateLegal(Move* moves){
    Move pseudo[MAX_MOVES];
    int pseudoCount = generatePseudo(pseudo, false);
    int legalCount = 0;
    for(int i=0;i<pseudoCount;++i){
        Move& m = pseudo[i];
        makeMove(m);
        if(!inCheck(!white_to_move)) {
            moves[legalCount++] = m;
        }
        undoMove(m);
    }
    return legalCount;
}