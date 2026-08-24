@echo off
setlocal EnableDelayedExpansion
title Deep Becky 3.0 - Central de Compilacao

:: Definir pasta da engine como o diretorio atual
set "ENGINE_DIR=%~dp0"

:: Localizar o MSYS2
set "MSYS2_SHELL="
if exist "C:\msys64\msys2_shell.cmd" set "MSYS2_SHELL=C:\msys64\msys2_shell.cmd"
if exist "D:\msys64\msys2_shell.cmd" set "MSYS2_SHELL=D:\msys64\msys2_shell.cmd"

:MENU
cls
echo ===============================================================================
echo                DEEP BECKY 2.5 - CENTRAL DE COMPILACAO E PGO
echo ===============================================================================
echo  Diretorio da Engine: %ENGINE_DIR%
if defined MSYS2_SHELL (
    echo  MSYS2 Detectado:    %MSYS2_SHELL%
) else (
    echo  MSYS2 Detectado:    [NAO ENCONTRADO EM C:\msys64] (Usando MinGW Nativo)
)
echo ===============================================================================
echo.
echo  --- COMPILACOES AVANCADAS COM PGO (PROFILE-GUIDED OPTIMIZATION) ---
echo   [1] Compilar com Clang PGO (MSYS2 CLANG64 / MINGW64) -- [RECOMENDADO / MAX NPS]
echo   [2] Compilar com GCC PGO   (MSYS2 MINGW64)           -- [GCC Classico]
echo   [3] Compilar com GCC PGO   (MSYS2 UCRT64)            -- [GCC UCRT Moderno]
echo.
echo  --- COMPILACOES RAPIDAS (DESENVOLVIMENTO / SEM PGO) ---
echo   [4] Compilar Rapido com Clang (MSYS2)
echo   [5] Compilar Rapido com GCC   (MSYS2 ou Windows)
echo   [6] Compilar com GCC Nativo no Windows (Terminal Atual)
echo.
echo  --- ABRIR TERMINAL INTERATIVO DO MSYS2 NA PASTA DA ENGINE ---
echo   [C] Abrir Terminal MSYS2 CLANG64 aqui
echo   [M] Abrir Terminal MSYS2 MINGW64 aqui
echo   [U] Abrir Terminal MSYS2 UCRT64 aqui
echo.
echo  --- FERRAMENTAS E TESTES ---
echo   [7] Menu de Testes da Engine (Movetime, Depth, Kiwipete, Perft, UCI)
echo   [8] Limpar Arquivos de Build (clean / distclean)
echo   [9] Instalar / Atualizar Pacotes Recomendados no MSYS2
echo.
echo   [0] Sair
echo.
echo ===============================================================================
set "OPTION="
set /p OPTION="Escolha uma opcao: "

if /i "%OPTION%"=="1" goto BUILD_CLANG_PGO
if /i "%OPTION%"=="2" goto BUILD_GCC_MINGW64_PGO
if /i "%OPTION%"=="3" goto BUILD_GCC_UCRT64_PGO
if /i "%OPTION%"=="4" goto BUILD_CLANG_FAST
if /i "%OPTION%"=="5" goto BUILD_GCC_FAST
if /i "%OPTION%"=="6" goto BUILD_WINDOWS_NATIVE
if /i "%OPTION%"=="C" goto OPEN_SHELL_CLANG64
if /i "%OPTION%"=="M" goto OPEN_SHELL_MINGW64
if /i "%OPTION%"=="U" goto OPEN_SHELL_UCRT64
if /i "%OPTION%"=="7" goto TEST_MENU
if /i "%OPTION%"=="8" goto CLEAN_BUILD
if /i "%OPTION%"=="9" goto INSTALL_PACKAGES
if /i "%OPTION%"=="0" goto EXIT
goto MENU

:CHECK_MSYS2
if not defined MSYS2_SHELL (
    echo.
    echo [ERRO] O MSYS2 nao foi encontrado em C:\msys64\msys2_shell.cmd.
    echo Por favor, verifique se o MSYS2 esta instalado no caminho padrao.
    pause
    goto MENU
)
goto :eof

