<div align="center">

<img src="assets/logo-deepbecky.png" alt="Deep Becky Logo" width="150"/>

<h3>Deep Becky - UCI Chess Engine</h3>
Version 1.2 — Architectural Restructure + Advanced Search & Time Management
<br>

SEE THE <strong>[LATEST VERSION UPDATE!][changelog]</strong>
<br><br>
<a href="https://lichess.org/@/DeepBecky" target="_blank">
    ♟ BOT Deep Becky on Lichess.org

</div>

---

## English

### About the Project

Deep Becky was born from a simple question: **"Can AI create a functional UCI chess engine from scratch?"**

Development began around July 2025 using conversations with ChatGPT to create the C++ code. The AI wrote 100% of the code while I provided guidance, testing, feedback, and strategic decisions about next steps.

The path was quite challenging - copying code from chat conversations to Notepad, attempting to compile, facing countless compilation errors, and when it finally compiled, dealing with recognition issues in Fritz. After many attempts and corrections, going through engines that weren't recognized, didn't make moves, or made illegal moves, I finally achieved functional code that respects all chess rules.

Today, Deep Becky is developed using **vibe coding** in VSCode, primarily with the AI **Claude Opus** and occasionally **ChatGPT Codex**. This workflow allows for a much faster and more fluid development cycle — writing, refactoring, and debugging code through natural conversation directly in the editor.

Version 1.2 is a **major architectural restructure** of the engine, splitting the codebase from 8 files into **25 files** with proper header/implementation separation and dedicated modules for each subsystem. The search was significantly strengthened with many new pruning techniques (razoring, reverse futility, late move pruning, IID, SEE pruning), a pre-computed logarithmic LMR table, and dynamic contempt that scales from 20 to 200 cp based on evaluation. A professional Stockfish-style **TimeManagement** class handles time allocation with stability/score-drop adjustments, obvious move detection, and game phase awareness. The **TranspositionTable** now supports dynamic sizing via UCI `Hash` option (1–4096 MB) with cache-aligned allocation and prefetching. New bitboard infrastructure (BETWEEN_BB, LINE_BB, RAY_BB) enables pin-aware legal move generation and faster check evasion.

Main v1.2 highlights:
- Full architectural restructure: 25 files with proper H/CPP separation and namespaces
- Search: LMR log table, razoring, reverse futility pruning, IID, late move pruning, futility pruning, SEE pruning
- Dynamic contempt: scales 20–200 cp based on evaluation advantage
- Stockfish-style TimeManagement: optimum/maximum time, stability adjustment, obvious move detection
- Configurable TT: dynamic Hash sizing (1–4096 MB), cache-aligned, prefetch, hashfull reporting
- Bitboard infrastructure: BETWEEN_BB, LINE_BB, RAY_BB for pin detection and check evasion
- Pawn hash table: caches pawn structure evaluation for efficiency
- Lazy evaluation: skips detailed eval when material advantage is decisive (non-endgame)
- Perft command for correctness testing

### How to Compile

#### Windows CMD (MinGW-w64 standalone)

If you have MinGW-w64 installed and available in the system PATH, you can compile directly from the Windows **Command Prompt (cmd)**:

```bash
mingw32-make                        # portable build (default)
mingw32-make PROFILE=avx2           # AVX2 optimized build
mingw32-make PROFILE=bmi2           # AVX2 + BMI2 build
mingw32-make PROFILE=native         # best for your local CPU
```

---

#### MSYS2 MinGW-w64 (Recommended for PGO)

Using the provided Makefile from the **MSYS2 MINGW64** shell:

```bash
make                                # portable build (default, x86-64 generic)
make PROFILE=avx2                   # AVX2 optimized build
make PROFILE=bmi2                   # AVX2 + BMI2 build
make PROFILE=native                 # best for your local CPU
```

**With PGO (Profile-Guided Optimization) for maximum strength:**
```bash
make pgo PROFILE=bmi2
```

**Or step by step:**
```bash
# Step 1: PGO instrumented build + run profiling workload
make pgo-gen PROFILE=bmi2

# Step 2: Rebuild using collected profile data
make pgo-use PROFILE=bmi2
```

**Other useful targets:**
```bash
make clean                          # remove build artifacts
make distclean                      # remove build + PGO data
make info                           # show current build configuration
```

**Requirements:** MSYS2 with `mingw-w64-x86_64-gcc` package installed. Run from the **MSYS2 MINGW64** shell.

---

#### Windows (MSVC)

Using the **x64 Native Tools Command Prompt for VS 2022**:

```bash
cl /nologo /EHsc /O2 /std:c++17 /DNDEBUG /MT /arch:AVX2 main.cpp magic.cpp bitboard.cpp position.cpp movegen.cpp evaluate.cpp search.cpp tt.cpp uci.cpp timeman.cpp movepick.cpp /Fe:deepbecky-v1.2-windows-x64.exe /link /LTCG /OPT:REF /OPT:ICF
```

