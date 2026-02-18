#include "engine.h"
#include <iomanip>

// ============ Quiescência ============
int DeepBeckyEngine::qsearch(int alpha, int beta, int ply){
    if((nodes & 0x3FFF)==0 && timeUp()){ stop=true; return alpha; }
    if(ply>=MAX_PLY-1) return evaluate();

    nodes++;
    int stand = evaluate();
    if(stand >= beta) return beta;
    if(stand > alpha) alpha = stand;

    // Gera apenas capturas pseudo-legais
    vector<Move> caps = generatePseudo(true);

    // Ordena capturas por MVV-LVA
    for(auto &m: caps){
        // CORRIGIDO: Usa a peça capturada que já está na struct Move
        m.score = 10*PIECE_VALUE[m.captured_piece] - PIECE_VALUE[m.piece_moved];
    }
    sort(caps.begin(), caps.end(), [](const Move&a,const Move&b){
        return a.score > b.score;
    });

    for(auto &m: caps){
        // Delta pruning
        int capGain = PIECE_VALUE[m.captured_piece];
        int promoGain = m.promotion ? (PIECE_VALUE[m.promotion] - PIECE_VALUE[m.piece_moved]) : 0;
        
        if(stand + capGain + promoGain + 100 < alpha) continue;

        makeMove(m);
        // OTIMIZAÇÃO: A legalidade é checada aqui. Se o rei ficou em xeque, o lance é ilegal.
        if (inCheck(!white_to_move)) {
            undoMove(m);
            continue;
        }
        int score = -qsearch(-beta, -alpha, ply+1);
        undoMove(m);

        if(score >= beta) return beta;
        if(score > alpha) alpha = score;
    }
    return alpha;
}


// ============ PVS com LMR leve ============
int DeepBeckyEngine::pvs(int depth, int ply, int alpha, int beta){
    if(stop) { return alpha; }
    if(ply>=MAX_PLY-1) return evaluate();

    if (ply > 0) { // Evita checar tempo a cada nó na raiz
        if((nodes & 0x3FFF)==0 && timeUp()) { stop=true; return alpha; }
        // Verificação de repetição (simplificada, pode ser melhorada com histórico de hash)
        if(halfmove >= 100) return 0;
    }

    bool isInCheck = inCheck(white_to_move);
    if(isInCheck) depth++;

    nodes++;

    TTEntry &te = TT[hash & (TT_SIZE-1)];
    Move ttMove = MOVE_NONE;
    if(te.key==hash && te.depth>=depth){
        int sc = te.score;
        if(sc >= MATE_IN_MAX) sc -= ply;
        if(sc <= -MATE_IN_MAX) sc += ply;
        if(te.flag==TT_EXACT) return sc;
        if(te.flag==TT_ALPHA && sc<=alpha) return alpha;
        if(te.flag==TT_BETA  && sc>=beta)  return beta;
    }
    if (te.key == hash) {
        ttMove = te.best;
    }
    
    if(depth<=0) return qsearch(alpha, beta, ply);

    // Null-move pruning
    if(!isInCheck && depth >= 3 && ply > 0){
        makeNullMove();
        int R = 2 + (depth / 6);
        int nmScore = -pvs(depth - 1 - R, ply+1, -beta, -beta+1);
        undoNullMove();
        if(nmScore >= beta) return beta;
    }

    vector<Move> mv = generatePseudo();
    scoreMoves(mv, ttMove, ply);

    int best=-INF_SCORE;
    Move bestMove = MOVE_NONE;
    int origAlpha = alpha;
    int moveCount=0;
    bool hasLegalMove = false;

    for(auto &m: mv){
        makeMove(m);
        if (inCheck(!white_to_move)) {
            undoMove(m);
            continue;
        }
        hasLegalMove = true;
        moveCount++;

        int sc;
        if(moveCount==1){ // Movimento Principal (PVS)
            sc = -pvs(depth-1, ply+1, -beta, -alpha);
        } else { // Outros movimentos (Zero Window Search)
            int newDepth = depth-1;
            // Late Move Reduction (LMR)
            if(moveCount > 3 && depth >= 3 && !m.is_capture && !m.promotion && !isInCheck){
                sc = -pvs(newDepth - 1, ply+1, -alpha-1, -alpha);
            } else {
                sc = alpha + 1; // Garante que a re-busca aconteça se necessário
            }
            
            if(sc > alpha){
                // Re-busca com janela completa
                sc = -pvs(newDepth, ply+1, -beta, -alpha);
            }
        }
        undoMove(m);
        if (stop) return alpha;

        if(sc > best){ best = sc; bestMove = m; }
        if(sc > alpha){
            alpha = sc;
            if(!m.is_capture){
                int side = white_to_move? 0:1;
                history_heur[side][sq(m.from_x,m.from_y)][sq(m.to_x,m.to_y)] += depth*depth;
                if (killers.killer[0][ply] == MOVE_NONE || !(killers.killer[0][ply] == m)) {
                    killers.killer[1][ply] = killers.killer[0][ply];
                    killers.killer[0][ply] = m;
                }
            }
            if(alpha >= beta) { // Beta cutoff
                if (!m.is_capture) {
                    // Atualiza killers no corte
                    if (killers.killer[0][ply] == MOVE_NONE || !(killers.killer[0][ply] == m)) {
                       killers.killer[1][ply] = killers.killer[0][ply];
                       killers.killer[0][ply] = m;
                    }
                }
                break;
            }
        }
    }

    if(!hasLegalMove){
        return isInCheck ? (-MATE_SCORE + ply) : 0; // Chequemate ou Afogamento
    }

    if(bestMove.piece_moved != EMPTY) {
        te.key = hash; te.depth = static_cast<int8_t>(depth); te.best=bestMove;
        int flag = TT_EXACT;
        if(best<=origAlpha) flag = TT_ALPHA;
        else if(best>=beta) flag = TT_BETA;
        te.flag = static_cast<int8_t>(flag);
        int store = best;
        if(best >= MATE_IN_MAX) store += ply;
        if(best <= -MATE_IN_MAX) store -= ply;
        te.score = (int16_t)store;
    }

    return best;
}

