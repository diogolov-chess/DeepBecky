#include "search.h"
#include "evaluate.h"
#include "movegen.h"
#include "movepick.h"
#include "position.h"
#include "thread.h"
#include "timeman.h"
#include "tt.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>

#ifdef ENABLE_SEARCH_STATS
#define DB_STAT_INC(field)                                                      \
  do {                                                                          \
    if (thread && thread->collectSearchStats)                                    \
      ++thread->searchStats.field;                                               \
  } while (false)
#define DB_STAT_ADD(field, value)                                                \
  do {                                                                          \
    if (thread && thread->collectSearchStats)                                    \
      thread->searchStats.field += static_cast<uint64_t>(value);                 \
  } while (false)
#define DB_RFP_DEPTH(field, searchDepth)                                         \
  do {                                                                          \
    if (thread && thread->collectSearchStats) {                                  \
      const int bucket = std::clamp((searchDepth), 0,                            \
                                    SearchStats::DepthBuckets - 1);              \
      ++thread->searchStats.field[bucket];                                       \
    }                                                                           \
  } while (false)
#else
#define DB_STAT_INC(field) do {} while (false)
#define DB_STAT_ADD(field, value) do { (void)sizeof(value); } while (false)
#define DB_RFP_DEPTH(field, searchDepth) do { (void)sizeof(searchDepth); } while (false)
#endif

namespace Search {

namespace Tune {
// ==========================================
// Batch 1 - Pruning Margins & Depth Limits (19 param)
// ==========================================

// Futility & Razoring
int FutilityMarginBase = 152;
int FutilityMarginMult = 91;
int FutilityMoveCountBase = 3;
int FutilityChildBase = 80;
int FutilityChildMult = 75;
int FutilityDepthLimit = 7;
int RfpDepthLimit = 8;
int RazorMarginBase = 512;
int RazorMarginMult = 244;

// NMP
int NmpBase = 3;
int NmpDivisor = 3;
int NmpEvalMarginDepth = 8;
int NmpEvalMarginBase = 140;
int NmpDepthLimit = 3;

// ProbCut
int ProbCutBetaBase = 235;
int ProbCutBetaImp = 61;
int ProbCutDepthLimit = 4;

// Aspiration Windows
int AspWindowBase = 10;
int AspWindowThreadMult = 6;

// ==========================================
// Batch 2 - Reductions, History & Extensions (22 param)
// ==========================================

// LMR
int LmrBaseBase = 76;
int LmrMultBase = 188;
int HistoryLmrDivisor = 3499;
int CaptureLmrBadBase = 7014;
int CaptureLmrGoodBase = 5756;

// History
int HistoryBonusMax = 1685;
int HistoryDivisor = 16659;
int CaptureHistoryDivisor = 16276;
int HistoryPruningMargin = 2048;

// Correction History
int CorHistDivisor = 244;
int CorHistWeightBase = 15;
int CorHistWeightMax = 497;
int CorHistBonusMax = 3671;

// Singular Extensions
int SingularMarginBase = 44;
int SingularMarginPvMult = 65;
int SingularMarginDiv = 58;
int SingularDepthLimit = 8;
int DoubleExtMargin = 16;
int TripleExtMargin = 96;

// SEE
int SeeQsearchCapture = -60;
int SeePruningDepthBase = -121;
int SeePruningQuietBase = -60;

// Misc
int IirDepthLimit = 5;
} // namespace Tune

inline int captureTypeIndex(int piece) {
  if (piece == EMPTY)
    return -1;
  return (piece - 1) % 6;
}

// =============================================================================
// Reductions and Initialization
// =============================================================================
int Reductions[64][64]; // [depth][moveNumber]

void init() {
  for (int d = 1; d < 64; d++) {
    for (int m = 1; m < 64; m++) {
      Reductions[d][m] = int(Search::Tune::LmrBaseBase / 100.0 +
                             (Search::Tune::LmrMultBase / 100.0) * std::log(d) *
                                 std::log(m) / 2.0);
    }
  }
}

// Reduction function like original
inline int reduction(bool improving, int depth, int moveCount) {
  int r = Reductions[std::min(depth, 63)][std::min(moveCount, 63)];
  return r - improving; // Less reduction when improving
}

// Futility margin (tuned for NNUE to match SF18 aggression)
inline int futilityMargin(int depth, bool improving) {
  int margin = Search::Tune::FutilityMarginMult * depth;
  if (improving)
    margin -= Search::Tune::FutilityMarginBase;
  return std::max(margin, 0); // Avoid negative margins
}

int rfpDepthLimit(bool zugzwangSensitive) {
  // Static evaluation is less reliable in low-material endings, where a
  // tempo can decide whether a winning pawn break is available. Keep the
  // tuned limit in normal positions, but only allow depths 1..4 in endings
  // already classified as zugzwang-sensitive by the engine.
  constexpr int ZugzwangSensitiveLimit = 5;
  return zugzwangSensitive
             ? std::min(Search::Tune::RfpDepthLimit, ZugzwangSensitiveLimit)
             : Search::Tune::RfpDepthLimit;
}

bool rfpTtMoveAllowsCutoff(Move ttMove, bool ttCapture) {
  // A quiet TT move is positional evidence that static evaluation alone may
  // not represent the node. Capturing TT moves remain compatible with RFP.
  return moveIsNone(ttMove) || ttCapture;
}

int rfpReturnValue(int eval, int beta) {
  // Do not expose the full (potentially optimistic) static evaluation as a
  // searched bound. Bias toward beta while preserving a fail-high result.
  return (2 * beta + eval) / 3;
}

// Move count pruning threshold (tuned modern quadratic formula)
inline int futilityMoveCount(bool improving, int depth) {
  return (Search::Tune::FutilityMoveCountBase + depth * depth) / (2 - improving);
}

// Draw score
inline int drawScore(uint64_t nodes) {
  return int(2 * (nodes & 1) - 1); // -1 or +1
}

std::string uciScore(int score) {
  if (score >= MATE_IN_MAX || score <= -MATE_IN_MAX) {
    const int mateDistance = MATE_SCORE - std::abs(score);
    int mateMoves = std::max(1, (mateDistance + 1) / 2);
    if (score < 0)
      mateMoves = -mateMoves;
    return "score mate " + std::to_string(mateMoves);
  }
  return "score cp " + std::to_string(score);
}

} // namespace Search

// =============================================================================
// Search member functions of Position
// =============================================================================

