# Deep Becky - Changelog

---

# Deep Becky 1.2

---

# 🇬🇧 English Version

## Improvements over version 1.1

### 🏗️ Full Architectural Restructure

Version 1.1 used 8 source files centered around a monolithic `engine.h` / `engine.cpp`. Version 1.2 splits the codebase into **25 files** with proper header/implementation separation, namespaces, and single-responsibility modules:

| File | Description |
|------|-------------|
| `types.h` | Core types, enums (Square, Color, Piece), constants, helper functions |
| `bitboard.h` / `bitboard.cpp` | Bitboard constants, attack tables, BETWEEN_BB, LINE_BB, RAY_BB |
| `position.h` / `position.cpp` | Position class, make/undo move, FEN, repetition, SEE |
| `search.h` / `search.cpp` | PVS, quiescence, iterative deepening, pruning techniques |
| `evaluate.h` / `evaluate.cpp` | Full evaluation function with pawn hash |
| `movegen.h` / `movegen.cpp` | Move generation, legality, attack detection, perft |
| `movepick.h` / `movepick.cpp` | Staged move ordering (MovePicker) |
| `tt.h` / `tt.cpp` | TranspositionTable class with dynamic sizing |
| `timeman.h` / `timeman.cpp` | TimeManagement class (Stockfish-style) |
| `uci.h` / `uci.cpp` | UCI protocol handling |
| `magic.h` / `magic.cpp` | Magic Bitboard tables and initialization |
| `main.cpp` | Entry point |

GPL v3 license headers added to all source files.

---

### 🔍 Search Improvements

#### LMR Reduction Table (NEW)
Pre-computed logarithmic reduction table `Reductions[64][64]` using the formula `log(d) × log(m) / 2`, with adjustments for PV nodes, improving moves, killers, and check moves.

#### Razoring (NEW)
At depth ≤ 1, if the static evaluation is far below alpha, the engine drops into quiescence search directly:
```cpp
if (depth <= 1 && staticEval + 300 <= alpha) return qsearch(alpha, beta, ply);
```

#### Reverse Futility Pruning / Static Null Move Pruning (NEW)
At shallow depths (< 5), if the static evaluation exceeds beta by a depth-dependent margin, the position is pruned early:
```cpp
if (depth < 5 && staticEval - 80 * depth >= beta) return staticEval;
```

#### Internal Iterative Deepening (NEW)
When at depth ≥ 6 with no TT move available, a shallow search (depth - 2) is performed first to find a likely best move for better ordering.

#### Capture Extension for Sacrifices (NEW)
Captures that lose material (SEE < 0) but are the only reasonable option are extended by 1 ply to ensure proper tactical resolution.

#### Late Move Pruning / Move Count Pruning (NEW)
Quiet moves beyond a depth-dependent count threshold are pruned entirely at shallow depths.

#### Futility Pruning at Child Node (NEW)
Quiet moves at shallow depths where the static evaluation plus a margin cannot reach alpha are skipped.

#### SEE Pruning for Captures (NEW)
Captures with negative SEE (losing exchanges) are pruned at shallow depths in the main search.

#### Dynamic Contempt (NEW)
Contempt now scales dynamically based on the root evaluation (20–200 cp), applying stronger draw avoidance when the engine is winning.

#### Minimum Depth Guarantee (NEW)
The engine now always completes at least 4 iterations of iterative deepening before time management can stop the search, preventing shallow blunders.

#### Mate Finding Optimization (NEW)
When a forced mate is found, the engine stops searching only after the best move is stable for at least 2 iterations and the search depth is sufficient to confirm the mate distance.

---

### ⏱️ TimeManagement Class (NEW)

Complete Stockfish-style time management system in a dedicated module (`timeman.h` / `timeman.cpp`):

- **Optimum/maximum time** calculation with distinct allocation strategies
- **Move importance** function — allocates more time to critical moments
- **Stability adjustment** — extends time when the best move keeps changing between iterations
- **Score drop adjustment** — extends time when the evaluation drops significantly
- **Legal moves factor** — reduces time for positions with few legal moves
- **Obvious move detection** — plays instantly when only one or two legal moves exist, or when a simple recapture is available
- **Game phase awareness** — different time allocation for opening, middlegame, and endgame
- **Safety caps** — hard limits to prevent time losses

