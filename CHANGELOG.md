# Deep Becky 1.0

---

# 🇬🇧 English Version

## New Features and Improvements over version 0.2

### 🏗️ Multi-File Architecture

Version 0.2 was a single monolithic file (`deepbecky02.cpp`). Version 1.0 splits the engine into a modular multi-file architecture:

| File | Description |
|------|-------------|
| `engine.h` | Header with all types, constants and class declaration |
| `engine.cpp` | Board representation, make/undo move, UCI loop |
| `eval.cpp` | Full evaluation function |
| `movegen.cpp` | Move generation and attack detection |
| `search.cpp` | Search (PVS, quiescence, iterative deepening) |
| `magic.cpp` / `magic.h` | Magic Bitboard tables and initialization |
| `main.cpp` | Entry point |

---

### ♟️ Full Bitboard Board Representation

The most significant change: version 0.2 used a simple `int b[8][8]` array. Version 1.0 uses a **full bitboard representation**:

```cpp
U64 bitboards[13];        // One bitboard per piece type
U64 color_bitboards[2];   // One bitboard per color (WHITE, BLACK)
int piece_board[64];      // Mailbox for O(1) piece lookup
int king_sq[2];           // King positions tracked at all times
```

This enables vastly faster move generation, attack detection, and evaluation using bitwise operations.

---

### 🪄 Magic Bitboards for Sliding Pieces

Complete **Magic Bitboard** system for bishops, rooks, and queens:
- Pre-computed magic numbers for all 64 squares
- Lookup tables for instant sliding piece attack generation
- `Magic::rookAttacks(sq, occ)` and `Magic::bishopAttacks(sq, occ)` for O(1) attack computation

Additionally, **pre-computed attack tables** for non-sliding pieces:
- `KNIGHT_ATK_BB[64]`, `KING_ATK_BB[64]`
- `WPAWN_ATK_BB[64]`, `BPAWN_ATK_BB[64]`

---

### ⚡ Bitboard-Based Move Generation

Pawn moves now use **bulk bitwise shift operations** instead of per-piece loops:
```cpp
U64 oneStep = (pawns << 8) & empty_squares;   // All white pawn pushes at once
U64 capL = ((pawns & ~FILE_A) << 7) & opp;    // All left captures at once
```

Sliding piece moves use Magic Bitboard lookups. Castling uses direct occupancy bitmask checks (`0x60ULL`, `0xEULL`, etc.) instead of individual square checks.

---

### 🔍 Enhanced Search

#### Null Move Pruning (NEW)
Version 1.0 adds **Null Move Pruning** — when not in check and at sufficient depth, the engine tries passing the turn to see if the position is still good enough to cause a beta cutoff:
```cpp
if(!isInCheck && depth >= 3 && ply > 0){
    makeNullMove();
    int R = 2 + (depth / 6);
    int nmScore = -pvs(depth - 1 - R, ply+1, -beta, -beta+1);
    undoNullMove();
    if(nmScore >= beta) return beta;
}
```

#### Improved Late Move Reductions (LMR)
LMR now triggers at `moveCount > 3` and `depth >= 3`, and also skips promotions (not only captures and castles as in v0.2).

#### Delta Pruning in Quiescence Search (NEW)
Captures that cannot possibly raise alpha are pruned:
```cpp
if(stand + capGain + promoGain + 100 < alpha) continue;
```

#### In-Search Legality Check
Instead of generating all legal moves upfront (expensive), v1.0 generates pseudo-legal moves and checks legality inline during search (make → check king → undo), saving time on positions with many illegal pseudo-legal moves.

#### Improved Time Management
- Time is checked only every 16,384 nodes (`nodes & 0x3FFF`) instead of every node
- Search stops early if more than half the allocated time has been spent
- 50-move rule draw detection (`halfmove >= 100`)

#### Full PV Line Display
New `getPV()` and `pvToString()` functions extract the Principal Variation from the TT, showing the full expected line in UCI output.

#### Improved Aspiration Windows
Starts at depth >= 4 with a tighter window of 25 cp (vs depth >= 3 with 35 + d×3); simpler and more robust re-search when failing.

---

### 📊 Vastly Improved Evaluation

