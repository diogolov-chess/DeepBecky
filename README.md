<div align="center">

<img src="assets/logo-deepbecky2.png" alt="Deep Becky Logo" width="220"/>

# Deep Becky — UCI Chess Engine
**Development (Towards v3.0) — NNUE Neural Network + Lazy SMP**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg?style=for-the-badge)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg?style=for-the-badge&logo=c%2B%2B)](https://isocpp.org/)
[![AVX2 + BMI2](https://img.shields.io/badge/SIMD-AVX2%20%7C%20BMI2-orange.svg?style=for-the-badge)]()
[![Lichess Bot](https://img.shields.io/badge/Lichess-DeepBecky-059669.svg?style=for-the-badge&logo=lichess&logoColor=white)](https://lichess.org/@/DeepBecky)
[![Vibe Coding](https://img.shields.io/badge/Made%20with-Vibe%20Coding-blueviolet.svg?style=for-the-badge)]()

</div>

---

## 🇧🇷 Sobre o Projeto

A **Deep Becky** é uma engine de xadrez experimental desenvolvida como **projeto de hobby**, nascida e refinada através do conceito de **Vibe Coding** (programação em par com Inteligência Artificial).

Tenho um apreço muito especial por este projeto: tudo começou em **julho do ano passado**, de forma caseira, intuitiva e arcaica, conversando em linguagem natural com o ChatGPT. No início, a IA cometia erros constantes — sugeria códigos que nem sequer compilavam, confundia regras fundamentais do xadrez e chegava a gerar movimentos ilegais. A primeiríssima versão sequer utilizava *bitboards*; era uma representação simples e rudimentar de tabuleiro.

Ao longo de todo esse tempo, o projeto passou por centenas de refatorações completas, caça a bugs silenciosos e reescritas de arquitetura. Daquela base experimental e imperfeita, a Deep Becky amadureceu e se transformou em uma engine competitiva em C++17, com bitboards de 64 bits, avaliação neural de última geração e busca paralela de alta velocidade.

Esta versão em desenvolvimento (rumo à versão **3.0**) marca a evolução para a rede neural **NNUE v5 Compact (13 King Buckets $\times$ 768 $\times$ 8 Material Buckets)**, treinada em centenas de milhões de posições puras do Leela Chess Zero com ponderação WDL + Centipawns, inferência ultra-rápida AVX2 SIMD e suporte a múltiplos núcleos via Lazy SMP.

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

**Deep Becky** is an experimental UCI chess engine developed as a **hobby project**, created and continuously evolved through the concept of **Vibe Coding** (human-AI pair programming).

This project holds a very special place for me: it began in **July of last year** in an artisanal, exploratory way, chatting naturally with ChatGPT. In the beginning, the AI made countless mistakes — outputting code that wouldn't even compile, hallucinating chess rules, and proposing illegal moves. The very first iteration didn't even use *bitboards*; it was a basic, naive board representation.

Through relentless iterations, deep refactorings, and bug-hunting over many months, Deep Becky matured into a full-featured, competitive C++17 chess engine. Today it features 64-bit bitboards, state-of-the-art NNUE neural evaluation, and scalable Lazy SMP search.

The current development branch (towards version **3.0**) introduces the transition to **NNUE v5 Compact (13 King Buckets $\times$ 768 $\times$ 8 Material Buckets)**, trained on massive Leela Chess Zero datasets, AVX2 SIMD vectorization, and robust Lazy SMP multi-threading.

### Key Highlights (Towards v3.0):
* **NNUE Evaluation (v5 Compact):** 13 King Buckets, 1536 dual-perspective accumulator, 8 output material buckets, trained on massive Leela Chess Zero datasets.
* **AVX2 SIMD Vectorization:** Highly optimized inference with sparse neuron skipping delivering >1.2M NPS on modern quad-core CPUs.
* **Advanced Search Heuristics:** Singular Extensions, 4-tier Continuation History, Correction History, ProbCut, Adaptive NMP, and depth-qualified Lazy SMP multi-threading.

---

## 🤖 Desenvolvimento por Vibe Coding & Contribuidores de IA / AI Contributors

A evolução da Deep Becky é fruto de uma colaboração estreita entre seu criador humano e múltiplos modelos de Inteligência Artificial:

| IA / Modelo | Provedor | Papel no Projeto |
| :--- | :--- | :--- |
| **ChatGPT / GPT-4o / GPT-5** | OpenAI | **A Centelha Inicial (Julho/2025)**: Concepção da engine, primeiras estruturas de dados, auditorias de código e depuração tática. |
| **Claude (Sonnet / Opus)** | Anthropic | **Refatoração & Performance**: Arquitetura NNUE (13 King Buckets), vetorização SIMD AVX2/BMI2 e afinação de heurísticas de busca. |
| **Gemini (Advanced / Antigravity)** | Google | **Pair Programming & Infraestrutura**: Diagnóstico de partidas no Lichess, rotinas de validação, testes de regressão e Lazy SMP. |

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