---

### 🗃️ TranspositionTable Class (NEW)

Dedicated transposition table module (`tt.h` / `tt.cpp`) with professional features:

- **Dynamic sizing** via UCI option `setoption name Hash value N` (1–4096 MB)
- **Cache-aligned allocation** for optimal CPU cache performance
- **`#pragma pack`** 16-byte entries for memory efficiency
- **`prefetch()`** for memory prefetching before probing
- **`hashfull()`** reporting in UCI info strings
- **Depth-preferred replacement** with aging between searches

---

### 🏗️ Bitboard Infrastructure Upgrade

New dedicated bitboard module (`bitboard.h` / `bitboard.cpp`) with:

- Named file/rank constants (`FileABB`..`FileHBB`, `Rank1BB`..`Rank8BB`)
- `BETWEEN_BB[64][64]` — squares strictly between two aligned squares (used for pin detection and check evasion)
- `LINE_BB[64][64]` — complete line through two aligned squares (used for pin-aware move legality)
- `RAY_BB[64][8]` — ray tables in all 8 directions
- Inline attack helper functions: `pawnAttacks()`, `knightAttacks()`, `kingAttacks()`, `queenAttacks()`

---

### ♟️ Position Class Enrichments

The engine class was renamed from `DeepBeckyEngine` to `Position` with many new capabilities:

- **Pawn hash key** (`pawnKey`) with dedicated pawn evaluation cache (`PawnEntry`, 65536 entries) — avoids recomputing pawn structure when only pieces move
- **Material hash key** (`materialKey`) with material evaluation cache (`MaterialEntry`)
- **`attackersTo(sq, occ)`** — returns all pieces attacking a square for any occupancy
- **`checkersBB()`** — bitboard of pieces giving check to one side
- **`pinnedBB()`** — bitboard of pinned pieces
- **`blockersForKing()`** — pieces that block sliding attacks on the king (pinners output)
- **`SEE()` with threshold** — full static exchange evaluation with X-ray support and early exit optimization
- **`hasNonPawnMaterial()`** — used for null move pruning safety (prevents null move in king+pawn endgames)
- **`selDepth`** tracking — reports maximum selective depth reached
- Improved repetition detection with `RepState` struct tracking repetition distance

---

### ♟️ Move Generation Improvements

- **Optimized legal move generation** using `checkersBB`, `pinnedBB`, `blockersForKing`, `BETWEEN_BB`, and `LINE_BB` for fast check evasion and pin-aware filtering
- **Special en passant legality check** for horizontal pin edge cases
- **Double-check handling** — when in double check, only king moves are generated
- **`perft()` function** for correctness testing and debugging

---

### 📊 Evaluation Improvements

- **Lazy evaluation** — when material advantage exceeds ±2000 cp (20 pawns) in non-endgame positions, detailed evaluation is skipped for speed
- **Pawn hash table** — pawn structure evaluation (doubled, isolated, passed pawns) is cached and reused when only pieces move
- **Never lazy eval in endgames** — ensures precise evaluation when material is low

---

### 🔧 UCI Improvements

- **`setoption name Hash value N`** — dynamically resize the transposition table (1–4096 MB)
- **`perft` command** with divide output for debugging and correctness verification
- **`hashfull`** reported in UCI info output

---

### 🛠 Build System

- Updated default binary name to `deepbecky-v1.2-windows-x64.exe`
- **SSE4.2 profile** added (`PROFILE=sse42`)
- **Cross-platform Makefile** works in both MSYS2 and Windows CMD/PowerShell
- **PGO training** uses 6 diverse positions (startpos, Italian, QGD, Kiwipete, endgame, promotions) for better profile coverage
- **Configurable PGO depth** (`PGO_DEPTH` variable)
- **`help` and `info` targets** for build system documentation
- **Debug build target** (`make debug`) with `-g -O0` flags

---

## Strength Comparison

