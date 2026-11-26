@echo off
rem Build nativo Windows (MSVC x64 + Ninja) do EternalChampionsRecomp neste repo.
rem   build.cmd              -> build incremental em build\
rem   build.cmd reconfigure  -> reconfigura o CMake (apos mudar game\CMakeLists.txt ou opcoes) e builda
rem   build.cmd reconfigure -DEC_GEN_DEV_TRACE=ON   -> argumentos extras vao para o configure
setlocal
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
if not defined VCToolsInstallDir (echo [build.cmd] vcvars64.bat falhou & exit /b 1)
set "NINJADIR=C:\Users\Administrator\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe"
set "CMAKE=C:\Users\Administrator\AppData\Local\Microsoft\WinGet\Packages\Kitware.CMake_Microsoft.Winget.Source_8wekyb3d8bbwe\cmake-4.4.4-windows-x86_64\bin\cmake.exe"
set "VCPKG_ROOT_DIR=C:\Users\Administrator\AppData\Local\Temp\opencode\vcpkg"
set "PATH=%NINJADIR%;%PATH%"
rem toolchain do vcpkg: o clone em %TEMP% pode ser podado pela limpeza do Windows; nesse caso usa o vcpkg do proprio VS,
rem mantendo os pacotes ja instalados (libpng/zlib) via VCPKG_INSTALLED_DIR
set "VCPKG_TOOLCHAIN=%VCPKG_ROOT_DIR%\scripts\buildsystems\vcpkg.cmake"
if not exist "%VCPKG_TOOLCHAIN%" set "VCPKG_TOOLCHAIN=%VSINSTALLDIR%VC\vcpkg\scripts\buildsystems\vcpkg.cmake"
rem pacotes libpng/zlib: copia duravel fora do %TEMP% (restaurada do cache binario do vcpkg), se existir
set "VCPKG_INSTALLED=%VCPKG_ROOT_DIR%\installed"
if exist "%LOCALAPPDATA%\eternalchampions-vcpkg\installed\x64-windows\include\png.h" set "VCPKG_INSTALLED=%LOCALAPPDATA%\eternalchampions-vcpkg\installed"
cd /d "%ROOT%" || exit /b 1
if not exist "segagenesisrecomp\external\m68k-recomp-core\common\m68k_timing.c" (
  echo [build.cmd] segagenesisrecomp\ ou os submodulos dele estao ausentes.
  echo            Rode: git submodule update --init --recursive   ^(veja README.md^)
  exit /b 1
)

set "DO_CONFIGURE="
if not exist "build\build.ninja" set "DO_CONFIGURE=1"
if /i "%~1"=="reconfigure" (set "DO_CONFIGURE=1" & shift)

if defined DO_CONFIGURE (
  echo === CONFIGURE ===
  "%CMAKE%" -S game -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH="%ROOT%/segagenesisrecomp/runner/external/SDL2/SDL2-2.28.5" ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" ^
    -DVCPKG_INSTALLED_DIR="%VCPKG_INSTALLED%" -DVCPKG_MANIFEST_MODE=OFF ^
    -DVCPKG_TARGET_TRIPLET=x64-windows %1 %2 %3 %4 %5 %6 %7 %8 %9
  if errorlevel 1 exit /b 1
)
echo === BUILD ===
"%CMAKE%" --build build
if errorlevel 1 exit /b 1
echo === BUILD_OK === build\EternalChampionsRecomp.exe
