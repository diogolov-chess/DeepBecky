#include "nnue.h"
#include "magic.h"
#include "position.h"
#include "threats.h"

#include <algorithm>
#include <cctype>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <mutex>
#include <system_error>

#ifdef __AVX2__
#include <immintrin.h>
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <climits>
#endif

namespace {

namespace fs = std::filesystem;

constexpr char kModelMagic[8] = {'D', 'B', 'N', 'N', 'U', 'E', '5', '\0'};

std::string toLowerCopy(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

fs::path currentDirectory() {
    std::error_code ec;
    fs::path cwd = fs::current_path(ec);
    if (ec) return fs::path();
    return cwd;
}

fs::path executableDirectory() {
#ifdef _WIN32
    std::vector<char> buffer(260);
    while (true) {
        const DWORD length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) break;
        if (length < buffer.size()) {
            return fs::path(std::string(buffer.data(), length)).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__linux__)
    char buffer[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length > 0) {
        buffer[length] = '\0';
        return fs::path(std::string(buffer, length)).parent_path();
    }
#endif
    return currentDirectory();
}

std::optional<fs::path> resolveExplicitModelPath(const std::string& requestedPath) {
    fs::path requested(requestedPath);
    std::error_code ec;

    if (requested.is_absolute()) {
        if (fs::exists(requested, ec) && !ec) return requested;
        return std::nullopt;
    }

    const fs::path cwdCandidate = currentDirectory() / requested;
    ec.clear();
    if (fs::exists(cwdCandidate, ec) && !ec) return cwdCandidate;

    const fs::path exeCandidate = executableDirectory() / requested;
    ec.clear();
    if (fs::exists(exeCandidate, ec) && !ec) return exeCandidate;

    return std::nullopt;
}

std::optional<fs::path> resolveAutoModelPath() {
    const fs::path searchDir = executableDirectory();
    std::error_code ec;
    if (!fs::exists(searchDir, ec) || ec) return std::nullopt;
    ec.clear();
    if (!fs::is_directory(searchDir, ec) || ec) return std::nullopt;

    std::optional<fs::path> bestCandidate;
    fs::file_time_type bestTime{};
    std::string bestName;

    for (fs::directory_iterator it(searchDir, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::directory_entry& entry = *it;
        std::error_code entryEc;
        if (!entry.is_regular_file(entryEc) || entryEc) continue;

        const fs::path candidate = entry.path();
        if (toLowerCopy(candidate.extension().string()) != ".nnue") continue;

        std::error_code timeEc;
        const fs::file_time_type modified = entry.last_write_time(timeEc);
        if (timeEc) continue;
        const std::string filename = candidate.filename().string();
        if (!bestCandidate || modified > bestTime || (modified == bestTime && filename > bestName)) {
            bestCandidate = candidate;
            bestTime = modified;
            bestName = filename;
        }
    }

    return bestCandidate;
}

template <typename T>
void readRawBlock(const std::uint8_t*& cursor, std::vector<T>& dest, std::size_t count) {
    dest.resize(count);
    std::memcpy(dest.data(), cursor, count * sizeof(T));
    cursor += count * sizeof(T);
}

struct ModelState {
    std::string path = NNUE::DEFAULT_MODEL_FILE;
    NNUE::ModelHeader header{};
    std::uint64_t generation = 0;
    bool hasThreats = false;
    std::vector<std::int16_t> inputBias;
    std::vector<std::int16_t> inputWeights;
    std::vector<std::int32_t> midBias;
    std::vector<std::int8_t>  midWeights;
    std::vector<std::int8_t>  midWeightsPacked;
    std::vector<std::int32_t> hiddenBias;
    std::vector<std::int8_t>  hiddenWeights;
    std::vector<std::int8_t>  hiddenWeightsPacked;
    std::vector<std::int8_t>  outputWeights;
    std::vector<std::int32_t> outputBias;
    std::vector<std::int32_t> bucketBias;
    std::string trainingLogPath = "deepbecky_train.jsonl";
    std::ofstream trainingLog;
    std::mutex trainingMutex;
    bool trainingEnabled = false;
    bool loaded = false;
};

ModelState& state() {
    static ModelState current;
    return current;
}

std::size_t expectedPayloadBytes() {
    constexpr std::size_t inputBiasBytes = NNUE::AccumulatorDimensions * sizeof(std::int16_t);
    constexpr std::size_t inputWeightBytes = static_cast<std::size_t>(NNUE::InputDimensions)
                                           * static_cast<std::size_t>(NNUE::AccumulatorDimensions)
                                           * sizeof(std::int16_t);
    constexpr std::size_t midBiasBytes = NNUE::HeadDimensions * sizeof(std::int32_t);
    constexpr std::size_t midWeightBytes = static_cast<std::size_t>(NNUE::MidInputDimensions)
                                         * static_cast<std::size_t>(NNUE::HeadDimensions)
                                         * sizeof(std::int8_t);
    constexpr std::size_t hiddenBiasBytes = NNUE::HiddenDimensions * sizeof(std::int32_t);
    constexpr std::size_t hiddenWeightBytes = static_cast<std::size_t>(NNUE::HeadDimensions)
                                            * static_cast<std::size_t>(NNUE::HiddenDimensions)
                                            * sizeof(std::int8_t);
    constexpr std::size_t outputBiasBytes = sizeof(std::int32_t);
    constexpr std::size_t outputWeightBytes = NNUE::HiddenDimensions * sizeof(std::int8_t);
    constexpr std::size_t bucketBiasBytes = NNUE::OutputBuckets * sizeof(std::int32_t);
    return inputBiasBytes + inputWeightBytes + midBiasBytes + midWeightBytes
         + hiddenBiasBytes + hiddenWeightBytes + outputBiasBytes + outputWeightBytes + bucketBiasBytes;
}

bool parsePayload(const std::vector<std::uint8_t>& payload, ModelState& current) {
    if (payload.size() != expectedPayloadBytes()) return false;

    current.hasThreats = false;
    const std::uint8_t* cursor = payload.data();

    readRawBlock(cursor, current.inputBias, NNUE::AccumulatorDimensions);
    readRawBlock(cursor, current.inputWeights, static_cast<std::size_t>(NNUE::InputDimensions)
                                             * static_cast<std::size_t>(NNUE::AccumulatorDimensions));

    readRawBlock(cursor, current.midBias, NNUE::HeadDimensions);
    readRawBlock(cursor, current.midWeights, static_cast<std::size_t>(NNUE::MidInputDimensions)
                                           * static_cast<std::size_t>(NNUE::HeadDimensions));

    // Populate packed mid layer weights for fast AVX2 SIMD maddubs
    current.midWeightsPacked.assign(384 * 2 * 32, 0);
    for (int c = 0; c < 384; ++c) {
        for (int r = 0; r < 2; ++r) {
            for (int n = 0; n < 8; ++n) {
                const int j = r * 8 + n;
                for (int b = 0; b < 4; ++b) {
                    const int i = c * 4 + b;
                    current.midWeightsPacked[c * 64 + r * 32 + n * 4 + b] = current.midWeights[i * NNUE::HeadDimensions + j];
                }
            }
        }
    }

    readRawBlock(cursor, current.hiddenBias, NNUE::HiddenDimensions);
    readRawBlock(cursor, current.hiddenWeights, static_cast<std::size_t>(NNUE::HeadDimensions)
                                              * static_cast<std::size_t>(NNUE::HiddenDimensions));

    // Populate packed hidden layer weights for fast AVX2 SIMD maddubs
    current.hiddenWeightsPacked.assign(4 * 4 * 32, 0);
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            for (int n = 0; n < 8; ++n) {
                const int j = r * 8 + n;
                for (int b = 0; b < 4; ++b) {
                    const int i = c * 4 + b;
                    current.hiddenWeightsPacked[c * 128 + r * 32 + n * 4 + b] = current.hiddenWeights[i * NNUE::HiddenDimensions + j];
                }
            }
        }
    }

    readRawBlock(cursor, current.outputBias, 1);
    readRawBlock(cursor, current.outputWeights, NNUE::HiddenDimensions);
    readRawBlock(cursor, current.bucketBias, NNUE::OutputBuckets);
    return true;
}

bool headerMatchesArchitecture(const NNUE::ModelHeader& header) {
    return std::memcmp(header.magic, kModelMagic, sizeof(kModelMagic)) == 0
        && header.version == 5u
        && header.architectureHash == NNUE::ArchitectureHash
        && header.inputDimensions == NNUE::InputDimensions
        && header.accumulatorDimensions == NNUE::AccumulatorDimensions
        && header.headDimensions == NNUE::HeadDimensions
        && header.hiddenDimensions == NNUE::HiddenDimensions
        && header.outputBuckets == NNUE::OutputBuckets
        && header.kingBuckets == NNUE::KingBuckets
        && header.scale == NNUE::NetworkScale
        && header.qa == NNUE::NetworkQA
        && header.qb == NNUE::NetworkQB
        && header.payloadBytes == expectedPayloadBytes();
}

#ifdef __AVX2__
std::uint32_t loadLittleEndianU32(const std::uint8_t* bytes) {
    // Reading a uint8_t array through a uint32_t pointer violates C++ strict
    // aliasing. memcpy is defined for object representations and optimizing
    // compilers turn this fixed-size copy into the same unaligned load.
    std::uint32_t value;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}
#endif

template <typename T>
inline __attribute__((always_inline)) void addFeatureContribution(const std::vector<T>& weights,
                             std::array<std::int16_t, NNUE::AccumulatorDimensions>& accumulator,
                             int feature,
                             int sign) {
    if (feature < 0) return;

    const T* featureWeights = weights.data() + static_cast<std::size_t>(feature) * NNUE::AccumulatorDimensions;

#ifdef __AVX2__
    if constexpr (std::is_same_v<T, std::int16_t>) {
        constexpr int chunks = NNUE::AccumulatorDimensions / 16;
        if (sign > 0) {
            for (int i = 0; i < chunks; ++i) {
                __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(accumulator.data()) + i);
                __m256i w = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(featureWeights) + i);
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(accumulator.data()) + i, _mm256_add_epi16(a, w));
            }
        } else {
            for (int i = 0; i < chunks; ++i) {
                __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(accumulator.data()) + i);
                __m256i w = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(featureWeights) + i);
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(accumulator.data()) + i, _mm256_sub_epi16(a, w));
            }
        }
        return;
    } else if constexpr (std::is_same_v<T, std::int8_t>) {
        constexpr int chunks = NNUE::AccumulatorDimensions / 16;
        for (int i = 0; i < chunks; ++i) {
            __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(accumulator.data()) + i);
            __m128i w8 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(featureWeights + i * 16));
            __m256i w16 = _mm256_cvtepi8_epi16(w8);
            if (sign > 0) {
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(accumulator.data()) + i, _mm256_add_epi16(a, w16));
            } else {
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(accumulator.data()) + i, _mm256_sub_epi16(a, w16));
            }
        }
        return;
    }
