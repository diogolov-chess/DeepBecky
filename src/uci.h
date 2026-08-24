#ifndef DEEPBECKY_UCI_H
#define DEEPBECKY_UCI_H

#include "position.h"
#include <string>
#include <sstream>

namespace UCI {

// Main UCI interaction loop
void loop(Position& engine);

// UCI Protocol Command Handlers
void cmdUci();
void cmdIsReady();
void cmdSetOption(Position& engine, std::istringstream& is);
void cmdNewGame(Position& engine);
void cmdPosition(Position& engine, std::istringstream& is);
void cmdGo(Position& engine, std::istringstream& is);
void cmdStop();
void cmdPonderHit(Position& engine);
void cmdPerft(Position& engine, std::istringstream& is);

// Utility string functions
std::string toLower(const std::string& str);

} // namespace UCI

// Compatibility alias
using DeepBeckyEngine = Position;

#endif // DEEPBECKY_UCI_H
