/*
 * Deep Becky - UCI Chess Engine
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

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <map>
#include <cmath>
#include <cstring>
#include <limits>
#include <chrono>
#include <random>
#include <cctype>

using namespace std;

// --- Constantes e Tipos Globais ---
const string ENGINE_NAME = "Deep Becky";
const string ENGINE_AUTHOR = "Diogo de Oliveira Almeida";
const string ENGINE_VERSION = "0.1";

const int INFINITY_SCORE = 32000;
const int CHECKMATE_SCORE = 31000;
const int MAX_PLY = 64;

enum Piece { EMPTY = 0, wP, wN, wB, wR, wQ, wK, bP, bN, bB, bR, bQ, bK };
enum Color { WHITE, BLACK };

struct Move {
    int from_x, from_y, to_x, to_y;
    Piece promotion_piece, captured_piece;
    bool is_castle, is_en_passant;
    Move() : from_x(0),from_y(0),to_x(0),to_y(0),promotion_piece(EMPTY),captured_piece(EMPTY),is_castle(false),is_en_passant(false) {}
    Move(int fx,int fy,int tx,int ty,Piece p_prom=EMPTY,Piece p_cap=EMPTY,bool c=false,bool ep=false)
        : from_x(fx),from_y(fy),to_x(tx),to_y(ty),promotion_piece(p_prom),captured_piece(p_cap),is_castle(c),is_en_passant(ep) {}
};
static inline bool operator==(const Move& a, const Move& b) {
    return a.from_x==b.from_x && a.from_y==b.from_y && a.to_x==b.to_x && a.to_y==b.to_y && a.promotion_piece==b.promotion_piece;
}
const Move MOVE_NONE;

struct ScoredMove { Move move; int score; };
enum TT_FLAG { TT_UNKNOWN, TT_EXACT, TT_ALPHA, TT_BETA };
struct TTEntry {
    uint64_t hash_key=0; int depth=0; int score=0;
    TT_FLAG flag=TT_UNKNOWN; Move best_move;
};
struct BoardState {
    bool white_king_moved, black_king_moved;
    bool white_rook_a1_moved, white_rook_h1_moved;
    bool black_rook_a8_moved, black_rook_h8_moved;
    int en_passant_x; Piece captured_piece;
    uint64_t hash; Move move;
};

static inline Color get_piece_color(Piece p) { return (p > 0 && p <= wK) ? WHITE : BLACK; }
// Tipo 1..6 para ambas as cores
static inline Piece get_piece_type(Piece p) { return (Piece)((p > wK) ? (p - 6) : p); }

const int PIECE_VALUES[7] = {0, 100, 320, 330, 500, 900, 20000};
const int BISHOP_PAIR_BONUS = 30;
const int MVV_LVA[7][7]={{0,0,0,0,0,0,0},{0,15,14,13,12,11,10},{0,25,24,23,22,21,20},{0,35,34,33,32,31,30},{0,45,44,43,42,41,40},{0,55,54,53,52,51,50},{0,0,0,0,0,0,0}};
const int PAWN_TABLE[64]={0,0,0,0,0,0,0,0,50,50,50,50,50,50,50,50,10,10,20,30,30,20,10,10,5,5,10,25,25,10,5,5,0,0,0,20,20,0,0,0,5,-5,-10,0,0,-10,-5,5,5,10,10,-20,-20,10,10,5,0,0,0,0,0,0,0,0};
const int KNIGHT_TABLE[64]={-50,-40,-30,-30,-30,-30,-40,-50,-40,-20,0,0,0,0,-20,-40,-30,0,10,15,15,10,0,-30,-30,5,15,20,20,15,5,-30,-30,0,15,20,20,15,0,-30,-30,5,10,15,15,10,5,-30,-40,-20,0,5,5,0,-20,-40,-50,-40,-30,-30,-30,-30,-40,-50};
const int BISHOP_TABLE[64]={-20,-10,-10,-10,-10,-10,-10,-20,-10,0,0,0,0,0,0,-10,-10,0,5,10,10,5,0,-10,-10,5,5,10,10,5,5,-10,-10,0,10,10,10,10,0,-10,-10,10,10,10,10,10,10,-10,-10,5,0,0,0,0,5,-10,-20,-10,-10,-10,-10,-10,-10,-20};
const int ROOK_TABLE[64]={0,0,0,0,0,0,0,0,5,10,10,10,10,10,10,5,-5,0,0,0,0,0,0,-5,-5,0,0,0,0,0,0,-5,-5,0,0,0,0,0,0,-5,-5,0,0,0,0,0,0,-5,-5,0,0,0,0,0,0,-5,0,0,0,5,5,0,0,0};
const int KING_TABLE_MIDDLE[64]={-30,-40,-40,-50,-50,-40,-40,-30,-30,-40,-40,-50,-50,-40,-40,-30,-30,-40,-40,-50,-50,-40,-40,-30,-30,-40,-40,-50,-50,-40,-40,-30,-20,-30,-30,-40,-40,-30,-30,-20,-10,-20,-20,-20,-20,-20,-20,-10,20,20,0,0,0,0,20,20,20,30,10,0,0,10,30,20};
const int KING_TABLE_ENDGAME[64]={-50,-30,-30,-30,-30,-30,-30,-50,-30,-30,0,0,0,0,-30,-30,-30,-10,20,30,30,20,-10,-30,-30,-10,30,40,40,30,-10,-30,-30,-10,30,40,40,30,-10,-30,-30,-10,20,30,30,20,-10,-30,-30,-30,0,0,0,0,-30,-30,-50,-40,-30,-30,-30,-30,-40,-50};

class DeepBeckyEngine {
private:
    Piece board[8][8];
    bool is_white_turn;
    
    bool white_king_moved, black_king_moved;
    bool white_rook_a1_moved, white_rook_h1_moved;
    bool black_rook_a8_moved, black_rook_h8_moved;
    int en_passant_x, king_x_white, king_y_white, king_x_black, king_y_black;

    uint64_t current_hash;
    vector<BoardState> board_history;
    uint64_t ZOBRIST_TABLE[13][8][8], ZOBRIST_TURN, ZOBRIST_CASTLE[16], ZOBRIST_ENPASSANT[8];

    static const int TT_SIZE = 1 << 22;
    static TTEntry tt[TT_SIZE];
    
    Move killer_moves[MAX_PLY][2];
    int history_heuristic[13][64];
    
    chrono::high_resolution_clock::time_point start_time;
    int time_limit_ms;
    bool stop_search;
    long nodes_searched;

    void initializeZobrist();
    uint64_t generateHash();
    void initializeBoard(const string& fen_string = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    // UCI helpers
    string moveToUCI(const Move& move) const;
    Move uciToMoveLoose(const string& uci_move);
    Move uciToMoveStrict(const string& uci_move);

    // Core make/unmake
    void applyMove(const Move& move);
    void undoMove(const Move& move);

    // Rules helpers
    bool isPieceOfSameColor(Piece p1, Piece p2);
    bool isAttacked(int target_x, int target_y, bool by_white) const;
    bool isInCheck(bool is_white_king) const;
    vector<Move> generateAllLegalMoves();

    // Search
    int evaluatePosition();
    bool isEndgame() const;
    int quiescenceSearch(int ply, int alpha, int beta);
    int pvs(int depth, int ply, int alpha, int beta);
    void scoreMoves(const vector<Move>& pseudo_moves, vector<ScoredMove>& scored_moves, int ply);
    Move search(int max_depth, int time_limit);

    void tt_clear() { memset(tt, 0, sizeof(tt)); }
    void clearHeuristics() { memset(killer_moves,0,sizeof(killer_moves)); memset(history_heuristic,0,sizeof(history_heuristic)); }

public:
    DeepBeckyEngine() { cout.setf(ios::unitbuf); initializeZobrist(); }
    void run();
};

TTEntry DeepBeckyEngine::tt[DeepBeckyEngine::TT_SIZE];

// <<<<<<<<<<<<<< IMPLEMENTAÇÃO COMPLETA >>>>>>>>>>>>>>>>

void DeepBeckyEngine::initializeZobrist() {
    mt19937_64 rng(0xDEADBEEF1337);
    for(int p=0; p<13; ++p) for(int y=0; y<8; ++y) for(int x=0; x<8; ++x) ZOBRIST_TABLE[p][y][x] = rng();
    ZOBRIST_TURN = rng();
    for(int i=0; i<16; ++i) ZOBRIST_CASTLE[i] = rng();
    for(int i=0; i<8; ++i) ZOBRIST_ENPASSANT[i] = rng();
}

uint64_t DeepBeckyEngine::generateHash() {
    uint64_t h = 0;
    for(int y=0; y<8; ++y) for(int x=0; x<8; ++x) if(board[y][x] != EMPTY) h ^= ZOBRIST_TABLE[board[y][x]][y][x];
    if(is_white_turn) h ^= ZOBRIST_TURN;
    int cr = 0;
    if(!white_king_moved && !white_rook_h1_moved) cr |= 1;
    if(!white_king_moved && !white_rook_a1_moved) cr |= 2;
    if(!black_king_moved && !black_rook_h8_moved) cr |= 4;
    if(!black_king_moved && !black_rook_a8_moved) cr |= 8;
    h ^= ZOBRIST_CASTLE[cr];
    if(en_passant_x != -1) h ^= ZOBRIST_ENPASSANT[en_passant_x];
    return h;
}

void DeepBeckyEngine::initializeBoard(const string& fen) {
    memset(board, EMPTY, sizeof(board));
    is_white_turn = true; en_passant_x = -1;
    white_king_moved=false; black_king_moved=false;
    white_rook_a1_moved=false; white_rook_h1_moved=false;
    black_rook_a8_moved=false; black_rook_h8_moved=false;
    board_history.clear(); tt_clear(); clearHeuristics();

    stringstream ss(fen); string part; ss >> part;
    int r=7, f=0;
    map<char,Piece> pm={{'p',bP},{'n',bN},{'b',bB},{'r',bR},{'q',bQ},{'k',bK},{'P',wP},{'N',wN},{'B',wB},{'R',wR},{'Q',wQ},{'K',wK}};
    for(char c:part) {
        if(c=='/'){r--;f=0;} else if(isdigit(c)){f+=c-'0';} else {
            board[r][f] = pm[c];
            if(board[r][f]==wK){king_x_white=f;king_y_white=r;}
            if(board[r][f]==bK){king_x_black=f;king_y_black=r;}
            f++;
        }
    }
    ss >> part; is_white_turn = (part=="w");
    ss >> part; // Direitos de roque
    string castle_rights_str = part;
    if (castle_rights_str.find('K') == string::npos) white_rook_h1_moved = true;
    if (castle_rights_str.find('Q') == string::npos) white_rook_a1_moved = true;
    if (castle_rights_str.find('k') == string::npos) black_rook_h8_moved = true;
    if (castle_rights_str.find('q') == string::npos) black_rook_a8_moved = true;
    if (white_rook_a1_moved && white_rook_h1_moved) white_king_moved = true;
    if (black_rook_a8_moved && black_rook_h8_moved) black_king_moved = true;
    if (castle_rights_str == "-") {
        white_king_moved=black_king_moved=white_rook_a1_moved=white_rook_h1_moved=black_rook_a8_moved=black_rook_h8_moved=true;
    }

    ss >> part; if(part!="-")en_passant_x=part[0]-'a'; else en_passant_x=-1;
    current_hash = generateHash();
}

string DeepBeckyEngine::moveToUCI(const Move& move) const {
    string uci_move;
    uci_move += ('a' + move.from_x); uci_move += ('1' + move.from_y);
    uci_move += ('a' + move.to_x);   uci_move += ('1' + move.to_y);
    if (move.promotion_piece != EMPTY) {
        Piece pt = get_piece_type(move.promotion_piece);
        if(pt == wQ) uci_move += 'q'; else if(pt == wR) uci_move += 'r';
        else if(pt == wB) uci_move += 'b'; else if(pt == wN) uci_move += 'n';
    }
    return uci_move;
}

// Conversão "solta" (sem validação) — usada apenas como fallback
Move DeepBeckyEngine::uciToMoveLoose(const string& uci_move) {
    Move move;
    move.from_x = uci_move[0] - 'a'; move.from_y = uci_move[1] - '1';
    move.to_x = uci_move[2] - 'a';   move.to_y = uci_move[3] - '1';
    Piece moving_piece = board[move.from_y][move.from_x];
    move.captured_piece = board[move.to_y][move.to_x];

    if (uci_move.length() == 5) {
        char prom_char = tolower(uci_move[4]);
        if(prom_char == 'q') move.promotion_piece = is_white_turn ? wQ : bQ;
        else if(prom_char == 'r') move.promotion_piece = is_white_turn ? wR : bR;
        else if(prom_char == 'b') move.promotion_piece = is_white_turn ? wB : bB;
        else if(prom_char == 'n') move.promotion_piece = is_white_turn ? wN : bN;
    }
    // Roque: detecta apenas pelo deslocamento do rei
    if (get_piece_type(moving_piece) == wK && abs(move.from_x - move.to_x) == 2) {
        move.is_castle = true;
    }
    // En passant (agora verificado com direitos e ranks corretos)
    if (get_piece_type(moving_piece) == wP && move.from_x != move.to_x && board[move.to_y][move.to_x] == EMPTY) {
        bool white_try = is_white_turn && move.from_y == 4 && move.to_y == 5;
        bool black_try = (!is_white_turn) && move.from_y == 3 && move.to_y == 2;
        if ((white_try || black_try) && en_passant_x == move.to_x) {
            move.is_en_passant = true;
            move.captured_piece = is_white_turn ? bP : wP;
        }
    }
    return move;
}

// Busca o lance exatamente dentro da lista de lances LEGAIS atuais
Move DeepBeckyEngine::uciToMoveStrict(const string& uci_move) {
    string u = uci_move; if(u.size()==5) u[4] = (char)tolower(u[4]);
    vector<Move> legal = generateAllLegalMoves();
    for(const auto& m : legal) if(moveToUCI(m) == u) return m;
    // não achou: como fallback tenta converter "solto" (pode ainda ser recusado ao aplicar)
    return uciToMoveLoose(u);
}

void DeepBeckyEngine::applyMove(const Move& move) {
    // (Opcional) Sanity-check: origem deve ter peça do lado a jogar
    // if(board[move.from_y][move.from_x]==EMPTY || get_piece_color(board[move.from_y][move.from_x]) != (is_white_turn?WHITE:BLACK)) { return; }

    board_history.push_back({white_king_moved, black_king_moved, white_rook_a1_moved, white_rook_h1_moved, black_rook_a8_moved, black_rook_h8_moved, en_passant_x, move.captured_piece, current_hash, move});
    
    Piece piece = board[move.from_y][move.from_x];
    board[move.to_y][move.to_x] = piece;
    board[move.from_y][move.from_x] = EMPTY;

    if (move.is_en_passant) {
        board[move.to_y + (is_white_turn ? -1 : 1)][move.to_x] = EMPTY;
    } else if (move.is_castle) {
        if (move.to_x == 6) { board[move.from_y][5] = board[move.from_y][7]; board[move.from_y][7] = EMPTY; } 
        else if (move.to_x == 2) { board[move.from_y][3] = board[move.from_y][0]; board[move.from_y][0] = EMPTY; }
    }
    if (move.promotion_piece != EMPTY) {
        board[move.to_y][move.to_x] = move.promotion_piece;
    }
    if (piece == wK) { white_king_moved = true; king_x_white = move.to_x; king_y_white = move.to_y; }
    if (piece == bK) { black_king_moved = true; king_x_black = move.to_x; king_y_black = move.to_y; }

    // Atualiza flags de torres se elas se moverem (ou forem capturadas)
    if (move.from_x == 0 && move.from_y == 0) white_rook_a1_moved = true;
    if (move.from_x == 7 && move.from_y == 0) white_rook_h1_moved = true;
    if (move.from_x == 0 && move.from_y == 7) black_rook_a8_moved = true;
    if (move.from_x == 7 && move.from_y == 7) black_rook_h8_moved = true;
    // Se uma torre foi capturada, invalida o direito de roque correspondente
    if (move.to_x == 0 && move.to_y == 0 && board[0][0] != wR) white_rook_a1_moved = true;
    if (move.to_x == 7 && move.to_y == 0 && board[0][7] != wR) white_rook_h1_moved = true;
    if (move.to_x == 0 && move.to_y == 7 && board[7][0] != bR) black_rook_a8_moved = true;
    if (move.to_x == 7 && move.to_y == 7 && board[7][7] != bR) black_rook_h8_moved = true;

    // Direitos de en passant
    en_passant_x = -1;
    if (get_piece_type(piece) == wP && abs(move.from_y - move.to_y) == 2) {
        en_passant_x = move.to_x; // vale para peões de ambas as cores (get_piece_type)
    }
    is_white_turn = !is_white_turn;
    current_hash = generateHash();
}

void DeepBeckyEngine::undoMove(const Move& move) {
    BoardState last_state = board_history.back(); board_history.pop_back();

    is_white_turn = !is_white_turn;
    Piece piece = board[move.to_y][move.to_x];
    if (move.promotion_piece != EMPTY) piece = is_white_turn ? wP : bP;
    
    board[move.from_y][move.from_x] = piece;
    board[move.to_y][move.to_x] = last_state.captured_piece;

    if (move.is_en_passant) {
        board[move.from_y][move.to_x] = EMPTY;
        board[move.to_y + (is_white_turn ? -1 : 1)][move.to_x] = is_white_turn ? bP : wP;
    } else if (move.is_castle) {
        if (move.to_x == 6) { board[move.from_y][7] = board[move.from_y][5]; board[move.from_y][5] = EMPTY; } 
        else if (move.to_x == 2) { board[move.from_y][0] = board[move.from_y][3]; board[move.from_y][3] = EMPTY; }
    }
    if (piece == wK) { king_x_white = move.from_x; king_y_white = move.from_y; }
    if (piece == bK) { king_x_black = move.from_x; king_y_black = move.from_y; }
    
    white_king_moved=last_state.white_king_moved; black_king_moved=last_state.black_king_moved;
    white_rook_a1_moved=last_state.white_rook_a1_moved; white_rook_h1_moved=last_state.white_rook_h1_moved;
    black_rook_a8_moved=last_state.black_rook_a8_moved; black_rook_h8_moved=last_state.black_rook_h8_moved;
    en_passant_x=last_state.en_passant_x;
    current_hash = last_state.hash;
}

bool DeepBeckyEngine::isPieceOfSameColor(Piece p1, Piece p2) {
    if (p1 == EMPTY || p2 == EMPTY) return false;
    bool is_white1 = (p1 >= wP && p1 <= wK);
    bool is_white2 = (p2 >= wP && p2 <= wK);
    return is_white1 == is_white2;
}

bool DeepBeckyEngine::isAttacked(int target_x, int target_y, bool by_white) const {
    // Direção correta dos peões para checagem de ataque
    int pawn_dir = by_white ? 1 : -1;
    Piece pawn_type = by_white ? wP : bP;
    if(target_y - pawn_dir >= 0 && target_y - pawn_dir < 8) {
        if(target_x > 0 && board[target_y-pawn_dir][target_x-1] == pawn_type) return true;
        if(target_x < 7 && board[target_y-pawn_dir][target_x+1] == pawn_type) return true;
    }
    Piece knight_type = by_white ? wN : bN;
    for (int dy : {-2,-1,1,2}) for (int dx : {-2,-1,1,2}) if (abs(dx)+abs(dy)==3) {
        int nx = target_x+dx, ny=target_y+dy;
        if (nx>=0 && nx<8 && ny>=0 && ny<8 && board[ny][nx]==knight_type) return true;
    }
    Piece R=by_white?wR:bR, B=by_white?wB:bB, Q=by_white?wQ:bQ;
    int dxs[]={0,0,1,-1,1,1,-1,-1}, dys[]={1,-1,0,0,1,-1,1,-1};
    for(int i=0;i<8;++i) for(int s=1;s<8;++s){
        int nx=target_x+dxs[i]*s, ny=target_y+dys[i]*s;
        if(nx<0||nx>=8||ny<0||ny>=8) break;
        Piece p=board[ny][nx]; if(p!=EMPTY){
            if((i<4&&(p==R||p==Q))||(i>=4&&(p==B||p==Q))) return true;
            break;
        }
    }
    Piece king_type = by_white ? wK : bK;
    for(int dy : {-1,0,1}) for(int dx:{-1,0,1}) if(dx!=0||dy!=0){
        int nx=target_x+dx, ny=target_y+dy;
        if(nx>=0&&nx<8&&ny>=0&&ny<8&&board[ny][nx]==king_type) return true;
    }
    return false;
}

bool DeepBeckyEngine::isInCheck(bool is_white_king) const {
    if (is_white_king) return isAttacked(king_x_white, king_y_white, false);
    else return isAttacked(king_x_black, king_y_black, true);
}

vector<Move> DeepBeckyEngine::generateAllLegalMoves() {
    vector<Move> moves; Color current_color = is_white_turn ? WHITE : BLACK;
    for (int y=0;y<8;y++) for (int x=0;x<8;x++) {
        if(board[y][x]==EMPTY || get_piece_color(board[y][x]) != current_color) continue;
        Piece p=board[y][x], pt=get_piece_type(p);
        if (pt == wP) {
            int dir=is_white_turn?1:-1, start_rank=is_white_turn?1:6, prom_rank=is_white_turn?7:0;
            if (y+dir>=0 && y+dir<8 && board[y+dir][x]==EMPTY) {
                if(y+dir==prom_rank){
                    moves.emplace_back(x,y,x,y+dir,(is_white_turn?wQ:bQ)); moves.emplace_back(x,y,x,y+dir,(is_white_turn?wR:bR));
                    moves.emplace_back(x,y,x,y+dir,(is_white_turn?wB:bB)); moves.emplace_back(x,y,x,y+dir,(is_white_turn?wN:bN));
                } else moves.emplace_back(x,y,x,y+dir);
                if(y==start_rank&&board[y+2*dir][x]==EMPTY) moves.emplace_back(x,y,x,y+2*dir);
            }
            for(int dx:{-1,1}){
                int nx=x+dx,ny=y+dir; if(nx>=0&&nx<8&&ny>=0&&ny<8){
                    if(board[ny][nx]!=EMPTY&&!isPieceOfSameColor(p,board[ny][nx])){
                        if(ny==prom_rank){
                            moves.emplace_back(x,y,nx,ny,(is_white_turn?wQ:bQ),board[ny][nx]); moves.emplace_back(x,y,nx,ny,(is_white_turn?wR:bR),board[ny][nx]);
                            moves.emplace_back(x,y,nx,ny,(is_white_turn?wB:bB),board[ny][nx]); moves.emplace_back(x,y,nx,ny,(is_white_turn?wN:bN),board[ny][nx]);
                        }else moves.emplace_back(x,y,nx,ny,EMPTY,board[ny][nx]);
                    }
                    if(nx==en_passant_x && y==(is_white_turn?4:3)) moves.emplace_back(x,y,nx,y+dir,EMPTY,(is_white_turn?bP:wP),false,true);
                }
            }
        } else if(pt==wN){
            for(int dy:{-2,-1,1,2})for(int dx:{-2,-1,1,2})if(abs(dx)+abs(dy)==3){int nx=x+dx,ny=y+dy;if(nx>=0&&nx<8&&ny>=0&&ny<8&&!isPieceOfSameColor(p,board[ny][nx]))moves.emplace_back(x,y,nx,ny,EMPTY,board[ny][nx]);}
        } else if(pt==wK){
            for(int dy:{-1,0,1})for(int dx:{-1,0,1})if(dx!=0||dy!=0){int nx=x+dx,ny=y+dy;if(nx>=0&&nx<8&&ny>=0&&ny<8&&!isPieceOfSameColor(p,board[ny][nx]))moves.emplace_back(x,y,nx,ny,EMPTY,board[ny][nx]);}
            if(!isInCheck(is_white_turn)){
                if(is_white_turn){
                    if(!white_king_moved&&!white_rook_h1_moved&&board[0][5]==EMPTY&&board[0][6]==EMPTY&&!isAttacked(5,0,false)&&!isAttacked(6,0,false)) moves.emplace_back(4,0,6,0,EMPTY,EMPTY,true);
                    if(!white_king_moved&&!white_rook_a1_moved&&board[0][1]==EMPTY&&board[0][2]==EMPTY&&board[0][3]==EMPTY&&!isAttacked(2,0,false)&&!isAttacked(3,0,false)) moves.emplace_back(4,0,2,0,EMPTY,EMPTY,true);
                }else{
                    if(!black_king_moved&&!black_rook_h8_moved&&board[7][5]==EMPTY&&board[7][6]==EMPTY&&!isAttacked(5,7,true)&&!isAttacked(6,7,true)) moves.emplace_back(4,7,6,7,EMPTY,EMPTY,true);
                    if(!black_king_moved&&!black_rook_a8_moved&&board[7][1]==EMPTY&&board[7][2]==EMPTY&&board[7][3]==EMPTY&&!isAttacked(2,7,true)&&!isAttacked(3,7,true)) moves.emplace_back(4,7,2,7,EMPTY,EMPTY,true);
                }
            }
        } else {
            int dxs[]={0,0,1,-1,1,1,-1,-1},dys[]={1,-1,0,0,1,-1,1,-1};
            int sd=0,ed=8; if(pt==wB){sd=4;}if(pt==wR){ed=4;}
            for(int i=sd;i<ed;++i)for(int s=1;s<8;++s){int nx=x+dxs[i]*s,ny=y+dys[i]*s;if(nx<0||nx>=8||ny<0||ny>=8)break;if(board[ny][nx]==EMPTY)moves.emplace_back(x,y,nx,ny);else{if(!isPieceOfSameColor(p,board[ny][nx]))moves.emplace_back(x,y,nx,ny,EMPTY,board[ny][nx]);break;}}
        }
    }
    vector<Move> legal_moves; legal_moves.reserve(moves.size());
    for(const auto& move : moves) {
        applyMove(move);
        if (!isInCheck(!is_white_turn)) legal_moves.push_back(move);
        undoMove(move);
    }
    return legal_moves;
}

bool DeepBeckyEngine::isEndgame() const {
    int piece_count = 0;
    for(int y=0; y<8; ++y) for(int x=0; x<8; ++x) {
        Piece p = board[y][x];
        if (p != EMPTY && get_piece_type(p) != wP && get_piece_type(p) != wK) {
            piece_count++;
        }
    }
    return piece_count < 10;
}

int DeepBeckyEngine::evaluatePosition() {
    int score = 0;
    int bishop_count[2]={0,0};
    bool endgame = isEndgame();
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            Piece p = board[y][x];
            if (p == EMPTY) continue;
            Color c = get_piece_color(p);
            int sign = (c == WHITE) ? 1 : -1;
            int pos = (c == WHITE) ? (7-y)*8+x : y*8+x;
            score += PIECE_VALUES[get_piece_type(p)] * sign;
            switch(get_piece_type(p)){
                case wP: score+=PAWN_TABLE[pos]*sign; break;
                case wN: score+=KNIGHT_TABLE[pos]*sign; break;
                case wB: score+=BISHOP_TABLE[pos]*sign; bishop_count[c]++; break;
                case wR: score+=ROOK_TABLE[pos]*sign; break;
                case wK: score+=(endgame ? KING_TABLE_ENDGAME[pos] : KING_TABLE_MIDDLE[pos])*sign; break;
                default: break;
            }
        }
    }
    if (bishop_count[WHITE] >= 2) score += BISHOP_PAIR_BONUS;
    if (bishop_count[BLACK] >= 2) score -= BISHOP_PAIR_BONUS;
    return is_white_turn ? score : -score;
}

void DeepBeckyEngine::scoreMoves(const vector<Move>& pseudo_moves, vector<ScoredMove>& scored_moves, int ply) {
    scored_moves.reserve(pseudo_moves.size());
    Move tt_move;
    TTEntry& tt_entry = tt[current_hash & (TT_SIZE - 1)];
    if(tt_entry.hash_key == current_hash) tt_move = tt_entry.best_move;

    for(const auto& m : pseudo_moves) {
        int score = 0;
        if(m == tt_move) { score = 30000; }
        else if(m.captured_piece != EMPTY) {
            score = 20000 + MVV_LVA[get_piece_type(m.captured_piece)][get_piece_type(board[m.from_y][m.from_x])];
        } else if(m==killer_moves[ply][0]) { score=10000; }
        else if(m==killer_moves[ply][1]) { score=9000; }
        else { score=history_heuristic[board[m.from_y][m.from_x]][m.to_y*8+m.to_x]; }
        scored_moves.push_back({m, score});
    }
}

int DeepBeckyEngine::quiescenceSearch(int ply, int alpha, int beta) {
    nodes_searched++;
    if((nodes_searched & 2047) == 0) {
        auto now = chrono::high_resolution_clock::now();
        if (chrono::duration_cast<chrono::milliseconds>(now - start_time).count() >= time_limit_ms) {
            stop_search = true;
        }
    }
    if(stop_search) return 0;
    
    int stand_pat = evaluatePosition();
    if(stand_pat >= beta) return beta;
    if(stand_pat > alpha) alpha = stand_pat;

    vector<Move> pseudo_moves = generateAllLegalMoves();
    vector<ScoredMove> moves; scoreMoves(pseudo_moves, moves, ply);
    sort(moves.begin(), moves.end(), [](const ScoredMove& a, const ScoredMove& b){ return a.score > b.score; });

    for (const auto& sm : moves) {
        if(!sm.move.captured_piece && !sm.move.promotion_piece) continue;
        applyMove(sm.move);
        int score = -quiescenceSearch(ply+1, -beta, -alpha);
        undoMove(sm.move);
        if(stop_search) return 0;
        if(score >= beta) return beta;
        if(score > alpha) alpha = score;
    }
    return alpha;
}

int DeepBeckyEngine::pvs(int depth, int ply, int alpha, int beta) {
    if (depth <= 0) return quiescenceSearch(ply, alpha, beta);
    if (ply >= MAX_PLY) return evaluatePosition();
    
    if (ply > 0) {
        if (board_history.size() > 4) {
            for (size_t i = board_history.size() - 2; i > 0 && i > board_history.size() - 10 ; i -= 2) {
                if(board_history[i].hash == current_hash) return 0;
            }
        }
    }

    nodes_searched++;
    if((nodes_searched & 2047) == 0) {
        auto now = chrono::high_resolution_clock::now();
        if (chrono::duration_cast<chrono::milliseconds>(now - start_time).count() >= time_limit_ms) { stop_search = true; }
    }
    if(stop_search) return 0;
    
    TTEntry& tt_entry = tt[current_hash & (TT_SIZE - 1)];
    if(tt_entry.hash_key == current_hash && tt_entry.depth >= depth && ply > 0){
        int tt_score = tt_entry.score;
        if(abs(tt_score) > CHECKMATE_SCORE - MAX_PLY) tt_score += (tt_score > 0 ? -ply : ply);
        if(tt_entry.flag == TT_EXACT) return tt_score;
        if(tt_entry.flag == TT_ALPHA && tt_score <= alpha) return alpha;
        if(tt_entry.flag == TT_BETA && tt_score >= beta) return beta;
    }

    vector<Move> pseudo_moves = generateAllLegalMoves();
    if (pseudo_moves.empty()) {
        return isInCheck(is_white_turn) ? -CHECKMATE_SCORE + ply : 0;
    }

    vector<ScoredMove> moves; scoreMoves(pseudo_moves, moves, ply);
    sort(moves.begin(), moves.end(), [](const ScoredMove& a, const ScoredMove& b){ return a.score > b.score; });
    
    int moves_made = 0; Move best_move = MOVE_NONE; TT_FLAG flag = TT_ALPHA;
    for (const auto& sm : moves) {
        applyMove(sm.move);
        moves_made++; int score;
        if (moves_made == 1) {
            score = -pvs(depth-1, ply+1, -beta, -alpha);
        } else {
            score = -pvs(depth-1, ply+1, -alpha-1, -alpha);
            if (score > alpha && score < beta) {
                score = -pvs(depth-1, ply+1, -beta, -alpha);
            }
        }
        undoMove(sm.move);
        if (stop_search) return 0;
        if (score > alpha) {
            alpha = score; best_move = sm.move; flag = TT_EXACT;
            if (alpha >= beta) {
                if(!sm.move.captured_piece){
                    killer_moves[ply][1]=killer_moves[ply][0]; killer_moves[ply][0]=sm.move;
                    history_heuristic[get_piece_type(board[sm.move.from_y][sm.move.from_x])][sm.move.to_y*8+sm.move.to_x]+=depth*depth;
                }
                int s_score=beta; if(abs(s_score)>CHECKMATE_SCORE-MAX_PLY)s_score+=(s_score>0?ply:-ply);
                tt_entry = {current_hash, depth, s_score, TT_BETA, sm.move};
                return beta;
            }
        }
    }
    
    int s_score=alpha; if(abs(s_score)>CHECKMATE_SCORE-MAX_PLY)s_score+=(s_score>0?ply:-ply);
    tt_entry = {current_hash, depth, s_score, flag, best_move};
    return alpha;
}

Move DeepBeckyEngine::search(int max_depth, int time_limit) {
    start_time = chrono::high_resolution_clock::now();
    time_limit_ms = time_limit;
    stop_search = false; nodes_searched = 0;
    clearHeuristics();
    
    vector<Move> root_moves = generateAllLegalMoves();
    if (root_moves.empty()) return MOVE_NONE;
    Move best_move_overall = root_moves[0];

    for (int d = 1; d <= max_depth; ++d) {
        int score = pvs(d, 0, -INFINITY_SCORE, INFINITY_SCORE);
        if (stop_search && d > 1) break;
        
        // Recupera lance do TT, mas **confere se é legal** na posição raiz
        TTEntry& tt_entry = tt[current_hash & (TT_SIZE - 1)];
        if (tt_entry.hash_key == current_hash) {
            auto it = find_if(root_moves.begin(), root_moves.end(), [&](const Move& m){ return m == tt_entry.best_move; });
            if (it != root_moves.end()) best_move_overall = *it; // só aceita se constar na lista legal
        }

        auto elapsed = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now() - start_time);
        cout << "info depth " << d << " score cp " << score << " nodes " << nodes_searched 
             << " nps " << (nodes_searched*1000 / (elapsed.count()+1))
             << " time " << elapsed.count() << " pv " << moveToUCI(best_move_overall) << endl;
        
        if (elapsed.count() >= time_limit_ms) break;
    }
    return best_move_overall;
}

void DeepBeckyEngine::run() {
    string line, command;
    initializeBoard();
    while(getline(cin, line)) {
        if (line.empty()) continue;
        stringstream ss(line);
        ss >> command;
        if (command == "uci") {cout << "id name " << ENGINE_NAME << " " << ENGINE_VERSION << endl; cout << "id author " << ENGINE_AUTHOR << endl; cout << "uciok" << endl;}
        else if (command == "isready") { cout << "readyok" << endl;}
        else if (command == "ucinewgame") { initializeBoard(); }
        else if (command.find("position") == 0) {
            string sub, fen_str="", move_uci; ss >> sub;
            if (sub == "startpos") { initializeBoard(); ss >> sub; }
            else if (sub == "fen") { while(ss >> sub && sub != "moves") fen_str += sub + " "; initializeBoard(fen_str); }
            while(ss >> move_uci) {
                Move m = uciToMoveStrict(move_uci);
                // Garante que só aplicamos lances LEGAIS vindos do GUI/arena
                vector<Move> legal = generateAllLegalMoves();
                bool ok=false; for(const auto& lm:legal){ if(lm==m) { ok=true; break; } }
                if(!ok) { cout << "info string gui sent illegal move: " << move_uci << endl; break; }
                applyMove(m);
            }
        } else if (command.find("go") == 0) {
            int wt=-1,bt=-1,mt=-1,inc=0;string p;
            while(ss>>p){if(p=="wtime")ss>>wt;if(p=="btime")ss>>bt;if(p=="movetime")ss>>mt;if(p=="winc"||p=="binc")ss>>inc;}
            int search_time;
            if(mt!=-1) search_time = mt - 100; else { int time_left=(is_white_turn?wt:bt); search_time=(time_left/30)+(inc*4/5); }
            Move best_move = search(MAX_PLY, max(50, search_time));
            cout << "bestmove " << moveToUCI(best_move) << endl;
        } else if (command == "quit") { break; }
    }
}

int main() {
    DeepBeckyEngine engine;
    engine.run();
    return 0;
}
