# Deep Becky 1.2 — Release Notes

Full changelog: [CHANGELOG.md](./CHANGELOG.md)

---

## 🇬🇧 English

Deep Becky 1.2 is a **major architectural restructure** and search upgrade over version 1.1. The codebase was split from 8 files into **25 files** with proper header/implementation separation and dedicated modules for each subsystem.

### Highlights

#### Architecture
- Full restructure into 25 source files with proper H/CPP separation and C++ namespaces
- Dedicated modules: `Position`, `Search`, `Eval`, `MovePicker`, `TranspositionTable`, `TimeManagement`, `UCI`
- GPL v3 license headers on all source files

#### Search
- Pre-computed logarithmic **LMR reduction table** (`Reductions[64][64]`) with PV/improving/killer adjustments
- **Razoring** at depth ≤ 1 when static eval is far below alpha
- **Reverse futility pruning** (static null move pruning) at depth < 5
- **Internal Iterative Deepening** (IID) at depth ≥ 6 when no TT move is available
- **Late move pruning** — quiet moves beyond a count threshold are pruned at shallow depths
- **Futility pruning at child node** — quiet moves where eval + margin can't reach alpha
- **SEE pruning for captures** — losing exchanges pruned at shallow depths
- **Capture extension** for sacrifice detection (SEE < 0 captures that deserve deeper analysis)
- **Dynamic contempt** scaling from 20 to 200 cp based on evaluation advantage
- **Minimum depth guarantee** — always completes at least 4 iterations before stopping
- **Mate finding optimization** — requires stable best move before stopping on forced mate

#### Time Management
- New Stockfish-style **TimeManagement** class with optimum/maximum time allocation
- Stability adjustment — extends time when best move keeps changing
- Score-drop adjustment — extends time when evaluation drops between iterations
- Obvious move detection — plays instantly with only 1-2 legal moves or simple recaptures
- Game phase awareness — different allocation for opening, middlegame, and endgame

#### Transposition Table
- **Dynamic Hash sizing** via UCI option `setoption name Hash value N` (1–4096 MB)
- Cache-aligned allocation for optimal CPU cache performance
- Memory prefetching (`prefetch()`) before probing
- `hashfull` reported in UCI info output

#### Bitboard Infrastructure
- `BETWEEN_BB[64][64]`, `LINE_BB[64][64]`, `RAY_BB[64][8]` tables
- Pin-aware legal move generation using `checkersBB()`, `pinnedBB()`, `blockersForKing()`
- Optimized double-check handling (only king moves generated)

#### Evaluation
- **Pawn hash table** (65536 entries) — caches pawn structure evaluation
- **Lazy evaluation** — skips detailed eval when material advantage > ±2000 cp (never in endgames)

#### UCI & Build
- `setoption name Hash value N` support
- `perft` command with divide output
- SSE4.2 profile added, PGO training with 6 diverse positions
- Cross-platform Makefile (MSYS2 + Windows CMD)

### Result
Version 1.2 represents a significant leap in both code quality and playing strength. The modular architecture enables faster development, while the many new pruning techniques and professional time management allow the engine to search deeper and play more efficiently under time pressure.

### Build Notes
- MSYS2 MinGW-w64: `make`, `make PROFILE=bmi2`, `make pgo PROFILE=bmi2`
- Windows CMD + MinGW: `mingw32-make`, `mingw32-make PROFILE=bmi2`
- MSVC: `cl /O2 /std:c++17 /DNDEBUG /MT main.cpp magic.cpp bitboard.cpp position.cpp movegen.cpp evaluate.cpp search.cpp tt.cpp uci.cpp timeman.cpp movepick.cpp /Fe:deepbecky-v1.2-windows-x64.exe /link /LTCG`

---

## 🇧🇷 Português

A Deep Becky 1.2 é uma **grande reestruturação arquitetural** e atualização de busca em relação à versão 1.1. O código foi dividido de 8 para **25 arquivos** com separação adequada header/implementação e módulos dedicados para cada subsistema.

### Destaques