#### Proper Tapered Evaluation (MG/EG)
Full middlegame/endgame score blending using a **phase counter** based on remaining material:
```
Knights/Bishops = 1, Rooks = 2, Queens = 4 (max phase = 24)
score = (scoreMG × phase + scoreEG × (24 − phase)) / 24
```

#### Complete Piece Mobility
Mobility is now computed for **all piece types** (knights, bishops, rooks, queens) with separate MG/EG weights, using bitboard attack tables. Version 0.2 only counted rook/queen mobility on rank/file lines.

#### Pawn Structure (NEW)
- **Doubled pawns** — penalty per extra pawn on the same file
- **Isolated pawns** — penalty for pawns with no friendly pawns on adjacent files
- **Passed pawns** — rank-based bonus tables (increasing bonus as pawn advances)

#### Rook on Open/Semi-Open Files (NEW)
Bonuses for rooks placed on files with no own pawns (semi-open) or no pawns at all (open).

#### Knight Outposts (NEW)
Bonus for knights on advanced squares that cannot be attacked by enemy pawns, with extra bonus if supported by own pawn.

#### King Safety (NEW)
- **Pawn shield** score around the king
- **King ring pressure** — penalty based on enemy piece attacks on squares surrounding the king
- **King activity in endgame** — mobility bonus for active king

#### Tempo Bonus (NEW)
Small positional bonus for the side to move (+10 MG, +5 EG).

#### 50-Move Rule Damping (NEW)
Score is gradually reduced as the halfmove counter increases, reflecting the approaching draw:
```cpp
score -= score * halfmove / 212;
```

---

### ⚙️ Incremental Zobrist Hashing

Version 0.2 recomputed the hash from scratch after every move (`computeHash()`). Version 1.0 updates the hash **incrementally** in `makeMove()` / `undoMove()`, only XOR-ing the changed bits. Much faster.

---

### 🔧 Professional Build System (Makefile)

New Makefile with advanced compilation features:
- **LTO** (Link-Time Optimization) enabled by default
- **PGO** (Profile-Guided Optimization) support (`make pgo-gen`, `make pgo-use`, `make profile-build`)
- **Multiple architecture profiles**: `portable` (default), `avx2`, `bmi2`, `native`
- **Static linking** by default for portable executables
- Compatible with **MSYS2 MinGW-w64** and standard Linux/macOS GCC/Clang

---

### 📈 Performance

The combined effect of bitboards, magic bitboards, incremental hashing, null move pruning, and delta pruning results in **dramatically higher NPS** (nodes per second) compared to version 0.2, allowing the engine to search deeper in the same amount of time.

---

### 🐛 Bug Fixes and Improvements

- **Node counter overflow**: Changed from `int` to `long long`
- **Lighter Undo struct**: Removed redundant fields (captured, full_before, side_before) — uses `piece_moved` and `captured_piece` from Move struct
- **Improved `uciToMove()`**: Searches pseudo-legal and legal move lists for proper move matching with all flags correctly set
- **Opening book removed**: The minimal hardcoded opening book from v0.2 was removed in favor of relying on search strength

---

## Strength Comparison

| Feature | v0.2 | v1.0 |
|---------|------|------|
| Board Representation | Array 8×8 | Full Bitboard |
| Magic Bitboards | ❌ | ✅ |
| Bitboard Move Generation | ❌ | ✅ |
| Incremental Hashing | ❌ | ✅ |
| Null Move Pruning | ❌ | ✅ |
| Delta Pruning (QSearch) | ❌ | ✅ |
| Full PV Line | ❌ | ✅ |
| Pawn Structure (doubled/isolated/passed) | ❌ | ✅ |
| Rook on Open Files | ❌ | ✅ |
| Knight Outposts | ❌ | ✅ |
| King Safety (shield + pressure) | ❌ | ✅ |
| Tapered Eval (proper MG/EG blend) | Partial | ✅ |
| Full Mobility (all pieces) | Partial | ✅ |
| Tempo Bonus | ❌ | ✅ |
| 50-Move Damping | ❌ | ✅ |
| LTO + PGO Build | ❌ | ✅ |
| Multi-File Architecture | ❌ | ✅ |

---

## Compilation

### MSYS2 MinGW-w64
```bash
make                                # portable build (default)
make PROFILE=avx2                   # AVX2 build
make PROFILE=native                 # native (best for local CPU)
make profile-build PROFILE=avx2     # PGO-optimized AVX2 build
```

