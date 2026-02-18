#include "engine.h"

// PST simples (espelhagem para pretas)
static const int PST_PAWN[64] = {
     0,  5,  5, -5, -5,  5,  5,  0, 0, 10, -5,  0,  0, -5, 10,  0,
     0, 10, 10, 20, 20, 10, 10,  0, 5, 15, 20, 25, 25, 20, 15,  5,
    10, 20, 25, 30, 30, 25, 20, 10, 15, 25, 30, 35, 35, 30, 25, 15,
    30, 40, 45, 50, 50, 45, 40, 30,  0,  0,  0,  0,  0,  0,  0,  0
};
static const int PST_KNIGHT[64] = {
   -30,-10,-10,-10,-10,-10,-10,-30, -10,  0,  5,  0,  0,  5,  0,-10,
   -10,  5, 10, 10, 10, 10,  5,-10, -10,  0, 10, 15, 15, 10,  0,-10,
   -10,  0, 10, 15, 15, 10,  0,-10, -10,  5, 10, 10, 10, 10,  5,-10,
   -10,  0,  5,  0,  0,  5,  0,-10, -30,-10,-10,-10,-10,-10,-10,-30
};
static const int PST_BISHOP[64] = {
   -20,-10,-10,-10,-10,-10,-10,-20, -10, 10,  0,  5,  5,  0, 10,-10,
   -10,  5, 10, 10, 10, 10,  5,-10, -10,  0, 10, 10, 10, 10,  0,-10,
   -10,  0, 10, 10, 10, 10,  0,-10, -10,  5, 10, 10, 10, 10,  5,-10,
   -10, 10,  0,  5,  5,  0, 10,-10, -20,-10,-10,-10,-10,-10,-10,-20
};
static const int PST_ROOK[64] = {
     0,  0,  5, 10, 10,  5,  0,  0, -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5, -5,  0,  0,  5,  5,  0,  0, -5,
    -5,  0,  0,  5,  5,  0,  0, -5, -5,  0,  0,  0,  0,  0,  0, -5,
     5, 10, 10, 10, 10, 10, 10,  5,  0,  0,  0,  0,  0,  0,  0,  0
};
static const int PST_QUEEN[64] = {
   -20,-10,-10, -5, -5,-10,-10,-20, -10,  0,  5,  0,  0,  0,  0,-10,
   -10,  5,  5,  5,  5,  5,  0,-10,  -5,  0,  5,  5,  5,  5,  0, -5,
    -5,  0,  5,  5,  5,  5,  0, -5, -10,  0,  5,  5,  5,  5,  0,-10,
   -10,  0,  0,  0,  0,  0,  0,-10, -20,-10,-10, -5, -5,-10,-10,-20
};
static const int PST_KING_MG[64] = {
   -30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30,
   -20,-30,-30,-40,-40,-30,-30,-20, -10,-20,-20,-20,-20,-20,-20,-10,
    20, 20,  0,  0,  0,  0, 20, 20,  20, 30, 10,  0,  0, 10, 30, 20
};
static const int PST_KING_EG[64] = {
   -50,-30,-30,-30,-30,-30,-30,-50, -30,-20,-10,  0,  0,-10,-20,-30,
   -30,-10, 20, 30, 30, 20,-10,-30, -30,-10, 30, 40, 40, 30,-10,-30,
   -30,-10, 30, 40, 40, 30,-10,-30, -30,-10, 20, 30, 30, 20,-10,-30,
   -30,-30,-10,  0,  0,-10,-30,-30, -50,-30,-30,-30,-30,-30,-30,-50
};

static inline int pstWhite(int p, int sqi){
    switch(p){
        case WPAWN:   return PST_PAWN[sqi];
        case WKNIGHT: return PST_KNIGHT[sqi];
        case WBISHOP: return PST_BISHOP[sqi];
        case WROOK:   return PST_ROOK[sqi];
        case WQUEEN:  return PST_QUEEN[sqi];
        case WKING:   return PST_KING_MG[sqi];
        default: return 0;
    }
}
static inline int pstBlack(int p, int sqi){
    int r = 56 ^ sqi;
    switch(p){
        case BPAWN:   return PST_PAWN[r];
        case BKNIGHT: return PST_KNIGHT[r];
        case BBISHOP: return PST_BISHOP[r];
        case BROOK:   return PST_ROOK[r];
        case BQUEEN:  return PST_QUEEN[r];
        case BKING:   return PST_KING_MG[r];
        default: return 0;
    }
}

