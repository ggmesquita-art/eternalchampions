@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "NINJADIR=C:\Users\Administrator\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe"
set "CMAKE=C:\Users\Administrator\AppData\Local\Microsoft\WinGet\Packages\Kitware.CMake_Microsoft.Winget.Source_8wekyb3d8bbwe\cmake-4.4.4-windows-x86_64\bin\cmake.exe"
set "PATH=%NINJADIR%;%PATH%"
cd /d "C:\Users\Administrator\AppData\Local\Temp\opencode\ec-win"
if errorlevel 1 exit /b 1
echo === CONFIGURE ===
"%CMAKE%" -S game -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Users/Administrator/AppData/Local/Temp/opencode/ec-win/segagenesisrecomp/runner/external/SDL2/SDL2-2.28.5 -DCMAKE_TOOLCHAIN_FILE=C:/Users/Administrator/AppData/Local/Temp/opencode/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
if errorlevel 1 exit /b 1
echo === BUILD ===
"%CMAKE%" --build build
if errorlevel 1 exit /b 1
echo === BUILD_OK ===