:BUILD_CLANG_PGO
call :CHECK_MSYS2
cls
echo ===============================================================================
echo  INICIANDO COMPILACAO: CLANG PGO + THINLTO (PROFILE=bmi2)
echo ===============================================================================
echo.
cd /d "%ENGINE_DIR%"
call "%MSYS2_SHELL%" -mingw64 -defterm -no-start -here -c "mingw32-make pgo CXX=clang++ PROFILE=bmi2"
echo.
echo ===============================================================================
echo Compilacao finalizada!
pause
goto MENU

:BUILD_GCC_MINGW64_PGO
call :CHECK_MSYS2
cls
echo ===============================================================================
echo  INICIANDO COMPILACAO: GCC PGO (MSYS2 MINGW64) (PROFILE=bmi2)
echo ===============================================================================
echo.
cd /d "%ENGINE_DIR%"
call "%MSYS2_SHELL%" -mingw64 -defterm -no-start -here -c "mingw32-make pgo PROFILE=bmi2"
echo.
echo ===============================================================================
echo Compilacao finalizada!
pause
goto MENU

:BUILD_GCC_UCRT64_PGO
call :CHECK_MSYS2
cls
echo ===============================================================================
echo  INICIANDO COMPILACAO: GCC PGO (MSYS2 UCRT64) (PROFILE=bmi2)
echo ===============================================================================
echo.
cd /d "%ENGINE_DIR%"
call "%MSYS2_SHELL%" -ucrt64 -defterm -no-start -here -c "which mingw32-make >/dev/null 2>&1 || { echo '[ERRO] Pacotes do UCRT64 nao instalados! Use a opcao 9 do menu.'; exit 1; }; mingw32-make pgo PROFILE=bmi2"
echo.
echo ===============================================================================
echo Processo finalizado!
pause
goto MENU

:BUILD_CLANG_FAST
call :CHECK_MSYS2
cls
echo ===============================================================================
echo  COMPILACAO RAPIDA COM CLANG (SEM PGO)
echo ===============================================================================
echo.
cd /d "%ENGINE_DIR%"
call "%MSYS2_SHELL%" -mingw64 -defterm -no-start -here -c "mingw32-make clean && mingw32-make CXX=clang++ PROFILE=bmi2"
echo.
echo ===============================================================================
echo Compilacao finalizada!
pause
goto MENU

:BUILD_GCC_FAST
call :CHECK_MSYS2
cls
echo ===============================================================================
echo  COMPILACAO RAPIDA COM GCC (MSYS2) (SEM PGO)
echo ===============================================================================
echo.
cd /d "%ENGINE_DIR%"
call "%MSYS2_SHELL%" -mingw64 -defterm -no-start -here -c "mingw32-make clean && mingw32-make PROFILE=bmi2"
echo.
echo ===============================================================================
echo Compilacao finalizada!
pause
goto MENU

:BUILD_WINDOWS_NATIVE
cls
echo ===============================================================================
echo  COMPILACAO NO TERMINAL WINDOWS (CMD / PowerShell)
echo ===============================================================================
echo.
cd /d "%ENGINE_DIR%"
call mingw32-make clean
call mingw32-make PROFILE=bmi2
echo.
echo ===============================================================================
echo Compilacao finalizada!
pause
goto MENU

:OPEN_SHELL_CLANG64
call :CHECK_MSYS2
cd /d "%ENGINE_DIR%"
start "" "%MSYS2_SHELL%" -clang64 -here
goto MENU

:OPEN_SHELL_MINGW64
call :CHECK_MSYS2
cd /d "%ENGINE_DIR%"
start "" "%MSYS2_SHELL%" -mingw64 -here
goto MENU

:OPEN_SHELL_UCRT64
call :CHECK_MSYS2
cd /d "%ENGINE_DIR%"
start "" "%MSYS2_SHELL%" -ucrt64 -here
goto MENU

:: ===============================================================================
:: SUBMENU DE TESTES E BENCHMARKS
:: ===============================================================================
:TEST_MENU
cls
cd /d "%ENGINE_DIR%"
if not exist "DeepBecky_3.0.exe" (
    echo.
    echo ===============================================================================
    echo [AVISO] O executavel DeepBecky_3.0.exe nao foi encontrado na pasta!
    echo Por favor, compile a engine primeiro usando as opcoes 1, 2 ou 4 do menu.
    echo ===============================================================================
    pause
    goto MENU
)

