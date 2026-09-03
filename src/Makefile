# ============================================================================
# Makefile — Deep Becky 2.5
# High-Performance Chess Engine Build System
# Supports: MSYS2 MINGW64, MSYS2 CLANG64, MSYS2 UCRT64, Windows CMD/PowerShell, Linux
# ============================================================================
#
# USAGE — Windows (MSYS2 MINGW64 / CLANG64):
#   mingw32-make                              - Build with GCC (default, bmi2)
#   mingw32-make PROFILE=bmi2                 - Build for modern CPUs (AVX2 + BMI2)
#   mingw32-make PROFILE=native               - Build optimized for YOUR CPU only
#   mingw32-make pgo PROFILE=bmi2             - Full PGO+LTO build with GCC
#   mingw32-make pgo CXX=clang++ PROFILE=bmi2 - Full PGO+ThinLTO build with Clang
#   mingw32-make CXX=clang++ PROFILE=bmi2     - Build with Clang (MSYS2 CLANG64 / MINGW64)
#
# USAGE — Linux (Ubuntu / Debian / Oracle Cloud AMD):
#   make                                      - Build with GCC (default, bmi2)
#   make PROFILE=bmi2                         - Build for modern CPUs (AVX2 + BMI2)
#   make PROFILE=native                       - Build optimized for YOUR CPU only (RECOMENDADO PARA VM)
#   make pgo PROFILE=native                   - Full PGO+LTO build with GCC (RECOMENDADO PARA VM)
#   make pgo CXX=clang++ PROFILE=native       - Full PGO+ThinLTO build with Clang (MAX NPS)
#   make CXX=clang++ PROFILE=native           - Build with Clang (sem PGO)
#
# PROFILE options:
#   portable - Baseline x86-64 (runs on any 64-bit x86 CPU)
#   sse42    - SSE4.2 + POPCNT (Intel Nehalem / AMD Bulldozer)
#   avx2     - AVX2 + BMI1 + FMA + LZCNT + POPCNT
#   bmi2     - AVX2 + BMI2 (Fast PEXT magic bitboards, fastest for modern x86)
#   avx512   - AVX512 + BMI2 (Zen 4/5, Intel Ice Lake/Tiger Lake/Sapphire Rapids)
#   native   - Automatically tuned to host CPU (RECOMENDADO para VMs Oracle AMD)
# ============================================================================

# Detect OS and Shell
ifeq ($(OS),Windows_NT)
  DETECTED_OS := Windows
  TARGET_EXT := .exe
  ifdef MSYSTEM
    SHELL_ENV := MSYS2_$(MSYSTEM)
    SHELL := /bin/bash
    RM_CMD := rm -rf
    MKDIR_CMD := mkdir -p
    ECHO_CMD := echo -e
    DEV_NULL := /dev/null
  else
    SHELL_ENV := Windows_Native
    SHELL := cmd.exe
    RM_CMD = if exist $(1) rmdir /s /q $(1)
    MKDIR_CMD = if not exist $(1) mkdir $(1)
    ECHO_CMD := echo
    DEV_NULL := NUL
  endif
else
  DETECTED_OS := $(shell uname -s)
  TARGET_EXT :=
  SHELL_ENV := POSIX
  SHELL := /bin/sh
  RM_CMD := rm -rf
  MKDIR_CMD := mkdir -p
  ECHO_CMD := echo -e
  DEV_NULL := /dev/null
endif

# Compiler Selection
# In MSYS2 CLANG64 environment, default to clang++
ifeq ($(MSYSTEM),CLANG64)
  CXX ?= clang++
else
  CXX ?= g++
endif

# User can specify COMPILER=clang or COMPILER=gcc directly
ifeq ($(COMPILER),clang)
  CXX := clang++
else ifeq ($(COMPILER),gcc)
  CXX := g++
endif

# Detect Compiler Type (gcc vs clang)
COMPILER_VERSION := $(shell $(CXX) -v 2>&1)
ifneq ($(findstring clang,$(COMPILER_VERSION)),)
  COMPILER_TYPE := clang
