# Deep Becky - Changelog

---

# Deep Becky 2.0

---

# 🇬🇧 English Version

## Improvements over version 1.2

### 🧵 Lazy SMP Multi-Threading (NEW)

Completely new threading module (`thread.h` / `thread.cpp`) enabling parallel search on multi-core CPUs:

| Component | Description |
|-----------|-------------|
| `SearchThread` | Each thread owns its own Position copy and per-thread heuristic tables |
| `ThreadPool` | Manages all search threads, controls start/stop lifecycle |
| UCI `Threads` | Configurable from 1 to 256 threads via `setoption name Threads value N` |

Key design principles:
- **No locks in hot path** — TT is lockless, per-thread tables avoid contention
- **Per-thread tables**: each thread has independent killers, history heuristic, and pawn hash table
- **Condition variable parking** — zero CPU cost when idle
- **Relaxed atomics** for stop flag — no fence overhead
- **Vote-based best thread selection** — combines score + depth across all threads to pick the optimal result
- **TT is the ONLY communication channel** between threads

---

### 🔮 Ponder Support (NEW)

- UCI option `Ponder` (true/false) enables continuous analysis during the opponent's turn
- In ponder mode, the engine searches without time limit until `ponderhit` or `stop`
- Ponder move extracted from PV/TT and output with `bestmove ... ponder ...`

---

### 🔍 Search Improvements

#### Singular Extensions (NEW)
At depth ≥ 6, the engine tests if the TT move is the only good move by searching all alternatives at reduced depth with a lowered beta (`singularBeta = ttScore - 2 * depth`). If no other move reaches singularBeta:
- **Single extension** (+1 ply) for singular moves
- **Double extension** (+2 plies) for clearly singular moves (`seScore < singularBeta - 60`)
- **MultiCut**: if `singularBeta >= beta`, all moves are good enough — return immediately
- **Negative extension** (-2 plies) at cut nodes when singularity test fails

#### ProbCut (NEW)
At depth ≥ 5, tests if a good capture with reduced search returns far above beta (`raisedBeta = beta + 190 - 45 * improving`). Two-phase verification:
1. Quiescence search confirmation
2. Full search at depth - 4

Prunes the position if both phases confirm the score.

#### Null Move Pruning with Verification (UPGRADED)
- **Aggressive R formula**: `R = (854 + 68 * depth) / 258 + min((staticEval - beta) / 192, 3)` — deeper reductions at high depths
- **Verification search** at depth ≥ 14: after null move cutoff, re-searches with null move disabled for the side that got the cutoff to prevent zugzwang errors
- Per-thread `nmpMinPly` / `nmpColor` tracking for verification

#### IIR - Internal Iterative Reduction (UPGRADED)
Replaces IID (Internal Iterative Deepening) from v1.2:
- Instead of a separate shallow search, simply **reduces the depth** when no TT move is available
- Deeper reduction at cut nodes (`iirReduction = cutNode ? 2 : 1`)
- More efficient than IID — saves a full search call

#### CutNode Tracking (NEW)
The search now explicitly tracks whether a node is an expected cut-node or all-node:
- Cut nodes get deeper IIR reduction
- Cut nodes get +2 LMR reduction
- Singular extension fails at cut nodes get -2 extension

#### Enhanced Late Move Reductions (UPGRADED)
Several new LMR adjustment factors:
- **History-based**: `R -= histScore / 5000` — moves with good history are reduced less
- **Cut node**: `R += 2` — more aggressive reduction at expected cut nodes
- **Singular extension**: `R -= 2` — less reduction when the TT move was singular
- **TT capture**: `R++` — when TT move is a capture, quiet moves deserve more reduction
- **Check-giving moves**: `R--` — moves that give check are reduced less

#### Aspiration Windows (UPGRADED)
- Dynamic delta widening: `delta += delta / 3` on fail-high/fail-low
- Beta adjustment on fail-low: `beta = (alpha + beta) / 2` for faster convergence

#### Root Best Move Tracking (NEW)
`rootBestMove` is tracked directly inside pvs() at ply 0, per-Position (per-thread). This avoids relying on the shared TT (which can be overwritten by other threads in Lazy SMP) to recover the root best move after the search completes.

---

### 🗃️ Transposition Table Redesign (UPGRADED)

Complete redesign of the TT entry format and cluster layout:

