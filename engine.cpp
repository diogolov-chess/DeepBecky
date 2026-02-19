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
#include <algorithm>

// ========================= Identidade =========================
const string ENGINE_NAME = "Deep Becky";
const string ENGINE_VERSION = "1.1";
const string ENGINE_AUTHOR = "Diogo de Oliveira Almeida";

// ========================= Zobrist =========================
Zobrist::Zobrist(){
    mt19937_64 rng(0xD10D10D10ULL ^ 0xC0FFEEBADBEEFULL);
    for(int p=0;p<13;p++) for(int s=0;s<64;s++) piece[p][s]=rng();
    side=rng();
    for(int i=0;i<16;i++) castling[i]=rng();
    for(int i=0;i<9;i++) ep[i]=rng();
}
Zobrist ZOB;

// ===== Tabelas de ataques pré-computadas =====
U64 KNIGHT_ATK_BB[64];
U64 KING_ATK_BB[64];
U64 WPAWN_ATK_BB[64];
U64 BPAWN_ATK_BB[64];

void initAttackTables(){
    for(int s=0; s<64; ++s){
        KNIGHT_ATK_BB[s] = 0; KING_ATK_BB[s] = 0;
        WPAWN_ATK_BB[s] = 0; BPAWN_ATK_BB[s] = 0;
        int x=sq_x(s), y=sq_y(s);

        const int KNDX[8]={1,2,2,1,-1,-2,-2,-1};
        const int KNDY[8]={2,1,-1,-2,-2,-1,1,2};
        for(int i=0;i<8;i++){
            int nx=x+KNDX[i], ny=y+KNDY[i];
            if(onBoard(nx,ny)) set_bit(KNIGHT_ATK_BB[s], sq(nx,ny));
        }

        const int KGDX[8]={1,1,1,0,0,-1,-1,-1};
        const int KGDY[8]={1,0,-1,1,-1,1,0,-1};
        for(int i=0;i<8;i++){
            int nx=x+KGDX[i], ny=y+KGDY[i];
            if(onBoard(nx,ny)) set_bit(KING_ATK_BB[s], sq(nx,ny));
        }
        
        if(onBoard(x-1,y+1)) set_bit(WPAWN_ATK_BB[s], sq(x-1,y+1));
        if(onBoard(x+1,y+1)) set_bit(WPAWN_ATK_BB[s], sq(x+1,y+1));
        if(onBoard(x-1,y-1)) set_bit(BPAWN_ATK_BB[s], sq(x-1,y-1));
        if(onBoard(x+1,y-1)) set_bit(BPAWN_ATK_BB[s], sq(x+1,y-1));
    }
}

// ========================= TT / Heurísticas =========================
TTEntry TT[TT_SIZE];
uint8_t TTGeneration = 1;
KillerTable killers;
int history_heur[2][64][64];

namespace {

U64 attackersToSquare(int sq, U64 occ, int color, const U64 pieces[13]){
    U64 attackers = 0;
    if(color == WHITE){
        attackers |= pieces[WPAWN] & BPAWN_ATK_BB[sq];
        attackers |= pieces[WKNIGHT] & KNIGHT_ATK_BB[sq];
        attackers |= pieces[WKING] & KING_ATK_BB[sq];
        U64 bishopsQueens = pieces[WBISHOP] | pieces[WQUEEN];
        attackers |= bishopsQueens & Magic::bishopAttacks(sq, occ);
        U64 rooksQueens = pieces[WROOK] | pieces[WQUEEN];
        attackers |= rooksQueens & Magic::rookAttacks(sq, occ);
    } else {
        attackers |= pieces[BPAWN] & WPAWN_ATK_BB[sq];
        attackers |= pieces[BKNIGHT] & KNIGHT_ATK_BB[sq];
        attackers |= pieces[BKING] & KING_ATK_BB[sq];
        U64 bishopsQueens = pieces[BBISHOP] | pieces[BQUEEN];
        attackers |= bishopsQueens & Magic::bishopAttacks(sq, occ);
        U64 rooksQueens = pieces[BROOK] | pieces[BQUEEN];
        attackers |= rooksQueens & Magic::rookAttacks(sq, occ);
    }
    return attackers;
}

int leastValuableAttacker(int color, U64 attackers, const U64 pieces[13]){
    static const int order[2][6] = {
        {WPAWN, WKNIGHT, WBISHOP, WROOK, WQUEEN, WKING},
        {BPAWN, BKNIGHT, BBISHOP, BROOK, BQUEEN, BKING}
    };
    for(int idx=0; idx<6; ++idx){
        int pieceType = order[color][idx];
        U64 bb = pieces[pieceType] & attackers;
        if(bb){
            return lsb_index(bb);
        }
    }
    return -1;
}

int pieceTypeAt(int color, int sq, const U64 pieces[13]){
    const int base = (color == WHITE) ? WPAWN : BPAWN;
    const U64 mask = 1ULL << sq;
    for(int i=0;i<6;++i){
        if(pieces[base + i] & mask) return base + i;
    }
    return EMPTY;
}

} // namespace