else ifneq ($(findstring gcc,$(COMPILER_VERSION)),)
  COMPILER_TYPE := gcc
else ifneq ($(findstring g++,$(COMPILER_VERSION)),)
  COMPILER_TYPE := gcc
else
  COMPILER_TYPE := gcc
endif

# Target Binary Name
TARGET := DeepBecky_3.0$(TARGET_EXT)

# Source and Header Files
SRC := main.cpp magic.cpp bitboard.cpp position.cpp movegen.cpp threats.cpp \
       evaluate.cpp nnue.cpp search.cpp searchstats.cpp tt.cpp uci.cpp timeman.cpp movepick.cpp thread.cpp

HEADERS := types.h magic.h bitboard.h position.h movegen.h movepick.h threats.h \
           evaluate.h nnue.h search.h searchstats.h tt.h uci.h timeman.h thread.h

# Directories
BUILD_DIR := build
PGO_DIR   := pgo-data

# Build Configuration Options
PROFILE   ?= bmi2
STATIC    ?= 1
PGO_DEPTH ?= 13
SELF_MK   := $(lastword $(MAKEFILE_LIST))

# Base Optimization and Warning Flags
CXXSTD    := -std=gnu++17
WARN      := -Wall -Wextra -Wshadow -Wpedantic -Wconversion \
             -Wno-sign-conversion -Wno-unused-parameter
OPT_BASE  := -O3 -pipe -DNDEBUG \
             -fno-exceptions -fno-rtti -fno-unwind-tables -fno-asynchronous-unwind-tables
STRIPFLAG := -s

# Compiler-Specific LTO and Linker Flags
ifeq ($(COMPILER_TYPE),clang)
  LTO_FLAG      ?= -flto=thin
  LLVM_PROFDATA ?= llvm-profdata
  # In Windows MinGW/Clang environments, use LLD if available
  LLD_TEST := $(shell $(CXX) -fuse-ld=lld -Wl,--version 2>&1)
  ifneq ($(findstring LLD,$(LLD_TEST)),)
    LDFLAGS_EXTRA := -fuse-ld=lld
  else
    LDFLAGS_EXTRA :=
  endif
else
  LTO_JOBS      ?= 8
  LTO_FLAG      ?= -flto=$(LTO_JOBS)
  LDFLAGS_EXTRA :=
endif

# Architecture Flags based on PROFILE
ifeq ($(PROFILE),portable)
  ARCHFLAGS := -march=x86-64 -mtune=generic
endif
ifeq ($(PROFILE),sse42)
  ARCHFLAGS := -march=x86-64 -msse4.2 -mpopcnt -mtune=generic
endif
ifeq ($(PROFILE),avx2)
  ARCHFLAGS := -march=x86-64 -mavx2 -mbmi -mlzcnt -mpopcnt -mfma -mtune=haswell
endif
ifeq ($(PROFILE),bmi2)
  ARCHFLAGS := -march=x86-64 -mavx2 -mbmi -mbmi2 -mlzcnt -mpopcnt -mfma -mtune=haswell -DUSE_PEXT
endif
ifeq ($(PROFILE),avx512)
  ARCHFLAGS := -march=x86-64-v4 -mbmi -mbmi2 -mlzcnt -mpopcnt -mfma -DUSE_PEXT
endif
ifeq ($(PROFILE),native)
  ARCHFLAGS := -march=native
  # Auto-detect PEXT support for native build
  NATIVE_PEXT := $(shell $(CXX) -march=native -dM -E - < $(DEV_NULL) 2>&1 | grep -E '__BMI2__|__AVX2__')
  ifneq ($(findstring __BMI2__,$(NATIVE_PEXT)),)
    ARCHFLAGS += -DUSE_PEXT
  endif
endif

# Version Detection (auto-generates "Deep Becky dev-YYYYMMDD" or official tag)
GIT_DATE := $(shell git log -1 --format=%cd --date=format:%Y%m%d 2>$(DEV_NULL))
ifeq ($(strip $(GIT_DATE)),)
  GIT_DATE := 20260824
endif