#endif

    for (int index = 0; index < NNUE::AccumulatorDimensions; ++index) {
        std::int16_t delta = static_cast<std::int16_t>(featureWeights[index]);
        if (sign > 0) accumulator[index] += delta;
        else accumulator[index] -= delta;
    }
}

// Accumulator Cache (Finny Table) per king bucket for rapid king-move transitions
struct alignas(64) BucketCacheEntry {
    std::array<std::int16_t, NNUE::AccumulatorDimensions> accumulator{};
    U64 occupancy = 0;
    std::uint64_t generation = 0;
    bool valid = false;
};

void fullRefreshPerspective(const Position& pos,
                            Color perspective,
                            std::array<std::int16_t, NNUE::AccumulatorDimensions>& accumulator) {
    ModelState& current = state();
    if (!current.loaded) {
        accumulator.fill(0);
        return;
    }

    const NNUE::KingBucketInfo kingBucket = NNUE::makeKingBucketInfo(perspective, static_cast<Square>(pos.king_sq[perspective]));

    int activeFeatures[32];
    int numFeatures = 0;

    for (int square = 0; square < 64; ++square) {
        const int piece = pos.piece_board[square];
        if (piece == EMPTY) continue;

        const int feature = NNUE::activeFeatureIndex(perspective, static_cast<Piece>(piece), static_cast<Square>(square), kingBucket);
        if (feature >= 0 && numFeatures < 32) {
            activeFeatures[numFeatures++] = feature;
        }
    }

#ifdef __AVX2__
    constexpr int chunks = NNUE::AccumulatorDimensions / 16;
    const int16_t* featurePtrs[32];
    for (int f = 0; f < numFeatures; ++f) {
        featurePtrs[f] = current.inputWeights.data() + static_cast<size_t>(activeFeatures[f]) * NNUE::AccumulatorDimensions;
    }

    for (int i = 0; i < chunks; ++i) {
        __m256i sum = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(current.inputBias.data()) + i);
        for (int f = 0; f < numFeatures; ++f) {
            __m256i w = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(featurePtrs[f]) + i);
            sum = _mm256_add_epi16(sum, w);
        }
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(accumulator.data()) + i, sum);
    }
