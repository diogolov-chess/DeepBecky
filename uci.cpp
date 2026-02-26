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

// uci.cpp - UCI Protocol Implementation 
#include "uci.h"
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
    std::cout << "id name Deep Becky 1.0" << std::endl;
    std::cout << "id author Diogo de Oliveira Almeida" << std::endl;
    std::cout << "option name Hash type spin default 64 min 1 max 4096" << std::endl;
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
    
}

void cmdNewGame(Position& engine) {
    TT.clear();
    TT.newSearch();
    engine.setStartPos();
    
    // Clear history and killers using globals
    std::memset(history_heur, 0, sizeof(history_heur));
    killers.clear();
    engine.counterMoves = {};
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
            std::cout << "info string Missing FEN in position command" << std::endl;
            return;
        }
        
        std::string fen;
        fen.reserve(fenParts.size() * 8);
        for (size_t i = 0; i < fenParts.size(); ++i) {
            if (i) fen.push_back(' ');
            fen += fenParts[i];
        }
        
        if (!engine.setFEN(fen)) {
            std::cout << "info string Invalid FEN: " << fen << std::endl;
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
    SearchLimits limits;
    limits.startTime = now();
    
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
        else if (key == "perft") {
            int perftDepth = 1;
            is >> perftDepth;
            std::istringstream perftIs(std::to_string(perftDepth));
            cmdPerft(engine, perftIs);
            return;
        }
    }
    
    // Calculate game ply (rough estimate from fullmove counter)
    int gamePly = (engine.fullmove - 1) * 2 + (engine.white_to_move ? 0 : 1);
    
    // Initialize time management
    TimeMgr.init(limits, engine.white_to_move, gamePly);
    
    // Set search parameters
    int maxDepth = (limits.depth > 0 ? limits.depth : MAX_PLY);
    int searchTime = static_cast<int>(TimeMgr.maximum());
    
    Move bestMove = engine.search(maxDepth, searchTime);
    
    if (moveIsNone(bestMove)) {
        std::cout << "bestmove 0000" << std::endl;
    } else {
        std::cout << "bestmove " << engine.moveToUCI(bestMove) << std::endl;
    }
    std::cout.flush();
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
            cmdSetOption(engine, is);
        } else if (cmdLower == "ucinewgame") {
            cmdNewGame(engine);
        } else if (cmdLower == "position") {
            cmdPosition(engine, is);
        } else if (cmdLower == "go") {
            cmdGo(engine, is);
        } else if (cmdLower == "perft") {
            cmdPerft(engine, is);
        } else if (cmdLower == "quit") {
            break;
        } else if (cmdLower == "d" || cmdLower == "display") {
            // Debug command to show the board
            std::cout << "info string Display board not implemented yet" << std::endl;
        }
    }
}

} // namespace UCI
