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

// UCI Protocol Implementation (Lazy SMP)
#include "uci.h"
#include "thread.h"
#include "tt.h"
#include "search.h"
#include "movegen.h"
#include "timeman.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <functional>

namespace UCI {

// =============================================================================
// Utilities
// =============================================================================
std::string toLower(const std::string& str) {
    std::string result = str;
    for (char& c : result) {
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    }
    return result;
}

// =============================================================================
// UCI Commands
// =============================================================================
void cmdUci() {
    std::cout << "id name Deep Becky 2.0" << std::endl;
    std::cout << "id author Diogo de Oliveira Almeida" << std::endl;
    std::cout << "option name Hash type spin default 64 min 1 max 4096" << std::endl;
    std::cout << "option name Threads type spin default 1 min 1 max 256" << std::endl;
    std::cout << "option name Ponder type check default true" << std::endl;
    std::cout << "uciok" << std::endl;
    std::cout.flush();
}

void cmdIsReady() {
    std::cout << "readyok" << std::endl;
    std::cout.flush();
}

void cmdSetOption(Position& engine, std::istringstream& is) {
    std::string token;
    is >> token; // "name"
    
    std::string optName;
    while (is >> token && toLower(token) != "value") {
        if (!optName.empty()) optName += " ";
        optName += token;
    }
    
    std::string optValue;
    while (is >> token) {
        if (!optValue.empty()) optValue += " ";
        optValue += token;
    }
    
    std::string optNameLower = toLower(optName);
    
    if (optNameLower == "hash") {
        int mb = std::stoi(optValue);
        mb = std::max(1, std::min(mb, 4096));
        TT.resize(static_cast<size_t>(mb));
    }
    else if (optNameLower == "threads") {
        int n = std::stoi(optValue);
        n = std::max(1, std::min(n, 256));
        Threads.set(static_cast<size_t>(n));
    }
    else if (optNameLower == "ponder") {
        Threads.ponderEnabled = (toLower(optValue) == "true");
    }
}

void cmdNewGame(Position& engine) {
    Threads.waitForSearchFinished();
    TT.clear();
    TT.newSearch();
    engine.setStartPos();
    Threads.clear();
}

void cmdPosition(Position& engine, std::istringstream& is) {
    std::string token;
    is >> token;
    std::string tokenLower = toLower(token);
    
    if (tokenLower == "startpos") {
        engine.setStartPos();
        is >> token; // Consume "moves" if present
    } else if (tokenLower == "fen") {
        std::vector<std::string> fenParts;
        std::string fenToken;
        bool sawMoves = false;
        
        while (is >> fenToken) {
            if (toLower(fenToken) == "moves") {
                sawMoves = true;
                break;
            }
            fenParts.push_back(fenToken);
        }
        
        if (fenParts.empty()) {
            std::cout << "info string missing FEN in position command" << std::endl;
            return;
        }
        
        std::string fen;
        fen.reserve(fenParts.size() * 8);
        for (size_t i = 0; i < fenParts.size(); ++i) {
            if (i) fen.push_back(' ');
            fen += fenParts[i];
        }
        
        if (!engine.setFEN(fen)) {
            std::cout << "info string invalid FEN: " << fen << std::endl;
            return;
        }
        
        if (!sawMoves) return;
    }
    
    // Process moves
    std::string moveStr;
    while (is >> moveStr) {
        Move move = engine.uciToMove(moveStr);
        if (moveIsNone(move) && moveStr != "0000") {
            std::cout << "info string illegal move from GUI: " << moveStr << std::endl;
            break;
        }
        engine.makeMove(move);
    }
}

void cmdGo(Position& engine, std::istringstream& is) {
    // Wait for any previous search to finish
    Threads.waitForSearchFinished();

    SearchLimits limits;
    limits.startTime = now();
    bool ponderMode = false;
    
    std::string token;
    
    while (is >> token) {
        std::string key = toLower(token);
        if (key == "wtime") is >> limits.time[0];
        else if (key == "btime") is >> limits.time[1];
        else if (key == "winc") is >> limits.inc[0];
        else if (key == "binc") is >> limits.inc[1];
        else if (key == "movestogo") is >> limits.movestogo;
        else if (key == "movetime") is >> limits.movetime;
        else if (key == "depth") is >> limits.depth;
        else if (key == "infinite") limits.infinite = true;
        else if (key == "ponder") ponderMode = true;
        else if (key == "perft") {
            int perftDepth = 1;
            is >> perftDepth;
            std::istringstream perftIs(std::to_string(perftDepth));
            cmdPerft(engine, perftIs);
            return;
        }
    }
    
    // Calculate game ply
    int gamePly = (engine.fullmove - 1) * 2 + (engine.white_to_move ? 0 : 1);
    
    // Initialize time management
    TimeMgr.init(limits, engine.white_to_move, gamePly);
    
    // Set search parameters
    int maxDepth = (limits.depth > 0 ? limits.depth : MAX_PLY);
    int searchTime = static_cast<int>(TimeMgr.maximum());
    
    // In ponder mode, search without time limit until ponderhit or stop
    if (ponderMode) {
        searchTime = 0;  // No time limit during pondering
        maxDepth = MAX_PLY;
    }
    
    // Start async search via ThreadPool
    // The main search thread will print bestmove when done
    Threads.startThinking(engine, maxDepth, searchTime, ponderMode);
}

void cmdStop() {
    Threads.ponder.store(false, std::memory_order_relaxed);
    Threads.stop.store(true, std::memory_order_relaxed);
    // Wake main thread if it's waiting for ponderhit
    if (Threads.main()) {
        std::lock_guard<std::mutex> lk(Threads.main()->mtx);
        Threads.main()->cv.notify_one();
    }
    Threads.waitForSearchFinished();
}

void cmdPonderHit(Position& engine) {
    // Opponent played the expected move - switch from ponder to normal search
    Threads.ponder.store(false, std::memory_order_relaxed);

    // Reinitialize time management now that real clock starts
    // The search will pick up time checking on the next node
    // We need to set a proper time limit for the main thread
    if (Threads.main()) {
        // Set the start time to NOW (pondering time doesn't count)
        auto now_time = std::chrono::high_resolution_clock::now();
        Threads.main()->pos.start_time = now_time;

        // Calculate proper time allocation from the limits stored earlier
        int timeMs = static_cast<int>(TimeMgr.optimum());
        Threads.main()->pos.time_limit_ms = timeMs;
        Threads.searchTimeMs = timeMs;

        // Wake main thread if it's waiting for ponderhit after search completed
        std::lock_guard<std::mutex> lk(Threads.main()->mtx);
        Threads.main()->cv.notify_one();
    }
}

void cmdPerft(Position& engine, std::istringstream& is) {
    int perftDepth = 1;
    is >> perftDepth;
    if (perftDepth < 1) perftDepth = 1;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Divide: show nodes per move
    Move moves[MAX_MOVES];
    int count = engine.generateLegal(moves);
    uint64_t totalNodes = 0;
    
    for (int i = 0; i < count; ++i) {
        engine.makeMove(moves[i]);
        uint64_t n = ::perft(engine, perftDepth - 1);
        engine.undoMove(moves[i]);
        std::cout << engine.moveToUCI(moves[i]) << ": " << n << std::endl;
        totalNodes += n;
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    if (ms == 0) ms = 1;
    uint64_t nps = (totalNodes * 1000ULL) / static_cast<uint64_t>(ms);
    
    std::cout << std::endl;
    std::cout << "Nodes searched: " << totalNodes << std::endl;
    std::cout << "Time: " << ms << " ms" << std::endl;
    std::cout << "NPS: " << nps << std::endl;
}

// =============================================================================
// Main Loop
// =============================================================================
void loop(Position& engine) {
    std::string line;
    
    // Initialize TT with default size
    TT.resize(64);
    
    engine.setStartPos();
    
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        
        std::istringstream is(line);
        std::string cmd;
        is >> cmd;
        std::string cmdLower = toLower(cmd);
        
        if (cmdLower == "uci") {
            cmdUci();
        } else if (cmdLower == "isready") {
            cmdIsReady();
        } else if (cmdLower == "setoption") {
            Threads.waitForSearchFinished();
            cmdSetOption(engine, is);
        } else if (cmdLower == "ucinewgame") {
            cmdNewGame(engine);
        } else if (cmdLower == "position") {
            Threads.waitForSearchFinished();
            cmdPosition(engine, is);
        } else if (cmdLower == "go") {
            cmdGo(engine, is);
        } else if (cmdLower == "stop") {
            cmdStop();
        } else if (cmdLower == "ponderhit") {
            cmdPonderHit(engine);
        } else if (cmdLower == "perft") {
            Threads.waitForSearchFinished();
            cmdPerft(engine, is);
        } else if (cmdLower == "quit") {
            Threads.stop.store(true, std::memory_order_relaxed);
            Threads.waitForSearchFinished();
            break;
        } else if (cmdLower == "d" || cmdLower == "display") {
            std::cout << "info string Display board not implemented yet" << std::endl;
        }
    }
}

} // namespace UCI