// ============ Engine ctor ============
DeepBeckyEngine::DeepBeckyEngine(){
    initAttackTables();
    Magic::init();
    clearTT();
    clearHeuristics();
    repetitionHistory.reserve(MAX_STACK);
    plies_since_null = 0;
    setStartPos();
}

// ============ Hash corrente ============
uint64_t DeepBeckyEngine::computeHash() const {
    uint64_t h=0;
    for(int p=WPAWN; p<=BKING; ++p){
        U64 bb = bitboards[p];
        while(bb){
            int s = pop_lsb(&bb);
            h ^= ZOB.piece[p][s];
        }
    }
    if(!white_to_move) h^=ZOB.side;
    h^=ZOB.castling[castling&15];
    h^=ZOB.ep[ep_file&15];
    return h;
}

// ============ Posição inicial ============
void DeepBeckyEngine::setStartPos(){
    if(!setFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")){
        cout << "info string falha ao carregar FEN inicial" << endl;
    }
}

// ============ FEN (versão bitboard) ============
bool DeepBeckyEngine::setFEN(const string &fen){
    vector<string> tokens;
    tokens.reserve(6);
    string token;
    stringstream ss(fen);
    while(ss >> token){
        if(tokens.size() < 6) tokens.push_back(token);
    }

    if(tokens.size() < 4) return false;
    while(tokens.size() < 6){
        tokens.push_back(tokens.size() == 4 ? "0" : "1");
    }

    memset(bitboards, 0, sizeof(bitboards));
    memset(color_bitboards, 0, sizeof(color_bitboards));
    memset(piece_board, EMPTY, sizeof(piece_board));

    const string& placements = tokens[0];
    const string& side = tokens[1];
    const string& castl_str = tokens[2];
    const string& ep_str = tokens[3];

    auto charToPiece = [](char c)->int{
        switch(c){
            case 'P': return WPAWN;   case 'N': return WKNIGHT; case 'B': return WBISHOP;
            case 'R': return WROOK;   case 'Q': return WQUEEN; case 'K': return WKING;
            case 'p': return BPAWN;   case 'n': return BKNIGHT; case 'b': return BBISHOP;
            case 'r': return BROOK;   case 'q': return BQUEEN; case 'k': return BKING;
            default:  return EMPTY;
        }
    };

    int file = 0;
    int rank = 7;
    king_sq[WHITE] = -1;
    king_sq[BLACK] = -1;

    for(char c : placements){
        if(c == '/'){
            if(file != 8) return false;
            if(--rank < 0) return false;
            file = 0;
            continue;
        }
        if(c >= '1' && c <= '8'){
            file += c - '0';
            if(file > 8) return false;
            continue;
        }
        int piece = charToPiece(c);
        if(piece == EMPTY) return false;
        if(file >= 8 || rank < 0) return false;
        int sqi = sq(file, rank);
        set_bit(bitboards[piece], sqi);
        piece_board[sqi] = piece;
        if(isWhitePiece(piece)) set_bit(color_bitboards[WHITE], sqi);
        else set_bit(color_bitboards[BLACK], sqi);
        if(piece == WKING) king_sq[WHITE] = sqi;
        if(piece == BKING) king_sq[BLACK] = sqi;
        ++file;
    }

    if(rank != 0 || file != 8) return false;
    if(king_sq[WHITE] < 0 || king_sq[BLACK] < 0) return false;

    if(side.empty()) return false;
    char sideChar = static_cast<char>(tolower(static_cast<unsigned char>(side[0])));
    if(sideChar == 'w') white_to_move = true;
    else if(sideChar == 'b') white_to_move = false;
    else return false;

    castling = 0;
    if(castl_str != "-"){
        for(char c : castl_str){
            switch(c){
                case 'K': castling |= 8; break;
                case 'Q': castling |= 4; break;
                case 'k': castling |= 2; break;
                case 'q': castling |= 1; break;
                default: return false;
            }
        }
    }

    ep_file = 0;
    if(ep_str != "-"){
        if(ep_str.size() != 2) return false;
        char fileChar = ep_str[0];
        char rankChar = ep_str[1];
        if(fileChar < 'a' || fileChar > 'h') return false;
        if(rankChar < '1' || rankChar > '8') return false;
        int fileIdx = fileChar - 'a';
        int rankIdx = rankChar - '1';
        if(rankChar == '3' || rankChar == '6'){
            int expectedRank = white_to_move ? 5 : 2;
            if(rankIdx == expectedRank) ep_file = fileIdx + 1;
        }
    }

    auto parseUnsigned = [](const string& s, int fallback)->int{
        if(s.empty()) return fallback;
        int value = 0;
        for(char ch : s){
            if(ch < '0' || ch > '9') return fallback;
            value = value * 10 + (ch - '0');
            if(value < 0) return fallback;
        }
        return value;
    };

    halfmove = parseUnsigned(tokens[4], 0);
    if(halfmove < 0) halfmove = 0;

    fullmove = parseUnsigned(tokens[5], 1);
    if(fullmove < 1) fullmove = 1;

    uci_history.clear();
    undoTop = 0;
    hash = computeHash();
    repetitionHistory.clear();
    repetitionHistory.push_back({hash, 0});
    plies_since_null = 0;
    return true;
}

