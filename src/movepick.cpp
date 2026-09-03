//
// Correctness model (see movepick.h): all emitted moves originate from
// Position::generateLegal(), which produces fully legal moves. The TT
// move, killers and counter-move are resolved against the generated
// legal lists before being emitted, so the search always receives legal
// moves with correct flags. Scoring, SEE filtering and sorting are only
// paid for the moves actually consumed; skipQuiets() lets the search
// abandon the quiet stage entirely.

#include "movepick.h"
#include "thread.h"
#include "search.h"
#include <algorithm>

namespace {

inline int captureTypeIndex(int piece) {
    if (piece == EMPTY) return -1;
    return (piece - 1) % 6;
}

inline void initBuffers(Position& pos, int bufIdx, Move*& captures, int*& captureScores,
                        Move*& badCaptures, int*& badScores,
                        Move*& quiets, int*& quietScores) {
    MovePickerBuffer* buf = nullptr;
    int safeIdx = std::clamp(bufIdx, 0, 2 * MAX_PLY - 1);
    if (pos.thread) {
        buf = &pos.thread->movePickBuffer[safeIdx];
    } else {
        static thread_local MovePickerBuffer fallbackBuf[MAX_PLY * 2];
        buf = &fallbackBuf[safeIdx];
    }
    captures = buf->captures;
    captureScores = buf->captureScores;
    badCaptures = buf->badCaptures;
    badScores = buf->badScores;
    quiets = buf->quiets;
    quietScores = buf->quietScores;
}

} // anonymous namespace

// ========================= Constructors =========================

// Main search constructor
MovePicker::MovePicker(Position& position, const Move& tt, SearchStack* searchStack, int d,
                       const Move& k0, const Move& k1,
                       const Move& counter)
    : pos(position), ttMove(tt), ss(searchStack),
      ply(searchStack ? searchStack->ply : 0), killer0(k0), killer1(k1), counterMove(counter)
{
    (void)d;
    initBuffers(pos, ply, captures, captureScores, badCaptures, badScores, quiets, quietScores);
    stage = Stage::TT_MOVE;
}

// QSearch constructor
MovePicker::MovePicker(Position& position, const Move& tt, bool inCheck, int p)
    : pos(position), ttMove(tt), ply(p), inCheckQs(inCheck)
{
    initBuffers(pos, MAX_PLY + ply, captures, captureScores, badCaptures, badScores, quiets, quietScores);
    stage = Stage::QS_TT_MOVE;
}

// ProbCut constructor
MovePicker::MovePicker(Position& position, const Move& tt, int seeThreshold, int p)
    : pos(position), ttMove(tt), ply(p),
      probcutThreshold(seeThreshold)
{
    initBuffers(pos, MAX_PLY + ply, captures, captureScores, badCaptures, badScores, quiets, quietScores);
    stage = Stage::PC_TT_MOVE;
}

// ========================= Scoring =========================

void MovePicker::scoreCaptures() {
    int us = pos.sideToMove();
    for (int i = 0; i < captureCount; ++i) {
        Move& m = captures[i];
        int from_sq = moveFrom(m);
        int to_sq = moveTo(m);
        int mover = pos.piece_board[from_sq];
        int captured = moveIsEnPassant(m)
            ? (us == WHITE ? BPAWN : WPAWN)
            : pos.piece_board[to_sq];
        int promotion = movePromotionType(m);

        // MVV-LVA: prioritize capturing valuable pieces with cheap pieces
        int score = 10 * PIECE_VALUE[captured] - PIECE_VALUE[mover];

        // Add capture history
        int capturedType = captureTypeIndex(captured);
        if (mover != EMPTY && mover < PIECE_NB && capturedType >= 0) {
            score += pos.thread->captureHistory[mover][to_sq][capturedType];
        }

        // Promotion bonus
        if (promotion) {
            score += PIECE_VALUE[promotion];
            if (promotion == WQUEEN)
                score += 10000; // Queen promotions first
        }

        captureScores[i] = score;
    }
}

