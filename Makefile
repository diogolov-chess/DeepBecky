# Makefile — Deep Becky 1.2
# Works on both MSYS2/MinGW and Windows CMD/PowerShell
#
# USAGE:
#   mingw32-make                       - Simple build (portable)
#   mingw32-make PROFILE=bmi2          - Build for modern CPUs
#   mingw32-make pgo PROFILE=bmi2      - Full PGO build (MSYS2 only)
#
# PROFILE options:
#   portable - Works on ANY x86-64 CPU
#   sse42    - Requires SSE4.2
#   avx2     - Requires AVX2
#   bmi2     - Requires BMI2 (fastest)
#   native   - Optimized for YOUR CPU only

# Detect environment
ifdef MSYSTEM
  # Running in MSYS2
  SHELL := /bin/bash
  RM_CMD = rm -rf
  MKDIR_CMD = mkdir -p
  ECHO_CMD = echo -e
  DEV_NULL = /dev/null
else
  # Running in Windows CMD/PowerShell
  SHELL := cmd.exe
  RM_CMD = if exist $(1) rmdir /s /q $(1)
  MKDIR_CMD = if not exist $(1) mkdir $(1)
  ECHO_CMD = echo
  DEV_NULL = NUL
endif

# Compiler and target
CXX       := g++
TARGET    := deepbecky-v1.2-windows-x64.exe

# Source files
SRC       := main.cpp magic.cpp bitboard.cpp position.cpp movegen.cpp \
             evaluate.cpp search.cpp tt.cpp uci.cpp timeman.cpp movepick.cpp

# Headers
HEADERS   := types.h magic.h bitboard.h position.h movegen.h movepick.h \
             evaluate.h search.h tt.h uci.h timeman.h

# Directories
BUILD_DIR := build
PGO_DIR   := pgo-data

# Build options
LTO_JOBS  ?= 8
STATIC    ?= 1
PROFILE   ?= portable
PGO_DEPTH ?= 14

# This makefile name for recursive calls
SELF_MK   := $(lastword $(MAKEFILE_LIST))

# Compiler flags
CXXSTD    := -std=gnu++17
WARN      := -Wall -Wextra -Wshadow -Wpedantic -Wconversion \
             -Wno-sign-conversion -Wno-unused-parameter
OPT       := -O3 -pipe -DNDEBUG -flto=$(LTO_JOBS) \
             -fno-exceptions -fno-rtti -fno-unwind-tables -fno-asynchronous-unwind-tables
STRIPFLAG := -s

# Architecture flags based on PROFILE
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
  ARCHFLAGS := -march=x86-64 -mavx2 -mbmi -mbmi2 -mlzcnt -mpopcnt -mfma -mtune=haswell
endif
ifeq ($(PROFILE),native)
  ARCHFLAGS := -march=native
endif

# Base flags
CXXFLAGS_BASE := $(CXXSTD) $(OPT) $(WARN) $(ARCHFLAGS) -I.
LDFLAGS_BASE  := $(STRIPFLAG) -flto=$(LTO_JOBS)

ifeq ($(STATIC),1)
  LDFLAGS_BASE += -static -static-libgcc -static-libstdc++
endif

# PGO flags
ifeq ($(PGO),gen)
  CXXFLAGS := $(CXXFLAGS_BASE) -fprofile-generate=$(PGO_DIR)
  LDFLAGS  := $(LDFLAGS_BASE)  -fprofile-generate=$(PGO_DIR)
else ifeq ($(PGO),use)
  CXXFLAGS := $(CXXFLAGS_BASE) -fprofile-use=$(PGO_DIR) -fprofile-correction
  LDFLAGS  := $(LDFLAGS_BASE)  -fprofile-use=$(PGO_DIR) -fprofile-correction
else
  CXXFLAGS := $(CXXFLAGS_BASE)
  LDFLAGS  := $(LDFLAGS_BASE)
endif

# Object files
OBJ := $(addprefix $(BUILD_DIR)/,$(SRC:.cpp=.o))

# Phony targets
.PHONY: all clean distclean info pgo pgo-gen pgo-use pgo-clean debug help

# Default target
all: $(TARGET)

# Link
$(TARGET): $(OBJ)
	@echo == Linking $(TARGET) ==
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)

# Compile
$(BUILD_DIR)/%.o: %.cpp $(HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Create build directory
$(BUILD_DIR):
ifdef MSYSTEM
	@mkdir -p $(BUILD_DIR)
else
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
endif

# Clean build files
clean:
	@echo Cleaning objects and binaries...
ifdef MSYSTEM
	@rm -rf $(BUILD_DIR) $(TARGET)
else
	@if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)
	@if exist $(TARGET) del /q $(TARGET)
