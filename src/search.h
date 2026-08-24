#ifndef DEEPBECKY_SEARCH_H
#define DEEPBECKY_SEARCH_H

#include "types.h"

// Forward declaration
class Position;

// SearchStack tracks per-ply information along the search path
struct SearchStack {
  Move pv[MAX_PLY + 1];
  int pvLength;
  int staticEval;
  int statScore;
  Move currentMove;
  Move excludedMove;
  int movedPiece; // Piece that executed ss->currentMove
  int16_t (
      *continuationHistory)[64]; // Pointer to history slice:
                                 // contHistory[...][movedPiece] -> [piece][to]
  int ply;
  int doubleExtensions;
};

// Search options and tunable parameters
namespace Search {

extern bool UseSingular;
extern bool UseSEEPruning;
extern bool UseLMR;
extern bool UseHistory;
extern bool UseNMP;

namespace Tune {
// ==========================================
// Batch 1 - Basic Pruning & Extension Margins
// ==========================================
extern int FutilityMarginBase;
extern int FutilityMarginMult;
extern int FutilityMoveCountBase;
extern int RazorMarginBase;
extern int RazorMarginMult;
extern int NmpBase;
extern int NmpDivisor;
extern int ProbCutBetaBase;
extern int ProbCutBetaImp;
extern int SingularMarginBase;
extern int SingularMarginPvMult;
extern int SingularMarginDiv;
extern int SeeQsearchCapture;
extern int SeePruningDepthBase;
extern int SeePruningQuietBase;
extern int HistoryLmrDivisor;

// ==========================================
// Batch 2 - LMR, History & Aspiration
// ==========================================
extern int LmrBaseBase;
extern int LmrMultBase;
extern int HistoryBonusMax;
extern int HistoryDivisor;
extern int CaptureHistoryDivisor;
extern int AspWindowBase;
extern int AspWindowThreadMult;
extern int FutilityChildBase;
extern int FutilityChildMult;
extern int HistoryPruningMargin;
extern int NmpEvalMarginDepth;
extern int NmpEvalMarginBase;
extern int DrawRejectMargin;
extern int RfpDepthLimit;
extern int NmpDepthLimit;
extern int IirDepthLimit;
extern int ProbCutDepthLimit;
extern int CaptureLmrBadBase;
extern int CaptureLmrGoodBase;
extern int CorHistDivisor;
extern int CorHistWeightBase;
extern int CorHistWeightMax;
extern int CorHistBonusMax;
extern int SingularDepthLimit;
extern int DoubleExtMargin;
extern int TripleExtMargin;
extern int FutilityDepthLimit;
} // namespace Tune

// LMR reduction table
extern int Reductions[64][64];

// Initialize search tables and reduction arrays
void init();

// Reduction calculation based on depth, move count, and improving flag
int reduction(bool improving, int depth, int moveCount);

// Futility pruning margins
int futilityMargin(int depth, bool improving);

// Move count pruning threshold
int futilityMoveCount(bool improving, int depth);

// Draw evaluation with contempt
int drawScore(uint64_t nodes);

} // namespace Search

#endif // DEEPBECKY_SEARCH_H