#else
    std::copy(current.inputBias.begin(), current.inputBias.end(), accumulator.begin());
    for (int f = 0; f < numFeatures; ++f) {
        addFeatureContribution(current.inputWeights, accumulator, activeFeatures[f], 1);
    }
#endif
}

void updatePerspectiveAccumulator(const Position& pos,
                                  const Move& move,
                                  Piece movedPiece,
                                  Piece capturedPiece,
                                  Color perspective,
                                  std::array<std::int16_t, NNUE::AccumulatorDimensions>& accumulator) {
    ModelState& current = state();
    if (!current.loaded) {
        accumulator.fill(0);
        return;
    }

    if ((movedPiece == WKING && perspective == WHITE) || (movedPiece == BKING && perspective == BLACK)) {
        fullRefreshPerspective(pos, perspective, accumulator);
        return;
    }

    const int fromSq = moveFrom(move);
    const int toSq = moveTo(move);
    const int promotionType = movePromotionType(move);
    const bool moverIsWhite = isWhitePiece(movedPiece);
    const Piece landingPiece = promotionType ? makePiece(moverIsWhite ? WHITE : BLACK, promotionType) : movedPiece;
    const NNUE::KingBucketInfo kingBucket = NNUE::makeKingBucketInfo(perspective, static_cast<Square>(pos.king_sq[perspective]));

    int subFeatures[2] = {-1, -1};
    int addFeatures[2] = {-1, -1};
    int numSub = 0, numAdd = 0;

    const int movedFeature = NNUE::activeFeatureIndex(perspective, movedPiece, static_cast<Square>(fromSq), kingBucket);
    if (movedFeature >= 0) subFeatures[numSub++] = movedFeature;

    const int landingFeature = NNUE::activeFeatureIndex(perspective, landingPiece, static_cast<Square>(toSq), kingBucket);
    if (landingFeature >= 0) addFeatures[numAdd++] = landingFeature;

    if (capturedPiece != EMPTY) {
        const int capSq = moveIsEnPassant(move) ? (toSq + (moverIsWhite ? -8 : 8)) : toSq;
        const int captureFeature = NNUE::activeFeatureIndex(perspective, capturedPiece, static_cast<Square>(capSq), kingBucket);
        if (captureFeature >= 0) subFeatures[numSub++] = captureFeature;
    }

    if (moveIsCastle(move)) {
        const Piece rookPiece = moverIsWhite ? WROOK : BROOK;
        const int rookFrom = (toSq > fromSq) ? (toSq + 1) : (toSq - 2);
        const int rookTo = (toSq > fromSq) ? (toSq - 1) : (toSq + 1);

        const int rookFromFeature = NNUE::activeFeatureIndex(perspective, rookPiece, static_cast<Square>(rookFrom), kingBucket);
        if (rookFromFeature >= 0) subFeatures[numSub++] = rookFromFeature;

        const int rookToFeature = NNUE::activeFeatureIndex(perspective, rookPiece, static_cast<Square>(rookTo), kingBucket);
        if (rookToFeature >= 0) addFeatures[numAdd++] = rookToFeature;
    }

#ifdef __AVX2__
    const int16_t* wSub0 = numSub > 0 ? (current.inputWeights.data() + static_cast<size_t>(subFeatures[0]) * NNUE::AccumulatorDimensions) : nullptr;
    const int16_t* wSub1 = numSub > 1 ? (current.inputWeights.data() + static_cast<size_t>(subFeatures[1]) * NNUE::AccumulatorDimensions) : nullptr;
    const int16_t* wAdd0 = numAdd > 0 ? (current.inputWeights.data() + static_cast<size_t>(addFeatures[0]) * NNUE::AccumulatorDimensions) : nullptr;
    const int16_t* wAdd1 = numAdd > 1 ? (current.inputWeights.data() + static_cast<size_t>(addFeatures[1]) * NNUE::AccumulatorDimensions) : nullptr;

    constexpr int chunks = NNUE::AccumulatorDimensions / 16;

    if (numSub == 1 && numAdd == 1) {
        for (int i = 0; i < chunks; ++i) {
            __m256i acc_val = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(accumulator.data()) + i);
            __m256i s0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(wSub0) + i);
            __m256i a0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(wAdd0) + i);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(accumulator.data()) + i, _mm256_add_epi16(acc_val, _mm256_sub_epi16(a0, s0)));
        }
    } else if (numSub == 2 && numAdd == 1) {
        for (int i = 0; i < chunks; ++i) {
            __m256i acc_val = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(accumulator.data()) + i);
            __m256i s0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(wSub0) + i);
            __m256i s1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(wSub1) + i);
            __m256i a0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(wAdd0) + i);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(accumulator.data()) + i, _mm256_sub_epi16(_mm256_add_epi16(acc_val, a0), _mm256_add_epi16(s0, s1)));
        }
    } else if (numSub == 2 && numAdd == 2) {
        for (int i = 0; i < chunks; ++i) {
            __m256i acc_val = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(accumulator.data()) + i);
            __m256i s0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(wSub0) + i);
            __m256i s1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(wSub1) + i);
            __m256i a0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(wAdd0) + i);
            __m256i a1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(wAdd1) + i);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(accumulator.data()) + i, _mm256_add_epi16(acc_val, _mm256_sub_epi16(_mm256_add_epi16(a0, a1), _mm256_add_epi16(s0, s1))));
        }
    } else {
        for (int s = 0; s < numSub; ++s) addFeatureContribution(current.inputWeights, accumulator, subFeatures[s], -1);
        for (int a = 0; a < numAdd; ++a) addFeatureContribution(current.inputWeights, accumulator, addFeatures[a], +1);
    }