### MSVC (Developer Command Prompt)
```bash
cl /nologo /EHsc /O2 /std:c++17 /DNDEBUG /MT /arch:AVX2 main.cpp engine.cpp eval.cpp magic.cpp movegen.cpp search.cpp /Fe:deepbecky-v1.0-windows-x64.exe /link /LTCG /OPT:REF /OPT:ICF
```

---

---

# 🇧🇷 Versão em Português

## Novidades e Melhorias em relação à versão 0.2

### 🏗️ Arquitetura Multi-Arquivo

A versão 0.2 era um único arquivo monolítico (`deepbecky02.cpp`). A versão 1.0 divide a engine em uma arquitetura modular multi-arquivo:

| Arquivo | Descrição |
|---------|-----------|
| `engine.h` | Header com todos os tipos, constantes e declaração da classe |
| `engine.cpp` | Representação do tabuleiro, make/undo move, loop UCI |
| `eval.cpp` | Função de avaliação completa |
| `movegen.cpp` | Geração de movimentos e detecção de ataques |
| `search.cpp` | Busca (PVS, quiescência, aprofundamento iterativo) |
| `magic.cpp` / `magic.h` | Tabelas e inicialização de Magic Bitboards |
| `main.cpp` | Ponto de entrada |

---

### ♟️ Representação Completa com Bitboards

A mudança mais significativa: a versão 0.2 usava um simples array `int b[8][8]`. A versão 1.0 usa uma **representação completa com bitboards**:

```cpp
U64 bitboards[13];        // Um bitboard por tipo de peça
U64 color_bitboards[2];   // Um bitboard por cor (WHITE, BLACK)
int piece_board[64];      // Mailbox para lookup O(1) de peça
int king_sq[2];           // Posições dos reis rastreadas a todo momento
```

Isso permite geração de movimentos, detecção de ataques e avaliação muito mais rápidas usando operações bitwise.

---

### 🪄 Magic Bitboards para Peças Deslizantes

Sistema completo de **Magic Bitboards** para bispos, torres e damas:
- Números mágicos pré-computados para todas as 64 casas
- Tabelas de lookup para geração instantânea de ataques de peças deslizantes
- `Magic::rookAttacks(sq, occ)` e `Magic::bishopAttacks(sq, occ)` para cálculo de ataques em O(1)

Além disso, **tabelas de ataques pré-computadas** para peças não-deslizantes:
- `KNIGHT_ATK_BB[64]`, `KING_ATK_BB[64]`
- `WPAWN_ATK_BB[64]`, `BPAWN_ATK_BB[64]`

---

### ⚡ Geração de Movimentos Baseada em Bitboards

Movimentos de peões agora usam **operações de shift em massa** em vez de loops por peça:
```cpp
U64 oneStep = (pawns << 8) & empty_squares;   // Todos os avanços de peões brancos de uma vez
U64 capL = ((pawns & ~FILE_A) << 7) & opp;    // Todas as capturas à esquerda de uma vez
```

Movimentos de peças deslizantes usam lookups de Magic Bitboards. O roque usa verificação direta de bitmasks de ocupação (`0x60ULL`, `0xEULL`, etc.) em vez de verificações casa por casa.

---

### 🔍 Busca Aprimorada

#### Null Move Pruning (NOVO)
A versão 1.0 adiciona **Null Move Pruning** — quando não está em xeque e em profundidade suficiente, a engine tenta passar a vez para ver se a posição ainda é boa o suficiente para causar um corte beta:
```cpp
if(!isInCheck && depth >= 3 && ply > 0){
    makeNullMove();
    int R = 2 + (depth / 6);
    int nmScore = -pvs(depth - 1 - R, ply+1, -beta, -beta+1);
    undoNullMove();
    if(nmScore >= beta) return beta;
}
```

#### Late Move Reductions (LMR) Melhoradas
LMR agora ativa em `moveCount > 3` e `depth >= 3`, e também ignora promoções (não apenas capturas e roques como na v0.2).

#### Delta Pruning na Quiescence Search (NOVO)
Capturas que não podem possivelmente melhorar alpha são podadas:
```cpp
if(stand + capGain + promoGain + 100 < alpha) continue;
```