// ============ Busca (Iterative + Aspiration Windows) ============
Move DeepBeckyEngine::search(int maxDepth, int timeMs){
    start_time = chrono::high_resolution_clock::now();
    time_limit_ms = timeMs;
    stop=false; nodes=0;
    clearHeuristics(); // Limpa histórico e killers a cada busca

    Move best = MOVE_NONE;
    int prev=0;

    for(int d=1; d<=maxDepth; ++d){
        int A = -INF_SCORE, B = INF_SCORE;
        if(d >= 4){
            int window = 25;
            A = prev - window;
            B = prev + window;
        }

        while(true){
            int sc = pvs(d, 0, A, B);
            if(stop) break;

            if (sc <= A) {
                A = -INF_SCORE; // Janela falhou baixo, abre para a esquerda
                continue;
            }
            if (sc >= B) {
                B = INF_SCORE; // Janela falhou alto, abre para a direita
                continue;
            }
            
            prev = sc;
            TTEntry &te = TT[hash & (TT_SIZE-1)];
            if(te.key==hash){
                best = te.best;
            }

            auto now = chrono::high_resolution_clock::now();
            long long ms = chrono::duration_cast<chrono::milliseconds>(now-start_time).count();
            if (ms == 0) ms = 1;
            std::vector<Move> pvLine = getPV(d);
            long long nps = (nodes * 1000LL) / ms;
            cout << "info depth " << d
                 << " score cp " << sc
                 << " time " << ms
                 << " nodes " << nodes
                 << " nps " << nps
                 << " pv " << pvToString(pvLine) << endl;
            cout << "info string nps " << std::fixed << std::setprecision(1)
                 << (static_cast<double>(nps) / 1000.0) << " kN/s" << std::endl;
            break;
        }
        
        if(stop) break;
        if(time_limit_ms > 0 && chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now() - start_time).count() * 2 > time_limit_ms && d < maxDepth) {
             break; // Para a busca se mais da metade do tempo já foi gasto
        }
    }
    return best;
}


// ============ PV helpers ===========
vector<Move> DeepBeckyEngine::getPV(int maxDepth){
    vector<Move> pv;
    uint64_t current_hash = hash;
    bool current_wtm = white_to_move;
    (void)current_wtm;
    
    for (int d = 0; d < maxDepth; d++){
        TTEntry &te = TT[current_hash & (TT_SIZE-1)];
        if (te.key != current_hash || te.best.piece_moved == EMPTY) break;
        
        Move m = te.best;

        // Valida o movimento para evitar PVs com lixo da TT
        vector<Move> legal_moves = generatePseudo();
        bool found = false;
        for(const auto& legal_m : legal_moves) {
            if(legal_m == m) {
                found = true;
                break;
            }
        }
        if(!found) break;

        pv.push_back(m);
        makeMove(m);
        current_hash = hash;
    }
    
    for (int i = (int)pv.size()-1; i >= 0; --i) undoMove(pv[i]);
    return pv;
}

string DeepBeckyEngine::pvToString(const vector<Move>& pv){
    ostringstream ss;
    for (auto &m : pv) ss << moveToUCI(m) << " ";
    return ss.str();
}