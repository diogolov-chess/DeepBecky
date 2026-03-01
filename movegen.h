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

// Move Generation
#ifndef DEEPBECKY_MOVEGEN_H
#define DEEPBECKY_MOVEGEN_H

#include "types.h"
#include "bitboard.h"

// Forward declaration
class Position;

// Move generation is implemented as Position methods
// This header exists for organization and future extensions

// Generation types
enum GenType {
    GEN_ALL,       // All moves
    GEN_CAPTURES,  // Captures only
    GEN_QUIETS,    // Non-captures only
    GEN_EVASIONS   // Check evasions
};

// Perft for debugging
uint64_t perft(Position& pos, int depth);

#endif // DEEPBECKY_MOVEGEN_H