#else
    for (int s = 0; s < numSub; ++s) addFeatureContribution(current.inputWeights, accumulator, subFeatures[s], -1);
    for (int a = 0; a < numAdd; ++a) addFeatureContribution(current.inputWeights, accumulator, addFeatures[a], +1);
#endif
}

} // namespace

namespace NNUE {

bool isReady() {
    return state().loaded;
}

void init() {
    Threats::init();
}

bool modelHasThreats() {
    return state().hasThreats;
}

bool loadModel(const std::string& path) {
    ModelState& current = state();
    current.loaded = false;

    std::optional<fs::path> resolved;
    if (path == DEFAULT_MODEL_FILE) {
        resolved = resolveAutoModelPath();
    } else {
        resolved = resolveExplicitModelPath(path);
    }

    if (!resolved.has_value()) {
        std::cerr << "info string NNUE file not found: " << path << std::endl;
        return false;
    }

    std::ifstream file(resolved.value(), std::ios::binary);
    if (!file.is_open()) return false;

    ModelHeader header{};
    if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) return false;
    if (!headerMatchesArchitecture(header)) return false;

    std::vector<std::uint8_t> payload(header.payloadBytes);
    if (!file.read(reinterpret_cast<char*>(payload.data()), header.payloadBytes)) return false;

