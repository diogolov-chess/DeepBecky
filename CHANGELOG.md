# Deep Becky 0.2 - Changelog

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
