/*
 * Deep Becky 0.2 - UCI Chess Engine
 * Copyright (C) 2025-2026 Diogo de Oliveira Almeida
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


// Compilar (g++): g++ -O3 -std=c++17 -march=native -DNDEBUG deepbecky02.cpp -o deepbecky-v0.2-windows-x64.exe

#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <array>
#include <unordered_map>
#include <chrono>
#include <random>
#include <cstdint>
#include <cstring>
#include <limits>

using namespace std;

// ========================= Identidade =========================
static const string ENGINE_NAME = "Deep Becky";
static const string ENGINE_VERSION = "0.2";
static const string ENGINE_AUTHOR = "Diogo de Oliveira Almeida";

// ========================= Constantes globais =========================
static const int INF_SCORE     = 30000;
static const int MATE_SCORE    = 29000;
static const int MATE_IN_MAX   = 28000;
static const int MAX_PLY       = 64;
static const int TT_SIZE       = 1 << 22; // ~4M entradas

// ========================= Peças =========================
enum Piece {
    EMPTY=0,
    WPAWN=1, WKNIGHT=2, WBISHOP=3, WROOK=4, WQUEEN=5, WKING=6,
    BPAWN=7, BKNIGHT=8, BBISHOP=9, BROOK=10, BQUEEN=11, BKING=12
};

inline bool isWhitePiece(int p){ return p>=WPAWN && p<=WKING; }
inline bool isBlackPiece(int p){ return p>=BPAWN && p<=BKING; }
inline int  pieceColor(int p){ if(p==EMPTY) return -1; return isWhitePiece(p)?0:1; }

// ========================= Movimentos =========================
struct Move {
    int from_x=0, from_y=0, to_x=0, to_y=0;
    int promotion=0; // 0 sem promo; se !=0 usar Piece destino (WQUEEN, etc.)
    bool is_capture=false, is_enpassant=false, is_castle=false, is_doublepush=false;
    int captured_piece=EMPTY;
    int score=0;
    bool operator==(const Move& o) const {
        return from_x==o.from_x && from_y==o.from_y && to_x==o.to_x && to_y==o.to_y &&
               promotion==o.promotion && is_enpassant==o.is_enpassant && is_castle==o.is_castle;
    }
};
static const Move MOVE_NONE;

// ========================= Zobrist =========================
struct Zobrist {
    uint64_t piece[13][64]{};
    uint64_t side=0, castling[16]{}, ep[9]{};
    Zobrist(){
        mt19937_64 rng(0xD10D10D10ULL ^ 0xC0FFEEBADBEEFULL);
        for(int p=0;p<13;p++) for(int s=0;s<64;s++) piece[p][s]=rng();
        side=rng();
        for(int i=0;i<16;i++) castling[i]=rng();
        for(int i=0;i<9;i++) ep[i]=rng();
    }
} ZOB;

// ========================= TT =========================
enum TTFlag { TT_EXACT=0, TT_ALPHA=1, TT_BETA=2 };
struct TTEntry {
    uint64_t key;
    int16_t  score;
    int8_t   depth;
    int8_t   flag;
    Move     best;
};
static TTEntry TT[TT_SIZE];

// ========================= Heurísticas =========================
struct KillerTable {
    Move killer[2][MAX_PLY];
    void clear(){ memset(killer,0,sizeof(killer)); }
} killers;

static int history_heur[2][64][64]; // side, from, to

// ========================= Utilidades =========================
inline int sq(int x,int y){ return y*8 + x; }
inline bool onBoard(int x,int y){ return x>=0 && x<8 && y>=0 && y<8; }
inline int sgn(int v){ return (v>0)-(v<0); }

// ========================= Avaliação =========================
static const int PIECE_VALUE[13] = {
    0, 100, 320, 330, 500, 900, 20000, 100, 320, 330, 500, 900, 20000
};

// PST simples (espelhagem para pretas)
static const int PST_PAWN[64] = {
     0,  5,  5, -5, -5,  5,  5,  0,
     0, 10, -5,  0,  0, -5, 10,  0,
     0, 10, 10, 20, 20, 10, 10,  0,
     5, 15, 20, 25, 25, 20, 15,  5,
    10, 20, 25, 30, 30, 25, 20, 10,
    15, 25, 30, 35, 35, 30, 25, 15,
    30, 40, 45, 50, 50, 45, 40, 30,
     0,  0,  0,  0,  0,  0,  0,  0
};
static const int PST_KNIGHT[64] = {
   -30,-10,-10,-10,-10,-10,-10,-30,
   -10,  0,  5,  0,  0,  5,  0,-10,
   -10,  5, 10, 10, 10, 10,  5,-10,
   -10,  0, 10, 15, 15, 10,  0,-10,
   -10,  0, 10, 15, 15, 10,  0,-10,
   -10,  5, 10, 10, 10, 10,  5,-10,
   -10,  0,  5,  0,  0,  5,  0,-10,
   -30,-10,-10,-10,-10,-10,-10,-30
};
static const int PST_BISHOP[64] = {
   -20,-10,-10,-10,-10,-10,-10,-20,
   -10, 10,  0,  5,  5,  0, 10,-10,
   -10,  5, 10, 10, 10, 10,  5,-10,
   -10,  0, 10, 10, 10, 10,  0,-10,
   -10,  0, 10, 10, 10, 10,  0,-10,
   -10,  5, 10, 10, 10, 10,  5,-10,
   -10, 10,  0,  5,  5,  0, 10,-10,
   -20,-10,-10,-10,-10,-10,-10,-20
};
static const int PST_ROOK[64] = {
     0,  0,  5, 10, 10,  5,  0,  0,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  5,  5,  0,  0, -5,
    -5,  0,  0,  5,  5,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     5, 10, 10, 10, 10, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0
};
static const int PST_QUEEN[64] = {
   -20,-10,-10, -5, -5,-10,-10,-20,
   -10,  0,  5,  0,  0,  0,  0,-10,
   -10,  5,  5,  5,  5,  5,  0,-10,
    -5,  0,  5,  5,  5,  5,  0, -5,
    -5,  0,  5,  5,  5,  5,  0, -5,
   -10,  0,  5,  5,  5,  5,  0,-10,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -20,-10,-10, -5, -5,-10,-10,-20
};
static const int PST_KING_MG[64] = {
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -20,-30,-30,-40,-40,-30,-30,-20,
   -10,-20,-20,-20,-20,-20,-20,-10,
    20, 20,  0,  0,  0,  0, 20, 20,
    20, 30, 10,  0,  0, 10, 30, 20
};
static const int PST_KING_EG[64] = {
   -50,-30,-30,-30,-30,-30,-30,-50,
   -30,-20,-10,  0,  0,-10,-20,-30,
   -30,-10, 20, 30, 30, 20,-10,-30,
   -30,-10, 30, 40, 40, 30,-10,-30,
   -30,-10, 30, 40, 40, 30,-10,-30,
   -30,-10, 20, 30, 30, 20,-10,-30,
   -30,-30,-10,  0,  0,-10,-30,-30,
   -50,-30,-30,-30,-30,-30,-30,-50
};

inline int pstWhite(int p, int sqi){
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
inline int pstBlack(int p, int sqi){
    // espelha verticalmente
    int r = 56 ^ sqi; // (7 - rank) * 8 + file  -> 56 ^ index espelha em 8x8
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

// ========================= Engine principal =========================
class DeepBeckyEngine {
public:
    // Tabuleiro 8x8
    int b[8][8]{};
    bool white_to_move=true;
    int castling=0b1111; // KQkq
    int ep_file=0;       // 1..8 se existe EP
    int halfmove=0, fullmove=1;

    // Hash
    uint64_t hash=0;

    // Search
    int nodes=0;
    bool stop=false;
    chrono::high_resolution_clock::time_point start_time;
    int time_limit_ms=0;

    // Histórico para repetição / book simples
    vector<string> uci_history;
    unordered_map<string, vector<string>> opening_book;

    // Stack p/ desfazer
    struct Undo {
        int captured, castling_before, ep_before, half_before, full_before;
        bool side_before;
        uint64_t hash_before;
    };
    vector<Undo> undo;

    DeepBeckyEngine(){
        initBook();
        clearTT();
        clearHeuristics();
        setStartPos();
    }

    // ===== Interface UCI =====
    void run();
    void setStartPos();
    void setFEN(const string &fen);

    // ===== Movimentos =====
    vector<Move> generateLegal();
    vector<Move> generatePseudo(bool capturesOnly=false);
    bool isAttacked(int x,int y,bool byWhite);
    bool inCheck(bool whiteSide);
    void makeMove(const Move& m);
    void undoMove(const Move& m);
    bool legalMove(const Move& m);

    // ===== Busca =====
    Move search(int maxDepth, int timeMs);
    int  pvs(int depth, int ply, int alpha, int beta);
    int  qsearch(int alpha, int beta, int ply);

    // ===== Ordenação =====
    void scoreMoves(vector<Move>& mv, const Move& ttMove, int ply);

    // ===== Avaliação =====
    int evaluate();

    // ===== Auxiliares =====
    string moveToUCI(const Move& m) const;
    Move   uciToMove(const string& s) const;
    uint64_t computeHash() const;
    void clearTT(){ for(int i=0;i<TT_SIZE;i++) TT[i]=TTEntry(); }
    void clearHeuristics(){ memset(history_heur,0,sizeof(history_heur)); killers.clear(); }
    string bookKey() const {
        string s; int limit=min<int>(12, uci_history.size());
        for(int i=(int)uci_history.size()-limit; i<(int)uci_history.size(); ++i) if(i>=0){
            s+=uci_history[i]; s+=' ';
        }
        return s;
    }
    bool timeUp() const {
        auto now = chrono::high_resolution_clock::now();
        return chrono::duration_cast<chrono::milliseconds>(now - start_time).count() > time_limit_ms;
    }
    void initBook(){
        opening_book.clear();
        opening_book["e2e4 e7e5 "] = {"g1f3","d2d4"};
        opening_book["d2d4 d7d5 "] = {"c1f4","g1f3"};
    }
};

// ============ Hash corrente ============
uint64_t DeepBeckyEngine::computeHash() const {
    uint64_t h=0;
    for(int y=0;y<8;y++)for(int x=0;x<8;x++){
        int p=b[y][x]; if(p) h^=ZOB.piece[p][sq(x,y)];
    }
    if(!white_to_move) h^=ZOB.side;
    h^=ZOB.castling[castling&15];
    h^=ZOB.ep[ep_file&15];
    return h;
}

// ============ Posição inicial ============
void DeepBeckyEngine::setStartPos(){
    const int rowW[8]={WROOK,WKNIGHT,WBISHOP,WQUEEN,WKING,WBISHOP,WKNIGHT,WROOK};
    const int rowB[8]={BROOK,BKNIGHT,BBISHOP,BQUEEN,BKING,BBISHOP,BKNIGHT,BROOK};
    memset(b,0,sizeof(b));
    for(int x=0;x<8;x++){ b[0][x]=rowW[x]; b[1][x]=WPAWN; b[6][x]=BPAWN; b[7][x]=rowB[x]; }
    white_to_move=true; castling=0b1111; ep_file=0; halfmove=0; fullmove=1;
    uci_history.clear();
    hash=computeHash();
}

// ============ FEN ============
void DeepBeckyEngine::setFEN(const string &fen){
    // Suporta campos: peças, side, roques, ep, halfmove, fullmove
    memset(b,0,sizeof(b));
    stringstream ss(fen); string piece, side, castl, ep; int hm=0, fm=1;
    ss>>piece>>side>>castl>>ep>>hm>>fm;
    int x=0,y=7;
    for(char c:piece){
        if(c=='/') { y--; x=0; continue; }
        if(isdigit((unsigned char)c)){ x+= c - '0'; continue; }
        int p=EMPTY;
        switch(c){
            case 'P': p=WPAWN; break; case 'N': p=WKNIGHT; break; case 'B': p=WBISHOP; break;
            case 'R': p=WROOK; break; case 'Q': p=WQUEEN; break; case 'K': p=WKING; break;
            case 'p': p=BPAWN; break; case 'n': p=BKNIGHT; break; case 'b': p=BBISHOP; break;
            case 'r': p=BROOK; break; case 'q': p=BQUEEN; break; case 'k': p=BKING; break;
        }
        if(p!=EMPTY){ b[y][x]=p; x++; }
    }
    white_to_move = (side=="w");
    castling=0;
    if(castl.find('K')!=string::npos) castling|=0b1000;
    if(castl.find('Q')!=string::npos) castling|=0b0100;
    if(castl.find('k')!=string::npos) castling|=0b0010;
    if(castl.find('q')!=string::npos) castling|=0b0001;
    ep_file=0;
    if(ep!="-" && ep.size()==2){ ep_file = (ep[0]-'a')+1; }
    halfmove=hm; fullmove=fm;
    uci_history.clear();
    hash=computeHash();
}

// ============ Cheque/ataque ============
bool DeepBeckyEngine::isAttacked(int x,int y,bool byWhite){
    // direções
    static const int KDX[8]={1,1,1,0,0,-1,-1,-1};
    static const int KDY[8]={1,0,-1,1,-1,1,0,-1};
    static const int NDX[8]={1,2,2,1,-1,-2,-2,-1};
    static const int NDY[8]={2,1,-1,-2,-2,-1,1,2};
    
    if(byWhite){
        if(onBoard(x-1,y-1) && b[y-1][x-1]==WPAWN) return true;
        if(onBoard(x+1,y-1) && b[y-1][x+1]==WPAWN) return true;
    }else{
        if(onBoard(x-1,y+1) && b[y+1][x-1]==BPAWN) return true;
        if(onBoard(x+1,y+1) && b[y+1][x+1]==BPAWN) return true;
    }
    // cavalos
    for(int i=0;i<8;i++){
        int nx=x+NDX[i], ny=y+NDY[i];
        if(!onBoard(nx,ny)) continue;
        int p=b[ny][nx];
        if(byWhite && p==WKNIGHT) return true;
        if(!byWhite && p==BKNIGHT) return true;
    }
    // rei
    for(int i=0;i<8;i++){
        int nx=x+KDX[i], ny=y+KDY[i];
        if(!onBoard(nx,ny)) continue;
        int p=b[ny][nx];
        if(byWhite && p==WKING) return true;
        if(!byWhite && p==BKING) return true;
    }
    // deslizantes
    auto attackLine=[&](int dx,int dy, int b1, int r1, int q1){
        int nx=x+dx, ny=y+dy;
        while(onBoard(nx,ny)){
            int p=b[ny][nx];
            if(p){
                if(byWhite){
                    if(p==q1 || p==r1) return true;
                    if((dx==0||dy==0) && p==q1) return true;
                    if((dx!=0&&dy!=0) && p==b1) return true;
                }else{
                    if(p==q1 || p==r1) return true;
                    if((dx==0||dy==0) && p==q1) return true;
                    if((dx!=0&&dy!=0) && p==b1) return true;
                }
                 break;
            }
            nx+=dx; ny+=dy;
        }
        return false;
    };
    
    for(int dx=-1; dx<=1; ++dx) for(int dy=-1; dy<=1; ++dy){
        if(dx==0 && dy==0) continue;
        int nx=x+dx, ny=y+dy;
        while(onBoard(nx,ny)){
            int p=b[ny][nx];
            if(p){
                if(byWhite){
                    if((dx==0||dy==0) && (p==WROOK || p==WQUEEN)) return true;
                    if((dx!=0&&dy!=0) && (p==WBISHOP || p==WQUEEN)) return true;
                }else{
                    if((dx==0||dy==0) && (p==BROOK || p==BQUEEN)) return true;
                    if((dx!=0&&dy!=0) && (p==BBISHOP || p==BQUEEN)) return true;
                }
                break;
            }
            nx+=dx; ny+=dy;
        }
    }
    return false;
}

bool DeepBeckyEngine::inCheck(bool whiteSide){
    // encontra rei
    int kx=-1, ky=-1;
    int king = whiteSide? WKING: BKING;
    for(int y=0;y<8;y++) for(int x=0;x<8;x++) if(b[y][x]==king){ kx=x; ky=y; }
    if(kx==-1) return false;
    return isAttacked(kx,ky,!whiteSide);
}

// ============ Legalidade ============
bool DeepBeckyEngine::legalMove(const Move& m){
    // aplica, verifica cheque próprio
    Move mv=m; int fromp=b[m.from_y][m.from_x];
    if(fromp==EMPTY) return false;
    makeMove(mv);
    bool ok = !inCheck(!white_to_move); // after makeMove, side alterna
    undoMove(mv);
    return ok;
}

// ============ Gerar movimentos ============
// Geração simples e correta, com EP, roques e promoções
vector<Move> DeepBeckyEngine::generatePseudo(bool capturesOnly){
    vector<Move> mv; mv.reserve(64);
    bool WT = white_to_move;
    int pawnFwd = WT? 1 : -1;
    int pawnStartRank = WT? 1 : 6; // y
    int promoRank = WT? 6 : 1;     // y antes de promover indo a y+/-1
    int meMin = WT? WPAWN : BPAWN;
    int meMax = WT? WKING : BKING;
    int oppMin = WT? BPAWN : WPAWN;
    int oppMax = WT? BKING : WKING;

    auto add=[&](int fx,int fy,int tx,int ty, bool cap=false, int capPiece=EMPTY, bool ep=false, bool castle=false, bool dbl=false, int promo=0){
        Move m; m.from_x=fx; m.from_y=fy; m.to_x=tx; m.to_y=ty; m.is_capture=cap; m.captured_piece=capPiece;
        m.is_enpassant=ep; m.is_castle=castle; m.is_doublepush=dbl; m.promotion=promo; mv.push_back(m);
    };

    for(int y=0;y<8;y++) for(int x=0;x<8;x++){
        int p=b[y][x]; if(p==EMPTY) continue;
        if(WT && !isWhitePiece(p)) continue;
        if(!WT && !isBlackPiece(p)) continue;

        switch(p){
            case WPAWN: case BPAWN:{
                int ny=y + pawnFwd;
                if(onBoard(x,ny) && b[ny][x]==EMPTY && !capturesOnly){
                    if(y==promoRank){
                        int q = WT? WQUEEN: BQUEEN;
                        int r = WT? WROOK : BROOK;
                        int n = WT? WKNIGHT:BKNIGHT;
                        int bb= WT? WBISHOP:BBISHOP;
                        add(x,y,x,ny,false,0,false,false,false,q);
                        add(x,y,x,ny,false,0,false,false,false,r);
                        add(x,y,x,ny,false,0,false,false,false,bb);
                        add(x,y,x,ny,false,0,false,false,false,n);
                    }else{
                        add(x,y,x,ny,false,0,false,false,false,0);
                        // duplo
                        if(y==pawnStartRank){
                            int nny=y + 2*pawnFwd;
                            if(onBoard(x,nny) && b[nny][x]==EMPTY){
                                add(x,y,x,nny,false,0,false,false,true,0);
                            }
                        }
                    }
                }
                // capturas
                for(int dx=-1; dx<=1; dx+=2){
                    int nx=x+dx, ny2=y+pawnFwd;
                    if(!onBoard(nx,ny2)) continue;
                    int t=b[ny2][nx];
                    if(t>=oppMin && t<=oppMax){
                        if(y==promoRank){
                            int q = WT? WQUEEN: BQUEEN;
                            int r = WT? WROOK : BROOK;
                            int n = WT? WKNIGHT:BKNIGHT;
                            int bb= WT? WBISHOP:BBISHOP;
                            add(x,y,nx,ny2,true,t,false,false,false,q);
                            add(x,y,nx,ny2,true,t,false,false,false,r);
                            add(x,y,nx,ny2,true,t,false,false,false,bb);
                            add(x,y,nx,ny2,true,t,false,false,false,n);
                        }else{
                            add(x,y,nx,ny2,true,t,false,false,false,0);
                        }
                    }
                }
                // en passant
                if(ep_file>=1 && ep_file<=8){
                    int ex = ep_file-1, ey = WT? 5:2;
                    if(ey==y+pawnFwd && abs(ex-x)==1 && y==(WT?4:3)){
                        int capY = y;
                        int capX = ex;
                        int capP = b[capY][capX];
                        if(capP==(WT? BPAWN:WPAWN))
                            add(x,y,ex,ey,true,capP,true,false,false,0);
                    }
                }
            }break;

            case WKNIGHT: case BKNIGHT:{
                static const int KX[8]={1,2,2,1,-1,-2,-2,-1};
                static const int KY[8]={2,1,-1,-2,-2,-1,1,2};
                for(int i=0;i<8;i++){
                    int nx=x+KX[i], ny=y+KY[i];
                    if(!onBoard(nx,ny)) continue;
                    int t=b[ny][nx];
                    if(t==EMPTY && !capturesOnly) add(x,y,nx,ny,false,0,false,false,false,0);
                    else if(t>=oppMin && t<=oppMax) add(x,y,nx,ny,true,t,false,false,false,0);
                }
            }break;

            case WBISHOP: case BBISHOP:
            case WROOK:   case BROOK:
            case WQUEEN:  case BQUEEN:{
                static const int DIRS[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1}};
                int start = (p==WROOK||p==BROOK)?0: (p==WBISHOP||p==BBISHOP)?4:0;
                int end   = (p==WROOK||p==BROOK)?4: (p==WBISHOP||p==BBISHOP)?8:8;
                for(int d=start; d<end; ++d){
                    int dx=DIRS[d][0], dy=DIRS[d][1];
                    int nx=x+dx, ny=y+dy;
                    while(onBoard(nx,ny)){
                        int t=b[ny][nx];
                        if(t==EMPTY){
                            if(!capturesOnly) add(x,y,nx,ny,false,0,false,false,false,0);
                        }else{
                            if(t>=oppMin && t<=oppMax) add(x,y,nx,ny,true,t,false,false,false,0);
                            break;
                        }
                        nx+=dx; ny+=dy;
                    }
                }
            }break;

            case WKING: case BKING:{
                static const int KDX[8]={1,1,1,0,0,-1,-1,-1};
                static const int KDY[8]={1,0,-1,1,-1,1,0,-1};
                for(int i=0;i<8;i++){
                    int nx=x+KDX[i], ny=y+KDY[i];
                    if(!onBoard(nx,ny)) continue;
                    int t=b[ny][nx];
                    if(t==EMPTY && !capturesOnly) add(x,y,nx,ny,false,0,false,false,false,0);
                    else if(t>=oppMin && t<=oppMax) add(x,y,nx,ny,true,t,false,false,false,0);
                }
                // Roques
                bool Kside = WT? (castling&0b1000): (castling&0b0010);
                bool Qside = WT? (castling&0b0100): (castling&0b0001);
                int ry = WT? 0:7;
                if(y==ry && x==4 && !inCheck(WT)){
                    // king side
                    if(Kside && b[ry][5]==EMPTY && b[ry][6]==EMPTY &&
                       !isAttacked(5,ry,!WT) && !isAttacked(6,ry,!WT)){
                        if(!capturesOnly) add(4,ry,6,ry,false,0,false,true,false,0);
                    }
                    // queen side
                    if(Qside && b[ry][3]==EMPTY && b[ry][2]==EMPTY && b[ry][1]==EMPTY &&
                       !isAttacked(3,ry,!WT) && !isAttacked(2,ry,!WT)){
                        if(!capturesOnly) add(4,ry,2,ry,false,0,false,true,false,0);
                    }
                }
            }break;
        }
    }
    return mv;
}

vector<Move> DeepBeckyEngine::generateLegal(){
    vector<Move> mv = generatePseudo(false);
    vector<Move> legal; legal.reserve(mv.size());
    for(auto &m: mv) if(legalMove(m)) legal.push_back(m);
    return legal;
}

// ============ Aplicar/Desfazer ============
void DeepBeckyEngine::makeMove(const Move& m){
    Undo u;
    u.captured = m.is_enpassant? (white_to_move? BPAWN:WPAWN) : b[m.to_y][m.to_x];
    u.castling_before = castling;
    u.ep_before = ep_file;
    u.half_before = halfmove;
    u.full_before = fullmove;
    u.side_before = white_to_move;
    u.hash_before = hash;
    undo.push_back(u);

    int piece = b[m.from_y][m.from_x];
    int target= b[m.to_y][m.to_x];

    // atualizar EP: válido somente após duplo-peão
    ep_file = 0;

    // move a peça
    b[m.from_y][m.from_x]=EMPTY;

    if(m.is_enpassant){
        b[m.to_y][m.to_x]=piece;
        int capy = white_to_move? (m.to_y-1):(m.to_y+1);
        b[capy][m.to_x]=EMPTY;
    }else if(m.is_castle){
        b[m.to_y][m.to_x]=piece;
        // mover torre
        if(m.to_x==6){ // roque pequeno
            b[m.to_y][5] = white_to_move? WROOK: BROOK;
            b[m.to_y][7] = EMPTY;
        }else if(m.to_x==2){ // roque grande
            b[m.to_y][3] = white_to_move? WROOK: BROOK;
            b[m.to_y][0] = EMPTY;
        }
    }else{
        b[m.to_y][m.to_x]=piece;
    }

    // promoção
    if(m.promotion){
        b[m.to_y][m.to_x] = m.promotion;
    }

    // duplo avanço de peão cria EP
    if(m.is_doublepush){
        ep_file = m.from_x+1;
    }

    // atualizar roques pela movimentação/captura de peças relevantes
    auto stripCastling=[&](int mask){ castling &= mask; };
    // Se mexeu rei
    if(piece==WKING){ stripCastling(0b0011); }
    if(piece==BKING){ stripCastling(0b1100); }
    // Se mexeu torre
    if(piece==WROOK && m.from_y==0){
        if(m.from_x==0) stripCastling(0b1011); // tira Q
        if(m.from_x==7) stripCastling(0b0111); // tira K
    }
    if(piece==BROOK && m.from_y==7){
        if(m.from_x==0) stripCastling(0b1110); // tira q
        if(m.from_x==7) stripCastling(0b1101); // tira k
    }
    // Se capturou torre
    if(target==WROOK && m.to_y==0){
        if(m.to_x==0) stripCastling(0b1011);
        if(m.to_x==7) stripCastling(0b0111);
    }
    if(target==BROOK && m.to_y==7){
        if(m.to_x==0) stripCastling(0b1110);
        if(m.to_x==7) stripCastling(0b1101);
    }

    // meia-jogada / jogada cheia
    if(piece==WPAWN || piece==BPAWN || m.is_capture) halfmove=0;
    else halfmove++;
    if(!white_to_move) fullmove++;

    // troca a vez
    white_to_move = !white_to_move;

    // atualiza hash
    hash = computeHash();

    // histórico UCI para book/repetição
    // (adiciona apenas lances realmente feitos)
    string uci = moveToUCI(m);
    uci_history.push_back(uci);
}

void DeepBeckyEngine::undoMove(const Move& m){
    Undo u = undo.back(); undo.pop_back();
    white_to_move = u.side_before;
    castling = u.castling_before;
    ep_file  = u.ep_before;
    halfmove = u.half_before;
    fullmove = u.full_before;
    hash     = u.hash_before;

    int piece = b[m.to_y][m.to_x];

    if(m.is_enpassant){
        // Restaura peão original
        b[m.from_y][m.from_x] = piece;
        b[m.to_y][m.to_x] = EMPTY;
        // Restaura peão capturado na posição correta
        int capy = white_to_move? (m.to_y-1):(m.to_y+1);
        b[capy][m.to_x] = u.captured;
    }else if(m.is_castle){
        // Limpa posição atual do rei
        b[m.to_y][m.to_x] = EMPTY;
        // Restaura rei na posição original
        b[m.from_y][m.from_x] = white_to_move? WKING: BKING;
        if(m.to_x==6){ // pequeno
            // Limpa torre em f1/f8 e restaura em h1/h8
            b[m.from_y][5] = EMPTY;
            b[m.from_y][7] = white_to_move? WROOK: BROOK;
        }else{ // grande
            // Limpa torre em d1/d8 e restaura em a1/a8
            b[m.from_y][3] = EMPTY;
            b[m.from_y][0] = white_to_move? WROOK: BROOK;
        }
    }else{
        // Movimento normal ou promoção
        b[m.from_y][m.from_x] = (m.promotion? (white_to_move? WPAWN:BPAWN): piece);
        b[m.to_y][m.to_x] = u.captured;
    }

    if(!uci_history.empty()) uci_history.pop_back();
}

// ============ UCI helpers ============
string DeepBeckyEngine::moveToUCI(const Move& m) const{
    auto alg=[&](int x,int y){
        string s; s.push_back('a'+x); s.push_back('1'+y); return s;
    };
    string u = alg(m.from_x,m.from_y) + alg(m.to_x,m.to_y);
    if(m.promotion){
        switch(m.promotion){
            case WQUEEN: case BQUEEN: u+='q'; break;
            case WROOK : case BROOK : u+='r'; break;
            case WBISHOP:case BBISHOP:u+='b'; break;
            case WKNIGHT:case BKNIGHT:u+='n'; break;
        }
    }
    return u;
}

Move DeepBeckyEngine::uciToMove(const string& s) const{
    Move m;
    if(s.size()<4) return m;
    int fx=s[0]-'a', fy=s[1]-'1';
    int tx=s[2]-'a', ty=s[3]-'1';
    m.from_x=fx; m.from_y=fy; m.to_x=tx; m.to_y=ty; m.promotion=0;
    if(s.size()>=5){
        char pc=s[4];
        if(pc=='q') m.promotion = white_to_move? WQUEEN:BQUEEN;
        else if(pc=='r') m.promotion = white_to_move? WROOK:BROOK;
        else if(pc=='b') m.promotion = white_to_move? WBISHOP:BBISHOP;
        else if(pc=='n') m.promotion = white_to_move? WKNIGHT:BKNIGHT;
    }
    return m;
}

// ============ Ordenação ============
void DeepBeckyEngine::scoreMoves(vector<Move>& mv, const Move& ttMove, int ply){
    auto mvv_lva=[&](const Move& m){
        int att = b[m.from_y][m.from_x];
        int def = m.is_enpassant? (white_to_move? BPAWN:WPAWN) : b[m.to_y][m.to_x];
        return 10*PIECE_VALUE[def] - PIECE_VALUE[att];
    };
    for(auto &m: mv){
        int sc=0;
        if( (ttMove.from_x|ttMove.from_y|ttMove.to_x|ttMove.to_y) && m==ttMove) sc += 2'000'000;
        if(m.is_capture) sc += 1'000'000 + mvv_lva(m);
        if(m.is_castle) sc += 50'000;
        // Killers
        for(int k=0;k<2;k++){
            const Move& km = killers.killer[k][ply];
            if((km.from_x|km.from_y|km.to_x|km.to_y) && m==km) sc += 40'000 - 5'000*k;
        }
        // History
        int side = white_to_move? 0:1;
        sc += history_heur[side][sq(m.from_x,m.from_y)][sq(m.to_x,m.to_y)];
        m.score=sc;
    }
    stable_sort(mv.begin(), mv.end(), [](const Move&a,const Move&b){return a.score>b.score;});
}

// ============ Avaliação ============
int DeepBeckyEngine::evaluate(){
    // Tapered (MG/EG) simples pelo material total
    int matW=0, matB=0;
    int pst=0;
    int minorW=0, minorB=0;

    for(int y=0;y<8;y++) for(int x=0;x<8;x++){
        int p=b[y][x]; if(!p) continue;
        int sqi=sq(x,y);
        if(isWhitePiece(p)){
            matW += PIECE_VALUE[p];
            switch(p){
                case WPAWN:   pst += pstWhite(p,sqi); break;
                case WKNIGHT: pst += pstWhite(p,sqi); minorW++; break;
                case WBISHOP: pst += pstWhite(p,sqi); minorW++; break;
                case WROOK:   pst += pstWhite(p,sqi); break;
                case WQUEEN:  pst += pstWhite(p,sqi); break;
                case WKING: {
                    // usa MG ou EG baseado no material
                    int mg = PST_KING_MG[sqi], eg = PST_KING_EG[sqi];
                    pst += (mg+eg)/2;
                } break;
            }
        }else{
            matB += PIECE_VALUE[p];
            switch(p){
                case BPAWN:   pst -= pstBlack(p,sqi); break;
                case BKNIGHT: pst -= pstBlack(p,sqi); minorB++; break;
                case BBISHOP: pst -= pstBlack(p,sqi); minorB++; break;
                case BROOK:   pst -= pstBlack(p,sqi); break;
                case BQUEEN:  pst -= pstBlack(p,sqi); break;
                case BKING: {
                    int mg = PST_KING_MG[56 ^ sqi], eg = PST_KING_EG[56 ^ sqi];
                    pst -= (mg+eg)/2;
                } break;
            }
        }
    }

    int score = (matW - matB) + pst;

    // par de bispos
    if(minorW>=2){
        bool has2B=false; int c=0;
        for(int y=0;y<8;y++)for(int x=0;x<8;x++) if(b[y][x]==WBISHOP) c++;
        if(c>=2) has2B=true;
        if(has2B) score += 25;
    }
    if(minorB>=2){
        bool has2B=false; int c=0;
        for(int y=0;y<8;y++)for(int x=0;x<8;x++) if(b[y][x]==BBISHOP) c++;
        if(c>=2) has2B=true;
        if(has2B) score -= 25;
    }

    // mobilidade simples
    int mob=0;
    for(int y=0;y<8;y++)for(int x=0;x<8;x++){
        int p=b[y][x]; if(!p) continue;
        if(p==WROOK||p==WQUEEN){
            int c=0;
            static const int D[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
            for(auto &d:D){
                int nx=x+d[0], ny=y+d[1];
                while(onBoard(nx,ny) && b[ny][nx]==EMPTY){ c++; nx+=d[0]; ny+=d[1]; }
            }
            mob += 2*c;
        }else if(p==BROOK||p==BQUEEN){
            int c=0;
            static const int D[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
            for(auto &d:D){
                int nx=x+d[0], ny=y+d[1];
                while(onBoard(nx,ny) && b[ny][nx]==EMPTY){ c++; nx+=d[0]; ny+=d[1]; }
            }
            mob -= 2*c;
        }
    }
    score += mob;

    return white_to_move ? score : -score;
}

// ============ Quiescência ============
int DeepBeckyEngine::qsearch(int alpha, int beta, int ply){
    if(ply>=MAX_PLY-1) return evaluate();
    int stand = evaluate();
    if(stand >= beta) return beta;
    if(stand > alpha) alpha = stand;

    vector<Move> caps = generatePseudo(true);
    // filtra somente capturas legais
    vector<Move> legal; legal.reserve(caps.size());
    for(auto &m: caps) if(legalMove(m)) legal.push_back(m);

    // ordena por MVV-LVA
    for(auto &m: legal){
        int att=b[m.from_y][m.from_x];
        int def = m.is_enpassant? (white_to_move? BPAWN:WPAWN) : b[m.to_y][m.to_x];
        m.score = 10*PIECE_VALUE[def] - PIECE_VALUE[att];
    }
    stable_sort(legal.begin(), legal.end(), [](const Move&a,const Move&b){return a.score>b.score;});

    for(auto &m: legal){
        makeMove(m);
        int sc = -qsearch(-beta, -alpha, ply+1);
        undoMove(m);
        if(sc >= beta) return beta;
        if(sc > alpha) alpha = sc;
    }
    return alpha;
}

// ============ PVS com LMR leve ============
int DeepBeckyEngine::pvs(int depth, int ply, int alpha, int beta){
    if(stop || timeUp()) { stop=true; return alpha; }
    if(depth<=0) return qsearch(alpha, beta, ply);
    if(ply>=MAX_PLY-1) return evaluate();

	if(inCheck(white_to_move))
    depth++;

    nodes++;

    // TT probe
    TTEntry &te = TT[hash & (TT_SIZE-1)];
    Move ttMove{};
    if(te.key==hash && te.depth>=depth){
        int sc = te.score;
        if(sc > INF_SCORE-1000) sc -= (ply); // desmatar
        if(sc < -INF_SCORE+1000) sc += (ply);
        if(te.flag==TT_EXACT) return sc;
        if(te.flag==TT_ALPHA && sc<=alpha) return alpha;
        if(te.flag==TT_BETA  && sc>=beta)  return beta;
        ttMove = te.best;
    }else if(te.key==hash){
        ttMove = te.best;
    }

    // Mate distance pruning (leve)
    int mate_alpha = -MATE_IN_MAX + ply;
    int mate_beta  =  MATE_IN_MAX - ply - 1;
    alpha = max(alpha, mate_alpha);
    beta  = min(beta , mate_beta );
    if(alpha>=beta) return alpha;

    // Geração e ordenação
    vector<Move> mv = generateLegal();
    if(mv.empty()){
        if(inCheck(white_to_move)) return -MATE_SCORE + ply; // mate
        return 0; // afogado
    }
    scoreMoves(mv, ttMove, ply);

    int best=-INF_SCORE;
    Move bestMove = mv[0];
    int origAlpha = alpha;
    int moveCount=0;

    for(auto &m: mv){
        moveCount++;
        makeMove(m);
        int sc;
        if(moveCount==1){
            sc = -pvs(depth-1, ply+1, -beta, -alpha);
        }else{
            // LMR simples
            int newDepth = depth-1;
            if(newDepth>=2 && !m.is_capture && !m.is_castle){
                sc = -pvs(newDepth-1, ply+1, -alpha-1, -alpha);
            }else{
                sc = alpha+1; // força pesquisa normal
            }
            if(sc>alpha){
                sc = -pvs(newDepth, ply+1, -alpha-1, -alpha);
                if(sc>alpha && sc<beta){
                    sc = -pvs(newDepth, ply+1, -beta, -alpha);
                }
            }
        }
        undoMove(m);

        if(sc>best){ best=sc; bestMove=m; }
        if(sc>alpha){
            alpha=sc;
            // atualiza heurísticas
            if(!m.is_capture){
                int side = white_to_move? 0:1; // após undo, volta side original
                history_heur[side][sq(m.from_x,m.from_y)][sq(m.to_x,m.to_y)] += depth*depth;
                killers.killer[1][ply] = killers.killer[0][ply];
                killers.killer[0][ply] = m;
            }
            if(alpha>=beta) break;
        }
        if(stop) break;
    }

    // TT store
    te.key = hash; te.depth=depth; te.best=bestMove;
    int flag = TT_EXACT;
    if(best<=origAlpha) flag = TT_ALPHA;
    else if(best>=beta) flag = TT_BETA;
    te.flag=flag;
    int store = best;
    if(best > INF_SCORE-1000) store += ply;
    if(best < -INF_SCORE+1000) store -= ply;
    te.score = (int16_t)store;

    return best;
}

// ============ Busca (Iterative + Aspiration Windows) ============
Move DeepBeckyEngine::search(int maxDepth, int timeMs){
    start_time = chrono::high_resolution_clock::now();
    time_limit_ms = timeMs;
    stop=false; nodes=0;
    killers.clear();
    // book
    vector<Move> root = generateLegal();
    if(root.empty()) return MOVE_NONE;

    if(uci_history.size()<12){
        auto it = opening_book.find(bookKey());
        if(it!=opening_book.end()){
            for(const auto& u: it->second){
                for(const auto& r: root){
                    if(moveToUCI(r)==u) return r;
                }
            }
        }
    }

    Move best = root[0];
    int prev=0;

    for(int d=1; d<=maxDepth; ++d){
        int A = -INF_SCORE, B = INF_SCORE;
        if(d>=3){
            int window = 35 + d*3;
            A = prev - window;
            B = prev + window;
        }

        int sc = pvs(d, 0, A, B);

        // re-search em falha
        int expand=80;
        while(!stop && (sc<=A || sc>=B)){
            if(sc<=A) A = max(-INF_SCORE, A - expand);
            else      B = min( INF_SCORE, B + expand);
            sc = pvs(d, 0, A, B);
            expand = int(expand*1.8)+10;
        }
        if(stop && d>1) break;

        // pega melhor do TT
        TTEntry &te = TT[hash & (TT_SIZE-1)];
        if(te.key==hash){
            // garantir que é lance do conjunto raiz
            for(const auto& r: root) if(r==te.best){ best = r; break; }
        }
        prev = sc;

        auto now = chrono::high_resolution_clock::now();
        long long ms = chrono::duration_cast<chrono::milliseconds>(now-start_time).count();
        
        // --- CÁLCULO DE NPS ---
        long long nps = 0;
        if(ms > 0) nps = (nodes * 1000) / ms; 
        // -----------------------------------

        cout << "info depth " << d << " score cp " << sc
             << " time " << ms << " nodes " << nodes
             << " nps " << nps
             << " pv " << moveToUCI(best) << endl;

        if(ms > time_limit_ms) break;
    }
    return best;
}

// ============ UCI Loop ============
void DeepBeckyEngine::run(){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    string line;
    setStartPos();
    while (std::getline(cin, line)) {
        if(line.empty()) continue;
        stringstream ss(line);
        string cmd; ss>>cmd;

        if(cmd=="uci"){
            cout << "id name " << ENGINE_NAME << " " << ENGINE_VERSION << endl;
			cout << "id author " << ENGINE_AUTHOR << endl;
			cout << "uciok" << endl;

        }
        else if(cmd=="isready"){
            cout << "readyok" << endl;
        }
        else if(cmd=="ucinewgame"){
            setStartPos();
            clearTT();
            clearHeuristics();
        }
        else if(cmd=="position"){
            string t; ss>>t;
            if(t=="startpos"){
                setStartPos();
                string tmp; if(ss>>tmp){ if(tmp!="moves"){ /*ignore*/ } }
            }else if(t=="fen"){
                string fen, token; int fields=0;
                while(fields<6 && ss>>token){ fen += token + " "; fields++; }
                setFEN(fen);
            }
            string mstr;
            while(ss>>mstr){
                Move want = uciToMove(mstr);
                vector<Move> legal = generateLegal();
                bool done=false;
                for(auto &lm: legal){
                    if(moveToUCI(lm)==moveToUCI(want)){ makeMove(lm); done=true; break; }
                }
                if(!done){
                    cout<<"info string illegal move from GUI: "<<mstr<<"\n";
                    break;
                }
            }
        }
        else if(cmd=="go"){
            int wtime=-1,btime=-1,movetime=-1,winc=0,binc=0,depth=-1;
            bool ponder=false,infinite=false;
            string tok;
            while(ss>>tok){
                if(tok=="wtime") ss>>wtime;
                else if(tok=="btime") ss>>btime;
                else if(tok=="winc") ss>>winc;
                else if(tok=="binc") ss>>binc;
                else if(tok=="movetime") ss>>movetime;
                else if(tok=="depth") ss>>depth;
                else if(tok=="ponder") ponder=true;
                else if(tok=="infinite") infinite=true;
                else if(tok=="movestogo"){ int dummy; ss>>dummy; }
                else if(tok=="nodes"){ long long dummy; ss>>dummy; }
            }
            int search_time=0;
            if(infinite) search_time = 24*60*60*1000;
            else if(movetime!=-1) search_time = max(50, movetime - 100);
            else{
                int tl = white_to_move? wtime:btime;
                int inc= white_to_move? winc : binc;
                if(tl<=0) tl=60000;
                search_time = (tl/30) + (inc*4/5);
            }
            int maxDepth = (depth>0? depth: MAX_PLY);

            vector<Move> root = generateLegal();
            if(root.empty()){
                if(inCheck(white_to_move)) cout<<"info string checkmate\n";
                else cout<<"info string stalemate\n";
                cout << "bestmove 0000" << endl;
                continue;
            }
            Move bm = search(maxDepth, search_time);
            if( (bm.from_x|bm.from_y|bm.to_x|bm.to_y)==0 ){
                cout<<"bestmove 0000\n";
            }else{
                cout << "bestmove " << moveToUCI(bm) << endl;
            }
        }
        else if(cmd=="quit"){
            break;
        }
    }
}

// ============ main ============
int main(){
    DeepBeckyEngine e;
    e.run();
    return 0;
}
