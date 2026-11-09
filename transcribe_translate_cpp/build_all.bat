@echo off
rem ===========================================================================
rem  Builds Transcribe & Translate into dist\, ready for make_installer.bat:
rem    transcribe_translate.exe   the window (Qt 6, MinGW) + Qt DLLs
rem    tt_engine.exe              the engine: CrispASR + ggml (CPU, Vulkan, CUDA)
rem                               linked in (MSVC)
rem    tt_translate.dll           translation: llama.cpp with its own ggml (MSVC)
rem    cublas*.dll                optional, for CUDA; without them Vulkan is used
rem
rem  Needs: Visual Studio 2022 (C++ workload), CUDA Toolkit 12.8+, Vulkan SDK,
rem         Git, and Qt 6 (MinGW kit). See README.md.
rem
rem  Usage:  build_all.bat          build everything (only what changed is recompiled)
rem          build_all.bat gui      only the window and the tests (engine kept)
rem ===========================================================================
setlocal

rem ---- paths and versions (edit if yours differ) ----------------------------
set "QT_DIR=C:\Qt\6.11.0\mingw_64"
set "QT_TOOLS=C:\Qt\Tools"
rem Tested versions. cmake\patch_crispasr.cmake and src\engine\crispasr_api.h
rem match this CrispASR; update them together.
set "CRISPASR_COMMIT=199de52d9068aeba366dd95d09a96bb45b2a7a14"
set "LLAMA_COMMIT=bd4eeaa047006cb1fe71999fbd11134b5836e167"
set "MINIZ_COMMIT=2948eebafc5b9f1c69bebed80333e403a09bd62d"
rem PDFium, for reading PDFs: a prebuilt release of
rem https://github.com/bblanchon/pdfium-binaries (chromium/NNNN).
set "PDFIUM_BUILD=8138"
rem CUDA kernels: 75-virtual = PTX for RTX 20xx and newer, plus native code for
rem RTX 30xx (86), 40xx (89) and 50xx (120). Fewer entries = faster build.
set "CUDA_ARCHS=75-virtual;86-real;89-real;120-real"
rem ---------------------------------------------------------------------------
rem Never let an RC variable reach CMake (it would be taken as the resource compiler).
set "RC="

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
cd /d "%ROOT%"
rem The translation library is built in a short folder outside the project:
rem llama.cpp builds a helper program (vulkan-shaders-gen) in folders nested so
rem deep that, under build-translate here, its paths pass Windows' 260-character
rem limit and the compiler fails ("Cannot open compiler generated file: ''",
rem "LNK1104 ... intermediate.manifest"). Set TT_TRANSLATE_BUILD to use another one.
if not defined TT_TRANSLATE_BUILD set "TT_TRANSLATE_BUILD=%SystemDrive%\tt-build\translate"
set "ONLY_GUI="
if /i "%~1"=="gui" set "ONLY_GUI=1"

where git >nul 2>&1 || (echo [ERROR] git not found on PATH. & exit /b 1)
if not defined ONLY_GUI (
    if not defined CUDA_PATH (
        echo [ERROR] CUDA Toolkit not found ^(CUDA_PATH is not set^). Install CUDA 12.8 or newer.
        exit /b 1
    )
    if not defined VULKAN_SDK (
        echo [ERROR] Vulkan SDK not found ^(VULKAN_SDK is not set^). Install it from https://vulkan.lunarg.com
        exit /b 1
    )
)

rem ---- 1. sources at the tested versions -------------------------------------
if not exist third_party mkdir third_party
call :fetch CrispASR https://github.com/CrispStrobe/CrispASR %CRISPASR_COMMIT% || exit /b 1
call :fetch llama.cpp https://github.com/ggml-org/llama.cpp %LLAMA_COMMIT% || exit /b 1
call :fetch miniz https://github.com/richgel999/miniz %MINIZ_COMMIT% || exit /b 1
call :pdfium || exit /b 1