    if (!parsePayload(payload, current)) return false;

    current.path = resolved.value().string();
    current.header = header;
    current.generation++;
    current.loaded = true;
    return true;
}

void unloadModel() {
    ModelState& current = state();
    current.loaded = false;
    current.path = DEFAULT_MODEL_FILE;
    current.generation++;
}

std::uint64_t modelGeneration() {
    return state().generation;
}

void refreshAccumulatorState(const Position& pos, AccumulatorState& state) {
    fullRefreshPerspective(pos, WHITE, state.white);
    fullRefreshPerspective(pos, BLACK, state.black);
    state.generation = modelGeneration();
    state.computed = true;
}

void updateAccumulatorStateAfterMove(const Position& pos, const Move& move, Piece movedPiece, Piece capturedPiece, AccumulatorState& state) {
    updatePerspectiveAccumulator(pos, move, movedPiece, capturedPiece, WHITE, state.white);
    updatePerspectiveAccumulator(pos, move, movedPiece, capturedPiece, BLACK, state.black);
    state.generation = modelGeneration();
    state.computed = true;
}

void setTrainingLogEnabled(bool enabled) { state().trainingEnabled = enabled; }
void setTrainingLogFile(const std::string& path) { state().trainingLogPath = path; }
bool trainingLogEnabled() { return state().trainingEnabled; }
const std::string& trainingLogFile() { return state().trainingLogPath; }
const ModelHeader& currentHeader() { return state().header; }
const std::string& currentModelPath() { return state().path; }
std::string architectureSummary() {
    return "NNUE v5 Compact (13 King Buckets x 768 x 8 Output Buckets + AVX2 SIMD)";
}

void logTrainingSample(const Position& pos, const Move& bestMove, int score, int depth, std::uint64_t nodes) {
    // Optional training data logging
}