| Feature | v1.2 | v2.0 |
|---------|------|------|
| Entry size | 16 bytes | **10 bytes** |
| Layout | Flat array | **3 entries per 32-byte cluster** |
| Entries per MB | ~65K | **~100K (+50%)** |
| PV node flag | ❌ | ✅ (stored in genBound8) |
| Static eval | ❌ | ✅ (eval16 field, saves evaluate() calls) |
| Move packing | `moveData` + `moveFlags` | **16-bit packed** (from:6+to:6+type:2+promo:2) |
| Generation | 6-bit | **5-bit with 3 reserved** (gen:5+pv:1+bound:2) |
| Max Hash | 4096 MB | **32768 MB** |
| EVAL_NONE sentinel | ❌ | ✅ (-32001) |

The 10-byte entry with 3-per-cluster design means 50% more entries per megabyte of hash, while maintaining perfect 32-byte cache-line alignment.

---

### 📊 Evaluation Improvements

#### Incremental PSQT Evaluation (NEW)
Pre-computed `Eval::PSQT_MG[piece][sq]` and `Eval::PSQT_EG[piece][sq]` tables with sign encoded for color. Piece-square scores, material totals, and game phase are now tracked **incrementally** in `makeMove()` / `undoMove()`, eliminating redundant full-board scans during evaluation.

#### Non-Linear King Safety (UPGRADED)
The king safety model was completely rewritten with **quadratic danger scaling**:
- Each piece type near the king zone contributes attack weight units (Knight: 8, Bishop: 6, Rook: 7, Queen: 12, Pawns: 3 per pressing pawn)
- **Multiple attacker synergy**: 2 attackers = +6 bonus, 3 = +16, 4+ = +30
- **Queen modifier**: attacks without a queen are reduced by 1/3
- **Weak shelter integration**: missing pawn shelter adds to attack weight
- **Quadratic penalty**: `min(700, attackUnits² / 3)`
- Example: knight + queen attacking = ~225 cp penalty (vs ~19 cp linear in v1.2)

#### Endgame Mating Evaluation (NEW)
Dedicated `evaluateKXK()` function for KQ vs K, KR vs K, and similar endgames:
- Pushes the losing king toward the edge and corner (CENTER_DISTANCE table)
- Rewards bringing the winning king closer (Chebyshev distance)
- Edge and corner bonuses for the losing king position
- 50-move rule damping only above halfmove 80

#### Static Eval in TT (NEW)
The transposition table now stores the raw static evaluation (`eval16`). On TT hits, the engine reuses this value instead of calling `evaluate()`, significantly reducing evaluation overhead. TT score is also used as a better estimate when bound direction agrees.

#### Improved Pawn Shield (UPGRADED)
Stronger bonuses and penalties for pawn shield scoring:
- Front pawn: +15 / -18 (was +10 / -12)
- Side pawns: +10 / -12 (was +6 / -8)
- Second rank: +6 / -6 (was +4 / -4)

---

### 🔧 History Heuristic Improvements

#### Gravity Formula (NEW)
History heuristic now uses the gravity formula for bounded updates:
```cpp
entry += bonus - entry * abs(bonus) / 16384;
```
This naturally bounds history scores without requiring periodic halving (v1.2 used simple addition with saturation at 8000).

#### History Malus (NEW)
At beta cutoffs, all previously searched quiet moves that were NOT the best move receive a **negative history update** (malus), teaching the engine to avoid these moves in future searches.

---

### ⏱️ Time Management Improvements

#### Winning Endgame Extension (NEW)
When the engine detects a winning position in an endgame (low phase count), time allocation is extended to allow deeper search for forced mates.

#### Mate Search Extension (NEW)
When a forced mate is found, the optimum time is pushed toward maximum (≥ 80% of max) to continue searching for the **shortest** mate path.

#### Improved Contempt Side Handling (UPGRADED)
Contempt is now applied as `contempt` for white-to-move and `-contempt` for black-to-move, with separate treatment in evaluate() vs search (contempt not stored in TT eval).

---

### 🔧 UCI Improvements

- **`setoption name Threads value N`** — configurable thread count (1–256)
- **`setoption name Ponder value true/false`** — enable/disable ponder mode
- **`ponderhit` command** — switch from pondering to normal time-controlled search
- **Thread count** reported in UCI info output (`threads N`)
- Default hash size increased to 64 MB

---

### 🛠 Build System