rem ---- 2. the window and the tests, with Qt's MinGW ---------------------------
echo [INFO] ===== the window (Qt, MinGW) =====
setlocal
set "PATH=%QT_TOOLS%\mingw1310_64\bin;%QT_TOOLS%\CMake_64\bin;%QT_TOOLS%\Ninja;%QT_DIR%\bin;%PATH%"
cmake -S . -B build-gui -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_PREFIX_PATH="%QT_DIR%"
if errorlevel 1 goto :fail
cmake --build build-gui
if errorlevel 1 goto :fail
rem The tests need no screen, and take the offscreen plugin from Qt itself
rem (windeployqt copies only the Windows one).
set "QT_QPA_PLATFORM=offscreen"
set "QT_PLUGIN_PATH=%QT_DIR%\plugins"
ctest --test-dir build-gui --output-on-failure
if errorlevel 1 goto :testfail
endlocal
if defined ONLY_GUI goto :assemble

rem ---- 3. Visual Studio for the engine and the translation library -------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo [ERROR] Visual Studio not found. & exit /b 1)
rem Visual Studio 2022 first (CUDA 12 supports it); a newer one only without it.
set "VS_PATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -version [17.0^,18.0^) -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
if not defined VS_PATH for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
if not defined VS_PATH (echo [ERROR] Visual Studio C++ tools not found. & exit /b 1)
echo [INFO] Visual Studio: %VS_PATH%
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo [ERROR] vcvars64.bat failed. & exit /b 1)
call :check_compiler build-engine
call :check_compiler "%TT_TRANSLATE_BUILD%"
rem Fall back to Qt's CMake/Ninja if Visual Studio's aren't on PATH.
set "PATH=%PATH%;%QT_TOOLS%\CMake_64\bin;%QT_TOOLS%\Ninja"

rem ---- 4. tt_engine: CrispASR's own build with our program injected -------------
echo [INFO] ===== tt_engine (CrispASR + CUDA + Vulkan) =====
cmake -S third_party\CrispASR -B build-engine -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PROJECT_crispasr_INCLUDE="%ROOT%\cmake\engine_overlay.cmake" ^
      -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
      -DBUILD_SHARED_LIBS=OFF -DCRISPASR_BUILD_TESTS=OFF -DCRISPASR_BUILD_EXAMPLES=OFF -DCRISPASR_BUILD_SERVER=OFF ^
      -DGGML_NATIVE=OFF -DGGML_BACKEND_DL=OFF -DGGML_STATIC=ON ^
      -DGGML_VULKAN=ON -DGGML_CUDA=ON -DGGML_CUDA_NO_VMM=ON ^
      "-DCMAKE_CUDA_ARCHITECTURES=%CUDA_ARCHS%"
if errorlevel 1 goto :fail
echo [INFO] Compiling (the CUDA kernels take a long time on the first build) ...
cmake --build build-engine --target tt_engine
if errorlevel 1 goto :fail

rem ---- 5. tt_translate.dll: llama.cpp with its own ggml ---------------------------
echo [INFO] ===== tt_translate.dll (llama.cpp + CUDA + Vulkan) =====
cmake -S src\translate -B "%TT_TRANSLATE_BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
      -DGGML_NATIVE=OFF -DGGML_BACKEND_DL=OFF -DGGML_STATIC=ON ^
      -DGGML_VULKAN=ON -DGGML_CUDA=ON -DGGML_CUDA_NO_VMM=ON ^
      "-DCMAKE_CUDA_ARCHITECTURES=%CUDA_ARCHS%"
if errorlevel 1 goto :fail
cmake --build "%TT_TRANSLATE_BUILD%" --target tt_translate
if errorlevel 1 goto :fail

