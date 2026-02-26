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

// evaluate.h - Evaluation
#ifndef DEEPBECKY_EVALUATE_H
#define DEEPBECKY_EVALUATE_H

#include "types.h"

// Forward declaration
class Position;

// Evaluation is implemented as a method of Position::evaluate()
// This header exists for organization and possible future extensions

// Evaluation constants
namespace Eval {

// Phase values for tapered eval
constexpr int PHASE_KNIGHT = 1;
constexpr int PHASE_BISHOP = 1;
constexpr int PHASE_ROOK = 2;
constexpr int PHASE_QUEEN = 4;
constexpr int PHASE_TOTAL = 24;

// Bonuses/penalties
constexpr int BISHOP_PAIR_BONUS = 25;
constexpr int DOUBLED_PAWN_PENALTY_MG = 12;
constexpr int DOUBLED_PAWN_PENALTY_EG = 8;
constexpr int ISOLATED_PAWN_PENALTY_MG = 15;
constexpr int ISOLATED_PAWN_PENALTY_EG = 10;

// Passed pawn bonuses by rank
constexpr int PASSED_PAWN_MG[8] = {0, 5, 10, 20, 35, 60, 90, 0};
constexpr int PASSED_PAWN_EG[8] = {0, 10, 20, 40, 70, 110, 180, 0};

// Lazy eval threshold
constexpr int LAZY_THRESHOLD = 2000;

} // namespace Eval

#endif // DEEPBECKY_EVALUATE_H
