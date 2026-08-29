# Deep Becky — Changelog

---

# Deep Becky 3.0 (In Development / Towards Release 3.0)

---

# 🇬🇧 English Version

## Improvements under development (Towards version 3.0)

### 🧠 NNUE Neural Network Evaluation (MAJOR ARCHITECTURAL LEAP)

The development towards Deep Becky 3.0 officially introduces the **NNUE (Efficiently Updatable Neural Network)** architecture, replacing the legacy Handcrafted Evaluation (HCE) with a deep learning evaluation engine:

| Component | Specification | Description |
|-----------|---------------|-------------|
| **Input Architecture** | 13 King Buckets $\times$ 704 Piece-Squares = 9,152 features | Anatomic king placement buckets providing sharp spatial awareness |
| **Accumulator Layer (L1)** | $1536$ Neurons (Dual Perspective: $768 \times 2$) | Parallel perspective evaluation for Side-to-Move and Non-Side-to-Move |
| **Mid / Hidden Layers** | $1536 \to 16 \to 32$ | Non-linear dense feature combination with SCReLU activations |
| **Output Layer (L3)** | $32 \to 1$ with 8 Material Buckets | Dynamic game-phase specialized outputs (from opening to pure endgame) |
| **Activation Function** | Squared Clipped ReLU (SCReLU) | $\text{SCReLU}(x) = \text{clamp}(x, 0, 1)^2$ for smooth gradients and sharp non-linearities |
| **Training Dataset** | 500 Million+ positions | Pure Leela Chess Zero self-play games with WDL + Centipawns sigmoid loss |
| **Quantization** | QA=255, QB=64, Scale=400 | Fully integer SIMD math with zero floating-point overhead |

---

### ⚡ AVX2 + BMI2 Vectorized SIMD Inference (NEW)

- **Sparse Neuron Updates**: Dynamic skip of inactive feature activations during accumulator evaluation.
- **AVX2 256-bit Vectorization**: Evaluates 16 weights simultaneously using `_mm256_madd_epi16`, `_mm256_packs_epi32`, and fused vector instructions.
- **Ultra-High Search Speed**: Delivers over **1,200,000 NPS (4 threads)** / **480,000 NPS (1 thread)** on modern x86-64 CPUs with Clang PGO.

---

### ⚔️ Threat-Aware Move Ordering (NEW)

New dedicated module (`threats.h` / `threats.cpp`) implementing fast bitboard-level tactical threat detection:
- **Piece Threat Matrix**: Detects when queens, rooks, bishops, or knights are attacked by lower-value pieces.
- **Dynamic Threat Bonuses**: Scores tactical counter-threats and defensive moves at the top of the MovePicker staging.
- **Massive Tree Pruning**: Dramatically cuts down search nodes by identifying critical defensive responses before quiet move exploration.

---

### 🔍 Search & Pruning Enhancements

#### Singular, Double & Triple Extensions (UPGRADED)
- **Triple Extensions (+3 plies)**: Added for moves that are decisively superior to all other alternatives in high-stakes tactical positions.
- **Multicut Verification**: Fast early exit when alternative branches prove that all moves exceed the cutoff threshold.

#### 4-Tier Continuation History (NEW)
- Multi-ply continuation history tables tracking moves played after the previous 1-ply, 2-ply, 4-ply, and 6-ply moves.
- Provides deep contextual quiet move ordering across complex tactical lines.

#### Correction History & Capture History (NEW)
- **Correction History**: Learns and corrects systematic biases between NNUE static evaluation and search outcome.
- **Capture History**: Independent history scoring for captures to refine capture move ordering beyond static SEE.

#### Adaptive Null Move Pruning with Verification (UPGRADED)
- Dynamically scales reduction $R$ based on depth, improving status, and NNUE static evaluation margin.
- Verification search at deep plies prevents Zugzwang errors in critical endgame positions.

---

## Strength Comparison

| Feature | v2.0 (HCE) | v3.0 (NNUE) |
|---------|------------|-------------|
| **Evaluation Engine** | Handcrafted (PSQT + King Safety) | **NNUE Neural Network (v5 Compact)** |
| **King Buckets** | ❌ (0) | **13 Anatomic King Buckets** |
| **Accumulator Size** | ❌ | **1536 Neurons (Dual Perspective)** |
| **Inference Math** | Scalar / Integer HCE | **AVX2 + BMI2 Vectorized SIMD** |
| **Threat-Aware Ordering** | ❌ | ✅ (`threats.cpp` Bitboard Matrices) |
| **Continuation History** | 1-ply | **4-Tier (1, 2, 4, 6 plies)** |
| **Correction History** | ❌ | ✅ (Dynamic NNUE Error Correction) |
| **Triple Extensions** | ❌ | ✅ (+3 plies on ultra-singular moves) |
| **Training Pipeline** | ❌ | **PyTorch CUDA + Streaming 500M+** |
| **Relative Strength** | Baseline | **+120 to +250 Elo** |

---

---

# 🇧🇷 Versão em Português

## Melhorias em desenvolvimento (Rumo à versão 3.0)