void MovePicker::scoreQuiets() {
    int us = pos.sideToMove();
    Position::Threats threats;
    pos.calcThreats(threats);

    for (int i = 0; i < quietCount; ++i) {
        Move& m = quiets[i];
        int from_sq = moveFrom(m);
        int to_sq = moveTo(m);

        // Butterfly history
        int score = pos.thread->history_heur[us][from_sq][to_sq];
        int movedPiece = pos.piece_board[from_sq];
        
        if (movedPiece != EMPTY && movedPiece < PIECE_NB) {
            U64 fromBB = square_bb(from_sq);
            U64 toBB = square_bb(to_sq);
            int pt = isWhitePiece(movedPiece) ? movedPiece : (movedPiece - 6);

            // Threat-Aware Move Ordering (Normalized & Saturated)
            if (pt != 1) { // not pawn
                int threatBonus = 0;
                if (threats.byPawn & fromBB) {
                    threatBonus = 16384; // Highest priority: piece attacked by pawn
                } else if (threats.byMinor & fromBB) {
                    threatBonus = (pt > 3) ? 12288 : 8192; // Queen/Rook escaping minor piece
                } else if (threats.byRook & fromBB) {
                    if (pt > 4) threatBonus = 8192; // Queen escaping Rook
                }

                // Penalize moving piece to a square attacked by pawn or minor
                if (threats.byPawn & toBB) {
                    threatBonus -= 16384;
                } else if ((threats.byMinor & toBB) && pt > 3) {
                    threatBonus -= 8192;
                }

                // Saturated clamp on threat delta to protect history/killer balance
                score += std::clamp(threatBonus, -16384, 16384);
            }

            // Pawn history
            score += pos.thread->pawnHistory[pos.pawnKey & 8191][movedPiece][to_sq];
            
            // Continuation history
            if (ss != nullptr) {
                int plies_back[4] = {1, 2, 4, 6};
                for (int l = 0; l < 4; ++l) {
                    int pb = plies_back[l];
                    if (ply >= pb && !moveIsNone((ss - pb)->currentMove)) {
                        int prevTo = moveTo((ss - pb)->currentMove);
                        int prevPiece = (ss - pb)->movedPiece;
                        if (prevPiece != EMPTY && prevPiece < PIECE_NB) {
                            score += pos.thread->contHistory[l][prevPiece][prevTo][movedPiece][to_sq];
                        }
                    }
                }
            }
        }

        quietScores[i] = score;
    }
}

// ========================= Selection Sort =========================

Move MovePicker::selectBest(Move* list, int* scores, int count, int& idx) {
    while (idx < count) {
        int best = idx;
        for (int j = idx + 1; j < count; ++j) {
            if (scores[j] > scores[best]) best = j;
        }
        if (best != idx) {
            std::swap(list[idx], list[best]);
            std::swap(scores[idx], scores[best]);
        }
        return list[idx++];
    }
    return MOVE_NONE;
}

// ========================= Special-move helpers =========================


// ========================= skipQuiets =========================

void MovePicker::skipQuiets() {
    quietsSkipped = true;
    // If we are currently streaming quiets, abandon them and move on to
    // the deferred bad captures.
    if (stage == Stage::QUIETS || stage == Stage::QUIET_INIT) {
        stage = Stage::BAD_CAPTURES;
    }
}

// ========================= Main next() Dispatch =========================