// ============ Aplicar Movimento ============
void DeepBeckyEngine::makeMove(const Move& m){
    Undo& u = undoStack[undoTop++];
    u.castling_before = castling;
    u.ep_before = ep_file;
    u.half_before = halfmove;
    u.fullmove_before = fullmove;
    u.hash_before = hash;
    u.captured_piece = EMPTY;
    u.moved_piece = EMPTY;
    u.repIndexBefore = repetitionHistory.size();
    u.repetition_before = repetitionHistory.empty() ? 0 : repetitionHistory.back().repetition;
    u.plies_from_null_before = plies_since_null;
    u.was_null = false;

    int from_sq = moveFrom(m);
    int to_sq = moveTo(m);
    int piece = piece_board[from_sq];
    int us = white_to_move ? WHITE : BLACK;
    int them = us ^ 1;

    u.moved_piece = piece;
    u.captured_piece = moveIsEnPassant(m) ? (white_to_move ? BPAWN : WPAWN) : piece_board[to_sq];

    hash ^= ZOB.castling[castling & 15];
    if(ep_file > 0) hash ^= ZOB.ep[ep_file & 15];
    hash ^= ZOB.side;

    pop_bit(bitboards[piece], from_sq);
    pop_bit(color_bitboards[us], from_sq);
    hash ^= ZOB.piece[piece][from_sq];
    piece_board[from_sq] = EMPTY;

    if (u.captured_piece != EMPTY) {
        int cap_sq = to_sq;
        if (moveIsEnPassant(m)) {
            cap_sq = to_sq + (us == WHITE ? -8 : 8);
        }
        pop_bit(bitboards[u.captured_piece], cap_sq);
        pop_bit(color_bitboards[them], cap_sq);
        hash ^= ZOB.piece[u.captured_piece][cap_sq];
        piece_board[cap_sq] = EMPTY;
    }

    int landing_piece = movePromotion(m);
    if (landing_piece == 0) landing_piece = piece;

    set_bit(bitboards[landing_piece], to_sq);
    set_bit(color_bitboards[us], to_sq);
    hash ^= ZOB.piece[landing_piece][to_sq];
    piece_board[to_sq] = landing_piece;

    if(moveIsCastle(m)){
        int r_from, r_to;
        int rook_piece = us == WHITE ? WROOK : BROOK;
        if (to_sq > from_sq) {
            r_from = to_sq + 1; r_to = to_sq - 1;
        } else {
            r_from = to_sq - 2; r_to = to_sq + 1;
        }
        pop_bit(bitboards[rook_piece], r_from);
        pop_bit(color_bitboards[us], r_from);
        hash ^= ZOB.piece[rook_piece][r_from];
        piece_board[r_from] = EMPTY;

        set_bit(bitboards[rook_piece], r_to);
        set_bit(color_bitboards[us], r_to);
        hash ^= ZOB.piece[rook_piece][r_to];
        piece_board[r_to] = rook_piece;
    }

    ep_file = 0;
    if(moveIsDoublePush(m)) {
        ep_file = sq_x(from_sq) + 1;
    }

    castling &= ~( (piece==WKING) * 12 | (piece==BKING) * 3 );
    castling &= ~( (from_sq==sq(7,0) || to_sq==sq(7,0)) * 8 );
    castling &= ~( (from_sq==sq(0,0) || to_sq==sq(0,0)) * 4 );
    castling &= ~( (from_sq==sq(7,7) || to_sq==sq(7,7)) * 2 );
    castling &= ~( (from_sq==sq(0,7) || to_sq==sq(0,7)) * 1 );

    if(piece == WKING || piece == BKING) king_sq[us] = to_sq;

    if(piece == WPAWN || piece == BPAWN || moveIsCapture(m)) halfmove=0;
    else halfmove++;
    if(!white_to_move) fullmove++;

    white_to_move = !white_to_move;
    hash ^= ZOB.castling[castling & 15];
    if(ep_file > 0) hash ^= ZOB.ep[ep_file & 15];

    bool irreversible = (piece == WPAWN || piece == BPAWN || u.captured_piece != EMPTY);
    plies_since_null = irreversible ? 0 : (u.plies_from_null_before + 1);
    int repetitionValue = 0;
    int end = std::min(halfmove, plies_since_null);
    const size_t sizeBefore = u.repIndexBefore;
    if(end >= 4 && sizeBefore >= 4){
        for(int dist = 4; dist <= end; dist += 2){
            if(dist > static_cast<int>(sizeBefore)) break;
            size_t idx = sizeBefore - dist;
            const RepState& prevState = repetitionHistory[idx];
            if(prevState.key == hash){
                repetitionValue = prevState.repetition != 0 ? -dist : dist;
                break;
            }
        }
    }
    repetitionHistory.push_back({hash, repetitionValue});
}


