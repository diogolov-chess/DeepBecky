#include "engine.h"

// ============ Cheque/ataque (VERSÃO BITBOARD) ============
bool DeepBeckyEngine::isAttacked(int s, bool byWhite){
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

bool DeepBeckyEngine::inCheck(bool whiteSide){
    return isAttacked(king_sq[whiteSide ? WHITE : BLACK], !whiteSide);
}

// ============ Legalidade ============
bool DeepBeckyEngine::legalMove(const Move& m){
    makeMove(m);
    bool ok = !inCheck(!white_to_move); // Lado que acabou de jogar
    undoMove(m);
    return ok;
}

// ============ Gerar movimentos (VERSÃO BITBOARD) ============
vector<Move> DeepBeckyEngine::generatePseudo(bool capturesOnly){
    vector<Move> mv; mv.reserve(64);
    
    int us = white_to_move ? WHITE : BLACK;
    int them = !us;

    U64 my_pieces = color_bitboards[us];
    U64 opp_pieces = color_bitboards[them];
    U64 all_pieces = my_pieces | opp_pieces;
    U64 empty_squares = ~all_pieces;

    auto add_moves = [&](int from_sq, U64 attack_bb, int piece) {
        while(attack_bb){
            int to_sq = pop_lsb(&attack_bb);
            Move m;
            m.from_x = sq_x(from_sq); m.from_y = sq_y(from_sq);
            m.to_x = sq_x(to_sq);   m.to_y = sq_y(to_sq);
            m.piece_moved = piece;
            m.captured_piece = piece_board[to_sq];
            m.is_capture = (m.captured_piece != EMPTY);
            mv.push_back(m);
        }
    };
    
    auto add_pawn_moves = [&](int from_sq, int to_sq, int piece, bool is_double, bool is_ep) {
        int promo_rank = us == WHITE ? 7 : 0;
        if (sq_y(to_sq) == promo_rank) {
            int promos[] = {us==WHITE?WQUEEN:BQUEEN, us==WHITE?WROOK:BROOK, us==WHITE?WBISHOP:BBISHOP, us==WHITE?WKNIGHT:BKNIGHT};
            for(int p : promos){
                Move m;
                m.from_x = sq_x(from_sq); m.from_y = sq_y(from_sq);
                m.to_x = sq_x(to_sq); m.to_y = sq_y(to_sq);
                m.piece_moved = piece;
                m.captured_piece = is_ep ? (us==WHITE?BPAWN:WPAWN) : piece_board[to_sq];
                m.is_capture = (m.captured_piece != EMPTY);
                m.promotion = p;
                mv.push_back(m);
            }
        } else {
            Move m;
            m.from_x = sq_x(from_sq); m.from_y = sq_y(from_sq);
            m.to_x = sq_x(to_sq); m.to_y = sq_y(to_sq);
            m.piece_moved = piece;
            m.captured_piece = is_ep ? (us==WHITE?BPAWN:WPAWN) : piece_board[to_sq];
            m.is_capture = (m.captured_piece != EMPTY);
            m.is_enpassant = is_ep;
            m.is_doublepush = is_double;
            mv.push_back(m);
        }
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
            add_moves(from_sq, attacks & (capturesOnly ? opp_pieces : ~my_pieces), piece);
        }
    }

    
    
    // Peões (fast-path com bit operations: sem loop por peão)
    {
        constexpr U64 FILE_A = 0x0101010101010101ULL;
        constexpr U64 FILE_H = 0x8080808080808080ULL;
        constexpr U64 RANK_2 = 0x000000000000FF00ULL;
        constexpr U64 RANK_1 = 0x00000000000000FFULL;
        constexpr U64 RANK_8 = 0xFF00000000000000ULL;
        constexpr U64 RANK_7 = 0x00FF000000000000ULL;

        const int pawn_piece = us == WHITE ? WPAWN : BPAWN;
        U64 pawns = bitboards[pawn_piece];

        if (us == WHITE){
            if(!capturesOnly){
                // Avanço simples
                U64 oneStep = (pawns << 8) & empty_squares;
                // Quiet (sem promoção)
                U64 quietPush = oneStep & ~RANK_8;
                U64 q = quietPush;
                while(q){
                    int to = pop_lsb(&q);
                    int from = to - 8;
                    add_pawn_moves(from, to, pawn_piece, false, false);
                }
                // Promoção por avanço
                U64 promoPush = oneStep & RANK_8;
                U64 pp = promoPush;
                while(pp){
                    int to = pop_lsb(&pp);
                    int from = to - 8;
                    add_pawn_moves(from, to, pawn_piece, false, false);
                }
                // Avanço duplo da 2ª fileira (ambas casas vazias)
                U64 mid = ((pawns & RANK_2) << 8) & empty_squares;
                U64 twoStep = (mid << 8) & empty_squares;
                U64 t = twoStep;
                while(t){
                    int to = pop_lsb(&t);
                    int from = to - 16;
                    add_pawn_moves(from, to, pawn_piece, true, false);
                }
            }
            // Capturas separadas (evita perda quando duas peças atacam mesmo destino)
            U64 capL = ((pawns & ~FILE_A) << 7) & opp_pieces; // NW
            U64 capR = ((pawns & ~FILE_H) << 9) & opp_pieces; // NE

            // Não-promocionais
            U64 nl = capL & ~RANK_8;
            while(nl){
                int to = pop_lsb(&nl);
                int from = to - 7;
                add_pawn_moves(from, to, pawn_piece, false, false);
            }
            U64 nr = capR & ~RANK_8;
            while(nr){
                int to = pop_lsb(&nr);
                int from = to - 9;
                add_pawn_moves(from, to, pawn_piece, false, false);
            }
            // Capturas com promoção
            U64 pl = capL & RANK_8;
            while(pl){
                int to = pop_lsb(&pl);
                int from = to - 7;
                add_pawn_moves(from, to, pawn_piece, false, false);
            }
            U64 pr = capR & RANK_8;
            while(pr){
                int to = pop_lsb(&pr);
                int from = to - 9;
                add_pawn_moves(from, to, pawn_piece, false, false);
            }

            // En passant
            if(ep_file > 0){
                int ep_sq = sq(ep_file - 1, 5); // destino do EP para as brancas
                U64 ep = 1ULL << ep_sq;
                U64 fromL = (pawns & ~FILE_A) & (ep >> 7);
                U64 fromR = (pawns & ~FILE_H) & (ep >> 9);
                U64 f = fromL | fromR;
                while(f){
                    int from = pop_lsb(&f);
                    add_pawn_moves(from, ep_sq, pawn_piece, false, true);
                }
            }
        } else {
            if(!capturesOnly){
                // Avanço simples
                U64 oneStep = (pawns >> 8) & empty_squares;
                // Quiet (sem promoção)
                U64 quietPush = oneStep & ~RANK_1;
                U64 q = quietPush;
                while(q){
                    int to = pop_lsb(&q);
                    int from = to + 8;
                    add_pawn_moves(from, to, pawn_piece, false, false);
                }
                // Promoção por avanço
                U64 promoPush = oneStep & RANK_1;
                U64 pp = promoPush;
                while(pp){
                    int to = pop_lsb(&pp);
                    int from = to + 8;
                    add_pawn_moves(from, to, pawn_piece, false, false);
                }
                // Avanço duplo da 7ª fileira
                U64 mid = ((pawns & RANK_7) >> 8) & empty_squares;
                U64 twoStep = (mid >> 8) & empty_squares;
                U64 t = twoStep;
                while(t){
                    int to = pop_lsb(&t);
                    int from = to + 16;
                    add_pawn_moves(from, to, pawn_piece, true, false);
                }
            }
            // Capturas separadas
            U64 capR = ((pawns & ~FILE_H) >> 7) & opp_pieces; // SE (do ponto de vista do tabuleiro)
            U64 capL = ((pawns & ~FILE_A) >> 9) & opp_pieces; // SW

            // Não-promocionais
            U64 nr = capR & ~RANK_1;
            while(nr){
                int to = pop_lsb(&nr);
                int from = to + 7;
                add_pawn_moves(from, to, pawn_piece, false, false);
            }
            U64 nl = capL & ~RANK_1;
            while(nl){
                int to = pop_lsb(&nl);
                int from = to + 9;
                add_pawn_moves(from, to, pawn_piece, false, false);
            }
            // Capturas com promoção
            U64 pr = capR & RANK_1;
            while(pr){
                int to = pop_lsb(&pr);
                int from = to + 7;
                add_pawn_moves(from, to, pawn_piece, false, false);
            }
            U64 pl = capL & RANK_1;
            while(pl){
                int to = pop_lsb(&pl);
                int from = to + 9;
                add_pawn_moves(from, to, pawn_piece, false, false);
            }

            // En passant
            if(ep_file > 0){
                int ep_sq = sq(ep_file - 1, 2); // destino do EP para as pretas
                U64 ep = 1ULL << ep_sq;
                U64 fromR = (pawns & ~FILE_H) & (ep << 7);
                U64 fromL = (pawns & ~FILE_A) & (ep << 9);
                U64 f = fromR | fromL;
                while(f){
                    int from = pop_lsb(&f);
                    add_pawn_moves(from, ep_sq, pawn_piece, false, true);
                }
            }
        }
    }
// Roque
    if(!capturesOnly && !inCheck(white_to_move)){
        if(us==WHITE){
            if((castling & 8) && !(all_pieces & 0x60ULL) && !isAttacked(5, (them==WHITE)) && !isAttacked(6, (them==WHITE))){
                Move m; m.from_x=4;m.from_y=0;m.to_x=6;m.to_y=0;m.is_castle=true;m.piece_moved=WKING; mv.push_back(m);
            }
            if((castling & 4) && !(all_pieces & 0xEULL) && !isAttacked(3, (them==WHITE)) && !isAttacked(2, (them==WHITE))){
                Move m; m.from_x=4;m.from_y=0;m.to_x=2;m.to_y=0;m.is_castle=true;m.piece_moved=WKING; mv.push_back(m);
            }
        } else {
             if((castling & 2) && !(all_pieces & 0x6000000000000000ULL) && !isAttacked(61, (them==WHITE)) && !isAttacked(62, (them==WHITE))){
                Move m; m.from_x=4;m.from_y=7;m.to_x=6;m.to_y=7;m.is_castle=true;m.piece_moved=BKING; mv.push_back(m);
            }
            if((castling & 1) && !(all_pieces & 0xE00000000000000ULL) && !isAttacked(59, (them==WHITE)) && !isAttacked(58, (them==WHITE))){
                Move m; m.from_x=4;m.from_y=7;m.to_x=2;m.to_y=7;m.is_castle=true;m.piece_moved=BKING; mv.push_back(m);
            }
        }
    }

    return mv;
}


vector<Move> DeepBeckyEngine::generateLegal(){
    vector<Move> pseudo = generatePseudo(false);
    vector<Move> legal;
    legal.reserve(pseudo.size());
    for(auto &m : pseudo){
        makeMove(m);
        if(!inCheck(!white_to_move)) {
            legal.push_back(m);
        }
        undoMove(m);
    }
    return legal;
}