int evaluate(Position& pos) {
    ModelState& current = state();
    if (!current.loaded) return 0;

    auto& nnueState = pos.nnueStack[pos.undoTop];
    
    if (!nnueState.computed || nnueState.generation != current.generation) {
        int currentPly = pos.undoTop;
        int computedPly = currentPly;
        
        while (computedPly > 0 && (!pos.nnueStack[computedPly].computed || pos.nnueStack[computedPly].generation != current.generation)) {
            computedPly--;
        }
        
        if (!pos.nnueStack[computedPly].computed || pos.nnueStack[computedPly].generation != current.generation) {
            refreshAccumulatorState(pos, nnueState);
        } else {
            for (int i = computedPly + 1; i <= currentPly; ++i) {
                pos.nnueStack[i].white = pos.nnueStack[i - 1].white;
                pos.nnueStack[i].black = pos.nnueStack[i - 1].black;
                pos.nnueStack[i].generation = current.generation;
                
                Move m = pos.nnueStack[i].move;
                if (!moveIsNone(m)) {
                    Piece movedPiece = pos.nnueStack[i].movedPiece;
                    Piece capturedPiece = pos.nnueStack[i].capturedPiece;
                    
                    updatePerspectiveAccumulator(pos, m, movedPiece, capturedPiece, WHITE, pos.nnueStack[i].white);
                    updatePerspectiveAccumulator(pos, m, movedPiece, capturedPiece, BLACK, pos.nnueStack[i].black);
                }
                pos.nnueStack[i].computed = true;
            }
        }
    }

    const Color stm = pos.white_to_move ? WHITE : BLACK;
    const int pieceCount = popcount(pos.pieces());
    const int bucket = outputBucket(pieceCount);
    const int inputScale = current.header.qa > 0 ? static_cast<int>(current.header.qa) : 255;
    const int scale = current.header.scale > 0 ? static_cast<int>(current.header.scale) : NetworkScale;

    const auto& stmPstAcc  = stm == WHITE ? nnueState.white : nnueState.black;
    const auto& nstmPstAcc = stm == WHITE ? nnueState.black : nnueState.white;

    alignas(64) std::array<std::uint8_t, MidInputDimensions> activations{};

#ifdef __AVX2__
    const __m256i zero = _mm256_setzero_si256();
    const __m256i vQA = _mm256_set1_epi16(static_cast<short>(inputScale));

    // Fast Pure-PST Path (0 threats overhead)
    for (int i = 0; i < AccumulatorDimensions / 32; ++i) {
        const int off0 = i * 32;
        const int off1 = i * 32 + 16;

        __m256i s0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&stmPstAcc[off0]));
        __m256i s1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&stmPstAcc[off1]));

        s0 = _mm256_min_epi16(_mm256_max_epi16(s0, zero), vQA);
        __m256i sqr0 = _mm256_mullo_epi16(s0, s0);
        __m256i shifted0 = _mm256_srli_epi16(sqr0, 9);

        s1 = _mm256_min_epi16(_mm256_max_epi16(s1, zero), vQA);
        __m256i sqr1 = _mm256_mullo_epi16(s1, s1);
        __m256i shifted1 = _mm256_srli_epi16(sqr1, 9);

        __m256i packed = _mm256_packus_epi16(shifted0, shifted1);
        packed = _mm256_permute4x64_epi64(packed, _MM_SHUFFLE(3, 1, 2, 0));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&activations[off0]), packed);
    }

    for (int i = 0; i < AccumulatorDimensions / 32; ++i) {
        const int off0 = i * 32;
        const int off1 = i * 32 + 16;

        __m256i n0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&nstmPstAcc[off0]));
        __m256i n1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&nstmPstAcc[off1]));

        n0 = _mm256_min_epi16(_mm256_max_epi16(n0, zero), vQA);
        __m256i sqr0 = _mm256_mullo_epi16(n0, n0);
        __m256i shifted0 = _mm256_srli_epi16(sqr0, 9);

        n1 = _mm256_min_epi16(_mm256_max_epi16(n1, zero), vQA);
        __m256i sqr1 = _mm256_mullo_epi16(n1, n1);
        __m256i shifted1 = _mm256_srli_epi16(sqr1, 9);

        __m256i packed = _mm256_packus_epi16(shifted0, shifted1);
        packed = _mm256_permute4x64_epi64(packed, _MM_SHUFFLE(3, 1, 2, 0));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&activations[AccumulatorDimensions + off0]), packed);
    }

    // --- Mid layer (1536 uint8 -> 16 int32) ---
    __m256i midAcc0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&current.midBias[0]));
    __m256i midAcc1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&current.midBias[8]));
    const __m256i ones = _mm256_set1_epi16(1);

    for (int chunk = 0; chunk < MidInputDimensions / 4; ++chunk) {
        const uint32_t in_val =
            loadLittleEndianU32(activations.data() + chunk * 4);
        if (in_val == 0) continue;

        __m256i in_vec = _mm256_set1_epi32(static_cast<int32_t>(in_val));

        __m256i mw0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(current.midWeightsPacked.data()) + chunk * 2 + 0);
        __m256i p0 = _mm256_maddubs_epi16(in_vec, mw0);
        midAcc0 = _mm256_add_epi32(midAcc0, _mm256_madd_epi16(p0, ones));

        __m256i mw1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(current.midWeightsPacked.data()) + chunk * 2 + 1);
        __m256i p1 = _mm256_maddubs_epi16(in_vec, mw1);
        midAcc1 = _mm256_add_epi32(midAcc1, _mm256_madd_epi16(p1, ones));
    }

    // ClippedReLU for Mid layer
    __m128i ma0_lo = _mm256_castsi256_si128(midAcc0);
    __m128i ma0_hi = _mm256_extracti128_si256(midAcc0, 1);
    __m128i ma1_lo = _mm256_castsi256_si128(midAcc1);
    __m128i ma1_hi = _mm256_extracti128_si256(midAcc1, 1);

    __m128i mp0 = _mm_packus_epi32(ma0_lo, ma0_hi);
    __m128i mp1 = _mm_packus_epi32(ma1_lo, ma1_hi);

    mp0 = _mm_min_epi16(_mm_srli_epi16(mp0, 6), _mm_set1_epi16(127));
    mp1 = _mm_min_epi16(_mm_srli_epi16(mp1, 6), _mm_set1_epi16(127));

    __m128i mid8 = _mm_packus_epi16(mp0, mp1);
    alignas(16) std::array<std::uint8_t, HeadDimensions> middle{};
    _mm_storeu_si128(reinterpret_cast<__m128i*>(middle.data()), mid8);

    // --- Hidden layer (16 uint8 -> 32 int32) ---
    __m256i hidAcc0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&current.hiddenBias[0]));
    __m256i hidAcc1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&current.hiddenBias[8]));
    __m256i hidAcc2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&current.hiddenBias[16]));
    __m256i hidAcc3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&current.hiddenBias[24]));

    for (int chunk = 0; chunk < HeadDimensions / 4; ++chunk) {
        const uint32_t m_val =
            loadLittleEndianU32(middle.data() + chunk * 4);
        if (m_val == 0) continue;

        __m256i m_vec = _mm256_set1_epi32(static_cast<int32_t>(m_val));

        __m256i hw0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(current.hiddenWeightsPacked.data()) + chunk * 4 + 0);
        __m256i hp0 = _mm256_maddubs_epi16(m_vec, hw0);
        hidAcc0 = _mm256_add_epi32(hidAcc0, _mm256_madd_epi16(hp0, ones));

        __m256i hw1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(current.hiddenWeightsPacked.data()) + chunk * 4 + 1);
        __m256i hp1 = _mm256_maddubs_epi16(m_vec, hw1);
        hidAcc1 = _mm256_add_epi32(hidAcc1, _mm256_madd_epi16(hp1, ones));

        __m256i hw2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(current.hiddenWeightsPacked.data()) + chunk * 4 + 2);
        __m256i hp2 = _mm256_maddubs_epi16(m_vec, hw2);
        hidAcc2 = _mm256_add_epi32(hidAcc2, _mm256_madd_epi16(hp2, ones));

        __m256i hw3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(current.hiddenWeightsPacked.data()) + chunk * 4 + 3);
        __m256i hp3 = _mm256_maddubs_epi16(m_vec, hw3);
        hidAcc3 = _mm256_add_epi32(hidAcc3, _mm256_madd_epi16(hp3, ones));
    }

    // ClippedReLU for Hidden layer
    __m128i h0_lo = _mm256_castsi256_si128(hidAcc0);
    __m128i h0_hi = _mm256_extracti128_si256(hidAcc0, 1);
    __m128i h1_lo = _mm256_castsi256_si128(hidAcc1);
    __m128i h1_hi = _mm256_extracti128_si256(hidAcc1, 1);
    __m128i h2_lo = _mm256_castsi256_si128(hidAcc2);
    __m128i h2_hi = _mm256_extracti128_si256(hidAcc2, 1);
    __m128i h3_lo = _mm256_castsi256_si128(hidAcc3);
    __m128i h3_hi = _mm256_extracti128_si256(hidAcc3, 1);

    __m128i hpack0 = _mm_packus_epi32(h0_lo, h0_hi);
    __m128i hpack1 = _mm_packus_epi32(h1_lo, h1_hi);
    __m128i hpack2 = _mm_packus_epi32(h2_lo, h2_hi);
    __m128i hpack3 = _mm_packus_epi32(h3_lo, h3_hi);

    hpack0 = _mm_min_epi16(_mm_srli_epi16(hpack0, 6), _mm_set1_epi16(127));
    hpack1 = _mm_min_epi16(_mm_srli_epi16(hpack1, 6), _mm_set1_epi16(127));
    hpack2 = _mm_min_epi16(_mm_srli_epi16(hpack2, 6), _mm_set1_epi16(127));
    hpack3 = _mm_min_epi16(_mm_srli_epi16(hpack3, 6), _mm_set1_epi16(127));

    __m128i hb0 = _mm_packus_epi16(hpack0, hpack1);
    __m128i hb1 = _mm_packus_epi16(hpack2, hpack3);

    __m256i hid8 = _mm256_set_m128i(hb1, hb0);
    alignas(32) std::array<std::uint8_t, HiddenDimensions> hidden{};
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(hidden.data()), hid8);

    // --- Output layer ---
    __m256i h_vec = hid8;
    __m256i ow_vec = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(current.outputWeights.data()));

    __m256i outp = _mm256_maddubs_epi16(h_vec, ow_vec);
    outp = _mm256_madd_epi16(outp, ones);

    __m128i sum128 = _mm_add_epi32(_mm256_castsi256_si128(outp), _mm256_extracti128_si256(outp, 1));
    sum128 = _mm_add_epi32(sum128, _mm_shuffle_epi32(sum128, 0x4E));
    sum128 = _mm_add_epi32(sum128, _mm_shuffle_epi32(sum128, 0xB1));
    int32_t dotProduct = _mm_cvtsi128_si32(sum128);

    int32_t score = current.outputBias[0] + current.bucketBias[bucket] + dotProduct;
