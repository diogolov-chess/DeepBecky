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

// movepick.cpp - Move Ordering
#include "movepick.h"
#include <algorithm>

// ========================= MovePicker Implementation =========================

MovePicker::MovePicker(Position& position, Move* moves, int count, const Move& tt, int p)
    : pos(position), ttMove(tt), ply(p) {
    
    int us = pos.white_to_move ? WHITE : BLACK;
    
    // Check if TT move is in the list
    if (!moveIsNone(ttMove)) {
        for (int i = 0; i < count; ++i) {
            if (moves[i] == ttMove) { 
                ttAvailable = true; 
                break; 
            }
        }
    }

    // Categorize and score all moves
    for (int i = 0; i < count; ++i) {
        Move m = moves[i];
        
        // Skip TT move (will be returned first)
        if (ttAvailable && m == ttMove) continue;

        if (moveIsCapture(m)) {
            int from_sq = moveFrom(m);
            int to_sq = moveTo(m);
            int mover = pos.piece_board[from_sq];
            int captured = moveIsEnPassant(m) 
                ? (us == WHITE ? BPAWN : WPAWN) 
                : pos.piece_board[to_sq];
            int promotion = movePromotion(m);
            if (promotion) mover = promotion;
            
            // MVV-LVA score: prioritize capturing valuable pieces with cheap pieces
            int score = 10 * PIECE_VALUE[captured] - PIECE_VALUE[mover];
            
            // Use fast heuristic: if MVV-LVA score is positive, it's likely good
            bool isLikelyGood = (score >= 0) || (PIECE_VALUE[captured] >= PIECE_VALUE[mover]);
            
            if (isLikelyGood) {
                goodCaptures[goodCount] = m;
                goodScores[goodCount++] = score + (promotion ? PIECE_VALUE[promotion] : 0);
            } else {
                // Bad captures - tried last
                badCaptures[badCount] = m;
                badScores[badCount++] = score;
            }
        } else {
            // Quiet moves - scored by history heuristic
            quiets[quietCount] = m;
            quietScores[quietCount++] = history_heur[us][moveFrom(m)][moveTo(m)];
        }
    }

    // Get killer moves for this ply
    killerCand[0] = killers.killer[0][ply];
    killerCand[1] = killers.killer[1][ply];
}

Move MovePicker::selectBest(Move* list, int* scores, int count, int& idx) {
    while (idx < count) {
        // Find best remaining move
        int best = idx;
        for (int j = idx + 1; j < count; ++j) {
            if (scores[j] > scores[best]) best = j;
        }
        
        // Swap to current position
        if (best != idx) {
            std::swap(list[idx], list[best]);
            std::swap(scores[idx], scores[best]);
        }
        
        Move m = list[idx++];
        
        // Skip if this is the TT move (already returned)
        if (ttAvailable && m == ttMove) continue;
        
        return m;
    }
    return MOVE_NONE;
}

bool MovePicker::useKiller(const Move& k, Move& out) {
    // Killer must exist, not be a capture, and not be TT move
    if (moveIsNone(k) || moveIsCapture(k) || (ttAvailable && k == ttMove))
        return false;
    
    // Find killer in quiet moves
    for (int i = quietIndex; i < quietCount; ++i) {
        if (quiets[i] == k) {
            // Move to front of remaining quiets
            std::swap(quiets[i], quiets[quietIndex]);
            std::swap(quietScores[i], quietScores[quietIndex]);
            out = quiets[quietIndex++];
            return true;
        }
    }
    return false;
}

Move MovePicker::next() {
    while (true) {
        switch (stage) {
            case Stage::HASH_MOVE:
                stage = Stage::GOOD_CAPTURES;
                if (ttAvailable) {
                    ttAvailable = false;  // Only return once
                    return ttMove;
                }
                break;
                
            case Stage::GOOD_CAPTURES: {
                Move m = selectBest(goodCaptures, goodScores, goodCount, goodIndex);
                if (!moveIsNone(m)) return m;
                stage = Stage::KILLER1;
                break;
            }
                
            case Stage::KILLER1: {
                Move candidate;
                if (useKiller(killerCand[0], candidate)) return candidate;
                stage = Stage::KILLER2;
                break;
            }
                
            case Stage::KILLER2: {
                Move candidate;
                if (useKiller(killerCand[1], candidate)) return candidate;
                stage = Stage::QUIETS;
                break;
            }
                
            case Stage::QUIETS: {
                Move m = selectBest(quiets, quietScores, quietCount, quietIndex);
                if (!moveIsNone(m)) return m;
                stage = Stage::BAD_CAPTURES;
                break;
            }
                
            case Stage::BAD_CAPTURES: {
                Move m = selectBest(badCaptures, badScores, badCount, badIndex);
                if (!moveIsNone(m)) return m;
                stage = Stage::DONE;
                break;
            }
                
            case Stage::DONE:
            default:
                return MOVE_NONE;
        }
    }
}