rem ---- 6. dist\ ----------------------------------------------------------------
:assemble
if not exist build-engine\bin\tt_engine.exe (echo [ERROR] build-engine is not built - run build_all.bat without "gui". & exit /b 1)
if not exist "%TT_TRANSLATE_BUILD%\bin\tt_translate.dll" (echo [ERROR] %TT_TRANSLATE_BUILD% is not built - run build_all.bat without "gui". & exit /b 1)
echo [INFO] Assembling dist\ ...
if exist dist rmdir /s /q dist
mkdir dist
xcopy /e /i /q /y build-gui\bin dist >nul
rem No test programs (and no stand-in engine) in dist\.
if exist dist\tests rmdir /s /q dist\tests
copy /y build-engine\bin\tt_engine.exe dist\ >nul
copy /y "%TT_TRANSLATE_BUILD%\bin\tt_translate.dll" dist\ >nul
copy /y build-engine\bin\cublas*.dll dist\ >nul 2>&1
rem PDFium: the window reads PDFs with it (it starts without it).
if exist third_party\pdfium\bin\pdfium.dll (copy /y third_party\pdfium\bin\pdfium.dll dist\ >nul) else (echo [WARN] No PDFium - the program will not read PDFs)
rem The programs use an ffmpeg.exe next to them before any on PATH (which may
rem lack the MP3/AAC encoders).
set "FFMPEG=installer\redist\ffmpeg.exe"
if not exist "%FFMPEG%" set "FFMPEG=..\transcribe_cpp\installer\redist\ffmpeg.exe"
if exist "%FFMPEG%" (
    copy /y "%FFMPEG%" dist\ >nul
    echo [INFO] Copied ffmpeg.exe into dist\
) else (
    echo [WARN] No installer\redist\ffmpeg.exe - ffmpeg from PATH will be used
)
mkdir dist\licenses
copy /y installer\license.txt dist\licenses\TranscribeTranslate-LICENSES.txt >nul
copy /y third_party\CrispASR\LICENSE dist\licenses\CrispASR-LICENSE.txt >nul
copy /y third_party\CrispASR\THIRD_PARTY_NOTICES.txt dist\licenses\CrispASR-THIRD_PARTY_NOTICES.txt >nul 2>&1
copy /y third_party\llama.cpp\LICENSE dist\licenses\llama.cpp-LICENSE.txt >nul
copy /y third_party\miniz\LICENSE dist\licenses\miniz-LICENSE.txt >nul
copy /y third_party\pdfium\LICENSE dist\licenses\pdfium-binaries-LICENSE.txt >nul 2>&1
if exist third_party\pdfium\licenses xcopy /e /i /q /y third_party\pdfium\licenses dist\licenses\PDFium >nul
copy /y "%QT_DIR%\..\..\Licenses\*" dist\licenses\ >nul 2>&1
for %%f in ("%FFMPEG%") do if exist "%%~dpfffmpeg-LICENSE.txt" copy /y "%%~dpfffmpeg-LICENSE.txt" dist\licenses\ >nul

rem The engine finds its translation library (a quick check that both load).
dist\tt_engine.exe translate --list-models >nul
if errorlevel 1 (echo [ERROR] dist\tt_engine.exe could not use tt_translate.dll. & exit /b 1)

echo.
echo [SUCCESS] %ROOT%\dist
echo   transcribe_translate.exe   the program (Transcribe, Translate, Speech translation, History)
echo   tt_engine.exe              its engine (CPU + Vulkan + CUDA), with tt_translate.dll
echo Next: make_installer.bat
exit /b 0

rem ---- PDFium (prebuilt) into third_party\pdfium ------------------------------------
rem Windows 10 (1803) and newer come with curl and tar.
:pdfium
if not exist "third_party\pdfium\VERSION" goto :pdfium_get
findstr /c:"BUILD=%PDFIUM_BUILD%" "third_party\pdfium\VERSION" >nul
if not errorlevel 1 if exist "third_party\pdfium\bin\pdfium.dll" exit /b 0
:pdfium_get
echo [INFO] Downloading PDFium %PDFIUM_BUILD% (for reading PDFs) ...
where curl >nul 2>&1 || (echo [ERROR] curl not found. & exit /b 1)
if exist third_party\pdfium rmdir /s /q third_party\pdfium
mkdir third_party\pdfium
curl -fsSL -o third_party\pdfium-win-x64.tgz "https://github.com/bblanchon/pdfium-binaries/releases/download/chromium/%PDFIUM_BUILD%/pdfium-win-x64.tgz"
if errorlevel 1 goto :pdfium_failed
tar -xzf third_party\pdfium-win-x64.tgz -C third_party\pdfium
if errorlevel 1 goto :pdfium_failed
del /q third_party\pdfium-win-x64.tgz
if not exist third_party\pdfium\bin\pdfium.dll goto :pdfium_failed
exit /b 0

