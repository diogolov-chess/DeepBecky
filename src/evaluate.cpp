#include "evaluate.h"

#include "nnue.h"
#include "position.h"

namespace Eval {

void init() {}

int evaluate(Position& pos) {
    return NNUE::evaluate(pos);
}

} // namespace Eval

int Position::evaluate() {
    return NNUE::evaluate(*this);
}