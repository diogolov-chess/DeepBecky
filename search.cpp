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
#include <iomanip>

namespace {

inline int drawScore(uint64_t nodes){
    return int(2 * (nodes & 1) - 1);  // Returns -1 or +1 based on node parity
}

struct MovePicker {
    DeepBeckyEngine& eng;
    Move ttMove;
    bool ttAvailable = false;
    int ply = 0;

    Move goodCaptures[MAX_MOVES];
    int goodScores[MAX_MOVES];
    int goodCount = 0;
    int goodIndex = 0;

    Move badCaptures[MAX_MOVES];
    int badScores[MAX_MOVES];
    int badCount = 0;
    int badIndex = 0;

    Move quiets[MAX_MOVES];
    int quietScores[MAX_MOVES];
    int quietCount = 0;
    int quietIndex = 0;

    Move killerCand[2];

    enum class Stage { TT, GOOD_CAPTURES, KILLER1, KILLER2, QUIETS, BAD_CAPTURES, DONE } stage = Stage::TT;

    MovePicker(DeepBeckyEngine& engine, Move* moves, int count, const Move& tt, int p)
        : eng(engine), ttMove(tt), ply(p) {
        int us = eng.white_to_move ? WHITE : BLACK;
        if(!moveIsNone(ttMove)){
            for(int i=0;i<count;++i){
                if(moves[i] == ttMove){ ttAvailable = true; break; }
            }
        }

        for(int i=0;i<count;++i){
            Move m = moves[i];
            bool isTT = ttAvailable && m == ttMove;
            if(isTT) continue;

            if(moveIsCapture(m)){
                int from_sq = moveFrom(m);
                int to_sq = moveTo(m);
                int mover = eng.piece_board[from_sq];
                int captured = moveIsEnPassant(m) ? (us == WHITE ? BPAWN : WPAWN) : eng.piece_board[to_sq];
                int promotion = movePromotion(m);
                if(promotion) mover = promotion;
                int score = 10*PIECE_VALUE[captured] - PIECE_VALUE[mover];
                if(eng.see(m) >= 0){
                    goodCaptures[goodCount] = m;
                    goodScores[goodCount++] = score;
                } else {
                    badCaptures[badCount] = m;
                    badScores[badCount++] = score;
                }
            } else {
                quiets[quietCount] = m;
                quietScores[quietCount++] = history_heur[us][moveFrom(m)][moveTo(m)];
            }
        }

        killerCand[0] = killers.killer[0][ply];
        killerCand[1] = killers.killer[1][ply];
    }

    Move selectBest(Move* list, int* scores, int count, int& idx){
        while(idx < count){
            int best = idx;
            for(int j=idx+1; j<count; ++j){
                if(scores[j] > scores[best]) best = j;
            }
            if(best != idx){
                std::swap(list[idx], list[best]);
                std::swap(scores[idx], scores[best]);
            }
            Move m = list[idx++];
            if(ttAvailable && m == ttMove) continue;
            return m;
        }
        return MOVE_NONE;
    }

    bool useKiller(const Move& k, Move& out){
        if(moveIsNone(k) || moveIsCapture(k) || (ttAvailable && k == ttMove)) return false;
        for(int i=quietIndex; i<quietCount; ++i){
            if(quiets[i] == k){
                std::swap(quiets[i], quiets[quietIndex]);
                std::swap(quietScores[i], quietScores[quietIndex]);
                out = quiets[quietIndex++];
                return true;
            }
        }
        return false;
    }

    Move next(){
        while(true){
            switch(stage){
                case Stage::TT:
                    stage = Stage::GOOD_CAPTURES;
                    if(ttAvailable){
                        ttAvailable = false;
                        return ttMove;
                    }
                    break;
                case Stage::GOOD_CAPTURES: {
                    Move m = selectBest(goodCaptures, goodScores, goodCount, goodIndex);
                    if(!moveIsNone(m)) return m;
                    stage = Stage::KILLER1;
                    break;
                }
                case Stage::KILLER1: {
                    Move candidate;
                    if(useKiller(killerCand[0], candidate)) return candidate;
                    stage = Stage::KILLER2;
                    break;
                }
                case Stage::KILLER2: {
                    Move candidate;
                    if(useKiller(killerCand[1], candidate)) return candidate;
                    stage = Stage::QUIETS;
                    break;
                }
                case Stage::QUIETS: {
                    Move m = selectBest(quiets, quietScores, quietCount, quietIndex);
                    if(!moveIsNone(m)) return m;
                    stage = Stage::BAD_CAPTURES;
                    break;
                }
                case Stage::BAD_CAPTURES: {
                    Move m = selectBest(badCaptures, badScores, badCount, badIndex);
                    if(!moveIsNone(m)) return m;
                    stage = Stage::DONE;
                    break;
                }
                case Stage::DONE:
                default:
                    return MOVE_NONE;
            }
        }
    }
};

} // namespace


