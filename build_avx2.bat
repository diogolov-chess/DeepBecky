@echo off
setlocal EnableExtensions

REM ======================================================
REM Build deepbecky01 - "um modo" (speed + size + compat)
REM - Compatibilidade: Windows x64 qualquer (sem VC++ Redist) => /MT
REM - Performance: /Ox /arch:AVX2 + /GL (LTCG) + /OPT:REF/ICF
REM - Tamanho: /Gy /Gw /GF + /LTCG + /OPT:REF/ICF
REM - UPX: desativado por padrao para torneio/benchmark
REM ======================================================

REM ========= se já estamos no ambiente do VS, pula pro build =========
if defined VSCMD_VER goto :BUILD

REM ---- localizar VsDevCmd (VS 2022)
set "VSCMD="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=* delims=" %%I in (`
    "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  `) do set "VSCMD=%%I\Common7\Tools\VsDevCmd.bat"
)
if not defined VSCMD if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"   set "VSCMD=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
if not defined VSCMD if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" set "VSCMD=C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
if not defined VSCMD if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"  set "VSCMD=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
if not defined VSCMD if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" set "VSCMD=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"

if not defined VSCMD (
  echo [x] Nao encontrei o VsDevCmd.bat do Visual Studio 2022. Ajuste o caminho no script.
  pause
  exit /b 1
)

REM ---- cria um .cmd temporario que entra no Native Tools e volta aqui em modo interno
set "TMPRUN=%TEMP%\__run_native_build_x64.cmd"
> "%TMPRUN%" (
  echo @echo off
  echo call "%VSCMD%" -arch=x64 -host_arch=x64
  echo cd /d "%~dp0"
  echo call "%~f0" --inner
)

start "Build (Native Tools x64)" "%ComSpec%" /k "%TMPRUN%"
exit /b 0


:BUILD
REM ======================== PARTE DE BUILD ==========================
if /I "%~1"=="--inner" shift
title Build deepbecky01 (MSVC x64 - speed+size+compat)
pushd "%~dp0"

REM ---- CONFIG
set "PROJECT=deepbecky-v0.1-windows-x64"
set "OUTDIR=build"
set "SOURCES=*.cpp"

REM ---- Compilador (MSVC)
REM /Ox /arch:AVX2  = max speed; /GL = LTCG; /Gy/Gw = COMDAT separavel; /GF = pool de strings
REM /MT  = runtime estatico (sem VC++ Redistributable)
REM /std:c++20 habilita recursos modernos (neutro p/ velocidade)
set "CLFLAGS=/nologo /EHsc /Ox /arch:AVX2 /GL /Gy /Gw /GF /Oi /Ob2 /DNDEBUG /MT /std:c++20"

REM ---- Linker (LTO + remoção maxima)
set "LDFLAGS=/link /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO"

REM ---- UPX (desligado por padrao p/ torneio)
set "USE_UPX=0"

if not exist "%OUTDIR%" mkdir "%OUTDIR%" >nul 2>&1

echo [*] Compilando (x64 baseline, sem AVX2 fixo)...
echo cl %CLFLAGS% %SOURCES% /Fe"%OUTDIR%\%PROJECT%.exe" %LDFLAGS%
cl %CLFLAGS% %SOURCES% /Fe"%OUTDIR%\%PROJECT%.exe" %LDFLAGS%
if errorlevel 1 (
  echo [x] Erro na compilacao.
  goto :END
)

REM ---- Compactacao opcional com UPX (mantenha 0 para torneio/benchmark)
if "%USE_UPX%"=="1" (
  set "UPX_BIN="
  where upx >nul 2>&1 && for /f "delims=" %%U in ('where upx') do set "UPX_BIN=%%U"
  if not defined UPX_BIN if exist "%ProgramFiles%\upx\upx.exe" set "UPX_BIN=%ProgramFiles%\upx\upx.exe"
  if defined UPX_BIN (
    echo [*] UPX: compactando (modo rapido para nao atrapalhar startup)...
    "%UPX_BIN%" --best "%OUTDIR%\%PROJECT%.exe"
  ) else (
    echo [!] UPX nao encontrado; continuando sem compactacao.
  )
) else (
  echo [*] UPX desativado (recomendado para competicao/benchmark).
)

echo.
echo [✓] Concluido! Binario: "%OUTDIR%\%PROJECT%.exe"
goto :END

:END
popd
exit /b 0
