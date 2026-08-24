#ifndef DEEPBECKY_NNUE_H
#define DEEPBECKY_NNUE_H

#include "types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <fstream>
#include <string>
#include <vector>

class Position;

namespace NNUE {

inline constexpr const char* DEFAULT_MODEL_FILE = "auto";

constexpr int PieceSquareFeatureDimensions = 11 * 64;
constexpr int KingBuckets = 13;
constexpr int InputDimensions = KingBuckets * PieceSquareFeatureDimensions; // 9152
constexpr int AccumulatorDimensions = 768;
// Dual perspective: mid layer receives [stm_accumulator | nstm_accumulator].
constexpr int MidInputDimensions = AccumulatorDimensions * 2; // 1536
constexpr int HeadDimensions = 16;
constexpr int HiddenDimensions = 32;
constexpr int OutputBuckets = 8;
constexpr int NetworkScale = 400;
constexpr int NetworkQA = 255;
constexpr int NetworkQB = 64;

// NNUE v5 Compact Architecture Hash (13 King Buckets x 768 x 8 Buckets)
constexpr std::uint32_t ArchitectureHash = 0x5a137683u;

// 13-region king bucket layout (Obsidian style)
inline constexpr std::array<std::uint8_t, 64> KING_BUCKET_LAYOUT = {
     0,  1,  2,  3,  3,  2,  1,  0,  // Fileira 1
     4,  5,  6,  7,  7,  6,  5,  4,  // Fileira 2
     8,  8,  9,  9,  9,  9,  8,  8,  // Fileira 3 (Alas: 8, Centro: 9)
    10, 10, 10, 10, 10, 10, 10, 10,  // Fileira 4
    11, 11, 11, 11, 11, 11, 11, 11,  // Fileiras 5 e 6
    11, 11, 11, 11, 11, 11, 11, 11,
    12, 12, 12, 12, 12, 12, 12, 12,  // Fileiras 7 e 8
    12, 12, 12, 12, 12, 12, 12, 12,
};

struct KingBucketInfo {
    int index = 0;
    bool mirrored = false;
};

inline int orientedSquare(Color sideToMove, Square square, bool mirrored) {
    int oriented = sideToMove == WHITE ? static_cast<int>(square) : flipRank(static_cast<int>(square));
    if (mirrored) oriented ^= 7;
    return oriented;
}

inline constexpr int PieceOffsetTable[2][PIECE_NB] = {
    // WHITE perspective (sideToMove == WHITE = 0)
    {-1, 0, 128, 256, 384, 512, 640, 64, 192, 320, 448, 576, 640},
    // BLACK perspective (sideToMove == BLACK = 1)
    {-1, 64, 192, 320, 448, 576, 640, 0, 128, 256, 384, 512, 640}
};

inline int pieceSquareOffset(Color sideToMove, Piece piece) {
    if (static_cast<size_t>(piece) >= PIECE_NB) return -1;
    return PieceOffsetTable[sideToMove][piece];
}

inline KingBucketInfo makeKingBucketInfo(Color sideToMove, Square kingSquare) {
    const int orientedKing = sideToMove == WHITE ? static_cast<int>(kingSquare) : flipRank(static_cast<int>(kingSquare));
    return {static_cast<int>(KING_BUCKET_LAYOUT[orientedKing]), sq_file(static_cast<int>(kingSquare)) < 4};
}

inline int bucketFeatureIndex(int bucket, int baseIndex) {
    return bucket * PieceSquareFeatureDimensions + baseIndex;
}

inline int activeFeatureIndex(Color sideToMove, Piece piece, Square square, const KingBucketInfo& kingBucket) {
    const int pieceOffset = pieceSquareOffset(sideToMove, piece);
    if (pieceOffset < 0) return -1;
    return bucketFeatureIndex(kingBucket.index, pieceOffset + orientedSquare(sideToMove, square, kingBucket.mirrored));
}

inline constexpr std::array<std::uint8_t, 33> OUTPUT_BUCKETS_LAYOUT = {
    0, 0, 0, 0, 0, 0, 0, 0, 0,  // Finais (0 a 8 peças)
    1, 1, 1, 1,                  // Finais com peças menores (9 a 12 peças)
    2, 2, 2, 2,                  // Meio-jogo tardio (13 a 16 peças)
    3, 3, 3,                     // Meio-jogo (17 a 19 peças)
    4, 4, 4,                     // Meio-jogo (20 a 22 peças)
    5, 5, 5,                     // Meio-jogo cheio (23 a 25 peças)
    6, 6, 6,                     // Transição abertura (26 a 28 peças)
    7, 7, 7, 7                   // Abertura completa (29 a 32 peças)
};

inline int outputBucket(int pieceCount) {
    if (pieceCount < 0) return 0;
    if (pieceCount > 32) return OutputBuckets - 1;
    return OUTPUT_BUCKETS_LAYOUT[pieceCount];
}

struct alignas(64) AccumulatorState {
    std::array<std::int16_t, AccumulatorDimensions> white{};
    std::array<std::int16_t, AccumulatorDimensions> black{};
    std::uint64_t generation = 0;
    bool computed = false;
    Move move = MOVE_NONE;
    Piece movedPiece = EMPTY;
    Piece capturedPiece = EMPTY;
};

#pragma pack(push, 1)
struct ModelHeader {
    char magic[8];
    std::uint32_t version;
    std::uint32_t architectureHash;
    std::uint32_t inputDimensions;
    std::uint32_t accumulatorDimensions;
    std::uint32_t headDimensions;
    std::uint32_t hiddenDimensions;
    std::uint32_t outputBuckets;
    std::uint32_t kingBuckets;
    std::int32_t scale;
    std::int32_t qa;
    std::int32_t qb;
    std::uint64_t payloadBytes;
};
#pragma pack(pop)

bool isReady();
void init();
bool loadModel(const std::string& path = DEFAULT_MODEL_FILE);
void unloadModel();
std::uint64_t modelGeneration();
bool modelHasThreats();
void refreshAccumulatorState(const Position& pos, AccumulatorState& state);
void updateAccumulatorStateAfterMove(const Position& pos, const Move& move, Piece movedPiece, Piece capturedPiece, AccumulatorState& state);
void setTrainingLogEnabled(bool enabled);
void setTrainingLogFile(const std::string& path);
bool trainingLogEnabled();
const std::string& trainingLogFile();
const ModelHeader& currentHeader();
const std::string& currentModelPath();
std::string architectureSummary();
void logTrainingSample(const Position& pos, const Move& bestMove, int score, int depth, std::uint64_t nodes);

int evaluate(Position& pos);

} // namespace NNUE

#endif // DEEPBECKY_NNUE_H