#### Verificação de Legalidade In-Search
Em vez de gerar todos os movimentos legais antecipadamente (caro), a v1.0 gera movimentos pseudo-legais e verifica a legalidade inline durante a busca (faz → verifica rei → desfaz), economizando tempo em posições com muitos movimentos pseudo-legais ilegais.

#### Gerenciamento de Tempo Melhorado
- Tempo verificado apenas a cada 16.384 nós (`nodes & 0x3FFF`) em vez de a cada nó
- Busca para cedo se mais da metade do tempo alocado foi gasto
- Detecção de empate pela regra dos 50 movimentos (`halfmove >= 100`)

#### Linha PV Completa
Novas funções `getPV()` e `pvToString()` extraem a Variação Principal da TT, mostrando a linha completa esperada na saída UCI.

#### Aspiration Windows Melhoradas
Inicia na profundidade >= 4 com janela mais apertada de 25 cp (vs profundidade >= 3 com 35 + d×3); re-busca mais simples e robusta em caso de falha.

---

### 📊 Avaliação Muito Mais Completa

#### Avaliação Tapered Correta (MG/EG)
Mistura completa de pontuações middlegame/endgame usando um **contador de fase** baseado no material restante:
```
Cavalos/Bispos = 1, Torres = 2, Damas = 4 (fase máxima = 24)
score = (scoreMG × fase + scoreEG × (24 − fase)) / 24
```

#### Mobilidade Completa de Peças
A mobilidade agora é calculada para **todos os tipos de peças** (cavalos, bispos, torres, damas) com pesos separados MG/EG, usando tabelas de ataques bitboard. A versão 0.2 contava apenas a mobilidade de torres/damas nas linhas de fileira/coluna.

#### Estrutura de Peões (NOVO)
- **Peões dobrados** — penalidade por peão extra na mesma coluna
- **Peões isolados** — penalidade para peões sem peões amigos nas colunas adjacentes
- **Peões passados** — tabelas de bônus por fileira (bônus crescente conforme o peão avança)

#### Torres em Colunas Abertas/Semi-Abertas (NOVO)
Bônus para torres posicionadas em colunas sem peões próprios (semi-aberta) ou sem peões de nenhum lado (aberta).

#### Cavalos em Postos Avançados (NOVO)
Bônus para cavalos em casas avançadas que não podem ser atacadas por peões inimigos, com bônus extra se apoiado por peão próprio.

#### Segurança do Rei (NOVO)
- **Escudo de peões** ao redor do rei
- **Pressão no anel do rei** — penalidade baseada nos ataques de peças inimigas nas casas ao redor do rei
- **Atividade do rei no endgame** — bônus de mobilidade para rei ativo

#### Bônus de Tempo (NOVO)
Pequeno bônus posicional para o lado a mover (+10 MG, +5 EG).

#### Amortecimento da Regra dos 50 Movimentos (NOVO)
A pontuação é gradualmente reduzida conforme o contador de meias-jogadas aumenta, refletindo o empate se aproximando:
```cpp
score -= score * halfmove / 212;
```

---

### ⚙️ Hashing Zobrist Incremental

A versão 0.2 recalculava o hash do zero após cada movimento (`computeHash()`). A versão 1.0 atualiza o hash **incrementalmente** em `makeMove()` / `undoMove()`, fazendo XOR apenas nos bits alterados. Muito mais rápido.

---

### 🔧 Sistema de Build Profissional (Makefile)

Novo Makefile com recursos avançados de compilação:
- **LTO** (Link-Time Optimization) habilitado por padrão
- Suporte a **PGO** (Profile-Guided Optimization) (`make pgo-gen`, `make pgo-use`, `make profile-build`)
- **Múltiplos perfis de arquitetura**: `portable` (padrão), `avx2`, `bmi2`, `native`
- **Linkagem estática** por padrão para executáveis portáteis
- Compatível com **MSYS2 MinGW-w64** e GCC/Clang padrão em Linux/macOS

---

### 📈 Desempenho

O efeito combinado de bitboards, magic bitboards, hashing incremental, null move pruning e delta pruning resulta em **NPS (nós por segundo) dramaticamente mais alto** comparado à versão 0.2, permitindo que a engine busque mais fundo no mesmo período de tempo.

---