// ============ Desfazer Movimento ============
void DeepBeckyEngine::undoMove(const Move& m){
    Undo& u = undoStack[--undoTop];
    white_to_move = !white_to_move;
    castling = u.castling_before;
    ep_file = u.ep_before;
    halfmove = u.half_before;
    fullmove = u.fullmove_before;
    hash = u.hash_before;

    int from_sq = moveFrom(m);
    int to_sq = moveTo(m);
    int us = white_to_move ? WHITE : BLACK;
    int them = us ^ 1;

    if (moveIsCastle(m)) {
        int r_from, r_to;
        int rook_piece = us == WHITE ? WROOK : BROOK;
        if (to_sq > from_sq) {
            r_from = to_sq + 1; r_to = to_sq - 1;
        } else {
            r_from = to_sq - 2; r_to = to_sq + 1;
        }
        set_bit(bitboards[rook_piece], r_from);
        set_bit(color_bitboards[us], r_from);
        piece_board[r_from] = rook_piece;

        pop_bit(bitboards[rook_piece], r_to);
        pop_bit(color_bitboards[us], r_to);
        piece_board[r_to] = EMPTY;
    }

    int landing_piece = movePromotion(m) ? movePromotion(m) : u.moved_piece;
    pop_bit(bitboards[landing_piece], to_sq);
    pop_bit(color_bitboards[us], to_sq);
    piece_board[to_sq] = EMPTY;

    set_bit(bitboards[u.moved_piece], from_sq);
    set_bit(color_bitboards[us], from_sq);
    piece_board[from_sq] = u.moved_piece;

    if (u.captured_piece != EMPTY) {
        int cap_sq = moveIsEnPassant(m) ? (to_sq + (us == WHITE ? -8 : 8)) : to_sq;
        set_bit(bitboards[u.captured_piece], cap_sq);
        set_bit(color_bitboards[them], cap_sq);
        piece_board[cap_sq] = u.captured_piece;
        if (!moveIsEnPassant(m)) {
            piece_board[to_sq] = u.captured_piece;
        }
    }

    if (u.moved_piece == WKING || u.moved_piece == BKING) {
        king_sq[us] = from_sq;
    }

    repetitionHistory.resize(u.repIndexBefore);
    plies_since_null = u.plies_from_null_before;
}


