@echo off
rem ===========================================================================
rem  Shareable build: llama.cpp + ggml (CPU, Vulkan, CUDA) linked statically into
rem  translate_text.exe, plus the Qt GUI. Result: the dist\ folder.
rem
rem  Needs: Visual Studio 2022 (C++ workload), CUDA Toolkit 12.8+, Vulkan SDK,
rem         Git, and Qt 6 (MinGW kit) for the GUI.  See README.md.
rem  Run from this folder in a normal command prompt:   build_static.bat
rem ===========================================================================
setlocal

rem ---- paths (edit if yours differ) -----------------------------------------
set "QT_DIR=C:\Qt\6.11.0\mingw_64"
set "QT_TOOLS=C:\Qt\Tools"
rem CUDA kernels: 75-virtual = PTX for RTX 20xx and newer, plus native code for
rem RTX 30xx (86), 40xx (89) and 50xx (120). Fewer entries = faster build.
set "CUDA_ARCHS=75-virtual;86-real;89-real;120-real"
rem Tested versions of the libraries (change to update them).
set "LLAMA_COMMIT=bd4eeaa047006cb1fe71999fbd11134b5836e167"
set "MINIZ_COMMIT=2948eebafc5b9f1c69bebed80333e403a09bd62d"
rem ---------------------------------------------------------------------------

rem An RC variable left by an earlier version of this script would be taken as
rem the resource compiler by CMake.
set "RC="
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
cd /d "%ROOT%"

if not defined CUDA_PATH (
    echo [ERROR] CUDA Toolkit not found ^(CUDA_PATH is not set^). Install CUDA 12.8 or newer.
    exit /b 1
)
if not defined VULKAN_SDK (
    echo [ERROR] Vulkan SDK not found ^(VULKAN_SDK is not set^). Install it from https://vulkan.lunarg.com
    exit /b 1
)
where git >nul 2>&1 || (echo [ERROR] git not found on PATH. & exit /b 1)

rem ---- 1. sources -------------------------------------------------------------
call :fetch llama.cpp https://github.com/ggml-org/llama.cpp %LLAMA_COMMIT% || exit /b 1
call :fetch miniz https://github.com/richgel999/miniz %MINIZ_COMMIT% || exit /b 1

rem ---- 2. GUI with Qt's MinGW (separate process, so a different compiler is fine)
echo [INFO] Building the GUI with MinGW ...
setlocal
set "PATH=%QT_TOOLS%\mingw1310_64\bin;%QT_TOOLS%\CMake_64\bin;%QT_TOOLS%\Ninja;%QT_DIR%\bin;%PATH%"
cmake -S . -B build-gui -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ ^
      -DCMAKE_PREFIX_PATH="%QT_DIR%" -DTRANSLATE_BUILD_CLI=OFF -DTRANSLATE_BUILD_TESTS=OFF
if errorlevel 1 goto :fail
cmake --build build-gui
if errorlevel 1 goto :fail
endlocal

rem ---- 3. Static command-line program with MSVC --------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo [ERROR] Visual Studio not found. & exit /b 1)
rem Visual Studio 2022 first: the build folder was made with it and CUDA 12
rem supports it. A newer one (Visual Studio 2026 = version 18) only without it.
set "VS_PATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -version [17.0^,18.0^) -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
if not defined VS_PATH for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
if not defined VS_PATH (echo [ERROR] Visual Studio C++ tools not found. & exit /b 1)
echo [INFO] Visual Studio: %VS_PATH%
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo [ERROR] vcvars64.bat failed. & exit /b 1)
call :check_compiler build-static
set "PATH=%PATH%;%QT_TOOLS%\CMake_64\bin;%QT_TOOLS%\Ninja"

echo [INFO] Configuring the static build (llama.cpp + CUDA + Vulkan) ...
cmake -S . -B build-static -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DTRANSLATE_BUILD_GUI=OFF -DTRANSLATE_BUILD_TESTS=ON ^
      -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
      -DBUILD_SHARED_LIBS=OFF -DGGML_NATIVE=OFF -DGGML_BACKEND_DL=OFF -DGGML_STATIC=ON ^
      -DGGML_VULKAN=ON -DGGML_CUDA=ON -DGGML_CUDA_NO_VMM=ON ^
      "-DCMAKE_CUDA_ARCHITECTURES=%CUDA_ARCHS%"
if errorlevel 1 goto :fail