### 🐛 Correções de Bugs e Melhorias

- **Overflow do contador de nós**: Alterado de `int` para `long long`
- **Struct Undo mais leve**: Campos redundantes removidos (captured, full_before, side_before) — usa `piece_moved` e `captured_piece` da struct Move
- **`uciToMove()` melhorado**: Busca nas listas de movimentos pseudo-legais e legais para correspondência correta com todas as flags
- **Opening book removido**: O livro de aberturas mínimo hardcoded da v0.2 foi removido em favor da força da busca

---

## Comparativo de Força

| Característica | v0.2 | v1.0 |
|----------------|------|------|
| Representação do Tabuleiro | Array 8×8 | Bitboard Completo |
| Magic Bitboards | ❌ | ✅ |
| Geração de Movimentos Bitboard | ❌ | ✅ |
| Hashing Incremental | ❌ | ✅ |
| Null Move Pruning | ❌ | ✅ |
| Delta Pruning (QSearch) | ❌ | ✅ |
| Linha PV Completa | ❌ | ✅ |
| Estrutura de Peões (dobr./isol./pass.) | ❌ | ✅ |
| Torres em Colunas Abertas | ❌ | ✅ |
| Cavalos em Postos Avançados | ❌ | ✅ |
| Segurança do Rei (escudo + pressão) | ❌ | ✅ |
| Avaliação Tapered (blend MG/EG correto) | Parcial | ✅ |
| Mobilidade Completa (todas peças) | Parcial | ✅ |
| Bônus de Tempo | ❌ | ✅ |
| Amortecimento 50 Movimentos | ❌ | ✅ |
| Build LTO + PGO | ❌ | ✅ |
| Arquitetura Multi-Arquivo | ❌ | ✅ |

---

## Compilação

### MSYS2 MinGW-w64
```bash
make                                # build portátil (padrão)
make PROFILE=avx2                   # build AVX2
make PROFILE=native                 # nativo (melhor para CPU local)
make profile-build PROFILE=avx2     # build PGO otimizado com AVX2
```

### MSVC (Prompt de Comando do Desenvolvedor)
```bash
cl /nologo /EHsc /O2 /std:c++17 /DNDEBUG /MT /arch:AVX2 main.cpp engine.cpp eval.cpp magic.cpp movegen.cpp search.cpp /Fe:deepbecky-v1.0-windows-x64.exe /link /LTCG /OPT:REF /OPT:ICF
```

---

*Deep Becky - UCI Chess Engine by Diogo de Oliveira Almeida*

---

---

# Deep Becky 0.2

---

# 🇬🇧 English Version

## New Features and Improvements over version 0.1

### 🔍 Enhanced Search Algorithm

#### Aspiration Windows
Version 0.2 implements **Aspiration Windows** in Iterative Deepening. Instead of always searching with a full window (-∞, +∞), the engine starts with a narrow window based on the previous iteration's score:

```cpp
if(d >= 3){
    int window = 35 + d*3;
    A = prev - window;
    B = prev + window;
}
```

If the search fails (score outside the window), the window is progressively expanded. This results in **faster cutoffs** and significant time savings.

#### Late Move Reductions (LMR)
Implementation of **light LMR**: late moves that are neither captures nor castles are initially searched at reduced depth. If they look promising, a full re-search is performed:

```cpp
if(newDepth >= 2 && !m.is_capture && !m.is_castle){
    sc = -pvs(newDepth-1, ply+1, -alpha-1, -alpha);
}
```

#### Check Extension
When the side to move is in check, the search depth is **extended by 1 ply**, ensuring more complete analysis of critical tactical lines.

#### Mate Distance Pruning
Early pruning based on mate distance, avoiding unnecessary searches when a mate has already been found at a shallower depth.

---

### 📊 Improved Evaluation

#### Mobility
Version 0.2 adds a **mobility term** for rooks and queens, counting free squares on horizontal and vertical lines. Pieces with greater mobility receive bonuses.

#### Piece-Square Tables (PST)
Separate and optimized PST tables for each piece type, including distinct tables for the king in **middlegame** and **endgame** (PST_KING_MG and PST_KING_EG).

#### Bishop Pair Bonus
Detection and bonus (+25 centipawns) for the side that has the **bishop pair**.

---

### 🗂️ Move Ordering