// ============ UCI helpers ============
string DeepBeckyEngine::moveToUCI(const Move& m) const{
    auto alg=[&](int sq){
        string s; s.push_back(static_cast<char>('a' + sq_x(sq))); s.push_back(static_cast<char>('1' + sq_y(sq))); return s;
    };
    string u = alg(moveFrom(m)) + alg(moveTo(m));
    int promo = movePromotion(m);
    if(promo){
        switch(promo){
            case WQUEEN: case BQUEEN: u+='q'; break;
            case WROOK : case BROOK : u+='r'; break;
            case WBISHOP:case BBISHOP:u+='b'; break;
            case WKNIGHT:case BKNIGHT:u+='n'; break;
        }
    }
    return u;
}

int DeepBeckyEngine::see(const Move& m) const{
    if(!moveIsCapture(m)) return 0;

    const int from_sq = moveFrom(m);
    const int to_sq = moveTo(m);
    const int side = white_to_move ? WHITE : BLACK;
    const int opp  = side ^ 1;

    int movingPieceOrig = piece_board[from_sq];
    if(movingPieceOrig == EMPTY) return 0;

    int capture_sq = to_sq;
    int capturedPiece = moveIsEnPassant(m) ? (side == WHITE ? BPAWN : WPAWN) : piece_board[to_sq];
    if(capturedPiece == EMPTY) return 0;

    int promotion = movePromotion(m);
    int movingPiece = promotion ? promotion : movingPieceOrig;

    U64 pieces[13];
    memcpy(pieces, bitboards, sizeof(pieces));
    U64 occ = color_bitboards[WHITE] | color_bitboards[BLACK];

    pieces[movingPieceOrig] &= ~(1ULL << from_sq);
    occ &= ~(1ULL << from_sq);

    if(moveIsEnPassant(m)){
        capture_sq = to_sq + (side == WHITE ? -8 : 8);
    }
    pieces[capturedPiece] &= ~(1ULL << capture_sq);
    occ &= ~(1ULL << capture_sq);

    pieces[movingPiece] |= (1ULL << to_sq);
    occ |= (1ULL << to_sq);

    U64 attackers[2];
    attackers[WHITE] = attackersToSquare(to_sq, occ, WHITE, pieces);
    attackers[BLACK] = attackersToSquare(to_sq, occ, BLACK, pieces);
    attackers[side] &= ~(1ULL << to_sq);

    int gain[32];
    int depth = 0;
    gain[depth] = PIECE_VALUE[capturedPiece];

    int stm = opp;
    int currentPiece = movingPiece;

    while(true){
        attackers[stm] &= occ;
        U64 mask = attackers[stm];
        if(!mask) break;

        int attackSq = leastValuableAttacker(stm, mask, pieces);
        if(attackSq == -1) break;

        int attackerPiece = pieceTypeAt(stm, attackSq, pieces);

        ++depth;
        gain[depth] = PIECE_VALUE[currentPiece] - gain[depth-1];

        pieces[attackerPiece] &= ~(1ULL << attackSq);
        occ &= ~(1ULL << attackSq);

        pieces[currentPiece] &= ~(1ULL << to_sq);
        pieces[attackerPiece] |= (1ULL << to_sq);
        occ |= (1ULL << to_sq);

        attackers[WHITE] = attackersToSquare(to_sq, occ, WHITE, pieces);
        attackers[BLACK] = attackersToSquare(to_sq, occ, BLACK, pieces);
        attackers[stm] &= ~(1ULL << to_sq);

        currentPiece = attackerPiece;
        stm ^= 1;

        if(depth >= 31) break;
    }

    while(depth){
        gain[depth-1] = -std::max(-gain[depth-1], gain[depth]);
        --depth;
    }
    return gain[0];
}

