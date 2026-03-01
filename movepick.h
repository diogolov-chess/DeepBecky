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

#ifndef DEEPBECKY_MOVEPICK_H
#define DEEPBECKY_MOVEPICK_H

// ============================================================
// MovePicker - Move Ordering for Search
// 
// Features:
// - Staged move generation (TT -> Good captures -> Killers -> Quiets -> Bad captures)
// - MVV-LVA scoring for captures
// - History heuristic for quiet moves
// - Killer move handling
// ============================================================

#include "position.h"

// ========================= MovePicker Class =========================
struct MovePicker {
    Position& pos;
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
    
    enum class Stage {
        HASH_MOVE, GOOD_CAPTURES, KILLER1, KILLER2, QUIETS, BAD_CAPTURES, DONE
    } stage = Stage::HASH_MOVE;
    
    // Constructor - scores and categorizes moves
    MovePicker(Position& position, Move* moves, int count, const Move& tt, int p);
    
    // Get next move in order
    Move next();
    
private:
    // Select best move from a list using partial selection sort
    Move selectBest(Move* list, int* scores, int count, int& idx);
    
    // Try to use a killer move
    bool useKiller(const Move& k, Move& out);
};

#endif // DEEPBECKY_MOVEPICK_H

