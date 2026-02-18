#include "engine.h"

// ========================= Identidade =========================
const string ENGINE_NAME = "Deep Becky";
const string ENGINE_VERSION = "1.0";
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
KillerTable killers;
int history_heur[2][64][64];

// ============ Engine ctor ============
DeepBeckyEngine::DeepBeckyEngine(){
    initAttackTables();
    Magic::init();
    clearTT();
    clearHeuristics();
    setStartPos();
}

// ============ Hash corrente (versão bitboard) ============
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

// ============ Posição inicial (versão bitboard) ============
void DeepBeckyEngine::setStartPos(){
    setFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

// ============ FEN (versão bitboard) ============
void DeepBeckyEngine::setFEN(const string &fen){
    memset(bitboards, 0, sizeof(bitboards));
    memset(color_bitboards, 0, sizeof(color_bitboards));
    memset(piece_board, EMPTY, sizeof(piece_board));

    stringstream ss(fen); string piece_placement, side, castl_str, ep_str;
    ss >> piece_placement >> side >> castl_str >> ep_str >> halfmove >> fullmove;

    int x=0,y=7;
    for(char c : piece_placement){
        if(c=='/') { y--; x=0; continue; }
        if(isdigit(c)) { x += c - '0'; continue; }
        
        int p = EMPTY;
        switch(c){
            case 'P': p=WPAWN; break; case 'N': p=WKNIGHT; break; case 'B': p=WBISHOP; break;
            case 'R': p=WROOK; break; case 'Q': p=WQUEEN; break; case 'K': p=WKING; break;
            case 'p': p=BPAWN; break; case 'n': p=BKNIGHT; break; case 'b': p=BBISHOP; break;
            case 'r': p=BROOK; break; case 'q': p=BQUEEN; break; case 'k': p=BKING; break;
        }
        if(p!=EMPTY){
            int s = sq(x,y);
            set_bit(bitboards[p], s);
            piece_board[s] = p;
            if (isWhitePiece(p)) set_bit(color_bitboards[WHITE], s);
            else set_bit(color_bitboards[BLACK], s);
            if (p == WKING) king_sq[WHITE] = s;
            if (p == BKING) king_sq[BLACK] = s;
            x++;
        }
    }

    white_to_move = (side=="w");
    
    castling=0;
    if(castl_str.find('K')!=string::npos) castling|=8;
    if(castl_str.find('Q')!=string::npos) castling|=4;
    if(castl_str.find('k')!=string::npos) castling|=2;
    if(castl_str.find('q')!=string::npos) castling|=1;

    ep_file=0;
    if(ep_str!="-" && ep_str.size()==2){ ep_file = (ep_str[0]-'a')+1; }

    uci_history.clear();
    hash = computeHash();
}

// ============ Aplicar Movimento ============
void DeepBeckyEngine::makeMove(const Move& m){
    Undo u;
    u.castling_before = castling;
    u.ep_before = ep_file;
    u.half_before = halfmove;
    u.hash_before = hash;
    undo.push_back(u);

    int from_sq = sq(m.from_x, m.from_y);
    int to_sq = sq(m.to_x, m.to_y);
    int piece = m.piece_moved;
    int captured = m.captured_piece;
    int us = white_to_move ? WHITE : BLACK;
    int them = !us;

    hash ^= ZOB.castling[castling & 15];
    if(ep_file > 0) hash ^= ZOB.ep[ep_file & 15];
    hash ^= ZOB.side;

    pop_bit(bitboards[piece], from_sq);
    pop_bit(color_bitboards[us], from_sq);
    hash ^= ZOB.piece[piece][from_sq];
    piece_board[from_sq] = EMPTY;

    if (captured != EMPTY) {
        int cap_sq = to_sq;
        if (m.is_enpassant) {
            cap_sq = to_sq + (us == WHITE ? -8 : 8);
        }
        pop_bit(bitboards[captured], cap_sq);
        pop_bit(color_bitboards[them], cap_sq);
        hash ^= ZOB.piece[captured][cap_sq];
        piece_board[cap_sq] = EMPTY;
    }
    
    int landing_piece = m.promotion ? m.promotion : piece;
    set_bit(bitboards[landing_piece], to_sq);
    set_bit(color_bitboards[us], to_sq);
    hash ^= ZOB.piece[landing_piece][to_sq];
    piece_board[to_sq] = landing_piece;
    
    if(m.is_castle){
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
    if(m.is_doublepush) {
        ep_file = m.from_x + 1;
    }

    castling &= ~( (piece==WKING) * 12 | (piece==BKING) * 3 );
    castling &= ~( (from_sq==sq(7,0) || to_sq==sq(7,0)) * 8 );
    castling &= ~( (from_sq==sq(0,0) || to_sq==sq(0,0)) * 4 );
    castling &= ~( (from_sq==sq(7,7) || to_sq==sq(7,7)) * 2 );
    castling &= ~( (from_sq==sq(0,7) || to_sq==sq(0,7)) * 1 );

    if(piece == WKING || piece == BKING) king_sq[us] = to_sq;
    
    if(piece == WPAWN || piece == BPAWN || m.is_capture) halfmove=0;
    else halfmove++;
    if(!white_to_move) fullmove++;

    white_to_move = !white_to_move;
    hash ^= ZOB.castling[castling & 15];
    if(ep_file > 0) hash ^= ZOB.ep[ep_file & 15];
}


// ============ Desfazer Movimento ============
void DeepBeckyEngine::undoMove(const Move& m){
    Undo u = undo.back(); undo.pop_back();
    white_to_move = !white_to_move;
    castling = u.castling_before;
    ep_file = u.ep_before;
    halfmove = u.half_before;
    hash = u.hash_before; // Restaura o hash completamente para garantir consistência
    
    if (!white_to_move) fullmove--;

    int from_sq = sq(m.from_x, m.from_y);
    int to_sq = sq(m.to_x, m.to_y);
    int piece = m.piece_moved;
    int captured = m.captured_piece;
    int us = white_to_move ? WHITE : BLACK;
    int them = !us;
    
    if (piece == WKING || piece == BKING) {
        king_sq[us] = from_sq;
    }

    // A ordem das operações é a inversa exata de makeMove para garantir a simetria.
    if (m.is_castle) {
        int r_from, r_to;
        int rook_piece = us == WHITE ? WROOK : BROOK;
        if (to_sq > from_sq) { // Roque pequeno
            r_from = to_sq + 1; r_to = to_sq - 1;
        } else { // Roque grande
            r_from = to_sq - 2; r_to = to_sq + 1;
        }
        set_bit(bitboards[rook_piece], r_from);
        set_bit(color_bitboards[us], r_from);
        piece_board[r_from] = rook_piece;

        pop_bit(bitboards[rook_piece], r_to);
        pop_bit(color_bitboards[us], r_to);
        piece_board[r_to] = EMPTY;
    }

    int landing_piece = m.promotion ? m.promotion : piece;
    pop_bit(bitboards[landing_piece], to_sq);
    pop_bit(color_bitboards[us], to_sq);
    piece_board[to_sq] = EMPTY;

    set_bit(bitboards[piece], from_sq);
    set_bit(color_bitboards[us], from_sq);
    piece_board[from_sq] = piece;
    
    if (captured != EMPTY) {
        int cap_sq = to_sq;
        if (m.is_enpassant) {
            cap_sq = to_sq + (us == WHITE ? -8 : 8);
        }
        set_bit(bitboards[captured], cap_sq);
        set_bit(color_bitboards[them], cap_sq);
        piece_board[cap_sq] = captured;
        if (!m.is_enpassant) {
             piece_board[to_sq] = captured;
        }
    }
}


// ============ UCI helpers ============
string DeepBeckyEngine::moveToUCI(const Move& m) const{
    auto alg=[&](int x,int y){
        string s; s.push_back(static_cast<char>('a' + x)); s.push_back(static_cast<char>('1' + y)); return s;
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

Move DeepBeckyEngine::uciToMove(const string& s){
Move m = MOVE_NONE;
if(s.size()<4) return m;
int from_sq = sq(s[0]-'a', s[1]-'1');
int to_sq   = sq(s[2]-'a', s[3]-'1');

auto match_promo = [&](const Move& cand)->bool{
    if(s.size() < 5) return cand.promotion == 0;
    char promo_char = s[4];
    int promo_piece_type = 0;
    if(promo_char == 'q') promo_piece_type = white_to_move ? WQUEEN : BQUEEN;
    else if(promo_char == 'r') promo_piece_type = white_to_move ? WROOK  : BROOK;
    else if(promo_char == 'b') promo_piece_type = white_to_move ? WBISHOP: BBISHOP;
    else if(promo_char == 'n') promo_piece_type = white_to_move ? WKNIGHT: BKNIGHT;
    else return false;
    return cand.promotion == 0 || cand.promotion == promo_piece_type;
};

// 1) First try pseudo-legal list (fast path)
{
    vector<Move> pseudo_moves = generatePseudo();
    for (const auto& p_move : pseudo_moves) {
        if (sq(p_move.from_x, p_move.from_y) == from_sq &&
            sq(p_move.to_x,   p_move.to_y)   == to_sq   &&
            match_promo(p_move)) {
            return p_move;
        }
    }
}

// 2) Fallback: scan the fully legal list to handle edge cases
{
    vector<Move> legal_moves = generateLegal();
    for (const auto& l_move : legal_moves) {
        if (sq(l_move.from_x, l_move.from_y) == from_sq &&
            sq(l_move.to_x,   l_move.to_y)   == to_sq   &&
            match_promo(l_move)) {
            return l_move;
        }
    }
}

// 3) Not found
return m;

}

// ============ Ordenação ============
void DeepBeckyEngine::scoreMoves(vector<Move>& mv, const Move& ttMove, int ply){
    auto mvv_lva=[&](const Move& m){
        int att = m.piece_moved;
        int def = m.captured_piece;
        return 10*PIECE_VALUE[def] - PIECE_VALUE[att];
    };
    for(auto &m: mv){
        int sc=0;
        if( (ttMove.from_x|ttMove.from_y|ttMove.to_x|ttMove.to_y) != 0 && m==ttMove) sc += 2'000'000;
        if(m.is_capture) sc += 1'000'000 + mvv_lva(m);
        if(m.is_castle) sc += 50'000;
        for(int k=0;k<2;k++){
            const Move& km = killers.killer[k][ply];
            if((km.from_x|km.from_y|km.to_x|km.to_y) != 0 && m==km) sc += 40'000 - 5'000*k;
        }
        int side = white_to_move? 0:1;
        sc += history_heur[side][sq(m.from_x,m.from_y)][sq(m.to_x,m.to_y)];
        m.score=sc;
    }
    sort(mv.begin(), mv.end(), [](const Move&a,const Move&b){return a.score>b.score;});
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
        } else if(cmd=="isready"){
            cout << "readyok" << endl;
        } else if(cmd=="ucinewgame"){
            setStartPos();
            clearTT();
            clearHeuristics();
        } else if(cmd=="position"){
            string t; ss>>t;
            if(t=="startpos"){
                setStartPos();
                ss >> t; // consome "moves" se existir
            } else if(t=="fen"){
                string fen, token; int fields=0;
                while(fields<6 && ss>>token && token != "moves"){ fen += token + " "; fields++; }
                setFEN(fen);
            }
            string mstr;
            while(ss>>mstr){
                Move want = uciToMove(mstr);
                if ((want.from_x | want.from_y | want.to_x | want.to_y) == 0 && mstr != "0000") {
                     cout << "info string illegal move from GUI: " << mstr << endl;
                     break;
                }
                makeMove(want); 
                uci_history.push_back(mstr);
            }
        } else if(cmd=="go"){
            int wtime=-1,btime=-1,movetime=-1,winc=0,binc=0,depth=-1;
            bool infinite=false;
            string tok;
            while(ss>>tok){
                if(tok=="wtime") ss>>wtime; else if(tok=="btime") ss>>btime;
                else if(tok=="winc") ss>>winc; else if(tok=="binc") ss>>binc;
                else if(tok=="movetime") ss>>movetime; else if(tok=="depth") ss>>depth;
                else if(tok=="infinite") infinite=true;
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
            if( (bm.from_x|bm.from_y|bm.to_x|bm.to_y)==0 ){
                cout<< "bestmove 0000" << endl;
            } else {
                cout << "bestmove " << moveToUCI(bm) << endl;
            }
        } else if(cmd=="quit"){
            break;
        }
    }
}

// ============ Null Move ============
void DeepBeckyEngine::makeNullMove(){
    Undo u;
    u.castling_before = castling;
    u.ep_before = ep_file;
    u.half_before = halfmove;
    u.hash_before = hash;
    undo.push_back(u);
    if (ep_file > 0) hash ^= ZOB.ep[ep_file & 15];
    ep_file = 0;
    white_to_move = !white_to_move;
    hash ^= ZOB.side;
    halfmove++;
}

void DeepBeckyEngine::undoNullMove(){
    Undo u = undo.back(); undo.pop_back();
    castling = u.castling_before;
    ep_file  = u.ep_before;
    halfmove = u.half_before;
    white_to_move = !white_to_move; 
    hash = u.hash_before;
}

// ============ Misc Helpers ============
void DeepBeckyEngine::initBook() {}
string DeepBeckyEngine::bookKey() const { return ""; }
bool DeepBeckyEngine::timeUp() const {
    if (time_limit_ms <= 0) return false;
    auto now = chrono::high_resolution_clock::now();
    long long ms = chrono::duration_cast<chrono::milliseconds>(now - start_time).count();
    return ms >= time_limit_ms;
}