Move DeepBeckyEngine::uciToMove(const string& s){
    Move m = MOVE_NONE;
    if(s.size() < 4) return m;

    string norm = s;
    for(char& ch : norm){
        ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    }

    auto valid_file = [](char c){ return c >= 'a' && c <= 'h'; };
    auto valid_rank = [](char c){ return c >= '1' && c <= '8'; };
    if(!valid_file(norm[0]) || !valid_rank(norm[1]) || !valid_file(norm[2]) || !valid_rank(norm[3])){
        return m;
    }

    int from_sq = sq(norm[0]-'a', norm[1]-'1');
    int to_sq   = sq(norm[2]-'a', norm[3]-'1');

    auto match_promo = [&](const Move& cand)->bool{
        int promo = movePromotion(cand);
        if(norm.size() < 5) return promo == 0;
        char promo_char = norm[4];
        int promo_piece_type = 0;
        if(promo_char == 'q') promo_piece_type = white_to_move ? WQUEEN : BQUEEN;
        else if(promo_char == 'r') promo_piece_type = white_to_move ? WROOK  : BROOK;
        else if(promo_char == 'b') promo_piece_type = white_to_move ? WBISHOP: BBISHOP;
        else if(promo_char == 'n') promo_piece_type = white_to_move ? WKNIGHT: BKNIGHT;
        else return false;
        return promo == promo_piece_type;
    };

    Move pseudo[MAX_MOVES];
    int pseudoCount = generatePseudo(pseudo, false);
    for(int i=0;i<pseudoCount;++i){
        const Move& cand = pseudo[i];
        if(moveFrom(cand) == from_sq && moveTo(cand) == to_sq && match_promo(cand)){
            return cand;
        }
    }

    Move legal[MAX_MOVES];
    int legalCount = generateLegal(legal);
    for(int i=0;i<legalCount;++i){
        const Move& cand = legal[i];
        if(moveFrom(cand) == from_sq && moveTo(cand) == to_sq && match_promo(cand)){
            return cand;
        }
    }

    return m;
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
        auto toLower = [](string text){
            for(char &ch : text){
                ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
            }
            return text;
        };
        string cmd; ss>>cmd;
        string cmdLower = toLower(cmd);
        if(cmdLower=="uci"){
            cout << "id name " << ENGINE_NAME << " " << ENGINE_VERSION << endl;
            cout << "id author " << ENGINE_AUTHOR << endl;
            cout << "uciok" << endl;
        } else if(cmdLower=="isready"){
            cout << "readyok" << endl;
        } else if(cmdLower=="ucinewgame"){
            setStartPos();
            clearTT();
            clearHeuristics();
        } else if(cmdLower=="position"){
            string t; ss>>t;
            string tLower = toLower(t);
            if(tLower=="startpos"){
                setStartPos();
                ss >> t; // consome "moves" se existir
            } else if(tLower=="fen"){
                vector<string> fenParts;
                string fenToken;
                bool sawMoves = false;
                while(ss >> fenToken){
                    if(toLower(fenToken) == "moves"){ sawMoves = true; break; }
                    fenParts.push_back(fenToken);
                }
                if(fenParts.empty()){
                    cout << "info string FEN ausente na instrução position" << endl;
                    continue;
                }
                string fen;
                fen.reserve(fenParts.size() * 8);
                for(size_t i=0;i<fenParts.size();++i){
                    if(i) fen.push_back(' ');
                    fen += fenParts[i];
                }
                if(!setFEN(fen)){
                    cout << "info string FEN inválido: " << fen << endl;
                    continue;
                }
                if(!sawMoves) {
                    continue;
                }
            }
            string mstr;
            while(ss>>mstr){
                Move want = uciToMove(mstr);
                if (moveIsNone(want) && mstr != "0000") {
                     cout << "info string illegal move from GUI: " << mstr << endl;
                     break;
                }
                makeMove(want); 
                uci_history.push_back(mstr);
            }
        } else if(cmdLower=="go"){
            int wtime=-1,btime=-1,movetime=-1,winc=0,binc=0,depth=-1;
            bool infinite=false;
            string tok;
            while(ss>>tok){
                string key = toLower(tok);
                if(key=="wtime") ss>>wtime; else if(key=="btime") ss>>btime;
                else if(key=="winc") ss>>winc; else if(key=="binc") ss>>binc;
                else if(key=="movetime") ss>>movetime; else if(key=="depth") ss>>depth;
                else if(key=="infinite") infinite=true;
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
            Move bm = search(maxDepth, search_time);
            if(moveIsNone(bm)){
                cout<< "bestmove 0000" << endl;
            } else {
                cout << "bestmove " << moveToUCI(bm) << endl;
            }
        } else if(cmdLower=="quit"){
            break;
        }
    }
}