GIT_TAG := $(shell git describe --tags --exact-match 2>$(DEV_NULL))
ifdef GIT_TAG
  ENGINE_VERSION_STR := Deep Becky $(subst v,,$(GIT_TAG))
else
  ENGINE_VERSION_STR := Deep Becky dev-$(GIT_DATE)
endif

# Combine Flags
CXXFLAGS_BASE := $(CXXSTD) $(OPT_BASE) $(LTO_FLAG) $(WARN) $(ARCHFLAGS) -faligned-new -I. -DENGINE_VERSION="\"$(ENGINE_VERSION_STR)\""
LDFLAGS_BASE  := $(STRIPFLAG) $(LTO_FLAG) $(LDFLAGS_EXTRA)

ifeq ($(SEARCH_STATS),1)
  # Keep diagnostic objects and executable isolated. This prevents Make from
  # reusing objects compiled with different preprocessor flags, and prevents a
  # diagnostic executable from masquerading as the production binary.
  BUILD_DIR := build-searchstats
  TARGET := DeepBecky_3.0-searchstats$(TARGET_EXT)
  CXXFLAGS_BASE += -DENABLE_SEARCH_STATS
endif

ifeq ($(STATIC),1)
  ifeq ($(DETECTED_OS),Windows)
    LDFLAGS_BASE += -static -static-libgcc -static-libstdc++ -lpthread
  else
    LDFLAGS_BASE += -static-libgcc -static-libstdc++ -lpthread
  endif
endif

# Profile-Guided Optimization (PGO) Flags
ifeq ($(PGO),gen)
  ifeq ($(COMPILER_TYPE),clang)
    CXXFLAGS := $(CXXFLAGS_BASE) -fprofile-generate=$(PGO_DIR)
    LDFLAGS  := $(LDFLAGS_BASE)  -fprofile-generate=$(PGO_DIR)
  else
    CXXFLAGS := $(CXXFLAGS_BASE) -fprofile-generate=$(PGO_DIR)
    LDFLAGS  := $(LDFLAGS_BASE)  -fprofile-generate=$(PGO_DIR)
  endif
else ifeq ($(PGO),use)
  ifeq ($(COMPILER_TYPE),clang)
    CXXFLAGS := $(CXXFLAGS_BASE) -fprofile-use=$(PGO_DIR)/deepbecky.profdata
    LDFLAGS  := $(LDFLAGS_BASE)  -fprofile-use=$(PGO_DIR)/deepbecky.profdata
  else
    CXXFLAGS := $(CXXFLAGS_BASE) -fprofile-use=$(PGO_DIR) -fprofile-correction
    LDFLAGS  := $(LDFLAGS_BASE)  -fprofile-use=$(PGO_DIR) -fprofile-correction
  endif
else
  CXXFLAGS := $(CXXFLAGS_BASE)
  LDFLAGS  := $(LDFLAGS_BASE)
endif

# Object Files
OBJ := $(addprefix $(BUILD_DIR)/,$(SRC:.cpp=.o))

# Targets
.PHONY: all clean distclean info pgo pgo-gen pgo-use pgo-clean debug help install-deps see-tests search-tests tt-tests sanitize-ub sanitize-asan

# Default Target
all: $(TARGET)

# Deterministic SEE regression tests. The target links the engine objects
# without the UCI entry point and does not alter the production binary.
SEE_TEST_TARGET := $(BUILD_DIR)/see_tests$(TARGET_EXT)
SEE_TEST_SRC := ../tests/see_tests.cpp
SEE_TEST_OBJ := $(filter-out $(BUILD_DIR)/main.o,$(OBJ))
# The test source is not part of the PGO training workload. Link it against
# the optimized engine objects, but do not ask Clang to consume engine profile
# counters for the test runner itself.
SEE_TEST_CXXFLAGS := $(filter-out -fprofile-use=%,$(CXXFLAGS))
SEE_TEST_LDFLAGS := $(filter-out -fprofile-use=%,$(LDFLAGS))

see-tests: $(SEE_TEST_TARGET)
	@$(SEE_TEST_TARGET)

