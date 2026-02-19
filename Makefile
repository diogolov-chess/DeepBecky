# Makefile — Deep Becky (MSYS2 + MinGW-w64) com PGO/LTO (compat v3)
# Correção: sub-makes agora passam explicitamente -f a este mesmo arquivo,
# evitando "No rule to make target 'all'" quando também existe outro Makefile na pasta.

SHELL     := /usr/bin/bash
CXX       ?= g++
TARGET    ?= deepbecky-v1.1-windows-x64.exe
SRC       := main.cpp engine.cpp eval.cpp magic.cpp movegen.cpp search.cpp
BUILD_DIR ?= build
PGO_DIR   ?= pgo-data
LTO_JOBS  ?= 8
STATIC    ?= 1
PROFILE   ?= portable

# nome do próprio makefile para reuso em sub-makes
SELF_MK   := $(lastword $(MAKEFILE_LIST))

CXXSTD    := -std=gnu++17
WARN      := -Wall -Wextra -Wshadow -Wpedantic -Wconversion -Wno-sign-conversion -Wno-unused-parameter
OPT       := -O3 -pipe -DNDEBUG -flto=$(LTO_JOBS) -fno-exceptions -fno-rtti -fno-unwind-tables -fno-asynchronous-unwind-tables
STRIPFLAG := -s

ifeq ($(PROFILE),portable)
  ARCHFLAGS := -march=x86-64 -mtune=generic
endif
ifeq ($(PROFILE),avx2)
  ARCHFLAGS := -mavx2 -mbmi -mlzcnt -mpopcnt -mfma -mtune=haswell
endif
ifeq ($(PROFILE),bmi2)
  ARCHFLAGS := -mavx2 -mbmi -mbmi2 -mlzcnt -mpopcnt -mfma -mtune=haswell
endif
ifeq ($(PROFILE),native)
  ARCHFLAGS := -march=native
endif

CXXFLAGS_BASE := $(CXXSTD) $(OPT) $(WARN) $(ARCHFLAGS) -I.
LDFLAGS_BASE  := $(STRIPFLAG) -flto=$(LTO_JOBS)
ifeq ($(STATIC),1)
  LDFLAGS_BASE += -static -static-libgcc -static-libstdc++
endif

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

OBJ := $(addprefix $(BUILD_DIR)/,$(SRC:.cpp=.o))

.PHONY: all clean distclean info fix-timestamps pgo-gen pgo-use profile-build pgo-clean

all: $(TARGET)

$(TARGET): $(OBJ)
	@echo "== Linkando $(TARGET) =="
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	@echo "Cleaning objects and binaries..."
	@rm -rf $(BUILD_DIR) $(TARGET)

distclean: clean
	@echo "Removing PGO data..."
	@rm -rf $(PGO_DIR)

pgo-gen: clean
	@echo "== PGO GEN = -fprofile-generate -> '$(PGO_DIR)' =="
	$(MAKE) -f $(SELF_MK) PGO=gen PROFILE=$(PROFILE) all
	@echo "== Rodando carga UCI para coletar perfis =="
	@[ -n "$$UCI_SCRIPT" ] || { \
		echo "Aviso: sem UCI_SCRIPT. Exemplo:"; \
		echo "  UCI_SCRIPT=\$$$$'uci\\nisready\\nucinewgame\\nposition startpos\\ngo depth 10\\nquit' make -f $(SELF_MK) pgo-gen PROFILE=$(PROFILE)"; \
	}
	@( printf "%s" "$$UCI_SCRIPT" ) | ./$(TARGET) >/dev/null || true
	@echo "== Perfis salvos em '$(PGO_DIR)' =="

pgo-use:
	@echo "== PGO USE = -fprofile-use de '$(PGO_DIR)' =="
	$(MAKE) -f $(SELF_MK) clean
	$(MAKE) -f $(SELF_MK) PGO=use PROFILE=$(PROFILE) all

profile-build:
	$(MAKE) -f $(SELF_MK) pgo-gen PROFILE=$(PROFILE)
	$(MAKE) -f $(SELF_MK) pgo-use PROFILE=$(PROFILE)

pgo-clean:
	@echo "Removing PGO data only..."
	@rm -rf $(PGO_DIR)

fix-timestamps:
	@echo "Fixing file timestamps to now..."
	@find . -type f -exec touch -d "now" {} +

info:
	@echo "CXX       = $(CXX)"
	@echo "PROFILE   = $(PROFILE)"
	@echo "PGO       = $(PGO)"
	@echo "STATIC    = $(STATIC)"
	@echo "LTO_JOBS  = $(LTO_JOBS)"
	@echo "CXXFLAGS  = $(CXXFLAGS)"
	@echo "LDFLAGS   = $(LDFLAGS)"
	@echo "SRCS      = $(SRC)"
	@echo "OBJS      = $(OBJ)"