// ============ Null Move ============
void DeepBeckyEngine::makeNullMove(){
    Undo& u = undoStack[undoTop++];
    u.castling_before = castling;
    u.ep_before = ep_file;
    u.half_before = halfmove;
    u.fullmove_before = fullmove;
    u.hash_before = hash;
    u.captured_piece = EMPTY;
    u.moved_piece = EMPTY;
    u.repIndexBefore = repetitionHistory.size();
    u.repetition_before = repetitionHistory.empty() ? 0 : repetitionHistory.back().repetition;
    u.plies_from_null_before = plies_since_null;
    u.was_null = true;
    if (ep_file > 0) hash ^= ZOB.ep[ep_file & 15];
    ep_file = 0;
    white_to_move = !white_to_move;
    hash ^= ZOB.side;
    halfmove++;
    plies_since_null = 0;

    int repetitionValue = 0;
    int limit = std::min(halfmove, plies_since_null);
    const size_t sizeBefore = u.repIndexBefore;
    if(limit >= 2 && sizeBefore > 0){
        for(int dist = 2; dist <= limit; dist += 2){
            if(dist > static_cast<int>(sizeBefore)) break;
            size_t idx = sizeBefore - dist;
            const RepState& prevState = repetitionHistory[idx];
            if(prevState.key == hash){
                repetitionValue = prevState.repetition != 0 ? -dist : dist;
                break;
            }
        }
    }
    repetitionHistory.push_back({hash, repetitionValue});
}

void DeepBeckyEngine::undoNullMove(){
    Undo& u = undoStack[--undoTop];
    castling = u.castling_before;
    ep_file  = u.ep_before;
    halfmove = u.half_before;
    fullmove = u.fullmove_before;
    white_to_move = !white_to_move; 
    hash = u.hash_before;
    repetitionHistory.resize(u.repIndexBefore);
    plies_since_null = u.plies_from_null_before;
}