// Quiescence search
// =============================================================================
// Quiescence Search (Q-Search)
// Explores all tactical sequences (captures, promotions) until a quiet position
// is reached. Prevents the Horizon Effect by ensuring static evaluations are
// tactically stable.
// =============================================================================
int Position::qsearch(int alpha, int beta, SearchStack *ss) {
  int ply = ss->ply;
  const int originalAlpha = alpha;

  // Safety limit MUST be checked before writing to ss+1 to avoid buffer
  // overflow
  if (ply >= MAX_PLY - 1) {
    ss->pvLength = 0;
    return Eval::evaluate(*this);
  }

  (ss + 1)->ply = ply + 1;
  ss->pvLength = 0;
  if (stopSearching)
    return alpha;

  nodes++;

  // Periodic stop check (every 2047 nodes)
  if ((nodes & 0x7FF) == 0) {
    if (Threads.stop.load(std::memory_order_relaxed) || timeUp()) {
      stopSearching = true;
      if (thread && thread->idx == 0)
        Threads.stop.store(true, std::memory_order_relaxed);
      return alpha;
    }
  }

  if (ply > selDepth)
    selDepth = ply;

  const bool isInCheck = inCheck(white_to_move);

  if (ply > 0 && isDraw(ply, isInCheck)) {
    return Search::drawScore(static_cast<uint64_t>(nodes));
  }

  // TT Probe in qsearch
  TT.prefetch(hash);
  TTProbe ttProbe = TT.probe(hash);
  bool ttHit = ttProbe.found;
  const TTData& ttData = ttProbe.data;
  Move qsTTMove = MOVE_NONE;
  if (ttHit) {
    int ttScore = static_cast<int>(ttData.value);
    TTFlag ttFlag = ttData.flag();
    // Adjust mate scores from TT relative to current ply
    if (ttScore >= MATE_IN_MAX)
      ttScore -= ply;
    if (ttScore <= -MATE_IN_MAX)
      ttScore += ply;
    // TT cutoff in qsearch
    if (ttFlag == TT_EXACT)
      return ttScore;
    if (ttFlag == TT_BETA && ttScore >= beta)
      return ttScore;
    if (ttFlag == TT_ALPHA && ttScore <= alpha)
      return ttScore;

    // Extract TT move for MovePicker ordering
    if (!moveIsNone(ttData.move)) {
      qsTTMove = ttData.move;
    }
  }

  // Use TT eval if available, otherwise compute evaluation. Keep the raw
  // value for TT storage; correction history and 50-move damping are local
  // search adjustments and must not be persisted as the static evaluation.
  int stand;
  int16_t rawEval = EVAL_NONE;
  if (ttHit && ttData.eval != EVAL_NONE) {
    rawEval = ttData.eval;
    stand = static_cast<int>(rawEval);
  } else {
    stand = evaluate();
    if (!isInCheck)
      rawEval = static_cast<int16_t>(std::clamp(stand, -32000, 32000));
  }
  if (isInCheck)
    rawEval = EVAL_NONE;
  if (stand != -INF_SCORE) {
    int pawnHash = pawnKey % 16384;
    int nonPawnHash = (hash ^ (pawnKey >> 16)) % 16384;
    int corHist = thread->corHist[white_to_move ? WHITE : BLACK][pawnHash];
    int nonPawnCor = thread->nonPawnCorHist[white_to_move ? WHITE : BLACK][nonPawnHash];
    stand += (corHist + nonPawnCor) / Search::Tune::CorHistDivisor;
  }

  // 50-Move Rule Damping: smoothly scale down static evaluation as halfmove approaches 100
  if (halfmove >= 70 && !isInCheck && std::abs(stand) < MATE_IN_MAX) {
    stand = (stand * (100 - halfmove)) / 30;
  }

  int best = stand;
  Move bestMove = MOVE_NONE;

  // Qsearch has its own logical depth. It may refresh another qsearch entry,
  // but must never replace an entry produced by the regular search.
  const bool mayStoreQsearch = !ttHit || ttData.depth() <= TT_DEPTH_QS;
  auto saveQsearch = [&](int score, TTFlag flag) {
    if (!mayStoreQsearch || stopSearching)
      return;

    int storeScore = score;
    if (score >= MATE_IN_MAX)
      storeScore += ply;
    else if (score <= -MATE_IN_MAX)
      storeScore -= ply;

    const uint16_t packedMove = moveIsNone(bestMove) ? 0 : bestMove.data;
    ttProbe.writer.save(hash, static_cast<int16_t>(storeScore), false, flag,
                        TT_DEPTH_QS, packedMove, rawEval, TT.generation());
  };

  if (!isInCheck) {
    if (stand >= beta) {
      saveQsearch(stand, TT_BETA);
      return stand;
    }
    if (stand > alpha)
      alpha = stand;
  } else {
    best = -INF_SCORE;
  }

  // Use staged MovePicker for qsearch:
  // - Not in check: generates only captures (lazy, scored with MVV-LVA)
  // - In check: generates all legal evasions
  MovePicker picker(*this, qsTTMove, isInCheck, ply);

  int legalMoves = 0;

  for (Move m = picker.next(); !moveIsNone(m); m = picker.next()) {

    bool isCapture = moveIsCapture(m);
    bool isPromotion = movePromotionType(m) != 0;

    // Skip bad captures using SEE threshold (but not when in check)
    // CRITICAL FIX: Allow slightly negative SEE (like -50) to prevent the
    // Horizon Effect where defensive captures (e.g., Bishop capturing a Knight,
    // SEE = -10) were pruned, causing the engine to hallucinate lost positions
    // and lag.
    if (!isInCheck && !isCapture && !isPromotion)
      continue; // skip non-tactical moves outside check
    if (!isInCheck && !SEE(m, Search::Tune::SeeQsearchCapture))
      continue;

    // Delta pruning - if capturing won't bring us close to alpha, skip
    if (!isInCheck && !isPromotion && isCapture) {
      int to_sq = moveTo(m);
      int captured = moveIsEnPassant(m) ? (white_to_move ? BPAWN : WPAWN)
                                        : piece_board[to_sq];
      int delta = PIECE_VALUE[captured] + 200;
      // Strict delta: if we are far behind, require larger captures
      if (stand + delta < alpha)
        continue;
    }

    makeMove(m);
    TT.prefetch(hash); // prefetch child's TT cluster
    legalMoves++;

    int score = -qsearch(-beta, -alpha, ss + 1);
    undoMove(m);

    if (stopSearching)
      return alpha;

    if (score > best) {
      best = score;
      bestMove = m;
      if (score > alpha) {
        alpha = score;
        if (score >= beta) {
          saveQsearch(score, TT_BETA);
          return score; // Fail high
        }
      }
    }
  }

  if (isInCheck && legalMoves == 0) {
    const int mateScore = -MATE_SCORE + ply;
    saveQsearch(mateScore, TT_EXACT);
    return mateScore;
  }

  const TTFlag flag = best > originalAlpha ? TT_EXACT : TT_ALPHA;
  saveQsearch(best, flag);
  return best;
}