// ==== Helpers PST: Middle-Game / End-Game ====
static inline int pstWhiteMG(int p, int sqi){
    switch(p){
        case WPAWN:   return PST_PAWN[sqi];
        case WKNIGHT: return PST_KNIGHT[sqi];
        case WBISHOP: return PST_BISHOP[sqi];
        case WROOK:   return PST_ROOK[sqi];
        case WQUEEN:  return PST_QUEEN[sqi];
        case WKING:   return PST_KING_MG[sqi];
        default: return 0;
    }
}
static inline int pstWhiteEG(int p, int sqi){
    switch(p){
        case WPAWN:   return PST_PAWN[sqi];
        case WKNIGHT: return PST_KNIGHT[sqi];
        case WBISHOP: return PST_BISHOP[sqi];
        case WROOK:   return PST_ROOK[sqi];
        case WQUEEN:  return PST_QUEEN[sqi];
        case WKING:   return PST_KING_EG[sqi];
        default: return 0;
    }
}
static inline int pstBlackMG(int p, int sqi){
    int r = 56 ^ sqi;
    switch(p){
        case BPAWN:   return PST_PAWN[r];
        case BKNIGHT: return PST_KNIGHT[r];
        case BBISHOP: return PST_BISHOP[r];
        case BROOK:   return PST_ROOK[r];
        case BQUEEN:  return PST_QUEEN[r];
        case BKING:   return PST_KING_MG[r];
        default: return 0;
    }
}
static inline int pstBlackEG(int p, int sqi){
    int r = 56 ^ sqi;
    switch(p){
        case BPAWN:   return PST_PAWN[r];
        case BKNIGHT: return PST_KNIGHT[r];
        case BBISHOP: return PST_BISHOP[r];
        case BROOK:   return PST_ROOK[r];
        case BQUEEN:  return PST_QUEEN[r];
        case BKING:   return PST_KING_EG[r];
        default: return 0;
    }
}


// ============ Avaliação (VERSÃO BITBOARD) ============