**Without AVX2 (broader compatibility):**
```bash
cl /nologo /EHsc /O2 /std:c++17 /DNDEBUG /MT main.cpp magic.cpp bitboard.cpp position.cpp movegen.cpp evaluate.cpp search.cpp tt.cpp uci.cpp timeman.cpp movepick.cpp /Fe:deepbecky-v1.2-windows-x64.exe /link /LTCG /OPT:REF /OPT:ICF
```

**Requirements:** Visual Studio 2022 with C++ tools

---

#### Linux / macOS (GCC/Clang)

The Makefile works natively on Linux and macOS:

```bash
make                                # portable build
make PROFILE=native                 # native optimized
```

**With PGO:**
```bash
make pgo PROFILE=native
```

**Manual compilation (without Makefile):**
```bash
g++ -O3 -std=c++17 -march=native -DNDEBUG -flto main.cpp magic.cpp bitboard.cpp position.cpp movegen.cpp evaluate.cpp search.cpp tt.cpp uci.cpp timeman.cpp movepick.cpp -o deepbecky
```

**Static linking (fully portable binary):**
```bash
g++ -O3 -std=c++17 -march=native -DNDEBUG -flto -static -static-libgcc -static-libstdc++ main.cpp magic.cpp bitboard.cpp position.cpp movegen.cpp evaluate.cpp search.cpp tt.cpp uci.cpp timeman.cpp movepick.cpp -o deepbecky
```

---

#### Notes

- **C++17 or higher required**
- The Makefile enables **LTO** (Link-Time Optimization) and **static linking** by default
- **PGO builds** can provide ~5-10% speed improvement (requires MSYS2 or Linux/macOS shell)
- Available profiles: `portable` (default), `sse42`, `avx2`, `bmi2`, `native`


### How to Use

The engine works via command line using the UCI protocol. Can be used in graphical interfaces such as:
- Arena Chess GUI
- Fritz
- ChessBase
- Cute Chess
- BanksiaGUI

## Acknowledgments

This project demonstrates the current capabilities of AI-assisted software development. The initial versions were created through conversations with ChatGPT, and today the engine is developed using **Claude Opus** and **ChatGPT Codex** via vibe coding in VSCode. All code is AI-generated based on human guidance, testing, strategic decisions, and iterative feedback.

---

**Note:** Deep Becky is an experimental project created with AI assistance for educational and research purposes.

---

## Português

### Sobre o Projeto

Deep Becky nasceu de uma pergunta simples: **"Será que a IA consegue criar do zero uma engine UCI de xadrez funcional?"**

O desenvolvimento começou por volta de julho de 2025, utilizando conversas com o ChatGPT para criar o código em C++. A IA escreveu 100% do código enquanto eu fornecia orientação, testes, feedback e decisões estratégicas sobre os próximos passos.

O caminho foi bastante desafiador - copiando código das conversas para o Notepad, tentando compilar, enfrentando inúmeros erros de compilação e, quando finalmente compilava, lidando com problemas de reconhecimento no Fritz. Depois de muitas tentativas e correções, passando por engines que não eram reconhecidas, que não faziam movimentos, ou que faziam lances ilegais, finalmente consegui um código funcional que respeita todas as regras do xadrez.

Hoje, a Deep Becky é desenvolvida utilizando **vibe coding** no VSCode, principalmente com a IA **Claude Opus** e ocasionalmente com o **ChatGPT Codex**. Esse fluxo de trabalho permite um ciclo de desenvolvimento muito mais rápido e fluido — escrevendo, refatorando e depurando código através de conversa natural diretamente no editor.

A versão 1.2 é uma **grande reestruturação arquitetural** da engine, dividindo o código de 8 para **25 arquivos** com separação adequada header/implementação e módulos dedicados para cada subsistema. A busca foi significativamente fortalecida com muitas novas técnicas de poda (razoring, reverse futility, late move pruning, IID, poda SEE), tabela LMR logarítmica pré-computada e contempt dinâmico que escala de 20 a 200 cp baseado na avaliação. Uma classe profissional **TimeManagement** estilo Stockfish gerencia a alocação de tempo com ajustes de estabilidade/queda de score, detecção de lance óbvio e consciência de fase do jogo. A **TranspositionTable** agora suporta dimensionamento dinâmico via opção UCI `Hash` (1–4096 MB) com alocação alinhada ao cache e prefetching. Nova infraestrutura de bitboard (BETWEEN_BB, LINE_BB, RAY_BB) permite geração legal de lances consciente de cravadas e evasão de xeque mais rápida.

Principais destaques da v1.2:
- Reestruturação arquitetural completa: 25 arquivos com separação H/CPP e namespaces
- Busca: tabela log LMR, razoring, reverse futility pruning, IID, late move pruning, futility pruning, poda SEE
- Contempt dinâmico: escala de 20–200 cp baseado na vantagem de avaliação
- TimeManagement estilo Stockfish: tempo ótimo/máximo, ajuste de estabilidade, detecção de lance óbvio
- TT configurável: dimensionamento dinâmico Hash (1–4096 MB), alinhada ao cache, prefetch, reporte hashfull
- Infraestrutura de bitboard: BETWEEN_BB, LINE_BB, RAY_BB para detecção de cravadas e evasão de xeque
- Tabela hash de peões: cacheia avaliação de estrutura de peões por eficiência
- Avaliação preguiçosa: pula avaliação detalhada quando vantagem material é decisiva (fora de finais)
- Comando perft para testes de correção

