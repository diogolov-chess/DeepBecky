#ifndef DEEPBECKY_MOVEGEN_H
#define DEEPBECKY_MOVEGEN_H

#include "types.h"
#include "bitboard.h"

// Forward declaration
class Position;

// Move generation algorithms are implemented as Position member functions.
// Move generator verification and performance benchmark
uint64_t perft(Position& pos, int depth);

#endif // DEEPBECKY_MOVEGEN_H