echo [INFO] Compiling (the CUDA kernels take a long time on the first build) ...
cmake --build build-static --target translate_text translate_tests translate_cli_contract
if errorlevel 1 goto :fail
rem The unit tests and the command-line exit-code contract (partial_exit_contract).
ctest --test-dir build-static --output-on-failure
if errorlevel 1 (echo [ERROR] Unit tests failed. & goto :fail)
echo [INFO] Unit tests passed

rem ---- 4. Assemble dist\ ----------------------------------------------------------
echo [INFO] Assembling dist\ ...
if exist dist rmdir /s /q dist
mkdir dist
xcopy /e /i /q /y build-gui\bin dist >nul
copy /y build-static\bin\translate_text.exe dist\ >nul
copy /y build-static\bin\cublas*.dll dist\ >nul 2>&1
mkdir dist\licenses
copy /y third_party\llama.cpp\LICENSE dist\licenses\llama.cpp-LICENSE.txt >nul
copy /y third_party\miniz\LICENSE dist\licenses\miniz-LICENSE.txt >nul
copy /y installer\license.txt dist\licenses\Translate-LICENSES.txt >nul
copy /y "%QT_DIR%\..\..\Licenses\*" dist\licenses\ >nul 2>&1

echo.
echo [SUCCESS] Shareable program folder: %ROOT%\dist
echo   translate_text.exe      self-contained (CPU + Vulkan + CUDA)
echo   cublas64_*.dll ...      only needed for CUDA; without them it uses Vulkan
echo   translate_gui.exe + Qt DLLs
exit /b 0

rem ---- clone REPO at COMMIT into third_party\NAME, or move an existing clone to it --
rem (No %ERRORLEVEL% inside parenthesized blocks and no variable named RC here:
rem CMake would take RC for the resource compiler.)
:fetch
if exist "third_party\%~1\.git" goto :fetch_update
echo [INFO] Downloading %~1 ...
mkdir "third_party\%~1" 2>nul
pushd "third_party\%~1"
git init -q && git remote add origin %~2 && git fetch -q --depth 1 origin %~3 && git checkout -q FETCH_HEAD
rem (Not named RC: CMake reads an RC variable as the resource compiler.)
set "FETCH_ERR=%ERRORLEVEL%"
popd
if not "%FETCH_ERR%"=="0" (echo [ERROR] downloading %~1 failed. & rmdir /s /q "third_party\%~1" & exit /b 1)
set "FETCH_ERR="
exit /b 0

:fetch_update
set "FETCH_NOW="
for /f "usebackq delims=" %%h in (`git -C "third_party\%~1" rev-parse HEAD`) do set "FETCH_NOW=%%h"
if /i "%FETCH_NOW%"=="%~3" exit /b 0
echo [INFO] Updating %~1 to %~3 ...
set "FETCH_DEPTH="
for /f "usebackq delims=" %%s in (`git -C "third_party\%~1" rev-parse --is-shallow-repository`) do if "%%s"=="true" set "FETCH_DEPTH=--depth 1"
git -C "third_party\%~1" fetch -q %FETCH_DEPTH% origin %~3
if errorlevel 1 goto :fetch_update_failed
git -C "third_party\%~1" checkout -q FETCH_HEAD
if errorlevel 1 goto :fetch_update_failed
exit /b 0

:fetch_update_failed
echo [ERROR] updating %~1 failed ^(local changes?^). Delete third_party\%~1 and run again.
exit /b 1

:fail
echo.
echo [ERROR] Build failed.
exit /b 1

rem ---- a build folder configured with another Visual Studio ------------------------
rem CMake keeps the compiler it first found; with another Visual Studio's vcvars it
rem would mix that compiler with these headers (error STL1001 "Unexpected compiler
rem version"). Such a folder is configured afresh (a full rebuild).
:check_compiler
if not exist "%~1\CMakeCache.txt" exit /b 0
if not defined VCToolsInstallDir exit /b 0
set "VCT_VER="
for %%d in ("%VCToolsInstallDir:~0,-1%") do set "VCT_VER=%%~nxd"
if not defined VCT_VER exit /b 0
rem (The compiler, linker and librarian entries name the MSVC version used;
rem a folder with no MSVC entry is left alone.)
findstr /l /c:"MSVC" "%~1\CMakeCache.txt" >nul
if errorlevel 1 exit /b 0
findstr /l /c:"%VCT_VER%" "%~1\CMakeCache.txt" >nul
if not errorlevel 1 exit /b 0
echo [INFO] %~1 was set up with another Visual Studio; starting it afresh with MSVC %VCT_VER% (a full rebuild) ...
del /q "%~1\CMakeCache.txt"
if exist "%~1\CMakeFiles" rmdir /s /q "%~1\CMakeFiles"
exit /b 0