endif

# Clean everything including PGO data
distclean: clean
	@echo Removing PGO data...
ifdef MSYSTEM
	@rm -rf $(PGO_DIR)
else
	@if exist $(PGO_DIR) rmdir /s /q $(PGO_DIR)
endif

# ============================================================================
# PGO BUILD TARGETS (MSYS2 only)
# ============================================================================

ifdef MSYSTEM
pgo-gen: clean
	@echo "== PGO Step 1: Building with -fprofile-generate =="
	@$(MAKE) -f $(SELF_MK) PGO=gen PROFILE=$(PROFILE) LTO_JOBS=$(LTO_JOBS) all
	@echo "== Running engine to collect profile data (depth=$(PGO_DEPTH)) =="
	@mkdir -p $(PGO_DIR)
	@echo -e "uci\nisready\nucinewgame\n\
position startpos\ngo depth $(PGO_DEPTH)\n\
position startpos moves e2e4 e7e5 g1f3 b8c6 f1b5\ngo depth $(PGO_DEPTH)\n\
position startpos moves d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5\ngo depth $(PGO_DEPTH)\n\
position fen r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -\ngo depth $(PGO_DEPTH)\n\
position fen 8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -\ngo depth $(PGO_DEPTH)\n\
position fen rnbqkb1r/pp1p1pPp/8/2p1pP2/1P1P4/3P3P/P1P1P3/RNBQKBNR w KQkq e6\ngo depth $(PGO_DEPTH)\n\
quit" | ./$(TARGET) > /dev/null 2>&1 || true
	@echo "== Profile data saved to '$(PGO_DIR)' =="

pgo-use:
	@echo "== PGO Step 2: Building with -fprofile-use =="
	@$(MAKE) -f $(SELF_MK) clean
	@$(MAKE) -f $(SELF_MK) PGO=use PROFILE=$(PROFILE) LTO_JOBS=$(LTO_JOBS) all
	@echo "== PGO optimized build complete! =="

pgo: distclean pgo-gen pgo-use
	@echo ""
	@echo "============================================"
	@echo "  PGO Build Complete!"
	@echo "  Profile:   $(PROFILE)"
	@echo "  PGO Depth: $(PGO_DEPTH)"
	@echo "  Output:    $(TARGET)"
	@echo "============================================"

pgo-clean:
	@echo "Removing PGO data..."
	@rm -rf $(PGO_DIR)
else
pgo pgo-gen pgo-use pgo-clean:
	@echo PGO builds require MSYS2 MINGW64 environment.
	@echo Please run from MSYS2 MINGW64 terminal.
endif

# ============================================================================
# DEBUG BUILD
# ============================================================================

debug: CXXFLAGS := -std=gnu++17 -g -O0 -Wall -Wextra -DDEBUG -I.
debug: LDFLAGS := 
debug: clean all
	@echo == Debug build complete ==

# ============================================================================
# INFO
# ============================================================================

info:
	@echo ============================================
	@echo   Deep Becky 1.2 Build Configuration
	@echo ============================================
	@echo CXX       = $(CXX)
	@echo PROFILE   = $(PROFILE)
	@echo PGO       = $(PGO)
	@echo PGO_DEPTH = $(PGO_DEPTH)
	@echo STATIC    = $(STATIC)
	@echo LTO_JOBS  = $(LTO_JOBS)
	@echo ARCHFLAGS = $(ARCHFLAGS)
	@echo --------------------------------------------
	@echo CXXFLAGS  = $(CXXFLAGS)
	@echo LDFLAGS   = $(LDFLAGS)
	@echo ============================================

help:
	@echo Deep Becky 1.2 - Build Commands
	@echo.
	@echo   mingw32-make                     - Simple build (portable)
	@echo   mingw32-make PROFILE=bmi2        - Build for modern CPUs
	@echo   mingw32-make pgo PROFILE=bmi2    - Full PGO build (MSYS2 only)
	@echo   mingw32-make debug               - Debug build
	@echo   mingw32-make clean               - Remove build files
	@echo   mingw32-make info                - Show configuration
	@echo.
	@echo PROFILE options:
	@echo   portable  - x86-64 baseline only
	@echo   sse42     - SSE4.2 + POPCNT
	@echo   avx2      - AVX2 + BMI + FMA
	@echo   bmi2      - AVX2 + BMI2 (fastest)
	@echo   native    - Auto-detect your CPU