| Feature | v1.1 | v1.2 |
|---------|------|------|
| Architecture | 8 files, single header | 25 files, full H/CPP separation |
| TT Dynamic Sizing | ❌ | ✅ (UCI `Hash` option) |
| LMR Log Table | ❌ | ✅ (`Reductions[64][64]`) |
| Razoring | ❌ | ✅ |
| Reverse Futility Pruning | ❌ | ✅ |
| Internal Iterative Deepening | ❌ | ✅ |
| Late Move Pruning | ❌ | ✅ |
| Futility Pruning (child) | ❌ | ✅ |
| SEE Pruning (captures) | ❌ | ✅ |
| Dynamic Contempt | Fixed ±20 cp | Scaled 20–200 cp |
| TimeManagement Class | Basic | Stockfish-style |
| Pawn Hash Table | ❌ | ✅ |
| Lazy Evaluation | ❌ | ✅ |
| BETWEEN_BB / LINE_BB Tables | ❌ | ✅ |
| Pin-Aware Legal Gen | ❌ | ✅ |
| Perft Command | ❌ | ✅ |
| `hashfull` Reporting | ❌ | ✅ |
| Cache-Aligned TT | ❌ | ✅ |
| TT Prefetch | ❌ | ✅ |
| Minimum Depth Guarantee | ❌ | ✅ (4 plies) |
| GPL v3 License Headers | ❌ | ✅ |

---

---

# 🇧🇷 Versão em Português

## Melhorias em relação à versão 1.1

### 🏗️ Reestruturação Arquitetural Completa

A versão 1.1 usava 8 arquivos-fonte centralizados em `engine.h` / `engine.cpp`. A versão 1.2 divide o código em **25 arquivos** com separação adequada header/implementação, namespaces e módulos com responsabilidade única:

| Arquivo | Descrição |
|---------|-----------|
| `types.h` | Tipos base, enums (Square, Color, Piece), constantes, funções auxiliares |
| `bitboard.h` / `bitboard.cpp` | Constantes de bitboard, tabelas de ataques, BETWEEN_BB, LINE_BB, RAY_BB |
| `position.h` / `position.cpp` | Classe Position, make/undo move, FEN, repetição, SEE |
| `search.h` / `search.cpp` | PVS, quiescência, aprofundamento iterativo, técnicas de poda |
| `evaluate.h` / `evaluate.cpp` | Função de avaliação completa com hash de peões |
| `movegen.h` / `movegen.cpp` | Geração de movimentos, legalidade, detecção de ataques, perft |
| `movepick.h` / `movepick.cpp` | Ordenação de lances em estágios (MovePicker) |
| `tt.h` / `tt.cpp` | Classe TranspositionTable com dimensionamento dinâmico |
| `timeman.h` / `timeman.cpp` | Classe TimeManagement (estilo Stockfish) |
| `uci.h` / `uci.cpp` | Tratamento do protocolo UCI |
| `magic.h` / `magic.cpp` | Tabelas e inicialização de Magic Bitboards |
| `main.cpp` | Ponto de entrada |

Cabeçalhos de licença GPL v3 adicionados a todos os arquivos-fonte.

---

### 🔍 Melhorias na Busca

#### Tabela de Redução LMR (NOVO)
Tabela de redução logarítmica pré-computada `Reductions[64][64]` usando a fórmula `log(d) × log(m) / 2`, com ajustes para nós PV, lances que melhoram, killers e lances de xeque.

#### Razoring (NOVO)
Na profundidade ≤ 1, se a avaliação estática estiver muito abaixo de alpha, a engine cai diretamente na quiescence search:
```cpp
if (depth <= 1 && staticEval + 300 <= alpha) return qsearch(alpha, beta, ply);
```

#### Reverse Futility Pruning / Static Null Move Pruning (NOVO)
Em profundidades rasas (< 5), se a avaliação estática exceder beta por uma margem dependente da profundidade, a posição é podada antecipadamente:
```cpp
if (depth < 5 && staticEval - 80 * depth >= beta) return staticEval;
```

#### Internal Iterative Deepening (NOVO)
Quando na profundidade ≥ 6 sem lance da TT disponível, uma busca rasa (depth - 2) é realizada primeiro para encontrar o provável melhor lance para melhor ordenação.