// ============ Quiescência ============
int DeepBeckyEngine::qsearch(int alpha, int beta, int ply){
    if((nodes & 0x3FFF)==0 && timeUp()){ stop=true; return alpha; }
    if(ply>=MAX_PLY-1) return evaluate();

    nodes++;
    
    // Draw detection
    if(ply > 0){
        if(isDraw(ply)) {
            return drawScore(nodes);
        }
    }
    
    int stand = evaluate();
    if(stand >= beta) return beta;
    if(stand > alpha) alpha = stand;

    Move caps[MAX_MOVES];
    int capCount = generatePseudo(caps, true);

    for(int i=0;i<capCount;++i){
        Move& m = caps[i];
        int from_sq = moveFrom(m);
        int to_sq = moveTo(m);
        int piece = piece_board[from_sq];
        int captured = moveIsEnPassant(m) ? (white_to_move ? BPAWN : WPAWN) : piece_board[to_sq];
        m.score = 10*PIECE_VALUE[captured] - PIECE_VALUE[piece];
    }

    for(int i=0;i<capCount;++i){
        int best = i;
        for(int j=i+1;j<capCount;++j){
            if(caps[j].score > caps[best].score) best = j;
        }
        if(best != i) std::swap(caps[i], caps[best]);

        Move& m = caps[i];
        if(see(m) < 0) continue;
        int from_sq = moveFrom(m);
        int to_sq = moveTo(m);
        int piece = piece_board[from_sq];
        int captured = moveIsEnPassant(m) ? (white_to_move ? BPAWN : WPAWN) : piece_board[to_sq];
        // Delta pruning
        int capGain = PIECE_VALUE[captured];
        int promo = movePromotion(m);
        int promoGain = promo ? (PIECE_VALUE[promo] - PIECE_VALUE[piece]) : 0;
        
        if(stand + capGain + promoGain + 100 < alpha) continue;

        makeMove(m);
        // A legalidade é checada aqui. Se o rei ficou em xeque, o lance é ilegal.
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
    bool rootNode = (ply == 0);

    if(!rootNode){
        // Draw detection
        if(isDraw(ply)){
            return drawScore(nodes);
        }
        
        // Check for upcoming draw by repetition
        // ONLY adjust alpha when we're losing (alpha < 0)
        if(halfmove >= 3 && alpha < 0 && hasGameCycle(ply)){
            alpha = drawScore(nodes);
            if(alpha >= beta) return alpha;
        }
    }
    if(ply>=MAX_PLY-1) return evaluate();

    if (ply > 0) { // Evita checar tempo a cada nó na raiz
        if((nodes & 0x3FFF)==0 && timeUp()) { stop=true; return alpha; }
    }

    bool isInCheck = inCheck(white_to_move);
    if(isInCheck) depth++;

    nodes++;

    TTEntry &te = TT[hash & (TT_SIZE-1)];
    Move ttMove = MOVE_NONE;
    if(te.key==hash && te.depth>=depth && halfmove < 90){
        int sc = te.score;
        if(sc >= MATE_IN_MAX) sc -= ply;
        if(sc <= -MATE_IN_MAX) sc += ply;
        int entryFlag = te.flag();
        if(entryFlag==TT_EXACT) return sc;
        if(entryFlag==TT_ALPHA && sc<=alpha) return sc;
        if(entryFlag==TT_BETA  && sc>=beta)  return sc;
    }
    if (te.key == hash) {
        ttMove = te.bestMove();
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

    int best=-INF_SCORE;
    Move bestMove = MOVE_NONE;
    int origAlpha = alpha;
    int moveCount=0;
    bool hasLegalMove = false;

    Move mv[MAX_MOVES];
    int generated = generatePseudo(mv, false);
    MovePicker picker(*this, mv, generated, ttMove, ply);

    for(Move m = picker.next(); !moveIsNone(m); m = picker.next()){
        makeMove(m);
        if (inCheck(!white_to_move)) {
            undoMove(m);
            continue;
        }
        hasLegalMove = true;
        moveCount++;

        int sc;
        if(moveCount==1){
            sc = -pvs(depth-1, ply+1, -beta, -alpha);
        } else {
            int newDepth = depth-1;
            if(moveCount > 3 && depth >= 3 && !moveIsCapture(m) && movePromotion(m)==0 && !isInCheck){
                sc = -pvs(newDepth - 1, ply+1, -alpha-1, -alpha);
            } else {
                sc = alpha + 1;
            }

            if(sc > alpha){
                sc = -pvs(newDepth, ply+1, -beta, -alpha);
            }
        }
        
        undoMove(m);
        if (stop) return alpha;

        if(sc > best){ best = sc; bestMove = m; }
        if(sc > alpha){
            alpha = sc;
            if(!moveIsCapture(m)){
                int side = white_to_move? 0:1;
                history_heur[side][moveFrom(m)][moveTo(m)] += depth*depth;
                if (killers.killer[0][ply] == MOVE_NONE || !(killers.killer[0][ply] == m)) {
                    killers.killer[1][ply] = killers.killer[0][ply];
                    killers.killer[0][ply] = m;
                }
            }
            if(alpha >= beta) {
                if (!moveIsCapture(m)) {
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

    if(!moveIsNone(bestMove)) {
        int flag = TT_EXACT;
        if(best<=origAlpha) flag = TT_ALPHA;
        else if(best>=beta) flag = TT_BETA;
        int store = best;
        if(best >= MATE_IN_MAX) store += ply;
        if(best <= -MATE_IN_MAX) store -= ply;

        TTEntry& entry = TT[hash & (TT_SIZE-1)];
        bool replace = entry.key != hash || entry.generation() != TTGeneration || entry.depth <= depth;
        if(flag == TT_EXACT && entry.flag() != TT_EXACT) replace = true;
        if(replace){
            entry.store(hash, depth, store, static_cast<uint8_t>(flag), bestMove, TTGeneration);
        }
    }

    return best;
}

// ============ Busca (Iterative + Aspiration Windows) ============
Move DeepBeckyEngine::search(int maxDepth, int timeMs){
    start_time = chrono::high_resolution_clock::now();
    time_limit_ms = timeMs;
    stop=false; nodes=0;
    clearHeuristics(); // Limpa histórico e killers a cada busca

    constexpr int CONTEMPT_VALUE = 20; // centipawns
    rootSideIsWhite = white_to_move;
    contempt = white_to_move ? CONTEMPT_VALUE : -CONTEMPT_VALUE;

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
                best = te.bestMove();
            }

            auto now = chrono::high_resolution_clock::now();
            long long ms = chrono::duration_cast<chrono::milliseconds>(now-start_time).count();
            if (ms == 0) ms = 1;
            std::vector<Move> pvLine = getPV(d);
            long long nps = (nodes * 1000LL) / ms;
            cout << "info depth " << d;
            if (sc >= MATE_IN_MAX || sc <= -MATE_IN_MAX) {
                int mateDistance = MATE_SCORE - (sc > 0 ? sc : -sc);
                int mateMoves = (mateDistance + 1) / 2;
                if (sc < 0) mateMoves = -mateMoves;
                cout << " score mate " << mateMoves;
            } else {
                cout << " score cp " << sc;
            }
            cout << " time " << ms
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

    if(moveIsNone(best)){
        Move legal[MAX_MOVES];
        int legalCount = generateLegal(legal);
        if(legalCount > 0){
            best = legal[0];
        }
    }
    return best;
}


// ============ PV helpers ===========
vector<Move> DeepBeckyEngine::getPV(int maxDepth){
    vector<Move> pv;
    uint64_t current_hash = hash;

    for (int d = 0; d < maxDepth; d++){
        TTEntry &te = TT[current_hash & (TT_SIZE-1)];
    Move m = te.bestMove();
    if (te.key != current_hash || moveIsNone(m)) break;

        Move legal[MAX_MOVES];
        int legalCount = generatePseudo(legal, false);
        bool found = false;
        for(int i=0;i<legalCount;++i) {
            if(legal[i] == m) {
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