// Principal Variation Search - with Singular Extensions, CutNode, ProbCut
// =============================================================================
// Principal Variation Search (PVS) / NegaMax with Alpha-Beta Pruning
// The core search function. Uses a zero-window search for non-PV nodes to prove
// they fail low faster, falling back to a full-window search if they fail high.
// =============================================================================
int Position::pvs(int depth, int alpha, int beta, SearchStack *ss,
                  bool cutNode) {
  int ply = ss->ply;
  int originalAlpha = alpha;

  // Safety limit MUST be checked before writing to ss+1 to avoid buffer
  // overflow
  if (ply >= MAX_PLY - 1) {
    ss->pvLength = 0;
    return Eval::evaluate(*this);
  }

  (ss + 1)->ply = ply + 1;
  ss->pvLength = 0;
  Move excludedMove = ss->excludedMove;
  if (stopSearching)
    return alpha;

  int threadId = thread ? static_cast<int>(thread->idx) : 0;

  bool rootNode = (ply == 0);
  bool pvNode = (beta - alpha > 1); // PV node detection

  // Quiescence at leaf
  if (depth <= 0)
    return qsearch(alpha, beta, ss);

  const bool isInCheck = inCheck(white_to_move);

  if (!rootNode) {
    // Draw detection
    if (isDraw(ply, isInCheck)) {
      return Search::drawScore(static_cast<uint64_t>(nodes));
    }

    alpha = std::max(alpha, -MATE_SCORE + ply);
    beta = std::min(beta, MATE_SCORE - ply - 1);
    if (alpha >= beta)
      return alpha;
  }

  // Time check (not too frequently, every 2047 nodes)
  if (ply > 0 && (nodes & 0x7FF) == 0) {
    if (Threads.stop.load(std::memory_order_relaxed) || timeUp()) {
      stopSearching = true;
      if (thread && thread->idx == 0)
        Threads.stop.store(true, std::memory_order_relaxed);
      return alpha;
    }
  }

  // Check extensions are assigned selectively in the main move loop after
  // the child position is available and givesCheck can be computed exactly.

  nodes++;

  // Update selective depth
  if (ply > selDepth)
    selDepth = ply;

  // ============ TT Probe ============
  TT.prefetch(hash);
  TTProbe ttProbe = TT.probe(hash);
  bool ttHit = ttProbe.found;
  const TTData& ttData = ttProbe.data;
  Move ttMove = MOVE_NONE;
  int ttScore = -INF_SCORE;
  int ttDepth = -1;
  TTFlag ttFlag = TT_ALPHA;
  bool ttCapture = false;
  int16_t ttEval = EVAL_NONE; // static eval from TT

  if (ttHit) {
    // Reconstruct the TT move from packed 16-bit format.
    // The MoveType is preserved intact in the 16 bits, so moveTypeOf()
    // and moveIsCapture() already return the correct flags — nothing to
    // reconstruct from the board.
    ttMove = ttData.move;

    // Anti-Collision Check: If the move is physically impossible,
    // this TT entry belongs to a DIFFERENT position with the same hash!
    if (!moveIsNone(ttMove)) {
      if (!isPseudoLegal(ttMove) || !legalMove(ttMove)) {
        ttHit = false;
        ttMove = MOVE_NONE;
      }
    }

    if (ttHit) {
      ttDepth = ttData.depth();
      ttScore = static_cast<int>(ttData.value);
      // Adjust mate scores from TT relative to current ply
      if (ttScore >= MATE_IN_MAX)
        ttScore -= ply;
      if (ttScore <= -MATE_IN_MAX)
        ttScore += ply;
      ttFlag = ttData.flag();
      ttEval = ttData.eval;

      // Track if TT move is a capture
      if (!moveIsNone(ttMove)) {
        ttCapture = moveIsCapture(ttMove);
      }

      // TT cutoffs - not at root, PV nodes, or singular search
      if (!rootNode && !pvNode && !excludedMove.data && ttDepth >= depth &&
          halfmove < 90) {
        if (ttFlag == TT_EXACT)
          return ttScore;
        if (ttFlag == TT_ALPHA && ttScore <= alpha)
          return ttScore;
        if (ttFlag == TT_BETA && ttScore >= beta)
          return ttScore;
      }
    }
  }

  // ============ Static Evaluation ============
  int staticEval;
  int eval;
  int16_t rawEval = EVAL_NONE; // unadjusted eval for TT storage
  if (isInCheck) {
    staticEval = eval = -INF_SCORE; // No static eval when in check
  } else if (ttHit && ttEval != EVAL_NONE) {
    // Use stored eval from TT — saves a full evaluate() call
    rawEval = ttEval;

    // Apply CorHist (Pawn + Non-Pawn)
    int pawnHash = pawnKey % 16384;
    int nonPawnHash = (hash ^ (pawnKey >> 16)) % 16384;
    int corHist = thread->corHist[white_to_move ? WHITE : BLACK][pawnHash];
    int nonPawnCor = thread->nonPawnCorHist[white_to_move ? WHITE : BLACK][nonPawnHash];
    staticEval = eval =
        static_cast<int>(ttEval) + (corHist + nonPawnCor) / Search::Tune::CorHistDivisor;

    if (ttFlag == TT_EXACT || (ttFlag == TT_BETA && ttScore > eval) ||
        (ttFlag == TT_ALPHA && ttScore < eval)) {
      eval = ttScore;
    }
  } else {
    staticEval = eval = evaluate();
    rawEval = static_cast<int16_t>(std::clamp(staticEval, -32000, 32000));

    // Apply CorHist (Pawn + Non-Pawn)
    int pawnHash = pawnKey % 16384;
    int nonPawnHash = (hash ^ (pawnKey >> 16)) % 16384;
    int corHist = thread->corHist[white_to_move ? WHITE : BLACK][pawnHash];
    int nonPawnCor = thread->nonPawnCorHist[white_to_move ? WHITE : BLACK][nonPawnHash];
    staticEval += (corHist + nonPawnCor) / Search::Tune::CorHistDivisor;
    eval = staticEval;
  }

  // 50-Move Rule Damping: smoothly scale down static evaluation as halfmove approaches 100
  if (halfmove >= 70 && !isInCheck && std::abs(eval) < MATE_IN_MAX) {
    eval = (eval * (100 - halfmove)) / 30;
    staticEval = eval;
  }

  // Keep one NMP gate independent from correction history and TT bounds.
  // rawEval is the unadjusted NNUE/static value saved in the TT. Only the
  // objective 50-move-rule damping is shared with the search score.
  int nmpStaticEval = static_cast<int>(rawEval);
  if (halfmove >= 70 && std::abs(nmpStaticEval) < MATE_IN_MAX)
    nmpStaticEval = (nmpStaticEval * (100 - halfmove)) / 30;

  // Store static eval in search stack for improving detection
  ss->staticEval = staticEval;

  // Guard against sentinel (-INF_SCORE) static evals left by in-check
  // ancestors.
  bool improving = false;
  if (isInCheck) {
    improving = false;
  } else if (ply >= 2 && (ss - 2)->staticEval != -INF_SCORE) {
    improving = (staticEval > (ss - 2)->staticEval);
  } else if (ply >= 4 && (ss - 4)->staticEval != -INF_SCORE) {
    improving = (staticEval > (ss - 4)->staticEval);
  } else {
    improving = true; // optimistic default when no reference is available
  }

  // ============ Razoring ============
  // If the static evaluation is far below alpha in non-PV nodes, drop directly
  // into quiescence search
  if (!pvNode && !isInCheck && depth <= 3) {
    int razorMargin = Search::Tune::RazorMarginBase +
                      Search::Tune::RazorMarginMult * depth * depth;
    if (eval < alpha - razorMargin) {
      DB_STAT_INC(razorCuts);
      return qsearch(alpha, beta, ss);
    }
  }

  // ============ Reverse Futility Pruning (Static Null Move) ============
  // Prune branches where static eval minus futility margin still exceeds beta.
  // In low-material endings, restrict RFP to shallow nodes because a single
  // quiet tempo can invalidate the static bound. A quiet TT move is likewise
  // searched instead of being discarded by the static evaluation.
  if (!pvNode && !isInCheck && depth < Search::Tune::RfpDepthLimit) {
    DB_STAT_INC(rfpCandidates);
    DB_RFP_DEPTH(rfpCandidatesByDepth, depth);

    if (!Search::rfpTtMoveAllowsCutoff(ttMove, ttCapture)) {
      DB_STAT_INC(rfpVetoTtQuiet);
    } else if (std::abs(beta) >= MATE_IN_MAX || std::abs(eval) >= MATE_IN_MAX) {
      DB_STAT_INC(rfpVetoMate);
    } else if (eval - Search::futilityMargin(depth, improving) >= beta) {
      const bool zugzwangSensitive = depth >= 5 && isZugzwangEndgame();
      if (depth >= Search::rfpDepthLimit(zugzwangSensitive)) {
        DB_STAT_INC(rfpVetoEndgame);
      } else {
        DB_STAT_INC(rfpCuts);
        DB_RFP_DEPTH(rfpCutsByDepth, depth);
        return Search::rfpReturnValue(eval, beta);
      }
    }
  }

  int nmpMargin = std::max(20, Search::Tune::NmpEvalMarginBase - Search::Tune::NmpEvalMarginDepth * depth);

  if (!pvNode && !isInCheck && depth >= Search::Tune::NmpDepthLimit &&
      ply > 0 &&
      eval >= beta &&
      nmpStaticEval >= beta + nmpMargin &&
      hasNonPawnMaterial(white_to_move) && !isZugzwangEndgame() && moveIsNone(excludedMove) &&
      !moveIsNone((ss - 1)->currentMove) &&
      (ply >= thread->nmpMinPly || sideToMove() != thread->nmpColor)) {
    DB_STAT_INC(nmpAttempts);
    ss->currentMove = MOVE_NONE; // No real move — prevents stale counter-move
                                 // lookup in children
    ss->movedPiece = EMPTY;
    makeNullMove();

    // ============ Adaptive Null Move Pruning ============
    int R = Search::Tune::NmpBase + depth / Search::Tune::NmpDivisor +
            std::min((eval - beta) / 150, 3);

    int nmScore = -pvs(depth - R, -beta, -beta + 1, ss + 1, !cutNode);
    (ss + 1)->pvLength = 0; // Prevent PV leak from null move branch
    undoNullMove();

    // Skip NMP cutoffs if it returns an unproven mate score (Stockfish 18
    // behavior)
    if (nmScore >= beta && std::abs(nmScore) < MATE_IN_MAX) {

      // At low depths or clear wins, accept directly
      if (thread->nmpMinPly || depth < 14) {
        DB_STAT_INC(nmpCuts);
        return nmScore;
      }

      // Verification search at high depths (depth >= 14):
      // Re-search with null move disabled for our side to avoid
      // false cutoffs in zugzwang positions (~20 Elo).
      thread->nmpMinPly = ply + 3 * (depth - R) / 4;
      thread->nmpColor = sideToMove();
      DB_STAT_INC(nmpVerifications);

      int vScore = pvs(depth - R, beta - 1, beta, ss, false);
      thread->nmpMinPly = 0;
      ss->pvLength = 0; // Prevent PV corruption from NMP verification

      if (vScore >= beta) {
        DB_STAT_INC(nmpCuts);
        return nmScore;
      }
      DB_STAT_INC(nmpVerificationFails);
      ss->pvLength = 0; // Prevent leak if verification fails and loop continues
    }
  }

  // ============ Internal Iterative Deepening ============
  // If no TT move at high depth, do a shallow search first (like original)
  // Deeper reduction at cut nodes (IIR - Internal Iterative Reduction)
  if (depth >= Search::Tune::IirDepthLimit && moveIsNone(ttMove) &&
      !isInCheck) {
    int iirReduction = cutNode ? 2 : 1;
    depth -= iirReduction;
  }

  // ============ ProbCut (~15-20 Elo) ============
  // If a capture with reduced search returns much above beta, prune
  if (!pvNode && depth >= Search::Tune::ProbCutDepthLimit &&
      std::abs(beta) < MATE_IN_MAX && moveIsNone(excludedMove)) {
    int raisedBeta = beta + Search::Tune::ProbCutBetaBase -
                     Search::Tune::ProbCutBetaImp * improving;

    // Use staged ProbCut MovePicker: generates captures with SEE >= 0
    MovePicker pcPicker(*this, ttMove, 0, ply); // SEE threshold = 0
    int probCutCount = 0;

    for (Move m = pcPicker.next(); !moveIsNone(m) && probCutCount < 3;
         m = pcPicker.next()) {
      DB_STAT_INC(probCutAttempts);
      const Move savedCurrentMove = ss->currentMove;
      const int savedMovedPiece = ss->movedPiece;
      const int probCutMovedPiece = piece_board[moveFrom(m)];
      ss->currentMove = m;
      ss->movedPiece = probCutMovedPiece;

      makeMove(m);
      probCutCount++;

      // Phase 1: qsearch verification
      int val = -qsearch(-raisedBeta, -raisedBeta + 1, ss + 1);

      // Phase 2: deeper verification
      if (val >= raisedBeta)
        val = -pvs(depth - 4, -raisedBeta, -raisedBeta + 1, ss + 1, !cutNode);

      undoMove(m);
      ss->currentMove = savedCurrentMove;
      ss->movedPiece = savedMovedPiece;

      if (val >= raisedBeta) {
        DB_STAT_INC(probCutCuts);
        (ss + 1)->pvLength = 0; // Prevent PV corruption from ProbCut
        // Prevent returning unproven mates from reduced depth search
        return std::abs(val) < MATE_IN_MAX ? val : raisedBeta;
      }
    }
  }

  int best = -INF_SCORE;
  Move bestMove = MOVE_NONE;
  int moveCount = 0;
  bool hasLegalMove = false;

  Move quietsSearched[64];
  int quietsMovedPiece[64];
  int quietCount = 0;

  // Track searched captures for capture history malus (v2.4.1)
  Move capsSearched[64];
  int capsMovedPiece[64];
  int capsCapturedType[64];
  int capCount = 0;

  // Look up killers for this ply
  Move k0 = thread->killers.killer[0][ply];
  Move k1 = thread->killers.killer[1][ply];

  // Look up counter-move: best quiet reply to previous ply's move
  Move counterMove = MOVE_NONE;
  if (ply > 0 && !moveIsNone((ss - 1)->currentMove)) {
    int prevTo = moveTo((ss - 1)->currentMove);
    int prevPiece = (ss - 1)->movedPiece; // piece that made the previous move
                                          // (stored pre-makeMove)
    if (prevPiece != EMPTY && prevPiece < PIECE_NB)
      counterMove = thread->counterMoves[prevPiece][prevTo];
  }

  // Staged MovePicker: generates captures lazily at CAPTURE_INIT,
  // quiets lazily at QUIET_INIT. If cutoff happens at GOOD_CAPTURES,
  // quiet generation is never executed.
  MovePicker picker(*this, ttMove, ss, depth, k0, k1, counterMove);

  // ============ Main Move Loop ============
  for (Move m = picker.next(); !moveIsNone(m); m = picker.next()) {

    // Skip excluded move (for singular extension search)
    if (m == excludedMove)
      continue;

    bool isCapture = moveIsCapture(m);
    bool isPromotion = movePromotionType(m) != 0;

    int newDepth = depth - 1;
    int extension = 0;
    bool singularLMR = false;

    // ============ Singular Extensions (~70-80 Elo) ============
    // Test if the TT move is the only good move; if so, extend its search
    if (depth >= Search::Tune::SingularDepthLimit && m == ttMove && !rootNode &&
        moveIsNone(excludedMove) && std::abs(ttScore) < MATE_IN_MAX &&
        (ttFlag == TT_BETA || ttFlag == TT_EXACT) && ttDepth >= depth - 3) {
      int singularMargin =
          (Search::Tune::SingularMarginBase +
           Search::Tune::SingularMarginPvMult * (pvNode ? 1 : 0)) *
          depth / Search::Tune::SingularMarginDiv;
      int singularBeta = ttScore - singularMargin;
      int rDepth = (depth - 1) / 2;

      // Search excluding the TT move at reduced depth
      ss->excludedMove = m;
      int seScore = pvs(rDepth, singularBeta - 1, singularBeta, ss, cutNode);
      ss->excludedMove = MOVE_NONE;
      ss->pvLength = 0; // Prevent PV corruption from Singular Extension

      if (seScore < singularBeta) {
        extension = 1; // TT move is singular - extend!
        singularLMR = true;

        // Double & Triple Singular Extensions: ONLY on non-PV nodes to avoid root explosion (~25 Elo)
        if (!pvNode && ss->doubleExtensions < 2 && seScore < singularBeta - Search::Tune::DoubleExtMargin) {
          extension = 2;
          if (depth >= 10 && !isCapture && ss->doubleExtensions == 0 && seScore < singularBeta - Search::Tune::TripleExtMargin) {
            extension = 3;
          }
        }
      } else if (seScore >= beta && std::abs(seScore) < MATE_IN_MAX) {
        return seScore; // MultiCut: all moves are good enough
      } else if (cutNode && !pvNode) {
        extension = -1; // Negative extension at cut nodes
      } else if (ttScore >= beta && !pvNode) {
        extension = -1; // Negative extension
      }
    }

    // ============ Pre-move pruning (SEE-based, BEFORE makeMove) ============
    // These checks avoid the cost of makeMove/undoMove for moves that will be
    // pruned.
    bool quietSeeFails = false;
    if (!rootNode && best > -MATE_IN_MAX) {
      // SEE pruning for captures at low depths
      if (depth <= 3 && isCapture && !isPromotion) {
        if (!SEE(m, Search::Tune::SeePruningDepthBase * depth)) {
          DB_STAT_INC(seeCaptureCuts);
          // The move is legal even though it was selectively pruned. Keep
          // checkmate/stalemate detection independent of pruning decisions.
          hasLegalMove = true;
          continue;
        }
      }

      // A quiet sacrifice can give check. SEE is evaluated while the parent
      // position is available, but its result is acted on only after the
      // child tells us whether the move gives check.
      if (!pvNode && depth <= 3 && !isCapture && !isPromotion && !isInCheck &&
          m != k0 && m != k1 && m != counterMove) {
        int seeThreshold = Search::Tune::SeePruningQuietBase * depth;
        quietSeeFails = !SEE(m, seeThreshold);
      }
    }

    // Make move (already legal - no legality check needed)
    ss->currentMove = m; // Store for counter-move heuristic

    // Extract pieces BEFORE makeMove so they are accurate for history and
    // pruning
    int currentMovedPiece = piece_board[moveFrom(m)];
    int currentCapturedPiece = moveIsEnPassant(m)
                                   ? (white_to_move ? BPAWN : WPAWN)
                                   : piece_board[moveTo(m)];
    int us = white_to_move ? WHITE : BLACK;
    const int parentPawnHistoryIndex = static_cast<int>(pawnKey & 8191);
    ss->movedPiece = currentMovedPiece; // store mover so children read correct
                                        // cont-history piece

    // Track node count at root before searching this move (for effort)
    uint64_t nodeCountBefore = rootNode ? static_cast<uint64_t>(nodes) : 0;

    makeMove(m);

    // Prefetch TT cluster for child position while we compute
    // givesCheck/extensions
    TT.prefetch(hash);

    // Check if opponent is in check (we give check) - SAME side as
    // white_to_move now This is computed early since we need it for pruning and
    // LMR
    bool givesCheck = inCheck(white_to_move);

    // Checks are explicitly exempt from quiet SEE pruning. Undoing is needed
    // because SEE itself must inspect the parent position; no child search or
    // history update has occurred yet.
    if (quietSeeFails && !givesCheck) {
      DB_STAT_INC(seeQuietCuts);
      undoMove(m);
      hasLegalMove = true;
      continue;
    }

    hasLegalMove = true;
    moveCount++;

    // Extend only forcing checks on the principal variation. The depth floor
    // prevents checks at the qsearch frontier from keeping the regular search
    // alive indefinitely, and singular extensions retain precedence.
    if (pvNode && givesCheck && depth >= 3 && extension == 0)
      extension = 1;

    if (extension > 0) {
      if (singularLMR)
        DB_STAT_INC(singularExtensions);
      else if (givesCheck)
        DB_STAT_INC(checkExtensions);
      if (extension >= 2)
        DB_STAT_INC(doubleExtensions);
      if (extension >= 3)
        DB_STAT_INC(tripleExtensions);
    }

    newDepth += extension;
    // Clamp only against negative extensions (cutNode -2) driving newDepth
    // below zero. A natural newDepth==0 must be preserved so leaf nodes
    // correctly drop into qsearch.
    if (extension < 0 && newDepth < 0)
      newDepth = 0;

    // Pass down double extensions count
    (ss + 1)->doubleExtensions = ss->doubleExtensions + (extension >= 2 ? 1 : 0);

    // ============ Pruning at low depths (HUGE speedup ~200 Elo) ============
    if (!rootNode && best > -MATE_IN_MAX) {

      // Late Move Pruning / Move Count Pruning
      if (depth <= 8 && !isCapture && !isPromotion && !isInCheck && !givesCheck) {
        if (moveCount > Search::futilityMoveCount(improving, depth)) {
          DB_STAT_INC(lmpTriggers);
          picker.skipQuiets();
        }
      }

      // Futility Pruning - child node (conservative like 0.21a)
      if (!isCapture && !isPromotion && !isInCheck && !givesCheck &&
          depth <= Search::Tune::FutilityDepthLimit && moveCount > 1) {
        int futilityValue = eval + Search::Tune::FutilityChildBase +
                            Search::Tune::FutilityChildMult * depth;
        if (futilityValue <= alpha) {
          DB_STAT_INC(futilityCuts);
          undoMove(m);
          if (futilityValue > best)
            best = futilityValue;
          continue;
        }
      }

      // History pruning: prune very weak quiet moves with terrible history
      if (!pvNode && depth <= 6 && moveCount > 4 && !isCapture &&
          !isPromotion && !isInCheck && !givesCheck) {
        int hpScore = thread->history_heur[us][moveFrom(m)][moveTo(m)];
        int movedPiece = currentMovedPiece;
        int to_sq = moveTo(m);
        if (movedPiece != EMPTY && movedPiece < PIECE_NB) {
          hpScore += thread->pawnHistory[parentPawnHistoryIndex][movedPiece][to_sq];
          int plies_back[4] = {1, 2, 4, 6};
          for (int l = 0; l < 4; ++l) {
            int pb = plies_back[l];
            if (ply >= pb && !moveIsNone((ss - pb)->currentMove)) {
              int prevTo = moveTo((ss - pb)->currentMove);
              int prevPiece = (ss - pb)->movedPiece;
              if (prevPiece != EMPTY && prevPiece < PIECE_NB) {
                hpScore +=
                    thread
                        ->contHistory[l][prevPiece][prevTo][movedPiece][to_sq];
              }
            }
          }
        }
        if (hpScore < -Search::Tune::HistoryPruningMargin * depth) {
          DB_STAT_INC(historyCuts);
          undoMove(m);
          continue;
        }
      }
    }

    int sc;
    (ss + 1)->pvLength = 0;
#ifdef ENABLE_SEARCH_STATS
    const uint64_t extensionNodesBefore =
        extension > 0 ? nodes.load(std::memory_order_relaxed) : 0;
#endif

    if (moveCount == 1) {
      // First move - full window search for PV, non-PV propagates !cutNode
      sc = -pvs(newDepth, -beta, -alpha, ss + 1, pvNode ? false : !cutNode);
    } else {
      // ============ Late Move Reductions (LMR) ============
      int R = 0;
      if (depth >= 3 && moveCount > 2 && !isCapture && !isPromotion &&
          !isInCheck) {
        {
          // --- Quiet LMR ---
          R = Search::reduction(improving, depth, moveCount);

          // Asymmetrical Helpers (Phase 5): Diversify LMR based on thread ID
          if (threadId && depth >= 6 && moveCount > 3) {
            if (threadId % 2 == 1)
              R++; // Odd helpers reduce more (deeper on PV)
            else
              R--; // Even helpers reduce less (broader search)
          }

          // Reduce more in non-PV nodes
          if (!pvNode)
            R++;

          // Reduce more at cut nodes (~10 Elo)
          if (cutNode)
            R += 2;

          // Reduce less for singular TT moves
          if (singularLMR)
            R -= 2;

          // Reduce more if TT move is a capture (quiet moves are worse)
          if (ttCapture)
            R++;

          // Reduce less for killer moves
          if (m == thread->killers.killer[0][ply] ||
              m == thread->killers.killer[1][ply])
            R--;

          // Reduce less if giving check
          if (givesCheck)
            R--;

          // History-based reduction: use a conservative divisor.
          // With weak eval, overly strong history-LMR coupling can destabilize
          // search.
          int side = us;
          int histScore = thread->history_heur[side][moveFrom(m)][moveTo(m)];
          int movedPiece = currentMovedPiece;
          int to_sq = moveTo(m);
          if (movedPiece != EMPTY && movedPiece < PIECE_NB) {
            histScore += thread->pawnHistory[parentPawnHistoryIndex][movedPiece][to_sq];
            int plies_back[4] = {1, 2, 4, 6};
            for (int l = 0; l < 4; ++l) {
              int pb = plies_back[l];
              if (ply >= pb && !moveIsNone((ss - pb)->currentMove)) {
                int prevTo = moveTo((ss - pb)->currentMove);
                int prevPiece = (ss - pb)->movedPiece;
                if (prevPiece != EMPTY && prevPiece < PIECE_NB) {
                  histScore += thread->contHistory[l][prevPiece][prevTo]
                                                  [movedPiece][to_sq];
                }
              }
            }
          }
          // History-based LMR reduction adjustment
          R -= histScore / Search::Tune::HistoryLmrDivisor;

          // Don't reduce below 1
          R = std::max(0, R);

          // Don't reduce too much
          R = std::min(R, newDepth - 1);
        }
      } else if (depth >= 4 && moveCount > 1 && isCapture && !isPromotion &&
                 !isInCheck && !givesCheck) {
        // --- Capture LMR (uses pre-move state, since makeMove already ran) ---
        int capturedType = Search::captureTypeIndex(currentCapturedPiece);

        if (currentMovedPiece != EMPTY && currentMovedPiece < PIECE_NB &&
            capturedType >= 0) {
          int captureHist =
              thread
                  ->captureHistory[currentMovedPiece][moveTo(m)][capturedType];
          if (captureHist < -Search::Tune::CaptureLmrBadBase)
            R += 2;
          else if (captureHist < -Search::Tune::CaptureLmrBadBase / 3)
            R += 1;
          else if (captureHist > Search::Tune::CaptureLmrGoodBase && depth >= 6)
            R -= 1;
          R = std::max(0, std::min(R, newDepth - 1));
        }
      }

      // Final LMR clamp applied to ALL branches (quiet and capture) so the
      // reduced search never sees newDepth - R <= 0, which would drop the
      // move straight into qsearch and risk misclassifying it.
      R = std::max(0, std::min(R, newDepth - 1));

      // Reduced depth search (null window)
      if (R > 0) {
        DB_STAT_INC(lmrReductions);
#ifdef ENABLE_SEARCH_STATS
        if (thread && thread->collectSearchStats) {
          const int depthBand = depth <= 3 ? 0 : depth <= 6 ? 1
                                                   : depth <= 10 ? 2 : 3;
          const int reductionBand = std::min(R, 4) - 1;
          ++thread->searchStats.lmrReductionsByDepth[depthBand];
          ++thread->searchStats.lmrReductionsByAmount[reductionBand];
        }
#endif
        sc = -pvs(newDepth - R, -alpha - 1, -alpha, ss + 1, true);
      } else {
        sc = alpha + 1; // Force re-search
      }

      // Re-search at full depth if LMR failed high
      if (sc > alpha) {
        if (R > 0) {
          DB_STAT_INC(lmrFailHighs);
          DB_STAT_INC(lmrResearches);
        }
        sc = -pvs(newDepth, -alpha - 1, -alpha, ss + 1, !cutNode);

        // Full window re-search for PV nodes
        if (sc > alpha && sc < beta) {
          sc = -pvs(newDepth, -beta, -alpha, ss + 1, false);
        }
      }
    }

#ifdef ENABLE_SEARCH_STATS
    if (extension > 0 && thread && thread->collectSearchStats) {
      const uint64_t extensionNodesAfter = nodes.load(std::memory_order_relaxed);
      thread->searchStats.extensionNodes +=
          extensionNodesAfter - extensionNodesBefore;
    }
#endif

    undoMove(m);

    if (stopSearching)
      return alpha;

    if (rootNode) {
      uint64_t nodesSpent = static_cast<uint64_t>(nodes) - nodeCountBefore;
      // Find this move's index in the root legal moves list
      for (int ri = 0; ri < rootMoveCount; ri++) {
        if (rootLegalMoves[ri] == m) {
          rootMoveEffort[ri] += nodesSpent;
          // Update running average score
          if (rootMoveAvgScore[ri] <= -INF_SCORE)
            rootMoveAvgScore[ri] = sc;
          else
            rootMoveAvgScore[ri] = (sc + rootMoveAvgScore[ri]) / 2;
          break;
        }
      }
    }

    // Track searched quiet moves for history malus
    if (!isCapture && !isPromotion && quietCount < 64) {
      quietsSearched[quietCount] = m;
      quietsMovedPiece[quietCount] = currentMovedPiece;
      quietCount++;
    }

    // Track searched captures for capture history malus (v2.4.1)
    if (isCapture && capCount < 64) {
      capsSearched[capCount] = m;
      capsMovedPiece[capCount] = currentMovedPiece;
      capsCapturedType[capCount] = Search::captureTypeIndex(currentCapturedPiece);
      capCount++;
    }

    // Update best
    if (sc > best) {
      best = sc;
      bestMove = m; // Track the best move even on fail-low so TT cutoffs can be
                    // validated

      if (sc > alpha) {
        // At root (ply==0), immediately track the best move in a
        // per-position variable. This avoids relying on the shared TT
        // (which can be overwritten by other threads) to recover
        // the root best move after the search completes.
        if (rootNode) {
          rootBestMove = m;
          rootBestScore = sc;

          // Only count when a non-first move becomes best
          if (moveCount > 1 && thread)
            thread->bestMoveChanges.fetch_add(1, std::memory_order_relaxed);
        }

        // Update history for quiet moves that improve alpha (gravity formula)
        if (!isCapture && !isPromotion) {
          int side = us;
          int bonus = std::min(depth * depth, Search::Tune::HistoryBonusMax);
          int &entry = thread->history_heur[side][moveFrom(m)][moveTo(m)];
          entry +=
              bonus - entry * std::abs(bonus) / Search::Tune::HistoryDivisor;

          int movedPiece = currentMovedPiece;
          if (movedPiece != EMPTY && movedPiece < PIECE_NB) {
            // Pawn History
            int16_t &pEntry =
                thread->pawnHistory[parentPawnHistoryIndex][movedPiece][moveTo(m)];
            int pv = static_cast<int>(pEntry);
            pv += bonus - pv * std::abs(bonus) / Search::Tune::HistoryDivisor;
            pEntry = static_cast<int16_t>(std::clamp(pv, -16384, 16384));

            // Continuation History (1, 2, 4, 6 plies back)
            int cbonus = bonus / 2;
            int plies_back[4] = {1, 2, 4, 6};
            for (int i = 0; i < 4; ++i) {
              int pb = plies_back[i];
              if (ply >= pb && !moveIsNone((ss - pb)->currentMove)) {
                int prevTo = moveTo((ss - pb)->currentMove);
                int prevPiece = (ss - pb)->movedPiece;
                if (prevPiece != EMPTY && prevPiece < PIECE_NB) {
                  int16_t &centry = thread->contHistory[i][prevPiece][prevTo]
                                                       [movedPiece][moveTo(m)];
                  int v = static_cast<int>(centry);
                  v += cbonus -
                       v * std::abs(cbonus) / Search::Tune::HistoryDivisor;
                  centry = static_cast<int16_t>(std::clamp(v, -16384, 16384));
                }
              }
            }
          }
        }

        alpha = sc;

        // PV Node - Update Triangular PV Array only when inside window (true PV node)
        if (alpha < beta) {
          ss->pv[0] = m;
          int childLen = (ss + 1)->pvLength;
          if (childLen > MAX_PLY - 1)
            childLen = MAX_PLY - 1;
          for (int i = 0; i < childLen; ++i) {
            ss->pv[i + 1] = (ss + 1)->pv[i];
          }
          ss->pvLength = childLen + 1;
        } else {
          ss->pvLength = 0;
        }

        if (alpha >= beta) {
          // Beta cutoff
          int side = us;
          int bonus = std::min(depth * depth, Search::Tune::HistoryBonusMax);

          if (!isCapture && !isPromotion) {
            // Update killers
            if (thread->killers.killer[0][ply] == MOVE_NONE ||
                !(thread->killers.killer[0][ply] == m)) {
              thread->killers.killer[1][ply] = thread->killers.killer[0][ply];
              thread->killers.killer[0][ply] = m;
            }

            // CounterMove heuristic
            if (ply > 0 && !moveIsNone((ss - 1)->currentMove)) {
              int prevTo = moveTo((ss - 1)->currentMove);
              int prevPiece = (ss - 1)->movedPiece;
              if (prevPiece != EMPTY && prevPiece < PIECE_NB)
                thread->counterMoves[prevPiece][prevTo] = m;
            }

            int malus = bonus;
            for (int q = 0; q < quietCount; q++) {
              if (!(quietsSearched[q] == m)) {
                int qFrom = moveFrom(quietsSearched[q]);
                int qTo = moveTo(quietsSearched[q]);
                int &he = thread->history_heur[side][qFrom][qTo];
                he += -malus - he * malus / Search::Tune::HistoryDivisor;

                int qMovedPiece = quietsMovedPiece[q];
                if (qMovedPiece != EMPTY && qMovedPiece < PIECE_NB) {
                  // Pawn History
                  int16_t &pEntry =
                      thread->pawnHistory[parentPawnHistoryIndex][qMovedPiece][qTo];
                  int pv = static_cast<int>(pEntry);
                  pv += -malus - pv * malus / Search::Tune::HistoryDivisor;
                  pEntry = static_cast<int16_t>(std::clamp(pv, -16384, 16384));

                  // Continuation History
                  int cmalus = malus / 2;
                  int plies_back[4] = {1, 2, 4, 6};
                  for (int i = 0; i < 4; ++i) {
                    int pb = plies_back[i];
                    if (ply >= pb && !moveIsNone((ss - pb)->currentMove)) {
                      int prevTo = moveTo((ss - pb)->currentMove);
                      int prevPiece = (ss - pb)->movedPiece;
                      if (prevPiece != EMPTY && prevPiece < PIECE_NB) {
                        int16_t &ce = thread->contHistory[i][prevPiece][prevTo]
                                                         [qMovedPiece][qTo];
                        int v = static_cast<int>(ce);
                        v +=
                            -cmalus - v * cmalus / Search::Tune::HistoryDivisor;
                        ce = static_cast<int16_t>(std::clamp(v, -16384, 16384));
                      }
                    }
                  }
                }
              }
            }
          }

          // Capture history bonus/malus in dedicated table.
          if (isCapture) {
            int movedPiece = currentMovedPiece;
            int capturedPiece = currentCapturedPiece;
            int capturedType = Search::captureTypeIndex(capturedPiece);
            if (movedPiece != EMPTY && movedPiece < PIECE_NB &&
                capturedType >= 0) {
              int16_t &ce =
                  thread->captureHistory[movedPiece][moveTo(m)][capturedType];
              int cv = static_cast<int>(ce);
              cv += bonus -
                    cv * std::abs(bonus) / Search::Tune::CaptureHistoryDivisor;
              cv = std::clamp(cv, -16384, 16384);
              ce = static_cast<int16_t>(cv);
            }

            int malus = bonus;
            for (int c = 0; c < capCount; c++) {
              if (!(capsSearched[c] == m)) {
                int ct = moveTo(capsSearched[c]);
                int cmoved = capsMovedPiece[c];
                int ccType = capsCapturedType[c];
                if (cmoved != EMPTY && cmoved < PIECE_NB && ccType >= 0) {
                  int16_t &cme = thread->captureHistory[cmoved][ct][ccType];
                  int cv2 = static_cast<int>(cme);
                  cv2 += -malus -
                         cv2 * malus / Search::Tune::CaptureHistoryDivisor;
                  cv2 = std::clamp(cv2, -16384, 16384);
                  cme = static_cast<int16_t>(cv2);
                }
              }
            }
          }

          break;
        }
      }
    }
  }

  if (!hasLegalMove) {
    return isInCheck ? (-MATE_SCORE + ply) : 0; // Checkmate ou Stalemate
  }

  // Store in TT (with eval and PV flag)
  // The save() function preserves the old move when mv==0.
  // Skip storing during singular extension searches (excludedMove set)
  // to avoid corrupting parent search's TT entry.
  // Uses the writer from the initial probe to avoid double-probing.
  if (moveIsNone(excludedMove)) {
    if (!isInCheck && depth >= 2 && std::abs(best) < MATE_IN_MAX &&
        rawEval != EVAL_NONE) {
      int pureEval = static_cast<int>(rawEval);
      int bonus = std::clamp(best - pureEval, -Search::Tune::CorHistBonusMax,
                             Search::Tune::CorHistBonusMax);
      int weight = std::min(Search::Tune::CorHistWeightBase + depth * depth,
                            Search::Tune::CorHistWeightMax);
      int pawnHash = pawnKey % 16384;
      int nonPawnHash = (hash ^ (pawnKey >> 16)) % 16384;

      // Update Pawn CorHist
      int16_t &entryPawn = thread->corHist[white_to_move ? WHITE : BLACK][pawnHash];
      int newValPawn = entryPawn + weight *
                               (bonus * Search::Tune::CorHistDivisor / 2 - entryPawn) /
                               16384;
      entryPawn = static_cast<int16_t>(std::clamp(newValPawn, -32767, 32767));

      // Update Non-Pawn CorHist
      int16_t &entryNP = thread->nonPawnCorHist[white_to_move ? WHITE : BLACK][nonPawnHash];
      int newValNP = entryNP + weight *
                             (bonus * Search::Tune::CorHistDivisor / 2 - entryNP) /
                             16384;
      entryNP = static_cast<int16_t>(std::clamp(newValNP, -32767, 32767));
    }

    TTFlag flag;
    if (best >= beta)
      flag = TT_BETA;
    else if (best > originalAlpha)
      flag = TT_EXACT;
    else
      flag = TT_ALPHA;

    int storeScore = best;
    if (best >= MATE_IN_MAX)
      storeScore += ply;
    if (best <= -MATE_IN_MAX)
      storeScore -= ply;

    uint16_t packedMove = moveIsNone(bestMove) ? 0 : bestMove.data;

    ttProbe.writer.save(hash, static_cast<int16_t>(storeScore), pvNode, flag,
                        depth, packedMove, rawEval, TT.generation());
  }

  return best;
}