echo ===============================================================================
echo                DEEP BECKY 2.5 - CENTRAL DE TESTES E BENCHMARK
echo ===============================================================================
echo  Executavel Ativo: %ENGINE_DIR%\DeepBecky_3.0.exe
echo ===============================================================================
echo.
echo   [1] Teste Rapido de 10 Segundos na Posicao Inicial (go movetime 10000)
echo   [2] Teste com Tempo Personalizado em Segundos       (go movetime X)
echo   [3] Teste por Profundidade Fixa                    (go depth X - ex: 15, 18, 20)
echo   [4] Teste em Posicao Tatica / Kiwipete             (go movetime 10000)
echo   [5] Teste em Final de Partida (Torres e Peoes)    (go movetime 10000)
echo   [6] Teste de Velocidade Perft                      (perft 6 - Movegen Benchmark)
echo   [7] Abrir Terminal Interativo UCI                  (Digitar comandos livremente)
echo.
echo   [0] Voltar ao Menu Principal
echo.
echo ===============================================================================
set "TEST_OPT="
set /p TEST_OPT="Escolha uma opcao de teste [0-7]: "

if "%TEST_OPT%"=="1" goto TEST_10S
if "%TEST_OPT%"=="2" goto TEST_CUSTOM_TIME
if "%TEST_OPT%"=="3" goto TEST_CUSTOM_DEPTH
if "%TEST_OPT%"=="4" goto TEST_KIWIPETE
if "%TEST_OPT%"=="5" goto TEST_ENDGAME
if "%TEST_OPT%"=="6" goto TEST_PERFT
if "%TEST_OPT%"=="7" goto TEST_INTERACTIVE
if "%TEST_OPT%"=="0" goto MENU
goto TEST_MENU

:TEST_10S
cls
echo ===============================================================================
echo  TESTE: POSICAO INICIAL COM 10 SEGUNDOS (go movetime 10000)
echo ===============================================================================
echo.
cd /d "%ENGINE_DIR%"
(
  echo uci
  echo isready
  echo position startpos
  echo go movetime 10000
  echo isready
  echo quit
) > deepbecky_test.tmp
DeepBecky_3.0.exe < deepbecky_test.tmp
if exist deepbecky_test.tmp del /q deepbecky_test.tmp
echo.
echo ===============================================================================
echo Teste concluido!
pause
goto TEST_MENU

:TEST_CUSTOM_TIME
cls
echo ===============================================================================
echo  TESTE: TEMPO PERSONALIZADO
echo ===============================================================================
echo.
set "USER_SECS="
set /p USER_SECS="Digite o tempo desejado em SEGUNDOS [padrao = 5]: "
if not defined USER_SECS set "USER_SECS=5"
set /a USER_MS=USER_SECS*1000
echo.
echo Executando busca na posicao inicial por %USER_SECS% segundos (%USER_MS% ms)...
echo.
cd /d "%ENGINE_DIR%"
(
  echo uci
  echo isready
  echo position startpos
  echo go movetime !USER_MS!
  echo isready
  echo quit
) > deepbecky_test.tmp
DeepBecky_3.0.exe < deepbecky_test.tmp
if exist deepbecky_test.tmp del /q deepbecky_test.tmp
echo.
echo ===============================================================================
echo Teste concluido!
pause
goto TEST_MENU

:TEST_CUSTOM_DEPTH
cls
echo ===============================================================================
echo  TESTE: PROFUNDIDADE FIXA
echo ===============================================================================
echo.
set "USER_DEPTH="
set /p USER_DEPTH="Digite a profundidade desejada [ex: 15, 18, 20]: "
if not defined USER_DEPTH set "USER_DEPTH=15"
echo.
echo Executando busca na posicao inicial ate a profundidade %USER_DEPTH%...
echo.
cd /d "%ENGINE_DIR%"
(
  echo uci
  echo isready
  echo position startpos
  echo go depth !USER_DEPTH!
  echo isready
  echo quit
) > deepbecky_test.tmp
DeepBecky_3.0.exe < deepbecky_test.tmp
if exist deepbecky_test.tmp del /q deepbecky_test.tmp
echo.
echo ===============================================================================
echo Teste concluido!
pause
goto TEST_MENU