#### Extensão de Captura para Sacrifícios (NOVO)
Capturas que perdem material (SEE < 0) mas são a única opção razoável são estendidas em 1 ply para garantir resolução tática adequada.

#### Late Move Pruning / Poda por Contagem de Lances (NOVO)
Lances quietos além de um limite de contagem dependente da profundidade são podados inteiramente em profundidades rasas.

#### Futility Pruning no Nó-Filho (NOVO)
Lances quietos em profundidades rasas onde a avaliação estática mais uma margem não pode alcançar alpha são ignorados.

#### Poda SEE para Capturas (NOVO)
Capturas com SEE negativo (trocas perdedoras) são podadas em profundidades rasas na busca principal.

#### Contempt Dinâmico (NOVO)
O contempt agora escala dinamicamente com base na avaliação da raiz (20–200 cp), aplicando evitação de empate mais forte quando a engine está ganhando.

#### Garantia de Profundidade Mínima (NOVO)
A engine agora sempre completa pelo menos 4 iterações de iterative deepening antes que o gerenciamento de tempo possa parar a busca, prevenindo blunders em profundidades rasas.

#### Otimização de Busca de Mate (NOVO)
Quando um mate forçado é encontrado, a engine só para de buscar depois que o melhor lance for estável por pelo menos 2 iterações e a profundidade de busca for suficiente para confirmar a distância do mate.

---

### ⏱️ Classe TimeManagement (NOVO)

Sistema completo de gerenciamento de tempo estilo Stockfish em módulo dedicado (`timeman.h` / `timeman.cpp`):

- **Tempo ótimo/máximo** com estratégias de alocação distintas
- **Importância do lance** — aloca mais tempo para momentos críticos
- **Ajuste de estabilidade** — estende o tempo quando o melhor lance muda entre iterações
- **Ajuste por queda de score** — estende o tempo quando a avaliação cai significativamente
- **Fator de lances legais** — reduz o tempo para posições com poucos lances legais
- **Detecção de lance óbvio** — joga instantaneamente quando há apenas um ou dois lances legais, ou quando uma recaptura simples está disponível
- **Consciência de fase do jogo** — alocação de tempo diferente para abertura, meio-jogo e final
- **Limites de segurança** — tetos rígidos para prevenir perda por tempo

---

### 🗃️ Classe TranspositionTable (NOVO)

Módulo dedicado de tabela de transposição (`tt.h` / `tt.cpp`) com recursos profissionais:

- **Dimensionamento dinâmico** via opção UCI `setoption name Hash value N` (1–4096 MB)
- **Alocação alinhada ao cache** para desempenho ideal de cache da CPU
- **`#pragma pack`** com entradas de 16 bytes para eficiência de memória
- **`prefetch()`** para pré-busca de memória antes do probing
- **`hashfull()`** reportado nas strings de info UCI
- **Substituição por profundidade preferencial** com envelhecimento entre buscas

---

### 🏗️ Infraestrutura de Bitboard Aprimorada

Novo módulo dedicado de bitboard (`bitboard.h` / `bitboard.cpp`) com:

- Constantes nomeadas de coluna/fileira (`FileABB`..`FileHBB`, `Rank1BB`..`Rank8BB`)
- `BETWEEN_BB[64][64]` — casas estritamente entre duas casas alinhadas (usado para detecção de cravadas e evasão de xeque)
- `LINE_BB[64][64]` — linha completa passando por duas casas alinhadas (usado para legalidade de lances considerando cravadas)
- `RAY_BB[64][8]` — tabelas de raios em todas as 8 direções
- Funções inline de ataque: `pawnAttacks()`, `knightAttacks()`, `kingAttacks()`, `queenAttacks()`

---

### ♟️ Enriquecimento da Classe Position

A classe da engine foi renomeada de `DeepBeckyEngine` para `Position` com muitas novas capacidades:

