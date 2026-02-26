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

// uci.h - UCI Protocol Header
#ifndef UCI_H
#define UCI_H

#include "position.h"
#include <string>
#include <sstream>

namespace UCI {

// Loop principal do UCI
void loop(Position& engine);

// Comandos UCI
void cmdUci();
void cmdIsReady();
void cmdSetOption(Position& engine, std::istringstream& is);
void cmdNewGame(Position& engine);
void cmdPosition(Position& engine, std::istringstream& is);
void cmdGo(Position& engine, std::istringstream& is);
void cmdPerft(Position& engine, std::istringstream& is);

// Utilities
std::string toLower(const std::string& str);

} // namespace UCI

// Alias for compatibility
using DeepBeckyEngine = Position;

#endif // UCI_H