#### Improved MVV-LVA Ordering
The MVV-LVA (Most Valuable Victim - Least Valuable Attacker) formula has been refined:

```cpp
return 10 * PIECE_VALUE[def] - PIECE_VALUE[att];
```

#### Improved Killers
Dedicated structure (`KillerTable`) with 2 slots per ply for killer moves.

#### Side-Indexed History Heuristic
The history table is now **indexed by side** (white/black), improving ordering accuracy:

```cpp
static int history_heur[2][64][64]; // side, from, to
```

#### Castling Bonus
Castling moves receive an ordering bonus (+50,000), encouraging the engine to consider castling early.

---

### 🎯 Move Generation

#### Pseudo-Legal / Legal Separation
New function `generatePseudo(bool capturesOnly)` allows generating only captures for Quiescence Search, saving time.

#### Detailed Flags
`Move` structure with explicit flags:
- `is_capture`
- `is_enpassant`  
- `is_castle`
- `is_doublepush`
- `captured_piece`
- `score`

---

### ⚡ Performance Optimizations

#### Simplified Undo Structure
Lighter undo stack, storing only the essentials:
```cpp
struct Undo {
    int captured, castling_before, ep_before, half_before, full_before;
    bool side_before;
    uint64_t hash_before;
};
```

#### Castling Rights with Bitmask
Compact representation of castling rights using **4 bits** (`0b1111` = KQkq), allowing fast operations with masks.

---

### 📖 Opening Book
Basic integrated opening book system, allowing instant responses in the first moves of known lines.

---

### 🔧 UCI Protocol

#### Support for New Commands
- `go depth N` - Search to specific depth
- `go infinite` - Search until receiving `stop`
- `go movestogo` - Support for time control with move count
- Time increment (`winc`/`binc`)

#### Improved Output
Correct calculation and sending of **NPS (nodes per second)** to the GUI:
```cpp
long long nps = (nodes * 1000) / ms;
cout << "... nps " << nps << " ...";
```

---

### 🐛 Bug Fixes

#### Pawn Attack Verification
Critical fix in the `isAttacked()` function: the pawn attack direction was inverted in version 0.1, causing failures in pawn protection detection.

#### Board Restoration (undoMove)
Fix in board restoration after castling moves, preventing piece duplication or state corruption.

---

## Strength Comparison

| Feature | v0.1 | v0.2 |
|---------|------|------|
| Aspiration Windows | ❌ | ✅ |
| LMR | ❌ | ✅ |
| Check Extension | ❌ | ✅ |
| Mate Distance Pruning | ❌ | ✅ |
| Mobility | ❌ | ✅ |
| King Endgame PST | Partial | ✅ |
| Side-Indexed History | ❌ | ✅ |
| Captures-Only Generation | ❌ | ✅ |
| Opening Book | ❌ | ✅ |

---

## Compilation

```bash
g++ -O3 -std=c++17 -march=native -DNDEBUG deepbecky02.cpp -o deepbecky-v0.2-windows-x64
```

---

---

# 🇧🇷 Versão em Português

## Novidades e Melhorias em relação à versão 0.1

### 🔍 Algoritmo de Busca Aprimorado

#### Aspiration Windows
A versão 0.2 implementa **Aspiration Windows** no Iterative Deepening. Em vez de buscar sempre com janela completa (-∞, +∞), a engine começa com uma janela estreita baseada na pontuação da iteração anterior:

```cpp
if(d >= 3){
    int window = 35 + d*3;
    A = prev - window;
    B = prev + window;
}
```

Se a busca falhar (score fora da janela), a janela é expandida progressivamente. Isso resulta em **cortes mais rápidos** e economia significativa de tempo.

#### Late Move Reductions (LMR)
Implementação de **LMR leve**: movimentos tardios que não são capturas nem roques são buscados inicialmente com profundidade reduzida. Se parecerem promissores, uma re-busca completa é feita:

```cpp
if(newDepth >= 2 && !m.is_capture && !m.is_castle){
    sc = -pvs(newDepth-1, ply+1, -alpha-1, -alpha);
}
```

#### Check Extension
Quando o lado a jogar está em xeque, a profundidade de busca é **estendida em 1 ply**, garantindo análise mais completa de linhas táticas críticas.