- **Chave hash de peões** (`pawnKey`) com cache dedicado de avaliação de peões (`PawnEntry`, 65536 entradas) — evita recomputar estrutura de peões quando apenas peças se movem
- **Chave hash de material** (`materialKey`) com cache de avaliação de material (`MaterialEntry`)
- **`attackersTo(sq, occ)`** — retorna todas as peças atacando uma casa para qualquer ocupação
- **`checkersBB()`** — bitboard de peças dando xeque a um lado
- **`pinnedBB()`** — bitboard de peças cravadas
- **`blockersForKing()`** — peças que bloqueiam ataques deslizantes no rei (saída de cravadores)
- **`SEE()` com threshold** — avaliação estática de troca completa com suporte a raios-X e otimização de saída antecipada
- **`hasNonPawnMaterial()`** — usado para segurança na poda de lance nulo (previne lance nulo em finais de rei+peões)
- **Rastreamento de `selDepth`** — reporta profundidade seletiva máxima alcançada
- Detecção de repetição melhorada com struct `RepState` rastreando distância de repetição

---

### ♟️ Melhorias na Geração de Movimentos

- **Geração legal otimizada** usando `checkersBB`, `pinnedBB`, `blockersForKing`, `BETWEEN_BB` e `LINE_BB` para evasão de xeque rápida e filtragem consciente de cravadas
- **Verificação especial de legalidade de en passant** para casos extremos de cravada horizontal
- **Tratamento de xeque duplo** — quando em xeque duplo, apenas lances de rei são gerados
- **Função `perft()`** para testes de correção e debugging

---

### 📊 Melhorias na Avaliação

- **Avaliação preguiçosa (lazy eval)** — quando a vantagem material excede ±2000 cp (20 peões) em posições fora do final, a avaliação detalhada é pulada por velocidade
- **Tabela hash de peões** — avaliação de estrutura de peões (dobrados, isolados, passados) é cacheada e reutilizada quando apenas peças se movem
- **Nunca lazy eval em finais** — garante avaliação precisa quando o material é baixo

---

### 🔧 Melhorias UCI

- **`setoption name Hash value N`** — redimensiona dinamicamente a tabela de transposição (1–4096 MB)
- **Comando `perft`** com saída divide para debugging e verificação de correção
- **`hashfull`** reportado na saída de info UCI

---

### 🛠 Sistema de Build

- Nome padrão do binário atualizado para `deepbecky-v1.2-windows-x64.exe`
- **Perfil SSE4.2** adicionado (`PROFILE=sse42`)
- **Makefile multiplataforma** funciona em MSYS2 e Windows CMD/PowerShell
- **Treinamento PGO** usa 6 posições diversas (posição inicial, Italiana, QGD, Kiwipete, final, promoções) para melhor cobertura de perfil
- **Profundidade PGO configurável** (variável `PGO_DEPTH`)
- **Targets `help` e `info`** para documentação do sistema de build
- **Target de build debug** (`make debug`) com flags `-g -O0`

---

## Comparativo de Força

| Característica | v1.1 | v1.2 |
|----------------|------|------|
| Arquitetura | 8 arquivos, header único | 25 arquivos, separação H/CPP completa |
| TT com Tamanho Dinâmico | ❌ | ✅ (opção UCI `Hash`) |
| Tabela Log LMR | ❌ | ✅ (`Reductions[64][64]`) |
| Razoring | ❌ | ✅ |
| Reverse Futility Pruning | ❌ | ✅ |
| Internal Iterative Deepening | ❌ | ✅ |
| Late Move Pruning | ❌ | ✅ |
| Futility Pruning (filho) | ❌ | ✅ |
| Poda SEE (capturas) | ❌ | ✅ |
| Contempt Dinâmico | Fixo ±20 cp | Escalado 20–200 cp |
| Classe TimeManagement | Básico | Estilo Stockfish |
| Tabela Hash de Peões | ❌ | ✅ |
| Avaliação Preguiçosa | ❌ | ✅ |
| Tabelas BETWEEN_BB / LINE_BB | ❌ | ✅ |
| Geração Legal com Cravadas | ❌ | ✅ |
| Comando Perft | ❌ | ✅ |
| Reporte `hashfull` | ❌ | ✅ |
| TT Alinhada ao Cache | ❌ | ✅ |
| Prefetch da TT | ❌ | ✅ |
| Garantia de Profundidade Mínima | ❌ | ✅ (4 plies) |
| Cabeçalhos Licença GPL v3 | ❌ | ✅ |

---

---

# Deep Becky 1.1