Move MovePicker::next() {
    Move m;

    while (true) {
        switch (stage) {

        // ==================== MAIN SEARCH ====================

        case Stage::TT_MOVE:
            stage = Stage::CAPTURE_INIT;
            if (!moveIsNone(ttMove)) {
                if (pos.isPseudoLegal(ttMove) && pos.legalMove(ttMove)) {
                    lastSee = SeeStatus::UNKNOWN;  // TT move SEE not evaluated here
                    return ttMove;
                }
            }
            break;

        case Stage::CAPTURE_INIT:
            // Generate only captures
            captureCount = pos.generateLegal(captures, GEN_CAPTURES);
            // Remove TT move from capture list if present
            if (!moveIsNone(ttMove)) {
                for (int i = 0; i < captureCount; ++i) {
                    if (captures[i] == ttMove) {
                        captures[i] = captures[--captureCount];
                        break;
                    }
                }
            }
            scoreCaptures();
            captureIdx = 0;
            stage = Stage::GOOD_CAPTURES;
            break;

        case Stage::GOOD_CAPTURES:
            while (captureIdx < captureCount) {
                m = selectBest(captures, captureScores, captureCount, captureIdx);
                if (moveIsNone(m)) break;
                // Defer SEE-losing captures to BAD_CAPTURES.
                if (!pos.SEE(m, 0)) {
                    int sIdx = captureIdx - 1;          // position of m after selectBest
                    badCaptures[badCount] = m;
                    badScores[badCount++] = captureScores[sIdx];
                    continue;
                }
                lastSee = SeeStatus::YES;  // passed SEE(m, 0): SEE >= 0 confirmed
                return m;
            }
            stage = Stage::KILLER_0;
            break;

        case Stage::KILLER_0:
            stage = Stage::KILLER_1;
            if (!moveIsNone(killer0) && killer0 != ttMove) {
                if (!moveIsCapture(killer0) && !movePromotionType(killer0)) {
                    if (pos.isPseudoLegal(killer0) && pos.legalMove(killer0)) {
                        lastSee = SeeStatus::UNKNOWN;  // quiet move (non-capture)
                        return killer0;
                    }
                }
            }
            break;

        case Stage::KILLER_1:
            stage = Stage::COUNTER_MOVE;
            if (!moveIsNone(killer1) && killer1 != ttMove && killer1 != killer0) {
                if (!moveIsCapture(killer1) && !movePromotionType(killer1)) {
                    if (pos.isPseudoLegal(killer1) && pos.legalMove(killer1)) {
                        lastSee = SeeStatus::UNKNOWN;  // quiet move (non-capture)
                        return killer1;
                    }
                }
            }
            break;

        case Stage::COUNTER_MOVE:
            stage = quietsSkipped ? Stage::BAD_CAPTURES : Stage::QUIET_INIT;
            if (!moveIsNone(counterMove) && counterMove != ttMove
                && counterMove != killer0 && counterMove != killer1) {
                if (!moveIsCapture(counterMove) && !movePromotionType(counterMove)) {
                    if (pos.isPseudoLegal(counterMove) && pos.legalMove(counterMove)) {
                        lastSee = SeeStatus::UNKNOWN;  // quiet move (non-capture)
                        return counterMove;
                    }
                }
            }
            break;

        case Stage::QUIET_INIT:
            if (quietsSkipped) {
                stage = Stage::BAD_CAPTURES;
                break;
            }
            // Direct generation of ONLY quiets into quiets buffer
            {
                int rawCount = pos.generateLegal(quiets, GEN_QUIETS);
                quietCount = 0;
                for (int i = 0; i < rawCount; ++i) {
                    Move mv = quiets[i];
                    if (mv == ttMove || mv == killer0 || mv == killer1 || mv == counterMove) continue;
                    quiets[quietCount++] = mv;
                }
            }
            scoreQuiets();
            quietIdx = 0;
            stage = Stage::QUIETS;
            break;

        case Stage::QUIETS:
            m = selectBest(quiets, quietScores, quietCount, quietIdx);
            if (!moveIsNone(m)) { lastSee = SeeStatus::UNKNOWN; return m; }  // quiet move
            stage = Stage::BAD_CAPTURES;
            break;

        case Stage::BAD_CAPTURES: {
            Move bm = selectBest(badCaptures, badScores, badCount, badIdx);
            if (!moveIsNone(bm)) { lastSee = SeeStatus::NO; return bm; }  // failed SEE(m, 0): SEE < 0
            stage = Stage::DONE;
            break;
        }

        // ==================== QSEARCH ====================

        case Stage::QS_TT_MOVE:
            stage = inCheckQs ? Stage::QS_EVASION_INIT : Stage::QS_CAPTURE_INIT;
            if (!moveIsNone(ttMove)) {
                if (pos.isPseudoLegal(ttMove) && pos.legalMove(ttMove)) {
                    return ttMove;
                }
            }
            break;

        case Stage::QS_CAPTURE_INIT:
            captureCount = pos.generateLegal(captures, GEN_CAPTURES);
            if (!moveIsNone(ttMove)) {
                for (int i = 0; i < captureCount; ++i) {
                    if (captures[i] == ttMove) {
                        captures[i] = captures[--captureCount];
                        break;
                    }
                }
            }
            scoreCaptures();
            captureIdx = 0;
            stage = Stage::QS_CAPTURES;
            break;

        case Stage::QS_CAPTURES:
            m = selectBest(captures, captureScores, captureCount, captureIdx);
            if (!moveIsNone(m)) return m;
            stage = Stage::DONE;
            break;

        case Stage::QS_EVASION_INIT:
            {
                int rawCaptures = pos.generateLegal(captures, GEN_CAPTURES);
                captureCount = 0;
                for (int i = 0; i < rawCaptures; ++i) {
                    if (captures[i] == ttMove) continue;
                    captures[captureCount++] = captures[i];
                }
                int rawQuiets = pos.generateLegal(quiets, GEN_QUIETS);
                quietCount = 0;
                for (int i = 0; i < rawQuiets; ++i) {
                    if (quiets[i] == ttMove) continue;
                    quiets[quietCount++] = quiets[i];
                }
            }
            // Score captures (MVV-LVA) above quiets (history).
            {
                int us = pos.white_to_move ? WHITE : BLACK;
                for (int i = 0; i < captureCount; ++i) {
                    Move& mv = captures[i];
                    int from_sq = moveFrom(mv);
                    int to_sq = moveTo(mv);
                    int mover = pos.piece_board[from_sq];
                    int captured = moveIsEnPassant(mv)
                        ? (us == WHITE ? BPAWN : WPAWN)
                        : pos.piece_board[to_sq];
                    captureScores[i] = 10 * PIECE_VALUE[captured] - PIECE_VALUE[mover] + 100000;
                }
                for (int i = 0; i < quietCount; ++i) {
                    Move& mv = quiets[i];
                    int from_sq = moveFrom(mv);
                    int to_sq = moveTo(mv);
                    int movedPiece = pos.piece_board[from_sq];
                    int score = pos.thread->history_heur[us][from_sq][to_sq];
                    if (movedPiece != EMPTY && movedPiece < PIECE_NB) {
                        score += pos.thread->pawnHistory[pos.pawnKey & 8191][movedPiece][to_sq];
                        if (ss != nullptr) {
                            int plies_back[4] = {1, 2, 4, 6};
                            for (int l = 0; l < 4; ++l) {
                                int pb = plies_back[l];
                                if (ply >= pb && !moveIsNone((ss - pb)->currentMove)) {
                                    int prevTo = moveTo((ss - pb)->currentMove);
                                    int prevPiece = (ss - pb)->movedPiece;
                                    if (prevPiece != EMPTY && prevPiece < PIECE_NB) {
                                        score += pos.thread->contHistory[l][prevPiece][prevTo][movedPiece][to_sq];
                                    }
                                }
                            }
                        }
                    }
                    quietScores[i] = score;
                }
            }
            captureIdx = 0;
            quietIdx = 0;
            stage = Stage::QS_EVASIONS;
            break;

        case Stage::QS_EVASIONS:
            m = selectBest(captures, captureScores, captureCount, captureIdx);
            if (!moveIsNone(m)) return m;
            m = selectBest(quiets, quietScores, quietCount, quietIdx);
            if (!moveIsNone(m)) return m;
            stage = Stage::DONE;
            break;

        // ==================== PROBCUT ====================

        case Stage::PC_TT_MOVE:
            stage = Stage::PC_CAPTURE_INIT;
            if (!moveIsNone(ttMove) && moveIsCapture(ttMove)) {
                if (pos.isPseudoLegal(ttMove) && pos.legalMove(ttMove)) {
                    if (pos.SEE(ttMove, probcutThreshold)) {
                        return ttMove;
                    }
                }
            }
            break;

        case Stage::PC_CAPTURE_INIT:
            captureCount = pos.generateLegal(captures, GEN_CAPTURES);
            if (!moveIsNone(ttMove)) {
                for (int i = 0; i < captureCount; ++i) {
                    if (captures[i] == ttMove) {
                        captures[i] = captures[--captureCount];
                        break;
                    }
                }
            }
            scoreCaptures();
            captureIdx = 0;
            stage = Stage::PC_CAPTURES;
            break;

        case Stage::PC_CAPTURES:
            while (captureIdx < captureCount) {
                m = selectBest(captures, captureScores, captureCount, captureIdx);
                if (moveIsNone(m)) break;
                if (pos.SEE(m, probcutThreshold)) {
                    return m;
                }
            }
            stage = Stage::DONE;
            break;

        // ==================== TERMINAL ====================

        case Stage::DONE:
        default:
            return MOVE_NONE;
        }
    }
}

