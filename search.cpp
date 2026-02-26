/*
 * This file is part of Deep Becky 1.2 - A UCI Chess Engine written by AI
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

// search.cpp - Search Implementation
#include "search.h"
#include "movepick.h"
#include "position.h"
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
            // Stockfish-style formula: log(d) * log(m) / 2
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
    if (ply >= MAX_PLY - 1) return evaluate();
    
    nodes++;
    
    // Periodic time check (0x3FFF = every 16383 nodes)
    if ((nodes & 0x3FFF) == 0 && timeUp()) {
        stopSearching = true;
        return alpha;
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
    if (ttHit && tte->depth >= 0) {
        int ttScore = tte->score;
        TTFlag ttFlag = tte->flag();
        if (ttFlag == TT_EXACT) return ttScore;
        if (ttFlag == TT_BETA && ttScore >= beta) return ttScore;
        if (ttFlag == TT_ALPHA && ttScore <= alpha) return ttScore;
    }

    int stand = evaluate();
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
        // Check if the move is illegal (left our king in check)
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

// Principal Variation Search
int Position::pvs(int depth, int alpha, int beta, int ply) {
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
    
    if (ply >= MAX_PLY - 1) return evaluate();
    
    // Time check (not too frequently)
    if (ply > 0 && (nodes & 0x1FFF) == 0 && timeUp()) {
        stopSearching = true;
        return alpha;
    }
    
    bool isInCheck = inCheck(white_to_move);
    
    // Check extension
    if (isInCheck) depth++;
    
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
    
    if (ttHit) {
        ttMove = makeMovePacked(tte->moveData, tte->moveFlags);
        ttDepth = tte->depth;
        ttScore = tte->score;
        // Adjust mate scores from TT
        if (ttScore >= MATE_IN_MAX) ttScore -= ply;
        if (ttScore <= -MATE_IN_MAX) ttScore += ply;
        ttFlag = tte->flag();
        
        // TT cutoffs - NOT at root, NOT in PV nodes
        if (!rootNode && !pvNode && ttDepth >= depth && halfmove < 90) {
            if (ttFlag == TT_EXACT) return ttScore;
            if (ttFlag == TT_ALPHA && ttScore <= alpha) return ttScore;
            if (ttFlag == TT_BETA && ttScore >= beta) return ttScore;
        }
    }
    
    // ============ Static Evaluation ============
    int staticEval;
    if (isInCheck) {
        staticEval = -INF_SCORE;  // No static eval when in check
    } else if (ttHit && ttFlag != TT_ALPHA) {
        // Use TT score as better estimate if available
        staticEval = ttScore;
    } else {
        staticEval = evaluate();
    }
    
    // "Improving" heuristic - using alpha like original (simplified)
    bool improving = (ply >= 2 && staticEval > alpha);
    
    // ============ Razoring ============
    if (!pvNode && !isInCheck && depth <= 1 && staticEval + 300 <= alpha) {
        return qsearch(alpha, beta, ply);
    }
    
    // ============ Futility Pruning / Static Null Move (reverse futility) ============
    if (!pvNode && !isInCheck && depth < 5 && staticEval - Search::futilityMargin(depth, improving) >= beta) {
        return staticEval;
    }
    
    // ============ Null Move Pruning ============
    if (!pvNode && !isInCheck && depth >= 3 && ply > 0 && staticEval >= beta) {
        if (hasNonPawnMaterial(white_to_move)) {
            makeNullMove();
            // Conservative: R = 2 + depth/6
            int R = 2 + (depth / 6);
            
            int nmScore = -pvs(depth - 1 - R, -beta, -beta + 1, ply + 1);
            undoNullMove();
            
            if (nmScore >= beta) {
                // Don't return unproven mates
                if (nmScore >= MATE_IN_MAX) nmScore = beta;
                return nmScore;
            }
        }
    }
    
    // ============ Internal Iterative Deepening ============
    // If no TT move at high depth, do a shallow search first
    if (depth >= 6 && moveIsNone(ttMove) && !isInCheck) {
        pvs(depth - 4, alpha, beta, ply);
        // Re-probe TT
        ttHit = false;
        tte = TT.probe(hash, ttHit);
        if (ttHit) {
            ttMove = makeMovePacked(tte->moveData, tte->moveFlags);
        }
    }
    
    int best = -INF_SCORE;
    Move bestMove = MOVE_NONE;
    int origAlpha = alpha;
    int moveCount = 0;
    bool hasLegalMove = false;
    
    // Move generation
    Move mv[MAX_MOVES];
    int generated = generatePseudo(mv, false);
    MovePicker picker(*this, mv, generated, ttMove, ply);
    
    // ============ Main Move Loop ============
    for (Move m = picker.next(); !moveIsNone(m); m = picker.next()) {
        
        // Make move and check legality
        makeMove(m);
        
        // Check if WE are in check (illegal move) - this is after side flipped
        bool weAreInCheck = inCheck(!white_to_move);
        if (weAreInCheck) {
            undoMove(m);
            continue;
        }
        
        bool givesCheck = inCheck(white_to_move);
        
        hasLegalMove = true;
        moveCount++;
        
        bool isCapture = moveIsCapture(m);
        bool isPromotion = movePromotion(m) != 0;
        
        int newDepth = depth - 1;
        
        // ============ Capture Extension for "sacrifices" ============
        // If we're capturing a piece with a MORE valuable piece, extend search
        // This helps avoid tactical mistakes like Rxd6+ in the game analysis
        if (isCapture && !isPromotion) {
            int from_sq = moveFrom(m);
            int to_sq = moveTo(m);
            int movingPiece = piece_board[from_sq];
            int capturedPiece = piece_board[to_sq];
            // If moving piece is significantly more valuable than captured (sacrifice)
            // Tower (500) capturing Knight (320) = sacrifice of 180
            if (capturedPiece != EMPTY && PIECE_VALUE[movingPiece] - PIECE_VALUE[capturedPiece] > 150) {
                // Extend to see consequences of sacrifice
                newDepth++;
            }
        }
        
        // ============ Pruning at low depths  ============
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
            // More conservative: use -100*depth instead of -50*depth
            // This prevents pruning potential sacrifices too early
            if (depth <= 3 && isCapture && !isPromotion && !givesCheck) {
                int seeValue = see(m);
                // Only prune clearly bad captures (not sacrifices that give check)
                if (seeValue < -100 * depth) {
                    undoMove(m);
                    continue;
                }
            }
        }
        
        int sc;
        
        if (moveCount == 1) {
            // First move - full window search
            sc = -pvs(newDepth, -beta, -alpha, ply + 1);
        } else {
            // ============ Late Move Reductions (LMR) ============
            int R = 0;
            if (depth >= 3 && moveCount > 2 && !isCapture && !isPromotion && !isInCheck) {
                // Reduction table
                R = Search::Reductions[std::min(depth, 63)][std::min(moveCount, 63)];
                
                // Reduce more in non-PV nodes
                if (!pvNode) R++;
                
                // Reduce less when improving
                if (improving) R--;
                
                // Reduce less for killer moves
                if (m == killers.killer[0][ply] || m == killers.killer[1][ply]) R--;
                
                // Reduce less if giving check
                if (givesCheck) R--;
                
                // Don't reduce below 1
                R = std::max(0, R);
                
                // Don't reduce too much
                R = std::min(R, newDepth - 1);
            }
            
            // Reduced depth search (null window)
            if (R > 0) {
                sc = -pvs(newDepth - R, -alpha - 1, -alpha, ply + 1);
            } else {
                sc = alpha + 1;  // Force re-search
            }
            
            // Re-search at full depth if LMR failed high
            if (sc > alpha) {
                sc = -pvs(newDepth, -alpha - 1, -alpha, ply + 1);
                
                // Full window re-search for PV nodes
                if (sc > alpha && sc < beta) {
                    sc = -pvs(newDepth, -beta, -alpha, ply + 1);
                }
            }
        }
        
        undoMove(m);
        
        if (stopSearching) return alpha;
        
        // Update best
        if (sc > best) {
            best = sc;
            bestMove = m;
        }
        
        if (sc > alpha) {
            alpha = sc;
            
            // Update history for quiet moves that improve alpha
            if (!isCapture) {
                int side = white_to_move ? 0 : 1;
                int bonus = depth * depth;
                history_heur[side][moveFrom(m)][moveTo(m)] += bonus;
                
                // Aging: reduce history to avoid overflow
                if (history_heur[side][moveFrom(m)][moveTo(m)] > 8000) {
                    for (int f = 0; f < 64; ++f)
                        for (int t = 0; t < 64; ++t)
                            history_heur[side][f][t] /= 2;
                }
            }
            
            if (alpha >= beta) {
                // Beta cutoff - update killers
                if (!isCapture) {
                    if(killers.killer[0][ply] == MOVE_NONE || !(killers.killer[0][ply] == m)){
                        killers.killer[1][ply] = killers.killer[0][ply];
                        killers.killer[0][ply] = m;
                    }
                }
                break;
            }
        }
    }
    
    if (!hasLegalMove) {
        return isInCheck ? (-MATE_SCORE + ply) : 0; // Checkmate or Stalemate
    }
    
    // Store in TT
    if (!moveIsNone(bestMove)) {
        TTFlag flag = TT_EXACT;
        if (best <= origAlpha) flag = TT_ALPHA;
        else if (best >= beta) flag = TT_BETA;
        
        int storeScore = best;
        if (best >= MATE_IN_MAX) storeScore += ply;
        if (best <= -MATE_IN_MAX) storeScore -= ply;
        
        TT.store(hash, depth, storeScore, flag, bestMove.squares, bestMove.flags);
    }
    
    return best;
}

// Iterative Deepening Search
Move Position::search(int maxDepth, int timeMs) {
    nodes = 0;
    selDepth = 0;
    stopSearching = false;
    
    start_time = std::chrono::high_resolution_clock::now();
    time_limit_ms = timeMs;
    
    TT.newSearch();
    clearHeuristics();
    
    // Dynamic contempt based on material evaluation
    // When we're winning, we should STRONGLY avoid draws/repetitions
    rootSideIsWhite = white_to_move;
    
    int eval = evaluate();  // Get current evaluation
    int absEval = eval > 0 ? eval : -eval;
    
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
    
        
    // Generate legal moves to check for obvious situations
    Move legalMoves[MAX_MOVES];
    int numLegalMoves = generateLegal(legalMoves);
    
    // Check if we're in check
    bool rootInCheck = inCheck(white_to_move);
    
    // Check for recapture (opponent just captured something)
    bool isRecapture = false;
    if (!uci_history.empty()) {
        // Get last move from history
        std::string lastMove = uci_history.back();
        if (lastMove.length() >= 4) {
            int toFile = lastMove[2] - 'a';
            int toRank = lastMove[3] - '1';
            int captureSquare = toRank * 8 + toFile;
            
            // Check if any of our top moves capture on that square
            for (int i = 0; i < std::min(numLegalMoves, 5); i++) {
                if (moveTo(legalMoves[i]) == captureSquare) {
                    int targetPiece = piece_board[captureSquare];
                    // It's a recapture if there's an enemy piece there
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
        // Do a quick depth-1 search just to have a score to report
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
        
        return legalMoves[0];
    }
    
    // In check with only 2 options, or simple recapture - do shallow search
    bool obviousMove = TimeManagement::isObviousMove(numLegalMoves, rootInCheck, isRecapture);
    int effectiveMaxDepth = maxDepth;
    TimePoint effectiveTimeLimit = timeMs;
    
    if (obviousMove && timeMs > 0) {
        // Limit search for obvious moves - search to depth 6 max, use only 20% of normal time
        effectiveMaxDepth = std::min(maxDepth, 8);
        double factor = TimeManagement::legalMovesFactor(numLegalMoves, rootInCheck);
        effectiveTimeLimit = static_cast<TimePoint>(timeMs * factor);
        effectiveTimeLimit = std::max(effectiveTimeLimit, TimePoint(50));  // At least 50ms
        time_limit_ms = static_cast<int>(effectiveTimeLimit);
    }
    
    Move best = MOVE_NONE;
    Move prevBest = MOVE_NONE;
    int prevScore = 0;
    int stabilityCount = 0;  // Count iterations with same best move
    int totalIterations = 0;
    
    int minimumDepth = 4;
    
    for (int d = 1; d <= effectiveMaxDepth && !stopSearching; d++) {
        selDepth = 0;
        
        int alpha = -INF_SCORE, beta = INF_SCORE;
        
        // Aspiration window starting from depth 4
        if (d >= 4) {
            int window = 25;
            alpha = prevScore - window;
            beta = prevScore + window;
        }
        
        while (true) {
            int score = pvs(d, alpha, beta, 0);
            
            if (stopSearching) break;
            
            if (score <= alpha) {
                // Window failed low, widen to the left
                alpha = -INF_SCORE;
                continue;
            }
            if (score >= beta) {
                // Window failed high, widen to the right  
                beta = INF_SCORE;
                continue;
            }
            
            prevScore = score;
            
            // Get best move from TT
            bool ttFound = false;
            TTEntry* tte = TT.probe(hash, ttFound);
            if (ttFound) {
                Move ttBest = makeMovePacked(tte->moveData, tte->moveFlags);
                bool isLegalMove = false;
                for (int i = 0; i < numLegalMoves; i++) {
                    if (legalMoves[i] == ttBest) {
                        isLegalMove = true;
                        break;
                    }
                }
                if (isLegalMove) {
                    best = ttBest;
                }
            }
            
            // Print info
            auto now = std::chrono::high_resolution_clock::now();
            long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
            if (ms == 0) ms = 1;
            uint64_t nps = (nodes * 1000ULL) / static_cast<uint64_t>(ms);
            
            std::vector<Move> pvLine = getPV(d);
            std::string pvStr = pvToString(pvLine);
            
            std::cout << "info depth " << d 
                      << " seldepth " << selDepth;
            
            // Mate score formatting
            if (score >= MATE_IN_MAX || score <= -MATE_IN_MAX) {
                int mateDistance = MATE_SCORE - (score > 0 ? score : -score);
                // mateDistance is in plies, convert to moves (2 plies = 1 move)
                int mateMoves = std::max(1, (mateDistance + 1) / 2);
                if (score < 0) mateMoves = -mateMoves;
                std::cout << " score mate " << mateMoves;
            } else {
                std::cout << " score cp " << score;
            }
            
            std::cout << " time " << ms
                      << " nodes " << nodes
                      << " nps " << nps
                      << " hashfull " << TT.hashfull()
                      << " pv " << pvStr
                      << std::endl;
            std::cout.flush();
            
            // Track best move stability
            totalIterations++;
            if (best == prevBest) {
                stabilityCount++;
            } else {
                prevBest = best;
            }
            
            // Check for score drops (extend time if score dropped)
            if (d > 1 && prevScore - score > 30) {
                TimeMgr.adjustForScoreDrop(prevScore - score);
            }
            
            // Stop early if we found a forced mate, but only if:
            // 1. We've searched deep enough (at least depth 6), OR
            // 2. It's a very short mate (mate in 3 or less)
            // 3. AND the best move is stable (same for at least 2 iterations)
            if (score >= MATE_IN_MAX || score <= -MATE_IN_MAX) {
                int mateDistance = MATE_SCORE - (score > 0 ? score : -score);
                // CRITICAL: Minimum 1 move, consistent with display
                int mateMoves = std::max(1, (mateDistance + 1) / 2);
                
                // For mates, we need to be more careful:
                // - Mate in 1-3: can stop at depth 6+ if stable
                // - Mate in 4+: need to search to at least mate_distance plies
                bool stableBest = (best == prevBest && stabilityCount >= 2);
                
                if (mateMoves <= 3 && d >= 6 && stableBest) {
                    stopSearching = true;
                } else if (d >= mateMoves * 2 + 2 && stableBest) {
                    // Searched deep enough to confirm the mate (mateMoves*2 = plies)
                    stopSearching = true;
                }
                // Otherwise, continue searching to find the shortest mate
            }
            
            break;
        }
        
        if (stopSearching) break;
        
        // Time management: use optimum time to decide when to stop
        TimePoint elapsed = TimeMgr.elapsed();
        TimePoint optimum = TimeMgr.optimum();
        TimePoint maximum = TimeMgr.maximum();
        
        // SAFETY: If we've exceeded maximum time, stop immediately
        // This prevents losing on time
        if (timeMs > 0 && elapsed >= maximum) {
            break;
        }
        
        // Adjust for stability: if best move keeps changing, extend time
        // But only if not an obvious move situation
        if (!obviousMove && totalIterations > 0) {
            double stability = static_cast<double>(stabilityCount) / totalIterations;
            TimeMgr.adjustForStability(stability);
        }
        
        // CRITICAL: Never stop before minimum depth!
        // This was causing blunders like Rb2?? when the engine had mate
        if (d < minimumDepth) {
            continue; // Must complete at least minimumDepth iterations
        }
        
        // Stop if we've used the optimum time (unless depth-only search)
        if (timeMs > 0 && elapsed >= optimum && d < effectiveMaxDepth) {
            break;
        }
        
        // REMOVED: obviousMove early stop - was causing shallow searches
        // The engine should always search deeply when it has time
    }
    
    // Fallback: if no move found, take first legal move
    if (moveIsNone(best)) {
        if (numLegalMoves > 0) {
            best = legalMoves[0];
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
        
        Move m = makeMovePacked(tte->moveData, tte->moveFlags);
        if (moveIsNone(m)) break;

        Move legal[MAX_MOVES];
        int legalCount = generatePseudo(legal, false);
        bool isLegal = false;
        for (int i = 0; i < legalCount; ++i) {
            if (legal[i] == m) {
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