---

# 🇬🇧 English Version

## Improvements over version 1.0

### 🎯 Draw Detection and Draw Avoidance

- Added robust draw handling in search:
    - threefold repetition detection
    - 50-move rule detection
    - insufficient material detection
- Added cycle detection (`hasGameCycle`) to identify upcoming repetition loops.
- Added **contempt** (`+/-20 cp` from root side perspective) so the engine avoids unnecessary draws when a better result is available.
- Added draw scoring by node parity (`drawScore`) to reduce deterministic draw bias.

---

### 🔍 Search and Move Ordering

- New staged **MovePicker** ordering:
    - TT move
    - good captures (SEE >= 0)
    - killer moves
    - quiet moves by history
    - bad captures
- Added **SEE (Static Exchange Evaluation)** in engine core and integrated into move ordering / qsearch filtering.
- TT handling improved with generation-aware replacement policy (`TTGeneration`) and refined probing/store behavior.
- TT cutoffs are now constrained near 50-move draw range (`halfmove < 90`) to reduce draw-rule instability.

---

### ⚙️ Core Engine and Robustness

- Reworked move representation to compact packed format (`squares + flags`) and helper accessors (`moveFrom`, `moveTo`, etc.).
- Replaced dynamic move vectors in hot paths with fixed-size move buffers (`MAX_MOVES`) for lower overhead.
- Replaced dynamic undo vector with fixed stack (`undoStack`) for predictable performance.
- `setFEN()` now returns `bool` and validates malformed FEN fields more strictly.
- UCI parser made case-insensitive for better interoperability (`uci`, `position`, `go`, etc.).
- UCI output now reports mate scores in `score mate N` format when applicable.
- Added legal-move fallback if TT best move is missing at the end of search.

---

### 🛠 Build

- Updated default binary name in Makefile to:
    - `deepbecky-v1.1-windows-x64.exe`

---

# 🇧🇷 Versão em Português

## Melhorias em relação à versão 1.0

### 🎯 Detecção de Empate e Estratégia para Evitar Empates

- Adicionado tratamento robusto de empate na busca:
    - detecção de tripla repetição
    - detecção da regra dos 50 lances
    - detecção de material insuficiente
- Adicionada detecção de ciclos (`hasGameCycle`) para identificar repetições iminentes.
- Adicionado **contempt** (`+/-20 cp` na perspectiva do lado na raiz) para evitar empates desnecessários quando houver posição melhor.
- Adicionado score de empate por paridade de nós (`drawScore`) para reduzir viés determinístico em empates.

---

### 🔍 Busca e Ordenação de Lances

- Nova ordenação em estágios com **MovePicker**:
    - lance da TT
    - boas capturas (SEE >= 0)
    - killers
    - quiets por histórico
    - capturas ruins
- **SEE (Static Exchange Evaluation)** adicionado ao núcleo e integrado à ordenação / filtragem da quiescência.
- TT aprimorada com política de substituição sensível à geração (`TTGeneration`) e ajustes de probe/store.
- Cortes por TT agora são limitados perto da faixa da regra dos 50 lances (`halfmove < 90`) para reduzir instabilidade.

---

### ⚙️ Núcleo e Robustez da Engine

- Representação de lances refeita para formato compacto (`squares + flags`) com helpers (`moveFrom`, `moveTo`, etc.).
- Vetores dinâmicos de lances removidos dos hot paths e substituídos por buffers fixos (`MAX_MOVES`).
- Vetor dinâmico de undo substituído por pilha fixa (`undoStack`) para desempenho previsível.
- `setFEN()` agora retorna `bool` e valida FEN inválida com mais rigor.
- Parser UCI agora é case-insensitive para melhor interoperabilidade (`uci`, `position`, `go`, etc.).
- Saída UCI passa a reportar mate no formato `score mate N` quando aplicável.
- Adicionado fallback para lance legal quando não há melhor lance válido vindo da TT ao fim da busca.

---

### 🛠 Build

- Nome padrão do binário no Makefile atualizado para:
    - `deepbecky-v1.1-windows-x64.exe`

---

*Deep Becky - UCI Chess Engine by Diogo de Oliveira Almeida*