:TEST_KIWIPETE
cls
echo ===============================================================================
echo  TESTE: POSICAO TATICA COMPLEXA (KIWIPETE) - 10 SEGUNDOS
echo  FEN: r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -
echo ===============================================================================
echo.
cd /d "%ENGINE_DIR%"
(
  echo uci
  echo isready
  echo position fen r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -
  echo go movetime 10000
  echo isready
  echo quit
) > deepbecky_test.tmp
DeepBecky_3.0.exe < deepbecky_test.tmp
if exist deepbecky_test.tmp del /q deepbecky_test.tmp
echo.
echo ===============================================================================
echo Teste concluido!
pause
goto TEST_MENU

:TEST_ENDGAME
cls
echo ===============================================================================
echo  TESTE: FINAL DE PARTIDA (TORRES E PEOES) - 10 SEGUNDOS
echo  FEN: 8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -
echo ===============================================================================
echo.
cd /d "%ENGINE_DIR%"
(
  echo uci
  echo isready
  echo position fen 8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -
  echo go movetime 10000
  echo isready
  echo quit
) > deepbecky_test.tmp
DeepBecky_3.0.exe < deepbecky_test.tmp
if exist deepbecky_test.tmp del /q deepbecky_test.tmp
echo.
echo ===============================================================================
echo Teste concluido!
pause
goto TEST_MENU

:TEST_PERFT
cls
echo ===============================================================================
echo  TESTE DE VELOCIDADE DO GERADOR DE LANCES (PERFT 6)
echo ===============================================================================
echo.
cd /d "%ENGINE_DIR%"
(
  echo uci
  echo isready
  echo position startpos
  echo perft 6
  echo quit
) > deepbecky_test.tmp
DeepBecky_3.0.exe < deepbecky_test.tmp
if exist deepbecky_test.tmp del /q deepbecky_test.tmp
echo.
echo ===============================================================================
echo Teste concluido!
pause
goto TEST_MENU

:TEST_INTERACTIVE
cls
echo ===============================================================================
echo  CONEXAO DIRETA COM A ENGINE (CONSOLE UCI INTERATIVO)
echo  Digite comandos UCI diretamente (ex: uci, isready, position startpos, go depth 10, quit)
echo ===============================================================================
echo.
cd /d "%ENGINE_DIR%"
DeepBecky_3.0.exe
echo.
echo ===============================================================================
pause
goto TEST_MENU

:: ===============================================================================
:: LIMPEZA E INSTALACAO
:: ===============================================================================
:CLEAN_BUILD
cls
echo ===============================================================================
echo  LIMPANDO OBJETOS E DADOS PGO
echo ===============================================================================
echo.
cd /d "%ENGINE_DIR%"
if defined MSYS2_SHELL (
    call "%MSYS2_SHELL%" -mingw64 -defterm -no-start -here -c "mingw32-make distclean"
) else (
    call mingw32-make distclean
)
echo.
echo Limpeza concluida!
echo ===============================================================================
pause
goto MENU

:INSTALL_PACKAGES
call :CHECK_MSYS2
cls
echo ===============================================================================
echo  INSTALANDO PACOTES RECOMENDADOS NO MSYS2 (MINGW64, CLANG64, UCRT64)
echo ===============================================================================
echo.
echo Sera executado o pacman para garantir que GCC, Clang, LLD, LLVM e Make
echo estejam prontos nos tres ambientes.
echo.
call "%MSYS2_SHELL%" -defterm -no-start -c "pacman -Sy --noconfirm --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-clang mingw-w64-x86_64-lld mingw-w64-x86_64-llvm mingw-w64-x86_64-make mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-lld mingw-w64-clang-x86_64-llvm mingw-w64-clang-x86_64-make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make"
echo.
echo ===============================================================================
echo Instalacao concluida!
pause
goto MENU

:EXIT
exit /b 0