- Updated default binary name to `deepbecky-v2.0-windows-x64.exe`
- Source files now include `thread.cpp` and header `thread.h`
- Added `-fno-exceptions -fno-rtti -fno-unwind-tables -fno-asynchronous-unwind-tables` for smaller, faster binary
- Static linking includes `-lpthread` for threading support
- **27 source files** total (13 .cpp + 13 .h + Makefile)

---

### 📁 File Structure

| File | Description |
|------|-------------|
| `types.h` | Core types, enums (Square, Color, Piece), constants, helper functions |
| `bitboard.h` / `bitboard.cpp` | Bitboard constants, attack tables, BETWEEN_BB, LINE_BB, RAY_BB |
| `position.h` / `position.cpp` | Position class, make/undo move, FEN, repetition, SEE, incremental eval |
| `search.h` / `search.cpp` | PVS, quiescence, iterative deepening, singular ext., ProbCut |
| `evaluate.h` / `evaluate.cpp` | Full evaluation with incremental PSQT, king safety, endgame |
| `movegen.h` / `movegen.cpp` | Move generation, legality, attack detection, perft |
| `movepick.h` / `movepick.cpp` | Staged move ordering (MovePicker) |
| `tt.h` / `tt.cpp` | TranspositionTable with 10-byte entries, clustered layout |
| `timeman.h` / `timeman.cpp` | TimeManagement class with endgame extensions |
| `thread.h` / `thread.cpp` | **NEW** — Lazy SMP ThreadPool and SearchThread |
| `uci.h` / `uci.cpp` | UCI protocol handling with Threads/Ponder support |
| `magic.h` / `magic.cpp` | Magic Bitboard tables and initialization |
| `main.cpp` | Entry point |

---

## Strength Comparison

| Feature | v1.2 | v2.0 |
|---------|------|------|
| Multi-Threading (Lazy SMP) | ❌ | ✅ (1–256 threads) |
| Ponder Support | ❌ | ✅ |
| Singular Extensions | ❌ | ✅ (single + double) |
| ProbCut | ❌ | ✅ |
| Null Move Verification | ❌ | ✅ (depth ≥ 14) |
| CutNode Tracking | ❌ | ✅ |
| TT Entry Size | 16 bytes | 10 bytes |
| TT Clustered Layout | ❌ | ✅ (3-per-cluster) |
| TT PV Node Flag | ❌ | ✅ |
| TT Static Eval Storage | ❌ | ✅ |
| TT Max Hash | 4096 MB | 32768 MB |
| Incremental PSQT | ❌ | ✅ |
| Non-Linear King Safety | Linear | Quadratic |
| Endgame Mating Eval | ❌ | ✅ (KQ/KR vs K) |
| History Gravity Formula | ❌ | ✅ |
| History Malus | ❌ | ✅ |
| IID / IIR | IID (shallow search) | IIR (depth reduction) |
| LMR History Adjustment | ❌ | ✅ |
| LMR Cut Node Adjustment | ❌ | ✅ |
| LMR Singular Adjustment | ❌ | ✅ |
| LMR TT Capture Adjustment | ❌ | ✅ |
| Root Best Move Tracking | TT-based | Per-thread |
| Winning Endgame Time Ext. | ❌ | ✅ |
| Mate Search Time Ext. | ❌ | ✅ |
| UCI Threads Option | ❌ | ✅ |
| UCI Ponder Option | ❌ | ✅ |

---

---

# 🇧🇷 Versão em Português

## Melhorias em relação à versão 1.2

### 🧵 Multi-Threading Lazy SMP (NOVO)

Módulo de threading completamente novo (`thread.h` / `thread.cpp`) habilitando busca paralela em CPUs multi-core:

| Componente | Descrição |
|------------|-----------|
| `SearchThread` | Cada thread possui sua própria cópia de Position e tabelas heurísticas por thread |
| `ThreadPool` | Gerencia todas as threads de busca, controla ciclo de vida start/stop |
| UCI `Threads` | Configurável de 1 a 256 threads via `setoption name Threads value N` |

Princípios de design:
- **Sem locks no caminho crítico** — TT é lockless, tabelas por thread evitam contenção
- **Tabelas por thread**: cada thread tem killers, heurística de histórico e hash de peões independentes
- **Estacionamento por condition variable** — zero custo de CPU quando ociosas
- **Atomics relaxados** para flag de parada — sem overhead de fence
- **Seleção do melhor thread por votação** — combina score + profundidade entre todas as threads
- **TT é o ÚNICO canal de comunicação** entre threads