// Iterative Deepening Search (Lazy SMP aware)
// Main thread (idx==0): orchestrates helpers, manages time, prints info
// Helper threads: search until Threads.stop is set
Move Position::search(int maxDepth, int timeMs) {
  bool isMainThread = (!thread || thread->idx == 0);

  // If stopSearching triggers inside deep recursion (pvs/qsearch),
  // we must not accept partially-computed scores as a valid iteration.
  // We'll use stopSearching itself (already a member) to avoid introducing
  // new undefined identifiers.

  nodes = 0;
  selDepth = 0;
  stopSearching = false;
  rootPV.clear();

  start_time = std::chrono::high_resolution_clock::now();
  time_limit_ms = timeMs;

  // ponderhit is asynchronous. The per-position limit remains the immutable
  // value passed at search start; the pool publishes the post-ponder limit.
  const auto hasActiveTimeLimit = [&]() {
    return time_limit_ms > 0 ||
           (isMainThread && thread &&
            Threads.searchTimeMs.load(std::memory_order_acquire) > 0);
  };

  // TT.newSearch() is called once in ThreadPool::startThinking().
  // Do NOT call it again here — doing so double-increments generation8,
  // making recent TT entries appear older and accelerating their eviction.

  // Initialize search stack for improving detection
  SearchStack stack[MAX_PLY + 7] = {};
  SearchStack *ss = stack + 7;
  for (int i = 0; i < 7; ++i) {
    (ss - i)->currentMove = MOVE_NONE;
    (ss - i)->excludedMove = MOVE_NONE;
    (ss - i)->movedPiece = EMPTY;
  }
  ss->ply = 0;

  // Dynamic contempt based on material evaluation
  // When we're winning, we should STRONGLY avoid draws/repetitions
  // ==========================================================
  // EARLY EXIT FOR OBVIOUS MOVES (main thread only)
  // ==========================================================

  Move legalMoves[MAX_MOVES];
  int numLegalMoves = 0;
  int effectiveMaxDepth = maxDepth;

  if (isMainThread) {
    // Generate legal moves
    numLegalMoves = generateLegal(legalMoves);

    // Emergency clock path. At a 1 ms hard allocation even a nominal depth-1
    // search cannot be guaranteed to finish and leave time for UCI delivery.
    // Prefer a legal TT move when available, otherwise return the first legal
    // move. Surviving on the clock is the only meaningful objective here.
    if (hasActiveTimeLimit() && TimeMgr.maximum() <= 1 && numLegalMoves > 0) {
      Move emergencyMove = legalMoves[0];
      const TTProbe emergencyProbe = TT.probe(hash);
      if (emergencyProbe.found && !moveIsNone(emergencyProbe.data.move)) {
        const Move storedMove = emergencyProbe.data.move;
        for (int i = 0; i < numLegalMoves; ++i) {
          if (sameMoveIdentity(legalMoves[i], storedMove)) {
            emergencyMove = legalMoves[i];
            break;
          }
        }
      }

      rootPV.assign(1, emergencyMove);
      if (thread) {
        thread->bestMove = emergencyMove;
        thread->bestScore = 0;
        thread->completedDepth = 1;
        thread->completedPV = rootPV;
        thread->hasCompletedIteration = true;
      }
      return emergencyMove;
    }

    // Only one legal move - play it immediately after minimal search
    if (numLegalMoves == 1) {
      selDepth = 1;
      const Move onlyMove = legalMoves[0];
      const int onlyMovedPiece = piece_board[moveFrom(onlyMove)];

      // SearchStack stores the move on the parent entry. Initialize the child
      // completely so ply-dependent draw and mate scores match the main path.
      ss->currentMove = onlyMove;
      ss->movedPiece = onlyMovedPiece;
      SearchStack* child = ss + 1;
      child->ply = 1;
      child->pvLength = 0;
      child->staticEval = -INF_SCORE;
      child->currentMove = MOVE_NONE;
      child->excludedMove = MOVE_NONE;
      child->movedPiece = EMPTY;
      child->doubleExtensions = 0;

      makeMove(onlyMove);
      int score = -qsearch(-INF_SCORE, INF_SCORE, child);
      undoMove(onlyMove);
      nodes++;

      auto now_time = std::chrono::high_resolution_clock::now();
      long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now_time - start_time)
                         .count();
      if (ms == 0)
        ms = 1;

      std::cout << "info depth 1 seldepth " << selDepth << " "
                << Search::uciScore(score) << " time "
                << ms << " nodes " << nodes << " nps " << (nodes * 1000 / ms)
                << " pv " << moveToUCI(onlyMove) << std::endl;

      if (thread) {
        thread->bestMove = onlyMove;
        thread->bestScore = score;
        thread->completedDepth = 1;
        thread->completedPV.assign(1, onlyMove);
        thread->hasCompletedIteration = true;
      }
      return onlyMove;
    }

  } else {
    // Helper threads: generate legal moves for TT validation
    numLegalMoves = generateLegal(legalMoves);
  }

  Move best = MOVE_NONE;
  Move prevBest = MOVE_NONE;
  int prevScore = 0;

  // CRITICAL: Always search to at least depth 4 to avoid blunders
  int minimumDepth = 4;

  // ================================================================
  // ================================================================
  double timeReduction = 1.0;
  double totBestMoveChanges = 0;
  int lastBestMoveDepth = 0;
  int iterValue[4] = {0, 0, 0, 0}; // Rolling window of iteration scores
  int iterIdx = 0;
  int completedDepthLocal = 0;

  // Initialize iterValue from previous search's score
  if (isMainThread && thread) {
    if (thread->bestPreviousScore > -INF_SCORE) {
      for (int i = 0; i < 4; i++)
        iterValue[i] = thread->bestPreviousScore;
    }
  }

  // Initialize rootMoveCount for effort tracking
  rootMoveCount = numLegalMoves;
  std::memcpy(rootLegalMoves, legalMoves, sizeof(Move) * numLegalMoves);
  std::memset(rootMoveEffort, 0, sizeof(rootMoveEffort));
  for (int i = 0; i < numLegalMoves; i++)
    rootMoveAvgScore[i] = -INF_SCORE;

  // Reset bestMoveChanges for this search
  if (thread)
    thread->bestMoveChanges.store(0, std::memory_order_relaxed);

  for (int d = 1; d <= effectiveMaxDepth && !stopSearching; d++) {
    if (isMainThread)
      totBestMoveChanges /= 2;

    // Asymmetrical Helpers: Depth staggering for helpers
    // Helpers skip depth 1 (or more) to avoid duplicating root node work
    // instantly
    if (!isMainThread && d == 1)
      continue;
    if (!isMainThread && thread && thread->idx > 1 && d == 2)
      continue;

    // Check global stop flag
    if (Threads.stop.load(std::memory_order_relaxed)) {
      stopSearching = true;
      break;
    }

    selDepth = 0;

    // Reset rootBestMove for this iteration so we don't use a
    // stale move from a previous depth if the search is aborted
    rootBestMove = MOVE_NONE;
    rootBestScore = -INF_SCORE;

    int alpha = -INF_SCORE, beta = INF_SCORE;
    int threadId = thread ? static_cast<int>(thread->idx) : 0;

    // Aspiration windows: same formula for ALL threads (Stockfish pattern).
    // threadIdx % 8 provides subtle per-thread diversification (0-7cp).
    // This is much better than the old delta*(1+threadId) which forced
    // helpers into near full-width search, wasting nodes.
    int delta = Search::Tune::AspWindowBase +
                threadId % Search::Tune::AspWindowThreadMult;

    if (d >= 4) {
      alpha = std::max(prevScore - delta, -INF_SCORE);
      beta = std::min(prevScore + delta, INF_SCORE);
    }

    while (true) {
#ifdef ENABLE_SEARCH_STATS
      const uint64_t aspirationNodesBefore =
          nodes.load(std::memory_order_relaxed);
#endif
      DB_STAT_INC(aspirationAttempts);
      int score = pvs(d, alpha, beta, ss, false);

      // If the search aborted due to stop/time-up inside deep recursion,
      // do not treat partially-computed results as a valid iteration.
      if (stopSearching)
        break;

      if (score <= alpha) {
        DB_STAT_INC(aspirationFailLow);
        DB_STAT_INC(aspirationResearches);
#ifdef ENABLE_SEARCH_STATS
        if (thread && thread->collectSearchStats)
          thread->searchStats.aspirationDiscardedNodes +=
              nodes.load(std::memory_order_relaxed) - aspirationNodesBefore;
#endif
        // Fail-low: contract beta to mid-point and widen alpha downward
        beta = (alpha + beta) / 2;
        alpha = std::max(score - delta, -INF_SCORE);
        delta += delta / 2;
        continue;
      }
      if (score >= beta) {
        DB_STAT_INC(aspirationFailHigh);
        DB_STAT_INC(aspirationResearches);
#ifdef ENABLE_SEARCH_STATS
        if (thread && thread->collectSearchStats)
          thread->searchStats.aspirationDiscardedNodes +=
              nodes.load(std::memory_order_relaxed) - aspirationNodesBefore;
#endif
        // Fail-high: contract alpha to safe boundary and widen beta upward
        alpha = std::max(beta - delta, alpha);
        beta = std::min(score + delta, INF_SCORE);
        delta += delta / 2;
        continue;
      }
      // If stopSearching triggered during the deep search, break immediately
      // to avoid overwriting `best` with a partial or uninitialized
      // rootBestMove.
      if (stopSearching)
        break;

      prevScore = score;

      // Use rootBestMove tracked directly inside pvs() at ply 0.
      // This is safe from multi-threading races because rootBestMove
      // is per-position (per-thread), unlike TT entries which are shared.
      if (rootBestMove != MOVE_NONE) {
        // Validate that rootBestMove is legal (defensive check)
        bool isLegalMove = false;
        for (int i = 0; i < numLegalMoves; i++) {
          if (legalMoves[i] == rootBestMove) {
            isLegalMove = true;
            break;
          }
        }
        if (isLegalMove) {
          best = rootBestMove;
        }
      }
      // Fallback to TT only if rootBestMove was not set
      if (best == MOVE_NONE) {
        const TTProbe rootProbe = TT.probe(hash);
        if (rootProbe.found) {
          Move ttBest = rootProbe.data.move;
          bool isLegalMove = false;
          for (int i = 0; i < numLegalMoves; i++) {
            if (sameMoveIdentity(legalMoves[i], ttBest)) {
              isLegalMove = true;
              ttBest = legalMoves[i]; // use generated move with correct flags
              break;
            }
          }
          if (isLegalMove) {
            best = ttBest;
          }
        }
      }

      std::vector<Move> pvLine;
      for (int i = 0; i < ss->pvLength; ++i) {
        pvLine.push_back(ss->pv[i]);
      }
      rootPV = pvLine;

      // Print info (main thread only)
      // Stockfish / Reckless safeguard: If the search was aborted due to time
      // and the score is surprisingly bad, it's likely a fail-low that we
      // didn't have time to resolve. DO NOT print it to the UCI to avoid
      // fake "blunder" illusions in the GUI.
      if (isMainThread && !(stopSearching && score < prevScore - 50)) {
        auto now = std::chrono::high_resolution_clock::now();
        long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - start_time)
                           .count();
        if (ms == 0)
          ms = 1;
        uint64_t totalNodes = Threads.nodes_searched();
        uint64_t nps = (totalNodes * 1000ULL) / static_cast<uint64_t>(ms);

        std::string pvStr = pvToString(pvLine);

        std::cout << "info depth " << d << " seldepth " << selDepth;

        std::cout << " " << Search::uciScore(score);

        std::cout << " time " << ms << " nodes " << totalNodes << " nps " << nps
                  << " hashfull " << TT.hashfull();
        if (Threads.size() > 1)
          std::cout << " threads " << Threads.size();
        if (!pvStr.empty()) {
          std::cout << " pv " << pvStr;
        } else if (!moveIsNone(best)) {
          std::cout << " pv " << moveToUCI(best);
        }
        std::cout << std::endl;
        std::cout.flush();
      }

      // Track best move and update lastBestMoveDepth
      if (best != prevBest) {
        prevBest = best;
        lastBestMoveDepth = d;
      }

      break;
    }

    if (stopSearching)
      break;

    // Mark this depth as completed
    completedDepthLocal = d;

    // Store completed depth for vote system
    if (thread) {
      thread->bestMove = best;
      thread->bestScore = prevScore;
      thread->completedDepth = d;
      thread->completedPV = rootPV;
      thread->hasCompletedIteration = true;
    }

    // ================================================================
    // ================================================================
    if (isMainThread && !stopSearching) {

      // A physical clock limit is always stronger than the preferred minimum
      // depth. The completed iteration above is already safe to publish.
      if (hasActiveTimeLimit() &&
          !Threads.ponder.load(std::memory_order_relaxed) &&
          TimeMgr.elapsed() >= TimeMgr.maximum()) {
        Threads.stop.store(true, std::memory_order_relaxed);
        break;
      }

      // Aggregate bestMoveChanges from all threads
      for (size_t t = 0; t < Threads.size(); t++) {
        totBestMoveChanges += static_cast<double>(
            Threads.at(t)->bestMoveChanges.load(std::memory_order_relaxed));
        Threads.at(t)->bestMoveChanges.store(0, std::memory_order_relaxed);
      }

      // Never stop before minimum depth
      if (d < minimumDepth) {
        continue;
      }

      // Do we have time for the next iteration? Can we stop searching now?
      if (hasActiveTimeLimit() && !Threads.stop.load(std::memory_order_relaxed)) {

        // --- nodesEffort: how many nodes were spent on the best root move ---
        // Find which root move index is the best move
        uint64_t bestMoveNodeCount = 0;
        for (int i = 0; i < numLegalMoves; i++) {
          if (legalMoves[i] == best) {
            bestMoveNodeCount = rootMoveEffort[i];
            break;
          }
        }
        uint64_t totalNodesNow = Threads.nodes_searched();
        uint64_t nodesEffort =
            bestMoveNodeCount * 100000 / std::max(uint64_t(1), totalNodesNow);

        // --- fallingEval: give more time when the score is dropping ---
        int bestPrevAvg =
            (thread && thread->bestPreviousAverageScore > -INF_SCORE)
                ? thread->bestPreviousAverageScore
                : prevScore;

        double fallingEval = (11.85 + 2.24 * (bestPrevAvg - prevScore) +
                              0.93 * (iterValue[iterIdx] - prevScore)) /
                             100.0;
        fallingEval = std::clamp(fallingEval, 0.57, 1.70);

        // --- timeReduction: stable best move = less time needed ---
        // Uses sigmoid based on how far completedDepth is from
        // lastBestMoveDepth
        double k = 0.51;
        double center = lastBestMoveDepth + 12.15;
        timeReduction =
            0.66 +
            0.85 / (0.98 + std::exp(-k * (completedDepthLocal - center)));

        // Cross-move momentum: blend with previous search's timeReduction
        double prevTR = (thread) ? thread->previousTimeReduction : 1.0;
        double reduction = (1.43 + prevTR) / (2.28 * timeReduction);

        // --- bestMoveInstability: more changes = more time ---
        double bestMoveInstability =
            1.02 + 2.14 * totBestMoveChanges /
                       static_cast<double>(std::max(size_t(1), Threads.size()));
        bestMoveInstability = std::clamp(bestMoveInstability, 0.8, 2.5);

        // --- highBestMoveEffort: if best move is very clear, save time ---
        double highBestMoveEffort = nodesEffort >= 93340 ? 0.76 : 1.0;

        // --- Compute dynamic total time ---
        double totalTime = TimeMgr.isFixedTime()
            ? static_cast<double>(TimeMgr.optimum())
            : static_cast<double>(TimeMgr.optimum()) *
              fallingEval * reduction * bestMoveInstability *
              highBestMoveEffort;

        if (!TimeMgr.isFixedTime() && numLegalMoves == 1)
          totalTime = std::min(502.0, totalTime);

        TimePoint elapsedTime = TimeMgr.elapsed();

        // Stop if exceeded totalTime or maximum
        if (static_cast<double>(elapsedTime) >
            std::min(totalTime, static_cast<double>(TimeMgr.maximum()))) {
          if (!Threads.ponder.load(std::memory_order_relaxed)) {
            Threads.stop.store(true, std::memory_order_relaxed);
            break;
          }
        }
      }

      // Store iterValue for this iteration
      iterValue[iterIdx] = prevScore;
      iterIdx = (iterIdx + 1) & 3;
    }
    // Helper threads: no time management, just check Threads.stop
  }

  // ================================================================
  // Post-search: save state for next move's time management
  // ================================================================
  if (isMainThread && thread) {
    thread->previousTimeReduction = timeReduction;
    thread->bestPreviousScore = prevScore;

    // Compute average score across all root moves that were searched
    int avgScore = prevScore; // Fallback to last score
    // Find best move's average score
    for (int i = 0; i < numLegalMoves; i++) {
      if (legalMoves[i] == best && rootMoveAvgScore[i] > -INF_SCORE) {
        avgScore = rootMoveAvgScore[i];
        break;
      }
    }
    thread->bestPreviousAverageScore = avgScore;
  }

  // Store final results for vote system.
  // If we aborted inside recursion, keep the last fully completed iteration's
  // results.
  if (thread) {
    if (!moveIsNone(best)) {
      thread->bestMove = best;
      thread->bestScore = prevScore;
    }
  }

  // Fallback: if no move was found, pick the first legal move
  if (moveIsNone(best)) {
    if (numLegalMoves > 0) {
      best = legalMoves[0];
      if (thread)
        thread->bestMove = best;
    }
  }

  return best;
}

std::string Position::pvToString(const std::vector<Move> &pv) {
  std::ostringstream ss;
  Position temp = *this; // Create a copy of the root position

  for (auto &m : pv) {
    if (moveIsNone(m))
      break;

    Move legalMoves[MAX_MOVES];
    int numLegalMoves = temp.generateLegal(legalMoves);
    Move matchedMove = MOVE_NONE;

    for (int i = 0; i < numLegalMoves; i++) {
      if (sameMoveIdentity(legalMoves[i], m)) {
        matchedMove = legalMoves[i];
        break;
      }
    }

    // If the move is not legal, truncate the PV
    if (moveIsNone(matchedMove))
      break;

    ss << temp.moveToUCI(matchedMove) << " ";
    temp.makeMove(matchedMove); // execute with canonical legal flags (castling, ep, etc.)
  }

  std::string result = ss.str();
  if (!result.empty() && result.back() == ' ') {
    result.pop_back(); // Remove trailing space
  }
  return result;
}
