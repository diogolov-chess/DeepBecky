#ifndef DEEPBECKY_THREAD_H
#define DEEPBECKY_THREAD_H

#include "position.h"
#ifdef ENABLE_SEARCH_STATS
#include "searchstats.h"
#endif
#include <cstdint>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

struct MovePickerBuffer {
    Move captures[MAX_MOVES];
    int captureScores[MAX_MOVES];
    Move badCaptures[MAX_MOVES];
    int badScores[MAX_MOVES];
    Move quiets[MAX_MOVES];
    int quietScores[MAX_MOVES];
};

#include <cstdlib>
#include <malloc.h>

// ========================= SearchThread =========================
// Each thread owns its own Position copy and per-thread heuristic tables.
// Threads communicate only through the shared TT (lockless).
struct alignas(64) SearchThread {
    void* operator new(size_t size) {
#ifdef _WIN32
        return _aligned_malloc(size, 64);
#else
        void* ptr = nullptr;
        posix_memalign(&ptr, 64, size);
        return ptr;
#endif
    }

    void operator delete(void* ptr) noexcept {
#ifdef _WIN32
        _aligned_free(ptr);
#else
        free(ptr);
#endif
    }

    Position pos;
    size_t idx;

    // Per-thread heuristic tables (no sharing = no contention)
    KillerTable killers;
    int history_heur[2][64][64]{};
    int16_t corHist[COLOR_NB][16384]{};
    int16_t nonPawnCorHist[COLOR_NB][16384]{};
    int16_t contHistory[4][PIECE_NB][64][PIECE_NB][64]{}; // [0=1ply, 1=2ply, 2=4ply, 3=6ply][prevPiece][prevTo][piece][to]
    int16_t pawnHistory[8192][PIECE_NB][64]{}; // [pawnHash][piece][to]
    int16_t captureHistory[PIECE_NB][64][6]{};          // [piece][to][capturedType]
    Move counterMoves[PIECE_NB][64];   // CounterMove heuristic: best quiet reply to (piece, to_sq)
    PawnEntry pawnTable[PAWN_TT_SIZE]{};
    MovePickerBuffer movePickBuffer[MAX_PLY * 2]{};

    // When doing verification search after null move cutoff at high depth,
    // null move is temporarily disabled for `nmpColor` until ply > nmpMinPly.
    int nmpMinPly = 0;
    int nmpColor = 0;  // 0 = WHITE, 1 = BLACK

#ifdef ENABLE_SEARCH_STATS
    SearchStats searchStats;
    bool collectSearchStats = false;
#endif

    // Search results (read by main thread after search completes)
    Move bestMove = MOVE_NONE;
    int bestScore = -INF_SCORE;
    int completedDepth = 0;
    // Snapshot published only after a fully completed root iteration.
    // It prevents the final Lazy SMP selection from mixing a completed score
    // with a PV from an iteration interrupted by stop/time.
    std::vector<Move> completedPV;
    bool hasCompletedIteration = false;

    std::atomic<uint64_t> bestMoveChanges{0};  // How many times best move changed this iteration
    double previousTimeReduction = 1.0;        // Persisted across moves (momentum)
    int bestPreviousScore = -INF_SCORE;        // Previous move's best score
    int bestPreviousAverageScore = -INF_SCORE; // Previous move's average score

    // Thread management
    std::mutex mtx;
    std::condition_variable cv;
    bool searching = false;
    bool exitFlag = false;
    std::thread nativeThread;

    SearchThread(size_t index);
    ~SearchThread();

    void idle_loop();
    void start_searching();
    void wait_for_search_finished();
    void clear();
};

// ========================= ThreadPool =========================
// Manages all search threads. Lazy SMP: all threads search the same
// root position independently; the TT is the only communication channel.
class ThreadPool {
    std::vector<SearchThread*> threads_;

public:
    std::atomic<bool> stop{false};

    // Search parameters (set by cmdGo before waking main thread)
    int searchMaxDepth = 64;
    int searchTimeMs = 0;

    // Ponder support
    std::atomic<bool> ponder{false};   // Currently in ponder mode (don't print bestmove)
    bool ponderEnabled = false;        // UCI option "Ponder" enabled
    bool lazySmpDebug = false;         // UCI diagnostic; disabled in normal play
#ifdef ENABLE_SEARCH_STATS
    bool searchStatsEnabled = false;
#endif

    ~ThreadPool();

    void set(size_t num);
    void clear();
    void waitForSearchFinished();

    // Setup all threads with root position, then start main thread
    void startThinking(Position& rootPos, int maxDepth, int timeMs, bool ponderMode = false);

    SearchThread* main() const { return threads_.empty() ? nullptr : threads_[0]; }
    SearchThread* at(size_t i) const { return threads_[i]; }
    size_t size() const { return threads_.size(); }
    uint64_t nodes_searched() const;
    bool runLazySmpSelectionTests() const;
};

extern ThreadPool Threads;

#endif // DEEPBECKY_THREAD_H
