#ifndef DEEPBECKY_EVALUATE_H
#define DEEPBECKY_EVALUATE_H

#include "types.h"

class Position;

namespace Eval {
void init();
int evaluate(Position& pos);
int evaluateKXK(const Position& pos);

} // namespace Eval

#endif // DEEPBECKY_EVALUATE_H