// ============ Misc Helpers ============
void DeepBeckyEngine::clearTT(){
    TTGeneration = (TTGeneration + 1) & ((1u << TT_GEN_BITS) - 1u);
    if(TTGeneration == 0){
        for(size_t i=0;i<TT_SIZE;++i) TT[i] = TTEntry();
        TTGeneration = 1;
    }
}

void DeepBeckyEngine::initBook() {}
string DeepBeckyEngine::bookKey() const { return ""; }
bool DeepBeckyEngine::timeUp() const {
    if (time_limit_ms <= 0) return false;
    auto now = chrono::high_resolution_clock::now();
    long long ms = chrono::duration_cast<chrono::milliseconds>(now - start_time).count();
    return ms >= time_limit_ms;
}

bool DeepBeckyEngine::isFiftyMoveDraw() const {
    return halfmove >= 100;
}

bool DeepBeckyEngine::isThreefoldRepetition() const {
    if(repetitionHistory.empty()) return false;
    return repetitionHistory.back().repetition < 0;
}

bool DeepBeckyEngine::isThreefoldRepetition(int ply) const {
    if(repetitionHistory.empty()) return false;
    int rep = repetitionHistory.back().repetition;
    if(rep == 0) return false;
    if(ply <= 0) return rep < 0;
    return rep < ply;
}

bool DeepBeckyEngine::hasGameCycle(int ply) const {
    if(ply <= 0) return false;
    const size_t historySize = repetitionHistory.size();
    if(historySize < 5) return false;

    size_t current = historySize - 1;
    int end = std::min(halfmove, plies_since_null);
    if(end < 3) return false;

    for(int dist = 4; dist <= end; dist += 2){
        if(dist > static_cast<int>(current)) break;
        const RepState& prev = repetitionHistory[current - dist];
        if(prev.key != hash) continue;
        if(ply > dist) return true;
        if(prev.repetition != 0) return true;
    }
    return false;
}

bool DeepBeckyEngine::isInsufficientMaterial() const {
    if((bitboards[WPAWN] | bitboards[BPAWN] | bitboards[WROOK] | bitboards[BROOK] | bitboards[WQUEEN] | bitboards[BQUEEN]) != 0)
        return false;

    const int whiteBishops = Magic::popcount64(bitboards[WBISHOP]);
    const int blackBishops = Magic::popcount64(bitboards[BBISHOP]);
    const int whiteKnights = Magic::popcount64(bitboards[WKNIGHT]);
    const int blackKnights = Magic::popcount64(bitboards[BKNIGHT]);

    const int whiteMinor = whiteBishops + whiteKnights;
    const int blackMinor = blackBishops + blackKnights;

    if(whiteMinor == 0 && blackMinor == 0)
        return true;

    if(whiteMinor == 1 && blackMinor == 0){
        return true;
    }
    if(whiteMinor == 0 && blackMinor == 1){
        return true;
    }

    if(whiteMinor == 1 && blackMinor == 1){
        if(whiteBishops == 1 && blackBishops == 1 && whiteKnights == 0 && blackKnights == 0){
            U64 bishops = bitboards[WBISHOP] | bitboards[BBISHOP];
            bool hasLight = false;
            bool hasDark = false;
            while(bishops){
                int sq = pop_lsb(&bishops);
                if(isLightSquare(sq)) hasLight = true;
                else hasDark = true;
            }
            return !(hasLight && hasDark);
        }
    }

    return false;
}

bool DeepBeckyEngine::isDraw(int ply) {
    if(halfmove > 99){
        if(!inCheck(white_to_move)) return true;
        Move tmp[MAX_MOVES];
        if(generateLegal(tmp) > 0) return true;
    }

    if(repetitionHistory.empty()) return false;
    int rep = repetitionHistory.back().repetition;
    return rep != 0 && rep < ply;
}