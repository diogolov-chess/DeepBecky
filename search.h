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

// search.h - Alpha-Beta Search
#ifndef DEEPBECKY_SEARCH_H
#define DEEPBECKY_SEARCH_H

#include "types.h"

// Forward declaration
class Position;

// Search is implemented as methods of Position
// This header exists for organization

// Search constants
namespace Search {

// LMR reduction table
extern int Reductions[64][64];

// Initialize search tables
void init();

// Reduction function
int reduction(bool improving, int depth, int moveCount);

// Futility margin
int futilityMargin(int depth, bool improving);

// Move count pruning threshold 
int futilityMoveCount(bool improving, int depth);

// Draw score with contempt
int drawScore(uint64_t nodes, int contempt);

} // namespace Search

#endif // DEEPBECKY_SEARCH_H