### Como Compilar

#### Windows CMD (MinGW-w64 standalone)

Se você tem o MinGW-w64 instalado e disponível no PATH do sistema, pode compilar diretamente pelo **Prompt de Comando (cmd)** do Windows:

```bash
mingw32-make                        # build portátil (padrão)
mingw32-make PROFILE=avx2           # build otimizado para AVX2
mingw32-make PROFILE=bmi2           # build AVX2 + BMI2
mingw32-make PROFILE=native         # melhor para sua CPU local
```

---

#### MSYS2 MinGW-w64 (Recomendado para PGO)

Usando o Makefile fornecido no shell **MSYS2 MINGW64**:

```bash
make                                # build portátil (padrão, x86-64 genérico)
make PROFILE=avx2                   # build otimizado para AVX2
make PROFILE=bmi2                   # build AVX2 + BMI2
make PROFILE=native                 # melhor para sua CPU local
```

**Com PGO (Profile-Guided Optimization) para força máxima:**
```bash
make pgo PROFILE=bmi2
```

**Ou passo a passo:**
```bash
# Passo 1: Build instrumentado PGO + execução para coleta de perfis
make pgo-gen PROFILE=bmi2

# Passo 2: Rebuild usando os dados de perfil coletados
make pgo-use PROFILE=bmi2
```

**Outros targets úteis:**
```bash
make clean                          # remove artefatos de build
make distclean                      # remove build + dados PGO
make info                           # mostra configuração atual de build
```

**Requisitos:** MSYS2 com pacote `mingw-w64-x86_64-gcc` instalado. Execute no shell **MSYS2 MINGW64**.

---

#### Windows (MSVC)

Usando o **Prompt de Comando de Ferramentas Nativas x64 do VS 2022**:

```bash
cl /nologo /EHsc /O2 /std:c++17 /DNDEBUG /MT /arch:AVX2 main.cpp magic.cpp bitboard.cpp position.cpp movegen.cpp evaluate.cpp search.cpp tt.cpp uci.cpp timeman.cpp movepick.cpp /Fe:deepbecky-v1.2-windows-x64.exe /link /LTCG /OPT:REF /OPT:ICF
```

**Sem AVX2 (compatibilidade mais ampla):**
```bash
cl /nologo /EHsc /O2 /std:c++17 /DNDEBUG /MT main.cpp magic.cpp bitboard.cpp position.cpp movegen.cpp evaluate.cpp search.cpp tt.cpp uci.cpp timeman.cpp movepick.cpp /Fe:deepbecky-v1.2-windows-x64.exe /link /LTCG /OPT:REF /OPT:ICF
```

**Requisitos:** Visual Studio 2022 com ferramentas C++

---

#### Linux / macOS (GCC/Clang)

O Makefile funciona nativamente no Linux e macOS:

```bash
make                                # build portátil
make PROFILE=native                 # otimizado nativo
```

**Com PGO:**
```bash
make pgo PROFILE=native
```

**Compilação manual (sem Makefile):**
```bash
g++ -O3 -std=c++17 -march=native -DNDEBUG -flto main.cpp magic.cpp bitboard.cpp position.cpp movegen.cpp evaluate.cpp search.cpp tt.cpp uci.cpp timeman.cpp movepick.cpp -o deepbecky
```

**Linkagem estática (binário totalmente portátil):**
```bash
g++ -O3 -std=c++17 -march=native -DNDEBUG -flto -static -static-libgcc -static-libstdc++ main.cpp magic.cpp bitboard.cpp position.cpp movegen.cpp evaluate.cpp search.cpp tt.cpp uci.cpp timeman.cpp movepick.cpp -o deepbecky
```

---

#### Observações

- **C++17 ou superior necessário**
- O Makefile habilita **LTO** (Link-Time Optimization) e **linkagem estática** por padrão
- **Builds PGO** podem fornecer ~5-10% de melhoria de velocidade (requer MSYS2 ou shell Linux/macOS)
- Perfis disponíveis: `portable` (padrão), `sse42`, `avx2`, `bmi2`, `native`

### Como Usar

A engine funciona através de linha de comando com o protocolo UCI. Pode ser usada em interfaces gráficas como:
- Arena Chess GUI
- Fritz
- ChessBase
- Cute Chess
- BanksiaGUI

## Agradecimentos

Este projeto demonstra as capacidades atuais de desenvolvimento de software assistido por IA. As versões iniciais foram criadas através de conversas com o ChatGPT, e hoje a engine é desenvolvida utilizando **Claude Opus** e **ChatGPT Codex** via vibe coding no VSCode. Todo o código é gerado por IA baseado em orientação humana, testes, decisões estratégicas e feedback iterativo.

---

**Nota:** Deep Becky é um projeto experimental criado com assistência de IA para fins educacionais e de pesquisa.


[changelog]:          https://github.com/diogolov-chess/DeepBecky/releases/latest
