<div align="center">

<img src="assets/logo-deepbecky2.png" alt="Deep Becky Logo" width="220"/>

# Deep Becky — UCI Chess Engine
**Development (Towards v3.0) — NNUE Neural Network + Lazy SMP**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![AVX2 + BMI2](https://img.shields.io/badge/SIMD-AVX2%20%7C%20BMI2-orange.svg)]()
[![Lichess Bot](https://img.shields.io/badge/Lichess-DeepBecky-green.svg)](https://lichess.org/@/DeepBecky)

</div>

---

## 🇧🇷 Sobre o Projeto

A **Deep Becky** é uma engine de xadrez de alta performance que implementa o protocolo UCI (Universal Chess Interface).

Esta versão em desenvolvimento (rumo à versão **3.0**) marca a evolução arquitetural para avaliação neural **NNUE v5 Compact (13 King Buckets $\times$ 768 $\times$ 8 Material Buckets)**, treinada em centenas de milhões de posições puras do Leela Chess Zero com ponderação WDL + Centipawns, inferência otimizada AVX2 SIMD e suporte a múltiplos núcleos via Lazy SMP.

### Destaques em Desenvolvimento (Rumo à 3.0):
* **Avaliação NNUE v5 Compact:** Rede neural com 13 King Buckets anatômicos, acumulador dual perspective de 1536 neurônios e 8 buckets de material.
* **SIMD AVX2 + BMI2:** Inferência ultra-rápida com poda de neurônios esparsos (~1.200.000 NPS em 4 threads).
* **Busca e Heurísticas Avançadas (Search):**
  * **Singular Extensions & PVS:** Detecção de lances táticos únicos com extensões e verificação de Multicut.
  * **Lazy SMP Multi-Threading:** Suporte escalável de 1 a 256 threads com sincronização de Transposition Table livre de contenção e seleção qualificada de melhor lance.
  * **4-Tier Continuation History:** Tabelas de histórico de 1, 2, 4 e 6 plies para ordenação de lances em posições silenciosas.
  * **Correction History & Capture History:** Calibração dinâmica da avaliação neural durante a busca.
  * **ProbCut & Adaptive NMP:** Verificação de segurança de Zugzwang e podas dinâmicas em variantes vencedoras.

---

## 🇺🇸 About the Project

**Deep Becky** is a high-performance UCI chess engine featuring state-of-the-art NNUE evaluation and modern search heuristics.

The current development branch (towards version **3.0**) introduces the transition to **NNUE v5 Compact (13 King Buckets $\times$ 768 $\times$ 8 Material Buckets)**, trained on massive Leela Chess Zero datasets, AVX2 SIMD vectorization, and robust Lazy SMP multi-threading.

### Key Highlights (Towards v3.0):
* **NNUE Evaluation (v5 Compact):** 13 King Buckets, 1536 dual-perspective accumulator, 8 output material buckets, trained on massive Leela Chess Zero datasets.
* **AVX2 SIMD Vectorization:** Highly optimized inference with sparse neuron skipping delivering >1.2M NPS on modern quad-core CPUs.
* **Advanced Search Heuristics:** Singular Extensions, 4-tier Continuation History, Correction History, ProbCut, Adaptive NMP, and depth-qualified Lazy SMP multi-threading.

---

## 🛠️ Como Compilar / How to Compile

### Windows (MSYS2 MINGW64 — Recomendado com PGO):
```bash
cd src
make pgo PROFILE=bmi2 CXX=clang++    # Compilação máxima com PGO + ThinLTO
```

### Windows CMD (MinGW64):
```bash
cd src
mingw32-make PROFILE=bmi2            # Compilação direta AVX2 + BMI2
```

### Linux (GCC / Clang):
```bash
cd src
make pgo PROFILE=native              # Otimizado para a CPU do host
```

---

## 📄 Licença / License

Este projeto está sob a licença [GNU General Public License v3.0 (GPLv3)](LICENSE).