---

### 🔮 Suporte a Ponder (NOVO)

- Opção UCI `Ponder` (true/false) habilita análise contínua durante o turno do oponente
- No modo ponder, a engine busca sem limite de tempo até `ponderhit` ou `stop`
- Lance ponder extraído do PV/TT e emitido com `bestmove ... ponder ...`

---

### 🔍 Melhorias na Busca

#### Singular Extensions (NOVO)
Na profundidade ≥ 6, a engine testa se o lance da TT é o único bom lance, buscando todas as alternativas em profundidade reduzida com beta rebaixado (`singularBeta = ttScore - 2 * depth`). Se nenhum outro lance alcança singularBeta:
- **Extensão simples** (+1 ply) para lances singulares
- **Extensão dupla** (+2 plies) para lances claramente singulares (`seScore < singularBeta - 60`)
- **MultiCut**: se `singularBeta >= beta`, todos os lances são bons o suficiente — retorna imediatamente
- **Extensão negativa** (-2 plies) em nós de corte quando o teste de singularidade falha

#### ProbCut (NOVO)
Na profundidade ≥ 5, testa se uma boa captura com busca reduzida retorna muito acima de beta (`raisedBeta = beta + 190 - 45 * improving`). Verificação em duas fases:
1. Confirmação por quiescence search
2. Busca completa em profundidade - 4

Poda a posição se ambas as fases confirmam o score.

#### Poda de Lance Nulo com Verificação (APRIMORADO)
- **Fórmula agressiva de R**: `R = (854 + 68 * depth) / 258 + min((staticEval - beta) / 192, 3)` — reduções mais profundas em altas profundidades
- **Busca de verificação** na profundidade ≥ 14: após cutoff de lance nulo, re-busca com lance nulo desabilitado para o lado que obteve o cutoff para prevenir erros de zugzwang
- Rastreamento por thread de `nmpMinPly` / `nmpColor` para verificação

#### IIR - Internal Iterative Reduction (APRIMORADO)
Substitui IID (Internal Iterative Deepening) da v1.2:
- Em vez de busca rasa separada, simplesmente **reduz a profundidade** quando não há lance da TT
- Redução mais profunda em nós de corte (`iirReduction = cutNode ? 2 : 1`)
- Mais eficiente que IID — economiza uma chamada de busca completa

#### Rastreamento de CutNode (NOVO)
A busca agora rastreia explicitamente se um nó é nó-de-corte esperado ou nó-total:
- Nós de corte recebem redução IIR mais profunda
- Nós de corte recebem +2 de redução LMR
- Falhas de extensão singular em nós de corte recebem -2 de extensão

#### Late Move Reductions Aprimorado (APRIMORADO)
Vários novos fatores de ajuste LMR:
- **Baseado em histórico**: `R -= histScore / 5000` — lances com bom histórico são menos reduzidos
- **Nó de corte**: `R += 2` — redução mais agressiva em nós de corte esperados
- **Extensão singular**: `R -= 2` — menos redução quando o lance da TT era singular
- **Captura TT**: `R++` — quando lance da TT é captura, lances quietos merecem mais redução
- **Lances que dão xeque**: `R--` — lances que dão xeque são menos reduzidos

#### Janelas de Aspiração (APRIMORADO)
- Alargamento dinâmico de delta: `delta += delta / 3` em fail-high/fail-low
- Ajuste de beta em fail-low: `beta = (alpha + beta) / 2` para convergência mais rápida

#### Rastreamento de Melhor Lance na Raiz (NOVO)
`rootBestMove` é rastreado diretamente dentro de pvs() no ply 0, por Position (por thread). Isso evita depender da TT compartilhada (que pode ser sobrescrita por outras threads no Lazy SMP) para recuperar o melhor lance da raiz após a busca completar.

---

### 🗃️ Redesenho da Tabela de Transposição (APRIMORADO)

Redesenho completo do formato de entrada e layout de clusters:

