<div align="center">

<img src="assets/logo-deepbecky2.png" alt="Deep Becky Logo" width="220"/>

# Deep Becky — UCI Chess Engine
**Version 3.0 — NNUE Neural Network + Lazy SMP + Advanced Pruning & Extensions**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![AVX2 + BMI2](https://img.shields.io/badge/SIMD-AVX2%20%7C%20BMI2-orange.svg)]()
[![Lichess Bot](https://img.shields.io/badge/Lichess-DeepBecky-green.svg)](https://lichess.org/@/DeepBecky)

</div>

---

## 🇧🇷 Sobre o Projeto

A **Deep Becky** é uma engine de xadrez de alta performance que implementa o protocolo UCI (Universal Chess Interface).

A versão **3.0** marca a evolução para avaliação neural **NNUE v5 Compact (13 King Buckets $\times$ 768 $\times$ 8 Material Buckets)**, treinada em centenas de milhões de posições puras do Leela Chess Zero com ponderação WDL + Centipawns e otimização AVX2 SIMD.

### Destaques da Deep Becky 3.0:
* **Avaliação NNUE v5 Compact:** Rede neural com 13 King Buckets anatômicos, acumulador dual perspective de 1536 neurônios e 8 buckets de material.
* **SIMD AVX2 + BMI2:** Inferência ultra-rápida com poda de neurônios esparsos (~1.200.000 NPS em 4 threads).
* **Busca Avançada (Search):**
  * **Singular / Double / Triple Extensions:** Detecção de lances táticos únicos com extensões profundas e verificação de Multicut.
  * **Threat-Aware Move Ordering:** Detecção dinâmica de ameaças de peças menores e maiores via bitboards nativos.
  * **4-Tier Continuation History:** Tabelas de histórico de 1, 2, 4 e 6 plies para ordenação de lances em posições silenciosas.
  * **Correction History & Capture History:** Calibração dinâmica da avaliação neural durante a busca.
  * **ProbCut de 2 Fases & Adaptive NMP:** Verificação de segurança de Zugzwang e podas agressivas em variantes vencedoras.
* **Lazy SMP Multi-Threading:** Suporte escalável de 1 a 256 threads com sincronização de Transposition Table livre de contenção.

---

## 🇺🇸 About the Project

**Deep Becky** is a high-performance UCI chess engine featuring state-of-the-art NNUE evaluation and modern search heuristics.

### Key Highlights (v3.0):
* **NNUE Evaluation (v5 Compact):** 13 King Buckets, 1536 dual-perspective accumulator, 8 output material buckets, trained on massive Leela Chess Zero datasets.
* **AVX2 SIMD Vectorization:** Highly optimized inference with sparse neuron skipping delivering >1.2M NPS on modern quad-core CPUs.
* **Advanced Search Heuristics:** Singular/Double/Triple Extensions, Threat-Aware Move Ordering, 4-tier Continuation History, Correction History, 2-phase ProbCut, Adaptive NMP with Zugzwang verification, and Lazy SMP multi-threading.

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