### 🧠 Avaliação por Rede Neural NNUE (GRANDE SALTO ARQUITETURAL)

O desenvolvimento rumo à Deep Becky 3.0 introduz oficialmente a era da **Rede Neural Profunda NNUE (Efficiently Updatable Neural Network)**, substituindo a antiga avaliação artesanal (HCE) por uma arquitetura de aprendizado profundo de ponta:

| Componente | Especificação | Descrição |
|------------|---------------|-----------|
| **Arquitetura de Entrada** | 13 King Buckets $\times$ 704 Piece-Squares = 9.152 features | Buckets anatômicos por posição do rei com refinamento espacial |
| **Camada do Acumulador (L1)** | $1536$ Neurônios (Dual Perspective: $768 \times 2$) | Perspectivas paralelas para o lado a jogar (STM) e oponente (NSTM) |
| **Camadas Ocultas (Mid/Hidden)** | $1536 \to 16 \to 32$ | Combinações densas não-lineares com ativações SCReLU |
| **Camada de Saída (L3)** | $32 \to 1$ com 8 Buckets de Material | Saídas especializadas por fase do jogo (da abertura a finais puros) |
| **Função de Ativação** | Squared Clipped ReLU (SCReLU) | $\text{SCReLU}(x) = \text{clamp}(x, 0, 1)^2$ para gradientes suaves |
| **Dataset de Treinamento** | 500 Milhões+ de posições | Partidas do Leela Chess Zero com loss WDL + Centipawns |
| **Quantização** | QA=255, QB=64, Scale=400 | Aritmética inteira SIMD com zero custo de ponto flutuante |

---

### ⚡ Inferência SIMD Vetorizada em AVX2 + BMI2 (NOVO)

- **Atualizações Esparsas no Acumulador**: Pulo dinâmico de neurônios inativos durante a avaliação.
- **Vetorização AVX2 de 256 bits**: Processa 16 pesos simultaneamente com instruções `_mm256_madd_epi16` e `_mm256_packs_epi32`.
- **Velocidade Extrema de Busca**: Atinge mais de **1.200.000 NPS (4 threads)** / **480.000 NPS (1 thread)** em processadores modernos.

---

### ⚔️ Ordenação de Lances com Detecção de Ameaças (NOVO)

Novo módulo dedicado (`threats.h` / `threats.cpp`) para detecção rápida de ameaças táticas em nível de bitboards:
- **Matriz de Ameaças por Peça**: Detecta damas, torres, bispos e cavalos atacados por peças de menor valor.
- **Bônus Dinâmicos de Ameaça**: Prioriza contra-ataques táticos e lances de defesa no topo do MovePicker.
- **Poda Significativa da Árvore**: Reduz expressivamente os nós de busca ao resolver ameaças críticas antes de explorar lances lentos.

---

### 🔍 Aprimoramentos na Busca e Podas

#### Extensões Singulares, Duplas e Triplas (APRIMORADO)
- **Extensões Triplas (+3 plies)**: Adicionadas para lances decisivamente superiores em posições táticas críticas.
- **Verificação Multicut**: Saída antecipada rápida quando variantes alternativas provam que todos os lances superam o cutoff.

#### Continuation History de 4 Níveis (NOVO)
- Tabelas de histórico de continuação rastreando lances jogados após os últimos 1, 2, 4 e 6 plies anteriores.
- Oferece ordenação contextual profunda de lances quietos em linhas táticas complexas.

#### Correction History e Capture History (NOVO)
- **Correction History**: Aprende e calibra erros sistemáticos entre a avaliação estática da NNUE e o resultado da busca.
- **Capture History**: Histórico independente para capturas, refinando a ordenação de capturas além do SEE estático.

#### Poda de Lance Nulo Adaptativa com Verificação (APRIMORADO)
- Escala a redução $R$ dinamicamente com base na profundidade, estado de melhora e margem da avaliação neural.
- Busca de verificação em altas profundidades previne erros de Zugzwang em finais críticos.

---

## Comparativo de Força

| Característica | v2.0 (HCE) | v3.0 (NNUE) |
|----------------|------------|-------------|
| **Motor de Avaliação** | Artesanal (PSQT + King Safety) | **Rede Neural NNUE (v5 Compact)** |
| **King Buckets** | ❌ (0) | **13 King Buckets Anatômicos** |
| **Tamanho do Acumulador** | ❌ | **1536 Neurônios (Dual Perspective)** |
| **Matemática de Inferência** | Escalar HCE | **SIMD Vetorizado AVX2 + BMI2** |
| **Detecção de Ameaças** | ❌ | ✅ (`threats.cpp` Matrizes Bitboard) |
| **Continuation History** | 1 ply | **4 Níveis (1, 2, 4, 6 plies)** |
| **Correction History** | ❌ | ✅ (Calibração Dinâmica de Erro NNUE) |
| **Extensões Triplas** | ❌ | ✅ (+3 plies em lances ultra-singulares) |
| **Pipeline de Treinamento** | ❌ | **PyTorch CUDA + Streaming 500M+** |
| **Força Relativa** | Base | **+120 a +250 Elo** |
