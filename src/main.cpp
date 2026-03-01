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

// Entry Point
#include "magic.h"
#include "bitboard.h"
#include "evaluate.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"

#include <iostream>

int main() {
    // Disable buffering for better compatibility with UCI GUIs
    std::ios_base::sync_with_stdio(false);
    
    // Initialize tables
    Magic::init();
    initBitboards();
    Eval::init();
    Search::init();
    
    // Initialize thread pool (1 thread by default)
    Threads.set(1);
    
    // Create engine and start UCI loop
    Position engine;
    UCI::loop(engine);
    
    // Clean up threads
    Threads.set(0);
    
    return 0;
}
