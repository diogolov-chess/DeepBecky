#ifndef DEEPBECKY_POSITION_H
#define DEEPBECKY_POSITION_H

#include "types.h"
#include "bitboard.h"
#include "nnue.h"
#include <atomic>
#include <vector>
#include <string>
#include <chrono>
#include <unordered_map>

// Forward declarations
struct TTEntry;
struct SearchStack;
struct SearchThread;

// ============================================================================
// Zobrist Hashing Keys
// ============================================================================
struct Zobrist {
    uint64_t piece[PIECE_NB][64]{};
    uint64_t side = 0;
    uint64_t castling[16]{};
    uint64_t ep[9]{};
    Zobrist();
};
extern Zobrist ZOB;

// ============================================================================
// Hash Table Cache Structures
// ============================================================================
constexpr int PAWN_TT_SIZE = 1 << 16;      // 64K entries for pawn structure cache
constexpr int MATERIAL_TT_SIZE = 1 << 14;  // 16K entries for material configuration cache

struct PawnEntry {
    uint64_t key = 0;
    int16_t scoreMG = 0;
    int16_t scoreEG = 0;
};

struct MaterialEntry {
    uint64_t key = 0;
    int16_t scoreMG = 0;
    int16_t scoreEG = 0;
    int8_t phase = 0;
    int8_t flags = 0;  // bit 0: white bishop pair, bit 1: black bishop pair
};

// ============================================================================
// Search Heuristics
// ============================================================================
struct KillerTable {
    Move killer[2][MAX_PLY];
    void clear() { std::memset(killer, 0, sizeof(killer)); }
};

// ============================================================================
// Position Representation & Game State
// ============================================================================
class Position {
public:
    // Board Representation
    U64 bitboards[PIECE_NB]{};       // Bitboard per piece type
    U64 color_bitboards[COLOR_NB]{}; // Bitboard per color (occupancies)
    int piece_board[64]{};           // Mailbox lookup per square

    // Game State
    bool white_to_move = true;
    int castling = 0b1111;  // Bitfield: KQkq
    int ep_file = 0;        // 1..8 if en-passant square is active
    int king_sq[COLOR_NB]{4, 60};
    int halfmove = 0;
    int fullmove = 1;
    uint64_t hash = 0;
    uint64_t pawnKey = 0;
    uint64_t materialKey = 0;

    // Repetition History
    struct RepState {
        uint64_t key = 0;
        int repetition = 0;
    };
    RepState repetitionHistory[MAX_STACK];
    int repHistSize = 0;
    int plies_since_null = 0;

    // Thread ownership (set when used inside SearchThread)
    SearchThread* thread = nullptr;

    // Search State
    bool rootSideIsWhite = true;
    std::atomic<int64_t> nodes{0};
    int selDepth = 0;
    bool stopSearching = false;
    std::chrono::high_resolution_clock::time_point start_time;
    int time_limit_ms = 0;
    
    // Root best move - tracked directly inside pvs() at ply 0 to prevent TT race conditions
    Move rootBestMove = MOVE_NONE;
    int rootBestScore = -INF_SCORE;
    
    Move rootLegalMoves[MAX_MOVES]{};            // Legal moves at root
    uint64_t rootMoveEffort[MAX_MOVES]{};        // Nodes spent on each root move
    int rootMoveAvgScore[MAX_MOVES]{};           // Running average score per root move
    int rootMoveCount = 0;                       // Number of root moves
    
    // Evaluation and Accumulator Stacks
    int evalStack[MAX_PLY]{};
    std::vector<NNUE::AccumulatorState> nnueStack;

    // Book/History
    std::vector<std::string> uci_history;
    std::unordered_map<std::string, std::vector<std::string>> opening_book;

    // Undo Stack for Make/Unmake
    struct Undo {
        int castling_before = 0;
        int ep_before = 0;
        int half_before = 0;
        int fullmove_before = 0;
        uint64_t hash_before = 0;
        uint64_t pawnKey_before = 0;
        int captured_piece = EMPTY;
        int moved_piece = EMPTY;
        size_t repIndexBefore = 0;
        int repetition_before = 0;
        int plies_from_null_before = 0;
        bool was_null = false;
    };
    Undo undoStack[MAX_STACK];
    int undoTop = 0;

    // Constructors & Copy Assignment
    Position();
    Position(const Position& other);
    Position& operator=(const Position& other);

    // Board Setup & FEN
    void setStartPos();
    bool setFEN(const std::string& fen);
    std::string toFEN() const;
    uint64_t computeHash() const;

    // Move Generation
    int generateLegal(Move* moves, GenType type = GEN_ALL);
    int generateLegal(Move* moves, bool capturesOnly);
    int generatePseudo(Move* moves, bool capturesOnly = false);

    // Attack Detection & Pins
    bool isAttacked(int s, bool byWhite) const;
    bool inCheck(bool whiteSide) const;
    U64 attackersTo(int sq, U64 occ) const;
    U64 checkersBB(bool whiteSide) const;
    U64 pinnedBB(bool whiteSide) const;
    U64 blockersForKing(bool whiteSide, U64& pinners) const;

    // Move Application
    void makeMove(const Move& m);
    void undoMove(const Move& m);
    void makeNullMove();
    void undoNullMove();
    bool legalMove(const Move& m);
    bool isPseudoLegal(const Move& m) const;

    // Search Interface
    Move search(int maxDepth, int timeMs);
    int pvs(int depth, int alpha, int beta, SearchStack* ss, bool cutNode = false);
    int qsearch(int alpha, int beta, SearchStack* ss);

struct Threats {
    uint64_t byPawn = 0;
    uint64_t byMinor = 0;
    uint64_t byRook = 0;
    uint64_t all = 0;
};

    // Evaluation & Threats
    int evaluate();
    void calcThreats(Threats& threats) const;
    int see(const Move& m) const;
    bool SEE(const Move& m, int threshold) const;
    bool hasNonPawnMaterial(bool white) const;
    bool isZugzwangEndgame() const;

    // Principal Variation at Root
    std::vector<Move> rootPV;
    std::string pvToString(const std::vector<Move>& pv);

    // UCI Helpers
    std::string moveToUCI(const Move& m) const;
    Move uciToMove(const std::string& s);

    // Table & Heuristic Resets
    void clearTT();
    void clearHeuristics();

    // Time Management
    bool timeUp() const;

    // Draw Detection
    bool isFiftyMoveDraw() const;
    bool isThreefoldRepetition() const;
    bool isThreefoldRepetition(int ply) const;
    bool isInsufficientMaterial() const;
    bool isDraw(int ply);

    // Opening Book
    void initBook();
    std::string bookKey() const;

    // Bitboard Query Helpers
    Color sideToMove() const { return white_to_move ? WHITE : BLACK; }
    U64 pieces() const { return color_bitboards[WHITE] | color_bitboards[BLACK]; }
    U64 pieces(Color c) const { return color_bitboards[c]; }
    U64 pieces(Piece p) const { return bitboards[p]; }
    int pieceOn(int sq) const { return piece_board[sq]; }
};

// Compatibility alias
using DeepBeckyEngine = Position;

#endif // DEEPBECKY_POSITION_H