:pdfium_failed
echo [ERROR] Downloading PDFium failed. Check the network, or unpack pdfium-win-x64.tgz
echo         from https://github.com/bblanchon/pdfium-binaries/releases into third_party\pdfium.
if exist third_party\pdfium-win-x64.tgz del /q third_party\pdfium-win-x64.tgz
if exist third_party\pdfium rmdir /s /q third_party\pdfium
exit /b 1

rem ---- clone REPO at COMMIT into third_party\NAME (with its submodules) ---------
rem (No %ERRORLEVEL% inside parenthesized blocks and no variable named RC here:
rem CMake would take RC for the resource compiler.)
:fetch
if exist "third_party\%~1\.git" goto :fetch_update
echo [INFO] Downloading %~1 %~3 ...
mkdir "third_party\%~1" 2>nul
git -C "third_party\%~1" init -q
if errorlevel 1 goto :fetch_failed
git -C "third_party\%~1" remote add origin %~2
if errorlevel 1 goto :fetch_failed
git -C "third_party\%~1" fetch -q --depth 1 origin %~3
if errorlevel 1 goto :fetch_failed
git -C "third_party\%~1" checkout -q FETCH_HEAD
if errorlevel 1 goto :fetch_failed
call :submodules "%~1"
if errorlevel 1 goto :fetch_failed
exit /b 0

:fetch_failed
echo [ERROR] Downloading %~1 failed.
rmdir /s /q "third_party\%~1" 2>nul
exit /b 1

:fetch_update
set "FETCH_NOW="
for /f "usebackq delims=" %%h in (`git -C "third_party\%~1" rev-parse HEAD`) do set "FETCH_NOW=%%h"
if /i "%FETCH_NOW%"=="%~3" exit /b 0
echo [INFO] Switching %~1 to the tested version %~3 ...
set "FETCH_DEPTH="
for /f "usebackq delims=" %%s in (`git -C "third_party\%~1" rev-parse --is-shallow-repository`) do if "%%s"=="true" set "FETCH_DEPTH=--depth 1"
git -C "third_party\%~1" fetch -q %FETCH_DEPTH% origin %~3
if errorlevel 1 goto :fetch_update_failed
rem -f: CrispASR's files carry our source patches; they are re-applied at configure time.
git -C "third_party\%~1" checkout -q -f FETCH_HEAD
if errorlevel 1 goto :fetch_update_failed
call :submodules "%~1"
if errorlevel 1 goto :fetch_update_failed
exit /b 0

:fetch_update_failed
echo [ERROR] Updating %~1 failed. Delete third_party\%~1 and run again.
exit /b 1

:submodules
if not exist "third_party\%~1\.gitmodules" exit /b 0
git -C "third_party\%~1" submodule update -q --init --recursive --depth 1 --force 2>nul
if not errorlevel 1 exit /b 0
git -C "third_party\%~1" submodule update -q --init --recursive --force
exit /b

:testfail
echo [ERROR] Unit tests failed.
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
findstr /l /c:"MSVC" "%~1\CMakeCache.txt" >nul
if errorlevel 1 exit /b 0
findstr /l /c:"%VCT_VER%" "%~1\CMakeCache.txt" >nul
if not errorlevel 1 exit /b 0
echo [INFO] %~1 was set up with another Visual Studio; starting it afresh with MSVC %VCT_VER% (a full rebuild) ...
del /q "%~1\CMakeCache.txt"
if exist "%~1\CMakeFiles" rmdir /s /q "%~1\CMakeFiles"
exit /b 0