| Característica | v1.2 | v2.0 |
|----------------|------|------|
| Tamanho da entrada | 16 bytes | **10 bytes** |
| Layout | Array plano | **3 entradas por cluster de 32 bytes** |
| Entradas por MB | ~65K | **~100K (+50%)** |
| Flag de nó PV | ❌ | ✅ (armazenado em genBound8) |
| Eval estática | ❌ | ✅ (campo eval16, economiza chamadas a evaluate()) |
| Empacotamento de lance | `moveData` + `moveFlags` | **16 bits compactados** (from:6+to:6+type:2+promo:2) |
| Geração | 6 bits | **5 bits com 3 reservados** (gen:5+pv:1+bound:2) |
| Max Hash | 4096 MB | **32768 MB** |
| Sentinela EVAL_NONE | ❌ | ✅ (-32001) |

O design de entrada de 10 bytes com 3 por cluster significa 50% mais entradas por megabyte de hash, mantendo alinhamento perfeito de 32 bytes por linha de cache.

---

### 📊 Melhorias na Avaliação

#### Avaliação PSQT Incremental (NOVO)
Tabelas pré-computadas `Eval::PSQT_MG[piece][sq]` e `Eval::PSQT_EG[piece][sq]` com sinal codificado por cor. Scores de peça-casa, totais de material e fase do jogo são agora rastreados **incrementalmente** em `makeMove()` / `undoMove()`, eliminando varreduras redundantes do tabuleiro inteiro durante a avaliação.

#### Segurança do Rei Não-Linear (APRIMORADO)
O modelo de segurança do rei foi completamente reescrito com **escala quadrática de perigo**:
- Cada tipo de peça próximo à zona do rei contribui unidades de peso de ataque (Cavalo: 8, Bispo: 6, Torre: 7, Dama: 12, Peões: 3 por peão pressionando)
- **Sinergia de múltiplos atacantes**: 2 atacantes = +6 bônus, 3 = +16, 4+ = +30
- **Modificador de dama**: ataques sem dama são reduzidos em 1/3
- **Integração de abrigo fraco**: abrigo de peões ausente adiciona ao peso de ataque
- **Penalidade quadrática**: `min(700, attackUnits² / 3)`
- Exemplo: cavalo + dama atacando = ~225 cp de penalidade (vs ~19 cp linear na v1.2)

#### Avaliação de Mate em Finais (NOVO)
Função dedicada `evaluateKXK()` para KQ vs K, KR vs K e finais similares:
- Empurra o rei perdedor em direção à borda e canto (tabela CENTER_DISTANCE)
- Recompensa aproximar o rei vencedor (distância de Chebyshev)
- Bônus de borda e canto para a posição do rei perdedor
- Amortecimento da regra dos 50 lances apenas acima de halfmove 80

#### Eval Estática na TT (NOVO)
A tabela de transposição agora armazena a avaliação estática pura (`eval16`). Em acertos da TT, a engine reutiliza esse valor em vez de chamar `evaluate()`, reduzindo significativamente o overhead de avaliação. O score da TT também é usado como estimativa melhor quando a direção do bound concorda.

#### Abrigo de Peões Melhorado (APRIMORADO)
Bônus e penalidades mais fortes para o abrigo de peões:
- Peão frontal: +15 / -18 (era +10 / -12)
- Peões laterais: +10 / -12 (era +6 / -8)
- Segunda fileira: +6 / -6 (era +4 / -4)

---

### 🔧 Melhorias na Heurística de Histórico

#### Fórmula de Gravidade (NOVO)
A heurística de histórico agora usa a fórmula de gravidade para atualizações limitadas:
```cpp
entry += bonus - entry * abs(bonus) / 16384;
```
Isso limita naturalmente os scores de histórico sem necessidade de halving periódico (v1.2 usava adição simples com saturação em 8000).

#### Malus de Histórico (NOVO)
Em cutoffs beta, todos os lances quietos previamente buscados que NÃO foram o melhor lance recebem uma **atualização negativa de histórico** (malus), ensinando a engine a evitar esses lances em buscas futuras.

---

### ⏱️ Melhorias no Gerenciamento de Tempo

#### Extensão para Finais Vencidos (NOVO)
Quando a engine detecta posição vencedora em final (fase baixa), a alocação de tempo é estendida para permitir busca mais profunda por mates forçados.

#### Extensão para Busca de Mate (NOVO)
Quando um mate forçado é encontrado, o tempo ótimo é empurrado em direção ao máximo (≥ 80% do max) para continuar buscando o caminho de mate **mais curto**.

#### Tratamento Melhorado de Contempt por Lado (APRIMORADO)
Contempt agora é aplicado como `contempt` para brancas-a-jogar e `-contempt` para pretas-a-jogar, com tratamento separado em evaluate() vs search (contempt não armazenado na eval da TT).