$(SEE_TEST_TARGET): $(SEE_TEST_OBJ) $(SEE_TEST_SRC) | $(BUILD_DIR)
	@echo "== Linking SEE tests ($(COMPILER_TYPE), $(PROFILE)) =="
	$(CXX) $(SEE_TEST_CXXFLAGS) $(SEE_TEST_SRC) $(SEE_TEST_OBJ) -o $@ $(SEE_TEST_LDFLAGS)

# Deterministic search/TT/promotion regression tests.
SEARCH_TEST_TARGET := $(BUILD_DIR)/search_regression_tests$(TARGET_EXT)
SEARCH_TEST_SRC := ../tests/search_regression_tests.cpp
SEARCH_TEST_OBJ := $(filter-out $(BUILD_DIR)/main.o,$(OBJ))
SEARCH_TEST_CXXFLAGS := $(filter-out -fprofile-use=%,$(CXXFLAGS))
SEARCH_TEST_LDFLAGS := $(filter-out -fprofile-use=%,$(LDFLAGS))

search-tests: $(SEARCH_TEST_TARGET)
	@$(SEARCH_TEST_TARGET)

$(SEARCH_TEST_TARGET): $(SEARCH_TEST_OBJ) $(SEARCH_TEST_SRC) | $(BUILD_DIR)
	@echo "== Linking search regression tests ($(COMPILER_TYPE), $(PROFILE)) =="
	$(CXX) $(SEARCH_TEST_CXXFLAGS) $(SEARCH_TEST_SRC) $(SEARCH_TEST_OBJ) -o $@ $(SEARCH_TEST_LDFLAGS)

# Coherent transposition-table snapshot and concurrency tests.
TT_TEST_TARGET := $(BUILD_DIR)/tt_concurrency_tests$(TARGET_EXT)
TT_TEST_SRC := ../tests/tt_concurrency_tests.cpp
TT_TEST_OBJ := $(BUILD_DIR)/tt.o
TT_TEST_CXXFLAGS := $(filter-out -fprofile-use=%,$(CXXFLAGS))
TT_TEST_LDFLAGS := $(filter-out -fprofile-use=%,$(LDFLAGS))

tt-tests: $(TT_TEST_TARGET)
	@$(TT_TEST_TARGET)

$(TT_TEST_TARGET): $(TT_TEST_OBJ) $(TT_TEST_SRC) | $(BUILD_DIR)
	@echo "== Linking TT concurrency tests ($(COMPILER_TYPE), $(PROFILE)) =="
	$(CXX) $(TT_TEST_CXXFLAGS) $(TT_TEST_SRC) $(TT_TEST_OBJ) -o $@ $(TT_TEST_LDFLAGS)

# Link
$(TARGET): $(OBJ)
	@echo "== Linking $(TARGET) ($(COMPILER_TYPE), $(PROFILE)) =="
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)

# Compile C++ Object Files
$(BUILD_DIR)/%.o: %.cpp $(HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Create Build Directory
$(BUILD_DIR):
ifdef MSYSTEM
	@mkdir -p $(BUILD_DIR)
else ifeq ($(DETECTED_OS),Windows)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
else
	@mkdir -p $(BUILD_DIR)
endif

# Clean Object Files and Target
clean:
	@echo "Cleaning build files..."
ifdef MSYSTEM
	@rm -rf $(BUILD_DIR) $(TARGET)
else ifeq ($(DETECTED_OS),Windows)
	@if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)
	@if exist $(TARGET) del /q $(TARGET)
else
	@rm -rf $(BUILD_DIR) $(TARGET)
endif

# Clean Everything (including PGO profiles)
distclean: clean
	@echo "Cleaning PGO data..."
ifdef MSYSTEM
	@rm -rf $(PGO_DIR)
else ifeq ($(DETECTED_OS),Windows)
	@if exist $(PGO_DIR) rmdir /s /q $(PGO_DIR)
else
	@rm -rf $(PGO_DIR)
endif

# ============================================================================
# PGO PROFILE-GUIDED OPTIMIZATION TARGETS
# ============================================================================

.NOTPARALLEL: pgo pgo-gen pgo-use

