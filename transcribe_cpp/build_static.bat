@echo off
rem ===========================================================================
rem  Shareable build: CrispASR + ggml (CPU, Vulkan, CUDA) linked statically into
rem  transcribe_multi.exe, plus the Qt GUI. Result: the dist\ folder.
rem
rem  Needs: Visual Studio 2022 (C++ workload), CUDA Toolkit 12.8+, Vulkan SDK,
rem         Git, and Qt 6 (MinGW kit) for the GUI.  See README.md.
rem  Run from this folder in a normal command prompt:   build_static.bat
rem ===========================================================================
setlocal

rem ---- paths (edit if yours differ) -----------------------------------------
set "QT_DIR=C:\Qt\6.11.0\mingw_64"
set "QT_TOOLS=C:\Qt\Tools"
set "CRISPASR_SRC=%~dp0third_party\CrispASR"
rem CrispASR version this program is tested with. cmake\patch_crispasr.cmake and
rem src\crispasr_api.h match it; update all three together.
set "CRISPASR_COMMIT=199de52d9068aeba366dd95d09a96bb45b2a7a14"
set "CRISPASR_URL=https://github.com/CrispStrobe/CrispASR"
rem CUDA kernels: 75-virtual = PTX for RTX 20xx and newer, plus native code for
rem RTX 30xx (86), 40xx (89) and 50xx (120). Fewer entries = faster build.
set "CUDA_ARCHS=75-virtual;86-real;89-real;120-real"
rem ---------------------------------------------------------------------------

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

rem ---- 1. CrispASR source ---------------------------------------------------
rem Paths echoed inside an if-block are quoted: an unquoted closing parenthesis
rem in the path, e.g. "Program Files (x86)", would end the block early.
rem Kept at CRISPASR_COMMIT. Its files carry our source patches, so switching
rem commits discards local changes there: they are re-applied at configure time.
call :crispasr
if errorlevel 1 exit /b 1

rem ---- 2. GUI with Qt's MinGW (separate process, so a different compiler is fine)
echo [INFO] Building the GUI with MinGW ...
setlocal
set "PATH=%QT_TOOLS%\mingw1310_64\bin;%QT_TOOLS%\CMake_64\bin;%QT_TOOLS%\Ninja;%QT_DIR%\bin;%PATH%"
cmake -S . -B build-gui -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ ^
      -DCMAKE_PREFIX_PATH="%QT_DIR%" -DTRANSCRIBE_BUILD_CLI=OFF
if errorlevel 1 goto :fail
cmake --build build-gui
if errorlevel 1 goto :fail
endlocal

rem ---- 3. Static command-line program with MSVC ----------------------------
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
rem Fall back to Qt's CMake/Ninja if Visual Studio's aren't on PATH.
set "PATH=%PATH%;%QT_TOOLS%\CMake_64\bin;%QT_TOOLS%\Ninja"

echo [INFO] Configuring the static build (CrispASR + CUDA + Vulkan) ...
cmake -S "%CRISPASR_SRC%" -B build-static -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PROJECT_crispasr_INCLUDE="%ROOT%\cmake\crispasr_overlay.cmake" ^
      -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
      -DBUILD_SHARED_LIBS=OFF -DCRISPASR_BUILD_TESTS=OFF -DCRISPASR_BUILD_EXAMPLES=OFF -DCRISPASR_BUILD_SERVER=OFF ^
      -DGGML_NATIVE=OFF -DGGML_BACKEND_DL=OFF -DGGML_STATIC=ON ^
      -DGGML_VULKAN=ON -DGGML_CUDA=ON -DGGML_CUDA_NO_VMM=ON ^
      "-DCMAKE_CUDA_ARCHITECTURES=%CUDA_ARCHS%"
if errorlevel 1 goto :fail

echo [INFO] Compiling (the CUDA kernels take a long time on the first build) ...
cmake --build build-static --target transcribe_multi
if errorlevel 1 goto :fail