int DeepBeckyEngine::evaluate(){
    // ---------- Helpers ----------
    auto file_of = [](int s){ return s & 7; };
    auto rank_of = [](int s){ return s >> 3; };
    auto make_sq = [](int f,int r){ return (r<<3) | f; };
    auto file_mask = [&](int f)->U64 { return 0x0101010101010101ULL << f; };
    auto in_front_mask_white = [&](int s)->U64 { return (~0ULL) << ((rank_of(s)+1)*8); };
    auto in_front_mask_black = [&](int s)->U64 { return ((rank_of(s)*8)==0? 0ULL : ((1ULL << (rank_of(s)*8)) - 1ULL)); };

    // ---------- Base material + PST (MG/EG) ----------
    int matW=0, matB=0;
    int pstMG=0, pstEG=0;
    int phaseCount=0;

    U64 occupancy = color_bitboards[WHITE] | color_bitboards[BLACK];

    for(int p = WPAWN; p <= BKING; ++p){
        U64 bb = bitboards[p];
        int piece_val = PIECE_VALUE[p];
        while(bb){
            int sqi = pop_lsb(&bb);
            if(isWhitePiece(p)){
                matW += piece_val;
                pstMG += PST_PAWN[sqi]   * (p==WPAWN)   + PST_KNIGHT[sqi] * (p==WKNIGHT) + 
                         PST_BISHOP[sqi] * (p==WBISHOP) + PST_ROOK[sqi]   * (p==WROOK) + 
                         PST_QUEEN[sqi]  * (p==WQUEEN)  + PST_KING_MG[sqi]* (p==WKING);
                pstEG += PST_PAWN[sqi]   * (p==WPAWN)   + PST_KNIGHT[sqi] * (p==WKNIGHT) + 
                         PST_BISHOP[sqi] * (p==WBISHOP) + PST_ROOK[sqi]   * (p==WROOK) + 
                         PST_QUEEN[sqi]  * (p==WQUEEN)  + PST_KING_EG[sqi]* (p==WKING);
            } else {
                matB += piece_val;
                int r_sqi = 56 ^ sqi; // mirror for black
                pstMG -= (PST_PAWN[r_sqi]   * (p==BPAWN)   + PST_KNIGHT[r_sqi] * (p==BKNIGHT) + 
                          PST_BISHOP[r_sqi] * (p==BBISHOP) + PST_ROOK[r_sqi]   * (p==BROOK) + 
                          PST_QUEEN[r_sqi]  * (p==BQUEEN)  + PST_KING_MG[r_sqi]* (p==BKING));
                pstEG -= (PST_PAWN[r_sqi]   * (p==BPAWN)   + PST_KNIGHT[r_sqi] * (p==BKNIGHT) + 
                          PST_BISHOP[r_sqi] * (p==BBISHOP) + PST_ROOK[r_sqi]   * (p==BROOK) + 
                          PST_QUEEN[r_sqi]  * (p==BQUEEN)  + PST_KING_EG[r_sqi]* (p==BKING));
            }

            // phase
            if(p==WKNIGHT || p==BKNIGHT || p==WBISHOP || p==BBISHOP) phaseCount += 1;
            else if(p==WROOK || p==BROOK) phaseCount += 2;
            else if(p==WQUEEN || p==BQUEEN) phaseCount += 4;
        }
    }

    // Bishop pair (small)
    if(Magic::popcount64(bitboards[WBISHOP]) >= 2) { pstMG += 25; pstEG += 25; }
    if(Magic::popcount64(bitboards[BBISHOP]) >= 2) { pstMG -= 25; pstEG -= 25; }

    // ---------- Mobility (more complete) ----------
    int mobMG = 0, mobEG = 0;
    U64 whitePieces = color_bitboards[WHITE];
    U64 blackPieces = color_bitboards[BLACK];

    // Knights
    {
        U64 bb = bitboards[WKNIGHT];
        while(bb){
            int s = pop_lsb(&bb);
            U64 moves = KNIGHT_ATK_BB[s] & ~whitePieces;
            int m = Magic::popcount64(moves);
            mobMG += 4 * m; mobEG += 3 * m;
        }
        bb = bitboards[BKNIGHT];
        while(bb){
            int s = pop_lsb(&bb);
            U64 moves = KNIGHT_ATK_BB[s] & ~blackPieces;
            int m = Magic::popcount64(moves);
            mobMG -= 4 * m; mobEG -= 3 * m;
        }
    }
    // Bishops
    {
        U64 bb = bitboards[WBISHOP];
        while(bb){
            int s = pop_lsb(&bb);
            U64 moves = Magic::bishopAttacks(s, occupancy) & ~whitePieces;
            int m = Magic::popcount64(moves);
            mobMG += 3 * m; mobEG += 4 * m;
        }
        bb = bitboards[BBISHOP];
        while(bb){
            int s = pop_lsb(&bb);
            U64 moves = Magic::bishopAttacks(s, occupancy) & ~blackPieces;
            int m = Magic::popcount64(moves);
            mobMG -= 3 * m; mobEG -= 4 * m;
        }
    }
    // Rooks
    {
        U64 bb = bitboards[WROOK];
        while(bb){
            int s = pop_lsb(&bb);
            U64 moves = Magic::rookAttacks(s, occupancy) & ~whitePieces;
            int m = Magic::popcount64(moves);
            mobMG += 2 * m; mobEG += 2 * m;
        }
        bb = bitboards[BROOK];
        while(bb){
            int s = pop_lsb(&bb);
            U64 moves = Magic::rookAttacks(s, occupancy) & ~blackPieces;
            int m = Magic::popcount64(moves);
            mobMG -= 2 * m; mobEG -= 2 * m;
        }
    }
    // Queens
    {
        U64 bb = bitboards[WQUEEN];
        while(bb){
            int s = pop_lsb(&bb);
            U64 moves = (Magic::rookAttacks(s, occupancy) | Magic::bishopAttacks(s, occupancy)) & ~whitePieces;
            int m = Magic::popcount64(moves);
            mobMG += 1 * m; mobEG += 2 * m;
        }
        bb = bitboards[BQUEEN];
        while(bb){
            int s = pop_lsb(&bb);
            U64 moves = (Magic::rookAttacks(s, occupancy) | Magic::bishopAttacks(s, occupancy)) & ~blackPieces;
            int m = Magic::popcount64(moves);
            mobMG -= 1 * m; mobEG -= 2 * m;
        }
    }

    // ---------- Pawn structure ----------
    int pawnMG = 0, pawnEG = 0;
    U64 wp = bitboards[WPAWN];
    U64 bp = bitboards[BPAWN];

    // Doubled pawns (per extra pawn in same file)
    for(int f=0; f<8; ++f){
        int wc = Magic::popcount64(wp & file_mask(f));
        if(wc > 1){ pawnMG -= 12 * (wc-1); pawnEG -= 8 * (wc-1); }
        int bc = Magic::popcount64(bp & file_mask(f));
        if(bc > 1){ pawnMG += 12 * (bc-1); pawnEG += 8 * (bc-1); } // from white's POV subtracting black -> add
    }

    // Isolated pawns
    auto is_isolated = [&](bool white, int sq)->bool{
        int f = file_of(sq);
        U64 my = white ? wp : bp;
        U64 left  = (f>0) ? (my & file_mask(f-1)) : 0ULL;
        U64 right = (f<7) ? (my & file_mask(f+1)) : 0ULL;
        return (left|right)==0ULL;
    };

    {
        U64 bb = wp;
        while(bb){
            int s = pop_lsb(&bb);
            if(is_isolated(true,s)){ pawnMG -= 15; pawnEG -= 10; }
        }
        bb = bp;
        while(bb){
            int s = pop_lsb(&bb);
            if(is_isolated(false,s)){ pawnMG += 15; pawnEG += 10; }
        }
    }

    // Passed pawns
    static const int PASSED_MG[8] = {0, 5, 10, 20, 35, 60, 90, 0};
    static const int PASSED_EG[8] = {0,10, 20, 40, 70,110,180, 0};

    auto is_passed_white = [&](int s)->bool{
        int f = file_of(s);
        U64 files = file_mask(f) | (f>0?file_mask(f-1):0ULL) | (f<7?file_mask(f+1):0ULL);
        U64 infront = in_front_mask_white(s);
        return (bp & files & infront) == 0ULL;
    };
    auto is_passed_black = [&](int s)->bool{
        int f = file_of(s);
        U64 files = file_mask(f) | (f>0?file_mask(f-1):0ULL) | (f<7?file_mask(f+1):0ULL);
        U64 infront = in_front_mask_black(s);
        return (wp & files & infront) == 0ULL;
    };

    {
        U64 bb = wp;
        while(bb){
            int s = pop_lsb(&bb);
            if(is_passed_white(s)){
                int r = rank_of(s);
                pawnMG += PASSED_MG[r];
                pawnEG += PASSED_EG[r];
            }
        }
        bb = bp;
        while(bb){
            int s = pop_lsb(&bb);
            if(is_passed_black(s)){
                int r = 7 - rank_of(s);
                pawnMG -= PASSED_MG[r];
                pawnEG -= PASSED_EG[r];
            }
        }
    }

    // ---------- Rooks on open / semi-open files ----------
    int rookMG = 0, rookEG = 0;
    auto is_open_file_for = [&](bool white, int f)->int{
        U64 myPawns  = white ? wp : bp;
        U64 oppPawns = white ? bp : wp;
        bool my = (myPawns & file_mask(f)) == 0ULL;
        bool op = (oppPawns & file_mask(f)) == 0ULL;
        return my && op ? 2 : (my ? 1 : 0); // 2=open, 1=semi-open, 0=closed
    };
    {
        U64 bb = bitboards[WROOK];
        while(bb){
            int s = pop_lsb(&bb);
            int f = file_of(s);
            int ty = is_open_file_for(true, f);
            if(ty==1){ rookMG += 12; rookEG += 8; }
            else if(ty==2){ rookMG += 24; rookEG += 12; }
        }
        bb = bitboards[BROOK];
        while(bb){
            int s = pop_lsb(&bb);
            int f = file_of(s);
            int ty = is_open_file_for(false, f);
            if(ty==1){ rookMG -= 12; rookEG -= 8; }
            else if(ty==2){ rookMG -= 24; rookEG -= 12; }
        }
    }

    // ---------- Knights on outposts ----------
    int outMG = 0, outEG = 0;
    auto supported_by_pawn = [&](bool white, int s)->bool{
        return white ? ( (WPAWN_ATK_BB[s] & wp) != 0ULL ) : ( (BPAWN_ATK_BB[s] & bp) != 0ULL );
    };
    {
        U64 bb = bitboards[WKNIGHT];
        while(bb){
            int s = pop_lsb(&bb);
            if( (BPAWN_ATK_BB[s] & bp)==0ULL && rank_of(s) >= 3 ){ // 4th rank or beyond
                outMG += supported_by_pawn(true,s) ? 20 : 10;
                outEG += 10;
            }
        }
        bb = bitboards[BKNIGHT];
        while(bb){
            int s = pop_lsb(&bb);
            if( (WPAWN_ATK_BB[s] & wp)==0ULL && rank_of(s) <= 4 ){ // 5th rank or beyond for black
                outMG -= supported_by_pawn(false,s) ? 20 : 10;
                outEG -= 10;
            }
        }
    }

    // ---------- King safety ----------
    int ksMG = 0, ksEG = 0;

    auto king_square = [&](bool white)->int{
        U64 bb = white ? bitboards[WKING] : bitboards[BKING];
        if(!bb) return -1;
        return lsb_index(bb);
    };

    auto pawn_shield_score = [&](bool white)->int{
        int ks = king_square(white);
        if(ks<0) return 0;
        int f = file_of(ks);
        int r = rank_of(ks);
        int sc = 0;
        auto have_pawn = [&](int ff,int rr)->bool{
            if(ff<0||ff>7||rr<0||rr>7) return false;
            int s = make_sq(ff,rr);
            U64 bb = white ? wp : bp;
            return (bb >> s) & 1ULL;
        };
        if(white){
            if(r<=6){
                sc += have_pawn(f, r+1) ? 10 : -12;
                sc += have_pawn(f-1, r+1) ? 6 : -8;
                sc += have_pawn(f+1, r+1) ? 6 : -8;
                if(r<=5){
                    sc += have_pawn(f, r+2) ? 4 : -4;
                }
            }
        }else{
            if(r>=1){
                sc += have_pawn(f, r-1) ? 10 : -12;
                sc += have_pawn(f-1, r-1) ? 6 : -8;
                sc += have_pawn(f+1, r-1) ? 6 : -8;
                if(r>=2){
                    sc += have_pawn(f, r-2) ? 4 : -4;
                }
            }
        }
        return sc;
    };

    auto king_ring_pressure = [&](bool white)->int{
        int ks = king_square(white);
        if(ks<0) return 0;
        U64 ring = KING_ATK_BB[ks]; // 8 surrounding squares
        int sc = 0;
        // accumulate enemy attacks onto ring squares
        U64 eKnights = white ? bitboards[BKNIGHT] : bitboards[WKNIGHT];
        U64 eBishops = white ? bitboards[BBISHOP] : bitboards[WBISHOP];
        U64 eRooks   = white ? bitboards[BROOK]   : bitboards[WROOK];
        U64 eQueens  = white ? bitboards[BQUEEN]  : bitboards[WQUEEN];
        U64 eKing    = white ? bitboards[BKING]   : bitboards[WKING];
        U64 ePawns   = white ? bitboards[BPAWN]   : bitboards[WPAWN];

        U64 rs = ring;
        while(rs){
            int s = pop_lsb(&rs);
            sc += 9 * Magic::popcount64(KNIGHT_ATK_BB[s] & eKnights);
            sc += 7 * Magic::popcount64(KING_ATK_BB[s] & eKing);
            // Pawn attacks use reverse tables
            sc += 5 * Magic::popcount64( (white ? BPAWN_ATK_BB[s] : WPAWN_ATK_BB[s]) & ePawns );
            // Sliders
            sc += 7 * Magic::popcount64( Magic::bishopAttacks(s, occupancy) & (eBishops | eQueens) );
            sc += 6 * Magic::popcount64( Magic::rookAttacks(s,   occupancy) & (eRooks   | eQueens) );
        }
        return sc;
    };

    // King safety mostly matters in middlegame
    ksMG += pawn_shield_score(true);
    ksMG -= pawn_shield_score(false);
    ksMG -= king_ring_pressure(true);
    ksMG += king_ring_pressure(false);

    // In endgames, reward king activity a bit (mobility)
    {
        int ws = king_square(true);
        if(ws>=0){
            int m = Magic::popcount64(KING_ATK_BB[ws] & ~whitePieces);
            ksEG += 6 * m;
        }
        int bs = king_square(false);
        if(bs>=0){
            int m = Magic::popcount64(KING_ATK_BB[bs] & ~blackPieces);
            ksEG -= 6 * m;
        }
    }

    // ---------- Tempo ----------
    int tempoMG = white_to_move ? 10 : -10;
    int tempoEG = white_to_move ? 5  : -5;

    // ---------- Blend MG/EG ----------
    int mat = (matW - matB);
    int scMG = mat + pstMG + mobMG + pawnMG + rookMG + outMG + ksMG + tempoMG;
    int scEG = mat + pstEG + mobEG + pawnEG + rookEG + outEG + ksEG + tempoEG;

    int phase = phaseCount; if(phase>24) phase=24; if(phase<0) phase=0;
    int score = (scMG * phase + scEG * (24 - phase)) / 24;

    // ---------- 50-move rule damping ----------
    score -= score * halfmove / 212;

    // Return from side to move perspective
    return white_to_move ? score : -score;
}