pgo-clean:
	@echo "Removing PGO profile data..."
ifdef MSYSTEM
	@rm -rf $(PGO_DIR)
else ifeq ($(DETECTED_OS),Windows)
	@if exist $(PGO_DIR) rmdir /s /q $(PGO_DIR)
else
	@rm -rf $(PGO_DIR)
endif

pgo-gen: clean
	@echo ""
	@echo "============================================================"
	@echo "  PGO Step 1: Building Instrumented Binary ($(COMPILER_TYPE))"
	@echo "============================================================"
	@$(MAKE) -f $(SELF_MK) PGO=gen PROFILE=$(PROFILE) CXX=$(CXX) all
	@echo ""
	@echo "== Running Diverse Training Suite to Collect Profile Data (depth=$(PGO_DEPTH)) =="
ifdef MSYSTEM
	@mkdir -p $(PGO_DIR)
	@echo -e "uci\nisready\nucinewgame\n\
position startpos\ngo depth $(PGO_DEPTH)\nisready\n\
position startpos moves e2e4 e7e5 g1f3 b8c6 f1b5\ngo depth $(PGO_DEPTH)\nisready\n\
position startpos moves d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5\ngo depth $(PGO_DEPTH)\nisready\n\
position fen r1bqk2r/pppp1ppp/2n5/1B2p3/4n3/5N2/PPPP1PPP/RNBQ1RK1 w kq -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen r1bqkb1r/pp2pppp/2np1n2/8/3NP3/2N5/PPP2PPP/R1BQKB1R w KQkq -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen rnbqkb1r/pp3ppp/4pn2/2pp4/2PP4/2N1PN2/PP3PPP/R1BQKB1R b KQkq -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen r2q1rk1/1pp1bppp/p1np1nb1/4p3/2B1P1P1/2NP1N1P/PPP2P2/R1BQR1K1 w - -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen r1b1qrk1/ppp2pbp/3p1np1/4p3/2PPP3/2N1BN2/PP2BPPP/R2Q1RK1 w - -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen 8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen 8/8/4kpp1/3p1b2/p6P/2B5/6PK/8 w - -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen 8/5k2/1p6/p1p2Pp1/P1P3K1/8/8/8 b - -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen 2r3k1/1p3ppp/3bpn2/3p4/3P4/1PN1P3/1B3PPP/5RK1 w - -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen 3r2k1/p4p1p/1p4p1/2r5/4B3/P3P1P1/1P1n1P1P/R2R2K1 b - -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen rnbqkb1r/pp1p1pPp/8/2p1pP2/1P1P4/3P3P/P1P1P3/RNBQKBNR w KQkq e6\ngo depth $(PGO_DEPTH)\nisready\n\
quit" | ./$(TARGET) > $(DEV_NULL) 2>&1 || true
ifeq ($(COMPILER_TYPE),clang)
	@echo "== Merging Clang profile data using $(LLVM_PROFDATA) =="
	@$(LLVM_PROFDATA) merge -output=$(PGO_DIR)/deepbecky.profdata $(PGO_DIR)/*.profraw
endif
else ifeq ($(DETECTED_OS),Windows)
	@if not exist $(PGO_DIR) mkdir $(PGO_DIR)
	@(echo uci & echo isready & echo ucinewgame & \
echo position startpos & echo go depth $(PGO_DEPTH) & echo isready & \
echo position startpos moves e2e4 e7e5 g1f3 b8c6 f1b5 & echo go depth $(PGO_DEPTH) & echo isready & \
echo position startpos moves d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 & echo go depth $(PGO_DEPTH) & echo isready & \
echo position fen r1bqk2r/pppp1ppp/2n5/1B2p3/4n3/5N2/PPPP1PPP/RNBQ1RK1 w kq - & echo go depth $(PGO_DEPTH) & echo isready & \
echo position fen r1bqkb1r/pp2pppp/2np1n2/8/3NP3/2N5/PPP2PPP/R1BQKB1R w KQkq - & echo go depth $(PGO_DEPTH) & echo isready & \
echo position fen rnbqkb1r/pp3ppp/4pn2/2pp4/2PP4/2N1PN2/PP3PPP/R1BQKB1R b KQkq - & echo go depth $(PGO_DEPTH) & echo isready & \
echo position fen r2q1rk1/1pp1bppp/p1np1nb1/4p3/2B1P1P1/2NP1N1P/PPP2P2/R1BQR1K1 w - - & echo go depth $(PGO_DEPTH) & echo isready & \
echo position fen r1b1qrk1/ppp2pbp/3p1np1/4p3/2PPP3/2N1BN2/PP2BPPP/R2Q1RK1 w - - & echo go depth $(PGO_DEPTH) & echo isready & \
echo position fen r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - & echo go depth $(PGO_DEPTH) & echo isready & \
echo position fen 8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - & echo go depth $(PGO_DEPTH) & echo isready & \
echo position fen 8/8/4kpp1/3p1b2/p6P/2B5/6PK/8 w - - & echo go depth $(PGO_DEPTH) & echo isready & \
echo position fen 8/5k2/1p6/p1p2Pp1/P1P3K1/8/8/8 b - - & echo go depth $(PGO_DEPTH) & echo isready & \
echo position fen 2r3k1/1p3ppp/3bpn2/3p4/3P4/1PN1P3/1B3PPP/5RK1 w - - & echo go depth $(PGO_DEPTH) & echo isready & \
echo position fen 3r2k1/p4p1p/1p4p1/2r5/4B3/P3P1P1/1P1n1P1P/R2R2K1 b - - & echo go depth $(PGO_DEPTH) & echo isready & \
echo position fen rnbqkb1r/pp1p1pPp/8/2p1pP2/1P1P4/3P3P/P1P1P3/RNBQKBNR w KQkq e6 & echo go depth $(PGO_DEPTH) & echo isready & \
echo quit) | $(TARGET) > $(DEV_NULL) 2>&1 || true
ifeq ($(COMPILER_TYPE),clang)
	@echo "== Merging Clang profile data using $(LLVM_PROFDATA) =="
	@$(LLVM_PROFDATA) merge -output=$(PGO_DIR)/deepbecky.profdata $(PGO_DIR)/*.profraw
endif
else
	@mkdir -p $(PGO_DIR)
	@echo "== Running Diverse Training Suite to Collect Profile Data (depth=$(PGO_DEPTH)) =="
	@printf "uci\nisready\nucinewgame\n\
position startpos\ngo depth $(PGO_DEPTH)\nisready\n\
position startpos moves e2e4 e7e5 g1f3 b8c6 f1b5\ngo depth $(PGO_DEPTH)\nisready\n\
position startpos moves d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5\ngo depth $(PGO_DEPTH)\nisready\n\
position fen r1bqk2r/pppp1ppp/2n5/1B2p3/4n3/5N2/PPPP1PPP/RNBQ1RK1 w kq -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen r1bqkb1r/pp2pppp/2np1n2/8/3NP3/2N5/PPP2PPP/R1BQKB1R w KQkq -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen rnbqkb1r/pp3ppp/4pn2/2pp4/2PP4/2N1PN2/PP3PPP/R1BQKB1R b KQkq -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen r2q1rk1/1pp1bppp/p1np1nb1/4p3/2B1P1P1/2NP1N1P/PPP2P2/R1BQR1K1 w - -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen r1b1qrk1/ppp2pbp/3p1np1/4p3/2PPP3/2N1BN2/PP2BPPP/R2Q1RK1 w - -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen 8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen 8/8/4kpp1/3p1b2/p6P/2B5/6PK/8 w - -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen 8/5k2/1p6/p1p2Pp1/P1P3K1/8/8/8 b - -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen 2r3k1/1p3ppp/3bpn2/3p4/3P4/1PN1P3/1B3PPP/5RK1 w - -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen 3r2k1/p4p1p/1p4p1/2r5/4B3/P3P1P1/1P1n1P1P/R2R2K1 b - -\ngo depth $(PGO_DEPTH)\nisready\n\
position fen rnbqkb1r/pp1p1pPp/8/2p1pP2/1P1P4/3P3P/P1P1P3/RNBQKBNR w KQkq e6\ngo depth $(PGO_DEPTH)\nisready\n\
quit\n" | ./$(TARGET) > $(DEV_NULL) 2>&1 || true
ifeq ($(COMPILER_TYPE),clang)
	@echo "== Merging Clang profile data using $(LLVM_PROFDATA) =="
	@$(LLVM_PROFDATA) merge -output=$(PGO_DIR)/deepbecky.profdata $(PGO_DIR)/*.profraw
endif
endif
	@echo "== Profile data successfully collected in '$(PGO_DIR)' =="

pgo-use:
	@echo ""
	@echo "============================================================"
	@echo "  PGO Step 2: Compiling Final Optimized Binary ($(COMPILER_TYPE))"
	@echo "============================================================"
	@$(MAKE) -f $(SELF_MK) clean
	@$(MAKE) -f $(SELF_MK) PGO=use PROFILE=$(PROFILE) CXX=$(CXX) all
	@echo "== PGO Optimized Build Complete! =="

pgo: distclean
	@$(MAKE) -f $(SELF_MK) pgo-gen PROFILE=$(PROFILE) CXX=$(CXX)
	@$(MAKE) -f $(SELF_MK) pgo-use PROFILE=$(PROFILE) CXX=$(CXX)
	@echo ""
	@echo "============================================================"
	@echo "  Deep Becky 2.5 PGO Build Succeeded!"
	@echo "  Compiler:    $(COMPILER_TYPE) ($(CXX))"
	@echo "  Profile:     $(PROFILE)"
	@echo "  PGO Depth:   $(PGO_DEPTH)"
	@echo "  Environment: $(SHELL_ENV)"
	@echo "  Output:      $(TARGET)"
	@echo "============================================================"

# ============================================================================
# LINUX DEPENDENCY INSTALLATION (Ubuntu / Debian / Oracle Linux com apt)
# ============================================================================

install-deps:
	@echo "============================================================"
	@echo "  Instalando dependencias de build para Linux..."
	@echo "  (Ubuntu / Debian / Oracle Cloud)"
	@echo "============================================================"
ifeq ($(DETECTED_OS),Linux)
	@echo "--> Atualizando lista de pacotes..."
	sudo apt-get update -y
	@echo ""
	@echo "--> Instalando GCC, Make e utilitarios base..."
	sudo apt-get install -y build-essential make
	@echo ""
	@echo "--> Instalando Clang, LLVM e LLD (para compilar com clang++ + ThinLTO)..."
	sudo apt-get install -y clang llvm lld
	@echo ""
	@echo "============================================================"
	@echo "  Dependencias instaladas com sucesso!"
	@echo "  Compiladores disponiveis:"
	@gcc --version | head -1
	@g++ --version | head -1
	@-clang++ --version 2>/dev/null | head -1 || echo "(clang++ nao disponivel)"
	@echo "============================================================"
else
	@echo "[AVISO] Este target so funciona no Linux."
	@echo "OS detectado: $(DETECTED_OS)"
endif

# ============================================================================
# DEBUG BUILD
# ============================================================================

debug: CXXFLAGS := $(CXXSTD) -g -O0 -Wall -Wextra -DDEBUG -I. -DENGINE_VERSION="\"$(ENGINE_VERSION_STR) (Debug)\""
debug: LDFLAGS := $(STRIPFLAG)
debug: clean all
	@echo == Debug build complete ==

# UndefinedBehaviorSanitizer (UBSan)
sanitize-ub: CXXFLAGS := $(CXXSTD) -g -O1 -fsanitize=undefined -fno-omit-frame-pointer $(WARN) $(ARCHFLAGS) -faligned-new -I. -DENGINE_VERSION="\"$(ENGINE_VERSION_STR) (UBSan)\""
sanitize-ub: LDFLAGS := -fsanitize=undefined $(LDFLAGS_EXTRA)
sanitize-ub: clean all
	@echo == UBSan build complete ==

# AddressSanitizer (ASan)
sanitize-asan: CXXFLAGS := $(CXXSTD) -g -O1 -fsanitize=address -fno-omit-frame-pointer $(WARN) $(ARCHFLAGS) -faligned-new -I. -DENGINE_VERSION="\"$(ENGINE_VERSION_STR) (ASan)\""
sanitize-asan: LDFLAGS := -fsanitize=address $(LDFLAGS_EXTRA)
sanitize-asan: clean all
	@echo == ASan build complete ==

# ============================================================================
# DIAGNOSTICS & HELP
# ============================================================================

info:
	@echo ============================================================
	@echo   Deep Becky 2.5 Build Configuration
	@echo ============================================================
	@echo OS          = $(DETECTED_OS)
	@echo SHELL_ENV   = $(SHELL_ENV)
	@echo CXX         = $(CXX)
	@echo COMPILER    = $(COMPILER_TYPE)
	@echo PROFILE     = $(PROFILE)
	@echo PGO         = $(PGO)
	@echo PGO_DEPTH   = $(PGO_DEPTH)
	@echo SEARCH_STATS= $(SEARCH_STATS)
	@echo LTO_FLAG    = $(LTO_FLAG)
	@echo STATIC      = $(STATIC)
	@echo ARCHFLAGS   = $(ARCHFLAGS)
	@echo ------------------------------------------------------------
	@echo CXXFLAGS    = $(CXXFLAGS)
	@echo LDFLAGS     = $(LDFLAGS)
	@echo ============================================================

help:
	@echo Deep Becky 2.5 - Build Commands
	@echo ------------------------------------------------------------
	@echo.
	@echo  ===== WINDOWS (MSYS2 MINGW64 / CLANG64) =====
	@echo   mingw32-make                              - Standard build (GCC, bmi2)
	@echo   mingw32-make PROFILE=bmi2                 - Build for modern x86 CPU (AVX2+BMI2)
	@echo   mingw32-make PROFILE=native               - Build for host CPU
	@echo   mingw32-make pgo PROFILE=bmi2             - Full PGO build with GCC
	@echo   mingw32-make pgo CXX=clang++ PROFILE=bmi2 - Full PGO build with Clang
	@echo   mingw32-make CXX=clang++ PROFILE=bmi2     - Build with Clang (MSYS2 CLANG64 / MINGW64)
	@echo.
	@echo  ===== LINUX (Ubuntu / Debian / Oracle Cloud AMD) =====
	@echo   make                                      - Standard build (GCC, bmi2)
	@echo   make PROFILE=native                       - Build for THIS VM CPU (RECOMENDADO)
	@echo   make pgo PROFILE=native                   - Full PGO+LTO build with GCC (RECOMENDADO)
	@echo   make pgo CXX=clang++ PROFILE=native       - Full PGO+ThinLTO build with Clang (MAX NPS)
	@echo   make CXX=clang++ PROFILE=native           - Build with Clang (sem PGO)
	@echo   make install-deps                         - Install build dependencies (apt-get)
	@echo.
	@echo  ===== COMUNS =====
	@echo   make search-tests                           - Regressões de busca/TT/relógio
	@echo   make tt-tests                               - Estresse de snapshots coerentes da TT
	@echo   make SEARCH_STATS=1 CXX=clang++ PROFILE=bmi2 - Build diagnóstico separado
	@echo   make debug                                 - Debug build
	@echo   make clean                                 - Remove object files and binary
	@echo   make distclean                             - Remove objects, binary and PGO data
	@echo   make info                                  - Show current build configuration
	@echo ------------------------------------------------------------
	@echo PROFILE options:
	@echo   portable  - x86-64 baseline only
	@echo   sse42     - SSE4.2 + POPCNT
	@echo   avx2      - AVX2 + BMI1 + FMA + LZCNT + POPCNT
	@echo   bmi2      - AVX2 + BMI2 (Fast PEXT magic bitboards)
	@echo   avx512    - AVX512 + BMI2 (Zen 4/5, Intel Ice Lake+)
	@echo   native    - Auto-detect host CPU capabilities (RECOMENDADO p/ VMs Oracle AMD)
