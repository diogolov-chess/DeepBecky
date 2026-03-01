/*
 * This file is part of Deep Becky 2.0 - A UCI Chess Engine written by AI
 * Copyright © 2025-2026 Diogo de O. Almeida.
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

// Search Implementation (Lazy SMP)
#include "search.h"
#include "movepick.h"
#include "position.h"
#include "thread.h"
#include "movegen.h"
#include "evaluate.h"
#include "tt.h"
#include "timeman.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <cmath>
#include <sstream>

namespace Search {

// =============================================================================
// LMR Reduction Table
// =============================================================================
int Reductions[64][64];  // [depth][moveNumber]

void init() {
    for (int d = 1; d < 64; d++) {
        for (int m = 1; m < 64; m++) {
            // Formula: log(d) * log(m) / 2
            Reductions[d][m] = int(std::log(d) * std::log(m) / 2.0);
        }
    }
}

// Reduction function
inline int reduction(bool improving, int depth, int moveCount) {
    int r = Reductions[std::min(depth, 63)][std::min(moveCount, 63)];
    return r - improving;  // Less reduction when improving
}

// Futility margin
inline int futilityMargin(int depth, bool improving) {
    return 100 * (depth - improving);  // ~100 centipawns per depth
}

// Move count pruning threshold
inline int futilityMoveCount(bool improving, int depth) {
    return (6 + depth * depth) / (2 - improving);
}

inline int drawScore(uint64_t nodes, int contempt) {
    // Base draw value with small noise to avoid 3-fold blindness
    int base = int(2 * (nodes & 1) - 1);  // -1 or +1
    // Add contempt: negative score if we have positive contempt (we want to win, not draw)
    // This makes the engine avoid repetitions when it's ahead
    return base - contempt;
}

} // namespace Search

// =============================================================================
// Search member functions of Position
// =============================================================================

// Quiescence search
int Position::qsearch(int alpha, int beta, int ply) {
    if (stopSearching) return alpha;
    
    // Safety limit
    if (ply >= MAX_PLY - 1) {
        int ev = evaluate();
        return ev + (white_to_move ? contempt : -contempt);
    }
    
    nodes++;
    
    // Periodic stop check
    if ((nodes & 0x3FFF) == 0) {
        if (Threads.stop.load(std::memory_order_relaxed) || timeUp()) {
            stopSearching = true;
            if (thread && thread->idx == 0)
                Threads.stop.store(true, std::memory_order_relaxed);
            return alpha;
        }
    }
    
    if (ply > selDepth) selDepth = ply;

    if (ply > 0 && isDraw(ply)) {
        return Search::drawScore(static_cast<uint64_t>(nodes), contempt);
    }

    bool isInCheck = inCheck(white_to_move);

    // TT Probe in qsearch
    TT.prefetch(hash);
    bool ttHit = false;
    TTEntry* tte = TT.probe(hash, ttHit);
    if (ttHit) {
        int ttScore = static_cast<int>(tte->value16);
        TTFlag ttFlag = tte->flag();
        // Adjust mate scores from TT (was missing — caused search instability)
        if (ttScore >= MATE_IN_MAX) ttScore -= ply;
        if (ttScore <= -MATE_IN_MAX) ttScore += ply;
        // TT cutoff in qsearch
        if (ttFlag == TT_EXACT) return ttScore;
        if (ttFlag == TT_BETA && ttScore >= beta) return ttScore;
        if (ttFlag == TT_ALPHA && ttScore <= alpha) return ttScore;
    }

    // Use TT eval if available, otherwise compute from scratch
    int stand;
    if (ttHit && tte->eval16 != EVAL_NONE) {
        stand = static_cast<int>(tte->eval16);
    } else {
        stand = evaluate();
    }
    // Add contempt AFTER loading pure eval (contempt not stored in TT)
    stand += (white_to_move ? contempt : -contempt);
    int best = stand;

    if (!isInCheck) {
        if (stand >= beta) return stand;
        if (stand > alpha) alpha = stand;
    } else {
        best = -INF_SCORE;
    }

    Move caps[MAX_MOVES];
    int capCount;

    if (isInCheck) {
        capCount = generatePseudo(caps, false);
    } else {
        capCount = generatePseudo(caps, true);
    }

    // Score moves
    for (int i = 0; i < capCount; ++i) {
        Move& m = caps[i];
        int from_sq = moveFrom(m);
        int to_sq = moveTo(m);
        int piece = piece_board[from_sq];
        int captured = moveIsEnPassant(m) ? (white_to_move ? BPAWN : WPAWN) : piece_board[to_sq];
        m.score = 10 * PIECE_VALUE[captured] - PIECE_VALUE[piece];
        if (movePromotion(m)) m.score += PIECE_VALUE[movePromotion(m)];
    }

    int legalMoves = 0;

    for (int i = 0; i < capCount; ++i) {
        int best_idx = i;
        for (int j = i + 1; j < capCount; ++j) {
            if (caps[j].score > caps[best_idx].score) best_idx = j;
        }
        if (best_idx != i) std::swap(caps[i], caps[best_idx]);

        Move& m = caps[i];
        
        // Skip bad captures using SEE (but not when in check)
        if (!isInCheck && see(m) < 0) continue;
        
        // Delta pruning - if capturing won't bring us close to alpha, skip
        if (!isInCheck && !movePromotion(m)) {
            int to_sq = moveTo(m);
            int captured = moveIsEnPassant(m) ? (white_to_move ? BPAWN : WPAWN) : piece_board[to_sq];
            int delta = PIECE_VALUE[captured] + 200;  // 200 is a safety margin
            if (stand + delta < alpha) continue;
        }

        makeMove(m);
        // Check if move is illegal (left our king in check)
        if (inCheck(!white_to_move)) {
            undoMove(m);
            continue;
        }
        legalMoves++;

        int score = -qsearch(-beta, -alpha, ply + 1);
        undoMove(m);

        if (stopSearching) return alpha;

        if (score > best) {
            best = score;
            if (score > alpha) {
                alpha = score;
                if (score >= beta) {
                    return score;  // Fail high
                }
            }
        }
    }

    if (isInCheck && legalMoves == 0) {
        return -MATE_SCORE + ply;
    }

    return best;
}

// Principal Variation Search - with Singular Extensions, CutNode, ProbCut
int Position::pvs(int depth, int alpha, int beta, int ply, bool cutNode, Move excludedMove) {
    if (stopSearching) return alpha;
    
    bool rootNode = (ply == 0);
    bool pvNode = (beta - alpha > 1);  // PV node detection
    
    // Quiescence at leaf
    if (depth <= 0) return qsearch(alpha, beta, ply);
    
    if (!rootNode) {
        // Draw detection - penalize draws based on contempt
        if (isDraw(ply)) {
            return Search::drawScore(static_cast<uint64_t>(nodes), contempt);
        }
        
        // Mate distance pruning
        alpha = std::max(alpha, -MATE_SCORE + ply);
        beta = std::min(beta, MATE_SCORE - ply - 1);
        if (alpha >= beta) return alpha;
        
        // Check for upcoming draw by repetition
        if (halfmove >= 3 && hasGameCycle(ply)) {
            int drawVal = Search::drawScore(static_cast<uint64_t>(nodes), contempt);
            if (drawVal > alpha) {
                alpha = drawVal;
                if (alpha >= beta) return alpha;
            }
        }
    }
    
    if (ply >= MAX_PLY - 1) {
        int ev = evaluate();
        return ev + (white_to_move ? contempt : -contempt);
    }
    
    // Time check (not too frequently)
    if (ply > 0 && (nodes & 0x1FFF) == 0) {
        if (Threads.stop.load(std::memory_order_relaxed) || timeUp()) {
            stopSearching = true;
            if (thread && thread->idx == 0)
                Threads.stop.store(true, std::memory_order_relaxed);
            return alpha;
        }
    }
    
    bool isInCheck = inCheck(white_to_move);
    
    nodes++;
    
    // Update selective depth
    if (ply > selDepth) selDepth = ply;
    
    // ============ TT Probe ============
    TT.prefetch(hash);
    bool ttHit = false;
    TTEntry* tte = TT.probe(hash, ttHit);
    Move ttMove = MOVE_NONE;
    int ttScore = -INF_SCORE;
    int ttDepth = -1;
    TTFlag ttFlag = TT_ALPHA;
    bool ttCapture = false;
    int16_t ttEval = EVAL_NONE;  // static eval from TT
    
    if (ttHit) {
        // Reconstruct the TT move from packed 16-bit format
        TTMoveUnpacked ttmu = unpackTTMove(tte->move16, white_to_move);
        ttMove = makeMovePacked(ttmu.squares, ttmu.flags);
        
        // Reconstruct flags lost during 16-bit packing:
        // - Capture flag: check if destination square has a piece
        // - Double-push flag: check if pawn moves 2 ranks
        if (!moveIsNone(ttMove)) {
            int ttFrom = moveFrom(ttMove);
            int ttTo   = moveTo(ttMove);
            if (piece_board[ttTo] != EMPTY) {
                ttMove.flags |= MOVE_FLAG_CAPTURE;
            }
            int piece = piece_board[ttFrom];
            if ((piece == WPAWN || piece == BPAWN) && std::abs(ttFrom - ttTo) == 16) {
                ttMove.flags |= MOVE_FLAG_DOUBLEPUSH;
            }
        }
        
        ttDepth = tte->depth();
        ttScore = static_cast<int>(tte->value16);
        // Adjust mate scores from TT
        if (ttScore >= MATE_IN_MAX) ttScore -= ply;
        if (ttScore <= -MATE_IN_MAX) ttScore += ply;
        ttFlag = tte->flag();
        ttEval = tte->eval16;
        
        // Track if TT move is a capture (for LMR)
        if (!moveIsNone(ttMove)) {
            ttCapture = moveIsCapture(ttMove); // now uses the reconstructed flag
        }
        
        // TT cutoffs - NOT at root, NOT in PV nodes, NOT during singular search
        if (!rootNode && !pvNode && !excludedMove.squares && ttDepth >= depth && halfmove < 90) {
            if (ttFlag == TT_EXACT) return ttScore;
            if (ttFlag == TT_ALPHA && ttScore <= alpha) return ttScore;
            if (ttFlag == TT_BETA && ttScore >= beta) return ttScore;
        }
    }
    
    // ============ Static Evaluation ============
    // Use TT eval when available to avoid calling evaluate()
    int staticEval;
    int16_t rawEval = EVAL_NONE; // unadjusted eval for TT storage
    if (isInCheck) {
        staticEval = -INF_SCORE;  // No static eval when in check
    } else if (ttHit && ttEval != EVAL_NONE) {
        // Use stored eval from TT — saves a full evaluate() call
        rawEval = ttEval;
        // Add contempt to the pure TT eval
        staticEval = static_cast<int>(ttEval) + (white_to_move ? contempt : -contempt);
        // Use TT score as better estimate if bound direction agrees.
        // ttScore naturally incorporates contempt effects from the search that produced it.
        if (ttFlag == TT_EXACT
            || (ttFlag == TT_BETA  && ttScore > staticEval)
            || (ttFlag == TT_ALPHA && ttScore < staticEval)) {
            staticEval = ttScore;
        }
    } else {
        staticEval = evaluate();
        rawEval = static_cast<int16_t>(std::clamp(staticEval, -32000, 32000));
        // Add contempt after storing pure rawEval
        staticEval += (white_to_move ? contempt : -contempt);
    }
    
    // Store static eval in search stack for improving detection
    searchStack[ply] = staticEval;
    
    // "Improving" heuristic - compare with 2 plies ago
    // Much more accurate than comparing with alpha
    bool improving = false;
    if (!isInCheck && ply >= 2) {
        improving = (staticEval > searchStack[ply - 2]);
    } else if (!isInCheck && ply < 2) {
        improving = (staticEval > alpha);
    }
    
    // ============ Razoring (conservative) ============
    if (!pvNode && !isInCheck && depth <= 1 && staticEval + 300 <= alpha) {
        return qsearch(alpha, beta, ply);
    }
    
    // ============ Futility Pruning / Static Null Move (reverse futility) ============
    if (!pvNode && !isInCheck && depth < 5 && staticEval - Search::futilityMargin(depth, improving) >= beta) {
        return staticEval;
    }
    
    // ============ Null Move Pruning with Verification ============
    if (!pvNode && !isInCheck && depth >= 3 && ply > 0 && staticEval >= beta
        && hasNonPawnMaterial(white_to_move)
        && (ply >= thread->nmpMinPly || (white_to_move ? 0 : 1) != thread->nmpColor))
    {
        makeNullMove();
        // Aggressive R formula at high depths
        // At depth=6: R~4, depth=10: R~5, depth=18: R~8
        int R = (854 + 68 * depth) / 258 + std::min((staticEval - beta) / 192, 3);
        
        int nmScore = -pvs(depth - R, -beta, -beta + 1, ply + 1, !cutNode);
        undoNullMove();
        
        if (nmScore >= beta) {
            // Don't return unproven mates
            if (nmScore >= MATE_IN_MAX) nmScore = beta;
            
            // At low depths or clear wins, accept directly
            if (thread->nmpMinPly || (std::abs(beta) < 20000 && depth < 14))
                return nmScore;
            
            // Verification search at high depths (depth >= 14):
            // Re-search with null move disabled for our side to avoid
            thread->nmpMinPly = ply + 3 * (depth - R) / 4;
            thread->nmpColor = white_to_move ? 0 : 1;
            
            int vScore = pvs(depth - R, beta - 1, beta, ply, false);
            
            thread->nmpMinPly = 0;
            
            if (vScore >= beta)
                return nmScore;
        }
    }
    
    // ============ Internal Iterative Deepening ============
    // If no TT move at high depth, do a shallow search first
    // Deeper reduction at cut nodes (IIR - Internal Iterative Reduction)
    if (depth >= 6 && moveIsNone(ttMove) && !isInCheck) {
        int iirReduction = cutNode ? 2 : 1;
        depth -= iirReduction;
    }
    
    // ============ ProbCut ============
    // If a capture with reduced search returns much above beta, prune
    if (!pvNode && depth >= 5 && std::abs(beta) < MATE_IN_MAX && moveIsNone(excludedMove)) {
        int raisedBeta = beta + 190 - 45 * improving;
        
        Move caps[MAX_MOVES];
        int capCount = generatePseudo(caps, true); // captures only
        int probCutCount = 0;
        
        // Sort captures by MVV-LVA
        for (int i = 0; i < capCount; ++i) {
            int from_sq = moveFrom(caps[i]);
            int to_sq = moveTo(caps[i]);
            int piece = piece_board[from_sq];
            int captured = moveIsEnPassant(caps[i]) ? (white_to_move ? BPAWN : WPAWN) : piece_board[to_sq];
            caps[i].score = 10 * PIECE_VALUE[captured] - PIECE_VALUE[piece];
        }
        
        for (int i = 0; i < capCount && probCutCount < 3; ++i) {
            // Selection sort next best
            int best_idx = i;
            for (int j = i + 1; j < capCount; ++j) {
                if (caps[j].score > caps[best_idx].score) best_idx = j;
            }
            if (best_idx != i) std::swap(caps[i], caps[best_idx]);
            
            Move& m = caps[i];
            
            // Skip bad captures by SEE
            if (see(m) < 0) continue;
            
            makeMove(m);
            if (inCheck(!white_to_move)) {
                undoMove(m);
                continue;
            }
            probCutCount++;
            
            // Phase 1: qsearch verification
            int val = -qsearch(-raisedBeta, -raisedBeta + 1, ply + 1);
            
            // Phase 2: deeper verification
            if (val >= raisedBeta)
                val = -pvs(depth - 4, -raisedBeta, -raisedBeta + 1, ply + 1, !cutNode);
            
            undoMove(m);
            
            if (val >= raisedBeta)
                return val;
        }
    }
    
    int best = -INF_SCORE;
    Move bestMove = MOVE_NONE;
    int moveCount = 0;
    bool hasLegalMove = false;
    
    // Track searched quiet moves for history malus
    Move quietsSearched[64];
    int quietCount = 0;
    
    // Move generation
    Move mv[MAX_MOVES];
    int generated = generatePseudo(mv, false);
    MovePicker picker(*this, mv, generated, ttMove, ply);
    
    // ============ Main Move Loop ============
    for (Move m = picker.next(); !moveIsNone(m); m = picker.next()) {
        
        // Skip excluded move (for singular extension search)
        if (m == excludedMove) continue;
        
        bool isCapture = moveIsCapture(m);
        bool isPromotion = movePromotion(m) != 0;
        
        // Pre-compute SEE for check extension (must be before makeMove)
        int moveSee = isCapture ? see(m) : 0;
        
        int newDepth = depth - 1;
        int extension = 0;
        bool singularLMR = false;
        
        // ============ Singular Extensions ============
        // Test if the TT move is the only good move; if so, extend its search
        if (depth >= 6
            && m == ttMove
            && !rootNode
            && moveIsNone(excludedMove)
            && std::abs(ttScore) < MATE_IN_MAX
            && (ttFlag == TT_BETA || ttFlag == TT_EXACT)
            && ttDepth >= depth - 3) 
        {
            int singularBeta = ttScore - 2 * depth;
            int halfDepth = depth / 2;
            
            // Search excluding the TT move at reduced depth
            int seScore = pvs(halfDepth, singularBeta - 1, singularBeta, ply, cutNode, m);
            
            if (seScore < singularBeta) {
                extension = 1;      // TT move is singular — extend!
                singularLMR = true;
                
                // Double extension for clearly singular moves
                if (!pvNode && seScore < singularBeta - 60)
                    extension = 2;
            }
            else if (singularBeta >= beta) {
                return singularBeta; // MultiCut: all moves are good enough
            }
            else if (cutNode) {
                extension = -2;     // Negative extension at cut nodes
            }
        }
        
        // Make move and check legality
        makeMove(m);
        
        // Check if WE are in check (illegal move) - this is after side flipped
        bool weAreInCheck = inCheck(!white_to_move);
        if (weAreInCheck) {
            undoMove(m);
            continue;
        }
        
        // Check if opponent is in check (we give check) - SAME side as white_to_move now
        // This is computed early since we need it for pruning and LMR
        bool givesCheck = inCheck(white_to_move);
        
        hasLegalMove = true;
        moveCount++;
        
        // ============ Check Extension ============
        // Only extend good checks: discovered checks or checks with SEE >= 0.
        // This replaces the old blanket "if (isInCheck) depth++" which
        // inflated the tree on forcing lines.
        if (extension == 0 && givesCheck && moveSee >= 0) {
            extension = 1;
        }
        
        newDepth += extension;
        
        // ============ Pruning at low depths ============
        if (!rootNode && best > -MATE_IN_MAX) {
            
            // Late Move Pruning / Move Count Pruning
            if (!isCapture && !isPromotion && !isInCheck && !givesCheck) {
                if (moveCount > Search::futilityMoveCount(improving, depth)) {
                    undoMove(m);
                    continue;
                }
            }
            
            // Futility Pruning - child node
            if (!isCapture && !isPromotion && !isInCheck && !givesCheck && depth <= 4) {
                int futilityValue = staticEval + 100 * depth;
                if (futilityValue <= alpha) {
                    undoMove(m);
                    if (futilityValue > best) best = futilityValue;
                    continue;
                }
            }
            
            // SEE pruning for captures at low depths
            if (depth <= 3 && isCapture && !isPromotion && !givesCheck) {
                int seeValue = see(m);
                if (seeValue < -100 * depth) {
                    undoMove(m);
                    continue;
                }
            }
        }
        
        int sc;
        
        if (moveCount == 1) {
            // First move - full window search
            sc = -pvs(newDepth, -beta, -alpha, ply + 1, false);
        } else {
            // ============ Late Move Reductions (LMR) - Enhanced ============
            int R = 0;
            if (depth >= 3 && moveCount > 2 && !isCapture && !isPromotion && !isInCheck) {
                // Use reduction table
                R = Search::Reductions[std::min(depth, 63)][std::min(moveCount, 63)];
                
                // Reduce more in non-PV nodes
                if (!pvNode) R++;
                
                // Reduce more at cut nodes
                if (cutNode) R += 2;
                
                // Reduce less when improving
                if (improving) R--;
                
                // Reduce less for singular TT moves
                if (singularLMR) R -= 2;
                
                // Reduce more if TT move is a capture (quiet moves are worse)
                if (ttCapture) R++;
                
                // Reduce less for killer moves
                if (m == thread->killers.killer[0][ply] || m == thread->killers.killer[1][ply]) R--;
                
                // Reduce less if giving check
                if (givesCheck) R--;
                
                // History-based reduction
                int side = white_to_move ? 1 : 0;  // flipped because we already made the move
                int histScore = thread->history_heur[side][moveFrom(m)][moveTo(m)];
                R -= histScore / 5000;
                
                // Don't reduce below 1
                R = std::max(0, R);
                
                // Don't reduce too much
                R = std::min(R, newDepth - 1);
            }
            
            // Reduced depth search (null window)
            if (R > 0) {
                sc = -pvs(newDepth - R, -alpha - 1, -alpha, ply + 1, true);
            } else {
                sc = alpha + 1;  // Force re-search
            }
            
            // Re-search at full depth if LMR failed high
            if (sc > alpha) {
                sc = -pvs(newDepth, -alpha - 1, -alpha, ply + 1, !cutNode);
                
                // Full window re-search for PV nodes
                if (sc > alpha && sc < beta) {
                    sc = -pvs(newDepth, -beta, -alpha, ply + 1, false);
                }
            }
        }
        
        undoMove(m);
        
        if (stopSearching) return alpha;
        
        // Track searched quiet moves for history malus
        if (!isCapture && !isPromotion && quietCount < 64) {
            quietsSearched[quietCount++] = m;
        }
        
        // Update best
        if (sc > best) {
            best = sc;
            
            if (sc > alpha) {
                // Only set bestMove when alpha is actually raised.
                // This is CRITICAL: if bestMove is set on fail-low, the TT store
                // logic would incorrectly mark the entry as TT_EXACT instead of
                // TT_ALPHA, corrupting the search tree.
                bestMove = m;
                
                // At root (ply==0), immediately track the best move in a
                // per-position variable. This avoids relying on the shared TT
                // (which can be overwritten by other threads) to recover
                // the root best move after the search completes.
                if (rootNode) {
                    rootBestMove = m;
                    rootBestScore = sc;
                }
                
                // Update history for quiet moves that improve alpha (gravity formula)
                if (!isCapture) {
                    int side = white_to_move ? 0 : 1;
                    int bonus = std::min(depth * depth, 400);
                    int& entry = thread->history_heur[side][moveFrom(m)][moveTo(m)];
                    // Gravity formula: entry += bonus - entry * |bonus| / 16384
                    entry += bonus - entry * std::abs(bonus) / 16384;
                }
                
                alpha = sc;
                
                if (alpha >= beta) {
                    // Beta cutoff - update killers
                    if (!isCapture) {
                        if(thread->killers.killer[0][ply] == MOVE_NONE || !(thread->killers.killer[0][ply] == m)){
                            thread->killers.killer[1][ply] = thread->killers.killer[0][ply];
                            thread->killers.killer[0][ply] = m;
                        }
                        
                        
                        int side = white_to_move ? 0 : 1;
                        int malus = std::min(depth * depth, 400);
                        for (int q = 0; q < quietCount; q++) {
                            if (!(quietsSearched[q] == m)) {
                                int& he = thread->history_heur[side][moveFrom(quietsSearched[q])][moveTo(quietsSearched[q])];
                                he += -malus - he * malus / 16384;
                            }
                        }
                    }
                    break;
                }
            }
        }
    }
    
    if (!hasLegalMove) {
        return isInCheck ? (-MATE_SCORE + ply) : 0; // Checkmate or Stalemate
    }
    
    
    if (moveIsNone(excludedMove)) {
        TTFlag flag;
        if (best >= beta)
            flag = TT_BETA;
        else if (pvNode && !moveIsNone(bestMove))
            flag = TT_EXACT;
        else
            flag = TT_ALPHA;
        
        int storeScore = best;
        if (best >= MATE_IN_MAX) storeScore += ply;
        if (best <= -MATE_IN_MAX) storeScore -= ply;
        
        uint16_t storeSq = moveIsNone(bestMove) ? uint16_t(0) : bestMove.squares;
        uint8_t  storeFl = moveIsNone(bestMove) ? uint8_t(0)  : bestMove.flags;
        uint16_t packedMove = packTTMove(storeSq, storeFl);
        
        tte->save(hash, static_cast<int16_t>(storeScore), pvNode, flag,
                  depth, packedMove, rawEval, TT.generation());
    }
    
    return best;
}

// Iterative Deepening Search (Lazy SMP aware)
// Main thread (idx==0): orchestrates helpers, manages time, prints info
// Helper threads: search until Threads.stop is set
Move Position::search(int maxDepth, int timeMs) {
    bool isMainThread = (!thread || thread->idx == 0);

    nodes = 0;
    selDepth = 0;
    stopSearching = false;
    
    start_time = std::chrono::high_resolution_clock::now();
    time_limit_ms = timeMs;
    
    // Only main thread bumps TT generation (helpers already set up by startThinking)
    if (isMainThread) {
        TT.newSearch();
    }
    clearHeuristics();
    
    // Initialize search stack for improving detection
    std::memset(searchStack, 0, sizeof(searchStack));
    
    // Dynamic contempt based on material evaluation
    // When we're winning, we should STRONGLY avoid draws/repetitions
    rootSideIsWhite = white_to_move;
    
    int eval = evaluate();  // Get current evaluation
    int absEval = eval > 0 ? eval : -eval;
    
    // Adjust time for winning endgames - need deeper search to find mates
    if (isMainThread && time_limit_ms > 0) {
        // Quick phase count for time adjustment
        int quickPhase = popcount(bitboards[WKNIGHT]) + popcount(bitboards[BKNIGHT])
                       + popcount(bitboards[WBISHOP]) + popcount(bitboards[BBISHOP])
                       + popcount(bitboards[WROOK]) * 2 + popcount(bitboards[BROOK]) * 2
                       + popcount(bitboards[WQUEEN]) * 4 + popcount(bitboards[BQUEEN]) * 4;
        TimeMgr.adjustForWinningEndgame(eval, quickPhase);
    }
    
    // Scale contempt based on how much we're winning
    // Base: 20cp, but increase dramatically when ahead
    int dynamicContempt;
    if (absEval >= 800) {
        // Winning by 8+ pawns (like Q+B vs K) - VERY strong contempt
        dynamicContempt = 200;
    } else if (absEval >= 400) {
        // Winning by 4+ pawns - strong contempt
        dynamicContempt = 100;
    } else if (absEval >= 200) {
        // Winning by 2+ pawns - moderate contempt  
        dynamicContempt = 50;
    } else if (absEval >= 100) {
        // Winning by 1+ pawn - slight contempt
        dynamicContempt = 30;
    } else {
        // Equal or slightly ahead - base contempt
        dynamicContempt = 20;
    }
    
    // Apply sign based on side to move
    contempt = white_to_move ? dynamicContempt : -dynamicContempt;
    
    
	
    Move legalMoves[MAX_MOVES];
    int numLegalMoves = 0;
    bool rootInCheck = false;
    bool obviousMove = false;
    int effectiveMaxDepth = maxDepth;
    
    if (isMainThread) {
        // Generate legal moves to check for obvious situations
        numLegalMoves = generateLegal(legalMoves);
        
        // Check if we're in check
        rootInCheck = inCheck(white_to_move);
        
        // Check for recapture (opponent just captured something)
        bool isRecapture = false;
        if (!uci_history.empty()) {
            std::string lastMove = uci_history.back();
            if (lastMove.length() >= 4) {
                int toFile = lastMove[2] - 'a';
                int toRank = lastMove[3] - '1';
                int captureSquare = toRank * 8 + toFile;
                
                for (int i = 0; i < std::min(numLegalMoves, 5); i++) {
                    if (moveTo(legalMoves[i]) == captureSquare) {
                        int targetPiece = piece_board[captureSquare];
                        if (targetPiece != EMPTY) {
                            bool targetIsWhite = (targetPiece >= WPAWN && targetPiece <= WKING);
                            if (targetIsWhite != white_to_move) {
                                isRecapture = true;
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        // Only one legal move - play it immediately after minimal search
        if (numLegalMoves == 1) {
            selDepth = 1;
            makeMove(legalMoves[0]);
            int score = -qsearch(-INF_SCORE, INF_SCORE, 1);
            undoMove(legalMoves[0]);
            nodes++;
            
            auto now_time = std::chrono::high_resolution_clock::now();
            long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now_time - start_time).count();
            if (ms == 0) ms = 1;
            
            std::cout << "info depth 1 seldepth 1 score cp " << score
                      << " time " << ms << " nodes " << nodes
                      << " nps " << (nodes * 1000 / ms)
                      << " pv " << moveToUCI(legalMoves[0])
                      << std::endl;
            
            if (thread) {
                thread->bestMove = legalMoves[0];
                thread->bestScore = score;
                thread->completedDepth = 1;
            }
            return legalMoves[0];
        }
        
        // Obvious move: only when there is exactly 1 legal move (already handled above
        // with instant return). With 2+ moves, always search normally.
        obviousMove = TimeManagement::isObviousMove(numLegalMoves, rootInCheck, isRecapture);
        
        // NOTE: When numLegalMoves == 1, we already returned above, so
        // obviousMove will never be true here. No depth/time reduction needed.
    } else {
        // Helper threads: generate legal moves for TT validation
        numLegalMoves = generateLegal(legalMoves);
    }
    
    Move best = MOVE_NONE;
    Move prevBest = MOVE_NONE;
    int prevScore = 0;
    int stabilityCount = 0;  // Count iterations with same best move
    int totalIterations = 0;
    
    // CRITICAL: Always search to at least depth 4 to avoid blunders
    // The engine was blundering Rb2?? at depth 0 when it had mate!
    int minimumDepth = 4;
    
    for (int d = 1; d <= effectiveMaxDepth && !stopSearching; d++) {
        // Check global stop flag
        if (Threads.stop.load(std::memory_order_relaxed)) {
            stopSearching = true;
            break;
        }
        
        selDepth = 0;
        
        // Reset rootBestMove for this iteration so we don't use a
        // stale move from a previous depth if the search is aborted
        rootBestMove = MOVE_NONE;
        rootBestScore = -INF_SCORE;
        
        int alpha = -INF_SCORE, beta = INF_SCORE;
        int delta = 25;
        
        // Aspiration window from depth 4
        if (d >= 4) {
            alpha = std::max(prevScore - delta, -INF_SCORE);
            beta  = std::min(prevScore + delta,  INF_SCORE);
        }
        
        while (true) {
            int score = pvs(d, alpha, beta, 0);
            
            if (stopSearching) break;
            
            if (score <= alpha) {
                // Fail-low: widen alpha gradually, move beta closer
                beta   = (alpha + beta) / 2;
                alpha  = std::max(score - delta, -INF_SCORE);
                delta += delta / 3;
                continue;
            }
            if (score >= beta) {
                // Fail-high: widen beta gradually
                beta   = std::min(score + delta, INF_SCORE);
                delta += delta / 3;
                continue;
            }
            
            prevScore = score;
            
            // Use rootBestMove tracked directly inside pvs() at ply 0.
            // This is safe from multi-threading races because rootBestMove
            // is per-position (per-thread), unlike TT entries which are shared.
            if (rootBestMove != MOVE_NONE) {
                // Validate that rootBestMove is legal (defensive check)
                bool isLegalMove = false;
                for (int i = 0; i < numLegalMoves; i++) {
                    if (legalMoves[i] == rootBestMove) {
                        isLegalMove = true;
                        break;
                    }
                }
                if (isLegalMove) {
                    best = rootBestMove;
                }
            }
            // Fallback to TT only if rootBestMove was not set
            if (best == MOVE_NONE) {
                bool ttFound = false;
                TTEntry* tte = TT.probe(hash, ttFound);
                if (ttFound) {
                    TTMoveUnpacked ttmu = unpackTTMove(tte->move16, white_to_move);
                    Move ttBest = makeMovePacked(ttmu.squares, ttmu.flags);
                    bool isLegalMove = false;
                    for (int i = 0; i < numLegalMoves; i++) {
                        // Match by from+to squares
                        if ((legalMoves[i].squares & 0x0FFF) == (ttBest.squares & 0x0FFF)) {
                            isLegalMove = true;
                            ttBest = legalMoves[i]; // use generated move with correct flags
                            break;
                        }
                    }
                    if (isLegalMove) {
                        best = ttBest;
                    }
                }
            }
            
            // Print info (main thread only)
            if (isMainThread) {
                auto now = std::chrono::high_resolution_clock::now();
                long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
                if (ms == 0) ms = 1;
                uint64_t totalNodes = Threads.nodes_searched();
                uint64_t nps = (totalNodes * 1000ULL) / static_cast<uint64_t>(ms);
                
                std::vector<Move> pvLine = getPV(d);
                std::string pvStr = pvToString(pvLine);
                
                std::cout << "info depth " << d 
                          << " seldepth " << selDepth;
                
                // Mate score formatting
                if (score >= MATE_IN_MAX || score <= -MATE_IN_MAX) {
                    int mateDistance = MATE_SCORE - (score > 0 ? score : -score);
                    int mateMoves = std::max(1, (mateDistance + 1) / 2);
                    if (score < 0) mateMoves = -mateMoves;
                    std::cout << " score mate " << mateMoves;
                } else {
                    std::cout << " score cp " << score;
                }
                
                std::cout << " time " << ms
                          << " nodes " << totalNodes
                          << " nps " << nps
                          << " hashfull " << TT.hashfull();
                if (Threads.size() > 1)
                    std::cout << " threads " << Threads.size();
                std::cout << " pv " << pvStr
                          << std::endl;
                std::cout.flush();
            }
            
            // Track best move stability
            totalIterations++;
            if (best == prevBest) {
                stabilityCount++;
            } else {
                prevBest = best;
                stabilityCount = 0;  // RESET on best move change
            }
            
            // Check for score drops (extend time if score dropped) - main thread only
            if (isMainThread && d > 1 && prevScore - score > 30) {
                TimeMgr.adjustForScoreDrop(prevScore - score);
            }
            
            // When a mate is found, DON'T stop early — continue searching
            // with normal time management to find the SHORTEST mate.
            // Only exception: mate in 1 at sufficient depth with stable best move.
            if (isMainThread && (score >= MATE_IN_MAX || score <= -MATE_IN_MAX)) {
                int mateDistance = MATE_SCORE - (score > 0 ? score : -score);
                int mateMoves = std::max(1, (mateDistance + 1) / 2);
                bool stableBest = (best == prevBest && stabilityCount >= 3);
                
                // Only stop for mate in 1 (nothing shorter exists)
                if (mateMoves <= 1 && d >= 6 && stableBest) {
                    stopSearching = true;
                }
                // For all other mates: extend time to search for shorter mate.
                // Push optimum time toward maximum so we use the full allocation.
                else if (d >= 4) {
                    TimePoint maxTime = TimeMgr.maximum();
                    TimePoint curOpt = TimeMgr.optimum();
                    if (curOpt < maxTime) {
                        // Use at least 80% of maximum time when mate is found
                        TimePoint newOpt = std::max(curOpt, maxTime * 4 / 5);
                        TimeMgr.setOptimum(newOpt);
                    }
                }
            }
            
            break;
        }
        
        if (stopSearching) break;
        
        // Store completed depth for vote system
        if (thread) {
            thread->bestMove = best;
            thread->bestScore = prevScore;
            thread->completedDepth = d;
        }
        
        // Time management (main thread only)
        if (isMainThread) {
            TimePoint elapsed = TimeMgr.elapsed();
            TimePoint optimum = TimeMgr.optimum();
            TimePoint maximum = TimeMgr.maximum();
            
            // SAFETY: If we've exceeded maximum time, stop immediately
            if (timeMs > 0 && elapsed >= maximum) {
                break;
            }
            
            // Adjust for stability
            if (!obviousMove && totalIterations > 0) {
                double stability = static_cast<double>(stabilityCount) / totalIterations;
                TimeMgr.adjustForStability(stability);
            }
            
            // Never stop before minimum depth
            if (d < minimumDepth) {
                continue;
            }
            
            // Stop if optimum time reached
            if (timeMs > 0 && elapsed >= optimum && d < effectiveMaxDepth) {
                break;
            }
        }
        // Helper threads: no time management, just check Threads.stop
    }
    
    // Store final results for vote system
    if (thread) {
        if (!moveIsNone(best)) {
            thread->bestMove = best;
            thread->bestScore = prevScore;
        }
    }
    
    // Fallback: if no move was found, pick first legal move
    if (moveIsNone(best)) {
        if (numLegalMoves > 0) {
            best = legalMoves[0];
            if (thread) thread->bestMove = best;
        }
    }
    
    return best;
}

// Get PV from TT
std::vector<Move> Position::getPV(int maxDepth) {
    std::vector<Move> pv;
    uint64_t current_hash = hash;
    
    // IMPORTANT: Limit PV to the search depth to avoid showing
    // more moves than we actually searched
    // Add small buffer for extensions but cap at reasonable limit
    int limit = std::min(maxDepth + 10, MAX_PLY);

    for (int d = 0; d < limit; d++) {
        bool found = false;
        TTEntry* tte = TT.probe(current_hash, found);
        if (!found) break;
        
        TTMoveUnpacked ttmu = unpackTTMove(tte->move16, white_to_move);
        Move m = makeMovePacked(ttmu.squares, ttmu.flags);
        if (moveIsNone(m)) break;

        Move legal[MAX_MOVES];
        int legalCount = generatePseudo(legal, false);
        bool isLegal = false;
        for (int i = 0; i < legalCount; ++i) {
            if ((legal[i].squares & 0x0FFF) == (m.squares & 0x0FFF)) {
                m = legal[i]; // use generated move with correct flags
                isLegal = true;
                break;
            }
        }
        if (!isLegal) break;
        
        // Additional check: verify move doesn't leave us in check
        makeMove(m);
        bool weAreInCheck = inCheck(!white_to_move);
        if (weAreInCheck) {
            undoMove(m);
            break;
        }

        pv.push_back(m);
        current_hash = hash;
        
        // Also undo to restore position at the end
    }

    // Undo all moves in reverse order
    for (int i = (int)pv.size() - 1; i >= 0; --i) undoMove(pv[i]);
    return pv;
}

// PV to string
std::string Position::pvToString(const std::vector<Move>& pv) {
    std::ostringstream ss;
    for (auto& m : pv) ss << moveToUCI(m) << " ";
    return ss.str();
}