#### Mate Distance Pruning
Poda antecipada baseada na distância do mate, evitando buscas desnecessárias quando um mate já foi encontrado em profundidade menor.

---

### 📊 Avaliação Melhorada

#### Mobilidade
A versão 0.2 adiciona um **termo de mobilidade** para torres e damas, contando casas livres nas linhas horizontais e verticais. Peças com maior mobilidade recebem bônus.

#### Piece-Square Tables (PST)
Tabelas PST separadas e otimizadas para cada tipo de peça, incluindo tabelas distintas para o rei no **middlegame** e **endgame** (PST_KING_MG e PST_KING_EG).

#### Bônus de Par de Bispos
Detecção e bonificação (+25 centipawns) para o lado que possui o **par de bispos**.

---

### 🗂️ Ordenação de Movimentos

#### Ordenação MVV-LVA Aprimorada
A fórmula MVV-LVA (Most Valuable Victim - Least Valuable Attacker) foi refinada:

```cpp
return 10 * PIECE_VALUE[def] - PIECE_VALUE[att];
```

#### Killers Melhorados
Estrutura dedicada (`KillerTable`) com 2 slots por ply para movimentos killer.

#### History Heuristic por Lado
A tabela de história agora é **indexada por lado** (brancas/pretas), melhorando a precisão da ordenação:

```cpp
static int history_heur[2][64][64]; // side, from, to
```

#### Bônus para Roques
Movimentos de roque recebem bônus de ordenação (+50.000), incentivando a engine a considerar o roque cedo.

---

### 🎯 Geração de Movimentos

#### Separação Pseudo-Legal / Legal
Nova função `generatePseudo(bool capturesOnly)` permite gerar apenas capturas para a Quiescence Search, economizando tempo.

#### Flags Detalhadas
Estrutura `Move` com flags explícitas:
- `is_capture`
- `is_enpassant`  
- `is_castle`
- `is_doublepush`
- `captured_piece`
- `score`

---

### ⚡ Otimizações de Desempenho

#### Estrutura Undo Simplificada
Pilha de undo mais leve, armazenando apenas o essencial:
```cpp
struct Undo {
    int captured, castling_before, ep_before, half_before, full_before;
    bool side_before;
    uint64_t hash_before;
};
```

#### Direitos de Roque com Bitmask
Representação compacta dos direitos de roque usando **4 bits** (`0b1111` = KQkq), permitindo operações rápidas com máscaras.

---

### 📖 Opening Book
Sistema básico de livro de aberturas integrado, permitindo respostas instantâneas nas primeiras jogadas de linhas conhecidas.

---

### 🔧 Protocolo UCI

#### Suporte a Novos Comandos
- `go depth N` - Busca até profundidade específica
- `go infinite` - Busca até receber `stop`
- `go movestogo` - Suporte a controle de tempo com número de lances
- Incremento de tempo (`winc`/`binc`)

#### Output Melhorado
Cálculo e envio correto do **NPS (nodes per second)** para a GUI:
```cpp
long long nps = (nodes * 1000) / ms;
cout << "... nps " << nps << " ...";
```

---

### 🐛 Correções de Bugs

#### Verificação de Ataque por Peões
Correção crítica na função `isAttacked()`: a direção de ataque dos peões estava invertida na versão 0.1, causando falhas na detecção de proteção por peões.

#### Restauração do Tabuleiro (undoMove)
Correção na restauração do tabuleiro após movimentos de roque, evitando duplicação de peças ou corrupção do estado.

---

## Comparativo de Força

| Característica | v0.1 | v0.2 |
|---------------|------|------|
| Aspiration Windows | ❌ | ✅ |
| LMR | ❌ | ✅ |
| Check Extension | ❌ | ✅ |
| Mate Distance Pruning | ❌ | ✅ |
| Mobilidade | ❌ | ✅ |
| PST Endgame Rei | Parcial | ✅ |
| History por Lado | ❌ | ✅ |
| Geração só capturas | ❌ | ✅ |
| Opening Book | ❌ | ✅ |

---

## Compilação

```bash
g++ -O3 -std=c++17 -march=native -DNDEBUG deepbecky02.cpp -o deepbecky-v0.2-windows-x64
```

---

*Deep Becky - UCI Chess Engine by Diogo de Oliveira Almeida*