rem ---- 4. Assemble dist\ ----------------------------------------------------
echo [INFO] Assembling dist\ ...
if exist dist rmdir /s /q dist
mkdir dist
xcopy /e /i /q /y build-gui\bin dist >nul
copy /y build-static\bin\transcribe_multi.exe dist\ >nul
copy /y build-static\bin\cublas*.dll dist\ >nul 2>&1
mkdir dist\licenses
copy /y "%CRISPASR_SRC%\LICENSE" dist\licenses\CrispASR-LICENSE.txt >nul
copy /y "%CRISPASR_SRC%\THIRD_PARTY_NOTICES.txt" dist\licenses\CrispASR-THIRD_PARTY_NOTICES.txt >nul
copy /y "%QT_DIR%\..\..\Licenses\*" dist\licenses\ >nul 2>&1

echo.
echo [SUCCESS] Shareable program folder: %ROOT%\dist
echo   transcribe_multi.exe     self-contained (CPU + Vulkan + CUDA)
echo   cublas64_*.dll ...       only needed for CUDA; without them it uses Vulkan
echo   transcribe_multi_gui.exe + Qt DLLs
exit /b 0

rem ---- third_party\CrispASR at CRISPASR_COMMIT ---------------------------------
rem (No %ERRORLEVEL% inside parenthesized blocks and no variable named RC here:
rem CMake would take RC for the resource compiler.)
:crispasr
if exist "%CRISPASR_SRC%\.git" goto :crispasr_check
echo [INFO] Downloading CrispASR %CRISPASR_COMMIT:~0,12% ...
mkdir "%CRISPASR_SRC%" 2>nul
git -C "%CRISPASR_SRC%" init -q
if errorlevel 1 goto :crispasr_download_failed
git -C "%CRISPASR_SRC%" remote add origin %CRISPASR_URL%
if errorlevel 1 goto :crispasr_download_failed
git -C "%CRISPASR_SRC%" fetch -q --depth 1 origin %CRISPASR_COMMIT%
if errorlevel 1 goto :crispasr_download_failed
git -C "%CRISPASR_SRC%" checkout -q FETCH_HEAD
if errorlevel 1 goto :crispasr_download_failed
call :crispasr_submodules
if errorlevel 1 goto :crispasr_download_failed
exit /b 0

:crispasr_download_failed
echo [ERROR] Downloading CrispASR failed.
rmdir /s /q "%CRISPASR_SRC%" 2>nul
exit /b 1

:crispasr_check
set "CRISPASR_NOW="
for /f "usebackq delims=" %%h in (`git -C "%CRISPASR_SRC%" rev-parse HEAD`) do set "CRISPASR_NOW=%%h"
if /i "%CRISPASR_NOW%"=="%CRISPASR_COMMIT%" exit /b 0
echo [INFO] Switching CrispASR to the tested version %CRISPASR_COMMIT:~0,12% ...
set "CRISPASR_DEPTH="
for /f "usebackq delims=" %%s in (`git -C "%CRISPASR_SRC%" rev-parse --is-shallow-repository`) do if "%%s"=="true" set "CRISPASR_DEPTH=--depth 1"
git -C "%CRISPASR_SRC%" fetch -q %CRISPASR_DEPTH% origin %CRISPASR_COMMIT%
if errorlevel 1 goto :crispasr_update_failed
git -C "%CRISPASR_SRC%" checkout -q -f FETCH_HEAD
if errorlevel 1 goto :crispasr_update_failed
call :crispasr_submodules
if errorlevel 1 goto :crispasr_update_failed
exit /b 0

:crispasr_update_failed
echo [ERROR] Updating CrispASR failed. Delete third_party\CrispASR and run again.
exit /b 1

:crispasr_submodules
git -C "%CRISPASR_SRC%" submodule update -q --init --recursive --depth 1 --force 2>nul
if not errorlevel 1 exit /b 0
git -C "%CRISPASR_SRC%" submodule update -q --init --recursive --force
exit /b

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