#### Arquitetura
- Reestruturação completa em 25 arquivos-fonte com separação H/CPP e namespaces C++
- Módulos dedicados: `Position`, `Search`, `Eval`, `MovePicker`, `TranspositionTable`, `TimeManagement`, `UCI`
- Cabeçalhos de licença GPL v3 em todos os arquivos-fonte

#### Busca
- **Tabela de redução LMR** logarítmica pré-computada (`Reductions[64][64]`) com ajustes para PV/improving/killer
- **Razoring** na profundidade ≤ 1 quando avaliação estática está muito abaixo de alpha
- **Reverse futility pruning** (poda de lance nulo estática) na profundidade < 5
- **Internal Iterative Deepening** (IID) na profundidade ≥ 6 quando não há lance da TT
- **Late move pruning** — lances quietos além de um limite de contagem são podados em profundidades rasas
- **Futility pruning no nó-filho** — lances quietos onde eval + margem não pode alcançar alpha
- **Poda SEE para capturas** — trocas perdedoras podadas em profundidades rasas
- **Extensão de captura** para detecção de sacrifícios (capturas com SEE < 0 que merecem análise mais profunda)
- **Contempt dinâmico** escalando de 20 a 200 cp baseado na vantagem de avaliação
- **Garantia de profundidade mínima** — sempre completa pelo menos 4 iterações antes de parar
- **Otimização de busca de mate** — exige melhor lance estável antes de parar em mate forçado

#### Gerenciamento de Tempo
- Nova classe **TimeManagement** estilo Stockfish com alocação de tempo ótimo/máximo
- Ajuste de estabilidade — estende tempo quando melhor lance muda entre iterações
- Ajuste por queda de score — estende tempo quando avaliação cai entre iterações
- Detecção de lance óbvio — joga instantaneamente com apenas 1-2 lances legais ou recapturas simples
- Consciência de fase do jogo — alocação diferente para abertura, meio-jogo e final

#### Tabela de Transposição
- **Dimensionamento dinâmico Hash** via opção UCI `setoption name Hash value N` (1–4096 MB)
- Alocação alinhada ao cache para desempenho ideal de cache da CPU
- Pré-busca de memória (`prefetch()`) antes do probing
- `hashfull` reportado na saída de info UCI

#### Infraestrutura de Bitboard
- Tabelas `BETWEEN_BB[64][64]`, `LINE_BB[64][64]`, `RAY_BB[64][8]`
- Geração legal de lances consciente de cravadas usando `checkersBB()`, `pinnedBB()`, `blockersForKing()`
- Tratamento otimizado de xeque duplo (apenas lances de rei gerados)

#### Avaliação
- **Tabela hash de peões** (65536 entradas) — cacheia avaliação de estrutura de peões
- **Avaliação preguiçosa** — pula avaliação detalhada quando vantagem material > ±2000 cp (nunca em finais)

#### UCI & Build
- Suporte a `setoption name Hash value N`
- Comando `perft` com saída divide
- Perfil SSE4.2 adicionado, treinamento PGO com 6 posições diversas
- Makefile multiplataforma (MSYS2 + Windows CMD)

### Resultado
A versão 1.2 representa um salto significativo tanto em qualidade de código quanto em força de jogo. A arquitetura modular permite desenvolvimento mais rápido, enquanto as muitas novas técnicas de poda e o gerenciamento de tempo profissional permitem que a engine busque mais fundo e jogue de forma mais eficiente sob pressão de tempo.

### Notas de compilação
- MSYS2 MinGW-w64: `make`, `make PROFILE=bmi2`, `make pgo PROFILE=bmi2`
- Windows CMD + MinGW: `mingw32-make`, `mingw32-make PROFILE=bmi2`
- MSVC: `cl /O2 /std:c++17 /DNDEBUG /MT main.cpp magic.cpp bitboard.cpp position.cpp movegen.cpp evaluate.cpp search.cpp tt.cpp uci.cpp timeman.cpp movepick.cpp /Fe:deepbecky-v1.2-windows-x64.exe /link /LTCG`

---

Full changelog: [CHANGELOG.md](./CHANGELOG.md)