---

### 🔧 Melhorias UCI

- **`setoption name Threads value N`** — contagem de threads configurável (1–256)
- **`setoption name Ponder value true/false`** — habilitar/desabilitar modo ponder
- **Comando `ponderhit`** — muda de pondering para busca com controle de tempo normal
- **Contagem de threads** reportada na saída de info UCI (`threads N`)
- Tamanho padrão de hash aumentado para 64 MB

---

### 🛠 Sistema de Build

- Nome padrão do binário atualizado para `deepbecky-v2.0-windows-x64.exe`
- Arquivos-fonte agora incluem `thread.cpp` e header `thread.h`
- Adicionado `-fno-exceptions -fno-rtti -fno-unwind-tables -fno-asynchronous-unwind-tables` para binário menor e mais rápido
- Linkagem estática inclui `-lpthread` para suporte a threading
- **27 arquivos-fonte** no total (13 .cpp + 13 .h + Makefile)

---

### 📁 Estrutura de Arquivos

| Arquivo | Descrição |
|---------|-----------|
| `types.h` | Tipos base, enums (Square, Color, Piece), constantes, funções auxiliares |
| `bitboard.h` / `bitboard.cpp` | Constantes de bitboard, tabelas de ataques, BETWEEN_BB, LINE_BB, RAY_BB |
| `position.h` / `position.cpp` | Classe Position, make/undo, FEN, repetição, SEE, eval incremental |
| `search.h` / `search.cpp` | PVS, quiescência, iterative deepening, ext. singular, ProbCut |
| `evaluate.h` / `evaluate.cpp` | Avaliação completa com PSQT incremental, segurança do rei, finais |
| `movegen.h` / `movegen.cpp` | Geração de movimentos, legalidade, detecção de ataques, perft |
| `movepick.h` / `movepick.cpp` | Ordenação de lances em estágios (MovePicker) |
| `tt.h` / `tt.cpp` | TranspositionTable com entradas de 10 bytes, layout clusterizado |
| `timeman.h` / `timeman.cpp` | Classe TimeManagement com extensões de final |
| `thread.h` / `thread.cpp` | **NOVO** — ThreadPool e SearchThread para Lazy SMP |
| `uci.h` / `uci.cpp` | Protocolo UCI com suporte a Threads/Ponder |
| `magic.h` / `magic.cpp` | Tabelas e inicialização de Magic Bitboards |
| `main.cpp` | Ponto de entrada |

---

## Comparativo de Força

| Característica | v1.2 | v2.0 |
|----------------|------|------|
| Multi-Threading (Lazy SMP) | ❌ | ✅ (1–256 threads) |
| Suporte a Ponder | ❌ | ✅ |
| Singular Extensions | ❌ | ✅ (simples + dupla) |
| ProbCut | ❌ | ✅ |
| Verificação de Lance Nulo | ❌ | ✅ (profundidade ≥ 14) |
| Rastreamento de CutNode | ❌ | ✅ |
| Tamanho da Entrada TT | 16 bytes | 10 bytes |
| Layout Clusterizado TT | ❌ | ✅ (3 por cluster) |
| Flag de Nó PV na TT | ❌ | ✅ |
| Eval Estática na TT | ❌ | ✅ |
| Max Hash TT | 4096 MB | 32768 MB |
| PSQT Incremental | ❌ | ✅ |
| Segurança do Rei | Linear | Quadrática |
| Eval de Mate em Finais | ❌ | ✅ (KQ/KR vs K) |
| Fórmula Gravidade Histórico | ❌ | ✅ |
| Malus de Histórico | ❌ | ✅ |
| IID / IIR | IID (busca rasa) | IIR (redução de profundidade) |
| Ajuste LMR por Histórico | ❌ | ✅ |
| Ajuste LMR por Nó de Corte | ❌ | ✅ |
| Ajuste LMR Singular | ❌ | ✅ |
| Ajuste LMR Captura TT | ❌ | ✅ |
| Rastreamento Melhor Lance Raiz | Baseado na TT | Por thread |
| Extensão Tempo Final Vencido | ❌ | ✅ |
| Extensão Tempo Busca de Mate | ❌ | ✅ |
| Opção UCI Threads | ❌ | ✅ |
| Opção UCI Ponder | ❌ | ✅ |

---

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

*Deep Becky - UCI Chess Engine by Diogo de Oliveira Almeida*