#else
    for (int i = 0; i < AccumulatorDimensions; ++i) {
        int v = std::clamp<int>(stmPstAcc[i], 0, inputScale);
        activations[i] = static_cast<std::uint8_t>((v * v) >> 9);
    }
    for (int i = 0; i < AccumulatorDimensions; ++i) {
        int v = std::clamp<int>(nstmPstAcc[i], 0, inputScale);
        activations[AccumulatorDimensions + i] = static_cast<std::uint8_t>((v * v) >> 9);
    }

    alignas(16) std::array<std::uint8_t, HeadDimensions> middle{};
    for (int j = 0; j < HeadDimensions; ++j) {
        int32_t sum = current.midBias[j];
        for (int i = 0; i < MidInputDimensions; ++i) {
            uint8_t val = activations[i];
            if (!val) continue;
            sum += static_cast<int32_t>(val) * static_cast<int32_t>(current.midWeights[i * HeadDimensions + j]);
        }
        middle[j] = static_cast<uint8_t>(std::clamp(sum >> 6, 0, 127));
    }

    alignas(32) std::array<std::uint8_t, HiddenDimensions> hidden{};
    for (int j = 0; j < HiddenDimensions; ++j) {
        int32_t sum = current.hiddenBias[j];
        for (int i = 0; i < HeadDimensions; ++i) {
            uint8_t val = middle[i];
            if (!val) continue;
            sum += static_cast<int32_t>(val) * static_cast<int32_t>(current.hiddenWeights[i * HiddenDimensions + j]);
        }
        hidden[j] = static_cast<uint8_t>(std::clamp(sum >> 6, 0, 127));
    }

    int32_t score = current.outputBias[0] + current.bucketBias[bucket];
    for (int j = 0; j < HiddenDimensions; ++j) {
        score += static_cast<int32_t>(hidden[j]) * static_cast<int32_t>(current.outputWeights[j]);
    }
#endif

    int centipawns = static_cast<int>(std::llround(static_cast<long double>(score) * static_cast<long double>(scale) / 8128.0));
    return std::clamp(centipawns, -32000, 32000);
}

} // namespace NNUE
