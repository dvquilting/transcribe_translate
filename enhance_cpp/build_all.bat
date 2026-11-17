@echo off
rem ===========================================================================
rem  Enhance Audio: build everything into dist\
rem
rem    1. downloads Resemble Enhance (model code), ONNX Runtime with DirectML,
rem       DirectML and pocketfft into third_party\ (pinned versions)
rem    2. converts the Resemble Enhance weights (713 MB, downloaded once) into the
rem       ONNX models, with a build-time Python environment in tools\.venv
rem    3. builds the GUI with Qt's MinGW and enhance_audio.exe with Visual Studio
rem    4. runs the tests and assembles dist\
rem
rem  Needs: Visual Studio 2022 or newer (C++ workload), Qt 6 (MinGW kit, with
rem         Qt Multimedia), Git, Python 3.10+ (build time only), internet once.
rem  Run from this folder in a normal command prompt:   build_all.bat
rem ===========================================================================
setlocal

rem ---- paths (edit if yours differ) -----------------------------------------
set "QT_DIR=C:\Qt\6.11.0\mingw_64"
set "QT_TOOLS=C:\Qt\Tools"
if not defined PYTHON set "PYTHON=python"
rem ---- pinned versions ------------------------------------------------------
set "RESEMBLE_URL=https://github.com/resemble-ai/resemble-enhance"
set "RESEMBLE_COMMIT=8e978149bfe8abab3eb77d965d579a111afdb0ff"
set "ORT_VERSION=1.24.4"
set "DML_VERSION=1.15.4"
set "POCKETFFT_COMMIT=4d865cba24359b05b37da4e75aee24498c3b78e7"
rem ---------------------------------------------------------------------------

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
cd /d "%ROOT%"
set "TP=%ROOT%\third_party"
where git >nul 2>&1 || (echo [ERROR] git not found on PATH. & exit /b 1)
where curl >nul 2>&1 || (echo [ERROR] curl not found ^(it comes with Windows 10 and newer^). & exit /b 1)
if not exist "%TP%" mkdir "%TP%"

rem ---- 1. third-party code ---------------------------------------------------
call :resemble
if errorlevel 1 exit /b 1
call :nuget microsoft.ml.onnxruntime.directml %ORT_VERSION% onnxruntime-dml
if errorlevel 1 exit /b 1
call :nuget microsoft.ai.directml %DML_VERSION% directml
if errorlevel 1 exit /b 1
if not exist "%TP%\pocketfft_hdronly.hpp" (
    echo [INFO] Downloading pocketfft ...
    curl -sfL -o "%TP%\pocketfft_hdronly.hpp" "https://raw.githubusercontent.com/mreineck/pocketfft/%POCKETFFT_COMMIT%/pocketfft_hdronly.h"
    if errorlevel 1 (echo [ERROR] Downloading pocketfft failed. & del "%TP%\pocketfft_hdronly.hpp" 2>nul & exit /b 1)
)

rem ---- 2. the ONNX models ------------------------------------------------------
set "MODELS=%ROOT%\build-models"
set "VENV=%ROOT%\tools\.venv"
set "NEED_MODELS="
for %%f in (filters.bin denoiser.onnx encoder.onnx velocity.onnx decoder.onnx vocoder.onnx) do if not exist "%MODELS%\%%f" set "NEED_MODELS=1"
if not exist "%ROOT%\test_data\dsp\filters.bin" set "NEED_MODELS=1"
if defined NEED_MODELS (
    call :venv
    if errorlevel 1 exit /b 1
    echo [INFO] Converting the Resemble Enhance weights to ONNX ^(downloads 713 MB the first time^) ...
    "%VENV%\Scripts\python.exe" -W ignore "%ROOT%\tools\export_onnx.py" "%MODELS%"
    if errorlevel 1 (echo [ERROR] The model conversion failed. & exit /b 1)
    echo [INFO] Writing the test data ...
    "%VENV%\Scripts\python.exe" -W ignore "%ROOT%\tools\make_test_data.py"
    if errorlevel 1 (echo [ERROR] Writing the test data failed. & exit /b 1)
)

rem ---- 3a. GUI with Qt's MinGW --------------------------------------------------
echo [INFO] Building the GUI with MinGW ...
setlocal
set "PATH=%QT_TOOLS%\mingw1310_64\bin;%QT_TOOLS%\CMake_64\bin;%QT_TOOLS%\Ninja;%QT_DIR%\bin;%PATH%"
cmake -S . -B build-gui -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ ^
      -DCMAKE_PREFIX_PATH="%QT_DIR%" -DENHANCE_BUILD_CLI=OFF
if errorlevel 1 goto :fail
cmake --build build-gui
if errorlevel 1 goto :fail
endlocal

rem ---- 3b. command-line program with Visual Studio ---------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo [ERROR] Visual Studio not found. & exit /b 1)
set "VS_PATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
if not defined VS_PATH (echo [ERROR] Visual Studio C++ tools not found. & exit /b 1)
echo [INFO] Visual Studio: %VS_PATH%
rem (vcvars64 looks for vswhere on PATH)
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo [ERROR] vcvars64.bat failed. & exit /b 1)
set "PATH=%PATH%;%QT_TOOLS%\CMake_64\bin;%QT_TOOLS%\Ninja"
echo [INFO] Building enhance_audio.exe ...
cmake -S . -B build-cli -G Ninja -DCMAKE_BUILD_TYPE=Release -DENHANCE_BUILD_GUI=OFF
if errorlevel 1 goto :fail
cmake --build build-cli
if errorlevel 1 goto :fail

rem ---- 4. tests and dist\ ------------------------------------------------------
echo [INFO] Running the tests ...
ctest --test-dir build-cli --output-on-failure
if errorlevel 1 goto :fail

echo [INFO] Assembling dist\ ...
if exist dist rmdir /s /q dist
mkdir dist
xcopy /e /i /q /y build-gui\bin dist >nul
for %%f in (enhance_audio.exe onnxruntime.dll onnxruntime_providers_shared.dll DirectML.dll) do copy /y "build-cli\bin\%%f" dist\ >nul
mkdir dist\models
for %%f in (filters.bin denoiser.onnx encoder.onnx velocity.onnx decoder.onnx vocoder.onnx) do copy /y "%MODELS%\%%f" dist\models\ >nul
mkdir dist\licenses
copy /y "%TP%\resemble-enhance\LICENSE" dist\licenses\ResembleEnhance-LICENSE.txt >nul
copy /y "%TP%\onnxruntime-dml\LICENSE" dist\licenses\OnnxRuntime-LICENSE.txt >nul
copy /y "%TP%\onnxruntime-dml\ThirdPartyNotices.txt" dist\licenses\OnnxRuntime-ThirdPartyNotices.txt >nul
copy /y "%TP%\directml\LICENSE.txt" dist\licenses\DirectML-LICENSE.txt >nul
copy /y "%QT_DIR%\..\..\Licenses\*" dist\licenses\ >nul 2>&1
if exist "%ROOT%\ffmpeg.exe" copy /y "%ROOT%\ffmpeg.exe" dist\ >nul

echo.
echo [SUCCESS] Program folder: %ROOT%\dist
echo   enhance_audio_gui.exe    the window
echo   enhance_audio.exe        the command-line program (enhance_audio --help)
echo   models\                  the converted Resemble Enhance models (1.4 GB)
echo   ffmpeg must be on PATH or ffmpeg.exe next to the programs (put one in this folder
echo   to have it copied into dist\).
exit /b 0

rem ---- third_party\resemble-enhance at RESEMBLE_COMMIT ---------------------------
:resemble
set "RE_SRC=%TP%\resemble-enhance"
if exist "%RE_SRC%\.git" goto :resemble_check
echo [INFO] Downloading Resemble Enhance %RESEMBLE_COMMIT:~0,12% ...
mkdir "%RE_SRC%" 2>nul
git -C "%RE_SRC%" init -q
if errorlevel 1 goto :resemble_failed
git -C "%RE_SRC%" remote add origin %RESEMBLE_URL%
if errorlevel 1 goto :resemble_failed
git -C "%RE_SRC%" fetch -q --depth 1 origin %RESEMBLE_COMMIT%
if errorlevel 1 goto :resemble_failed
git -C "%RE_SRC%" checkout -q FETCH_HEAD
if errorlevel 1 goto :resemble_failed
exit /b 0
:resemble_check
set "RE_NOW="
for /f "usebackq delims=" %%h in (`git -C "%RE_SRC%" rev-parse HEAD`) do set "RE_NOW=%%h"
if /i "%RE_NOW%"=="%RESEMBLE_COMMIT%" exit /b 0
echo [INFO] Switching Resemble Enhance to the tested version %RESEMBLE_COMMIT:~0,12% ...
git -C "%RE_SRC%" fetch -q --depth 1 origin %RESEMBLE_COMMIT%
if errorlevel 1 goto :resemble_failed
git -C "%RE_SRC%" checkout -q -f FETCH_HEAD
if errorlevel 1 goto :resemble_failed
exit /b 0
:resemble_failed
echo [ERROR] Downloading Resemble Enhance failed.
exit /b 1

rem ---- a NuGet package, unpacked into third_party\NAME (%1 id, %2 version, %3 NAME) ----
:nuget
if exist "%TP%\%~3\.version-%~2" exit /b 0
echo [INFO] Downloading %~1 %~2 ...
if exist "%TP%\%~3" rmdir /s /q "%TP%\%~3"
mkdir "%TP%\%~3"
curl -sfL -o "%TP%\%~3.zip" "https://api.nuget.org/v3-flatcontainer/%~1/%~2/%~1.%~2.nupkg"
if errorlevel 1 (echo [ERROR] Downloading %~1 failed. & exit /b 1)
rem (Windows' own tar: a GNU tar from Git on PATH reads "C:" as a remote host)
"%SystemRoot%\System32\tar.exe" -xf "%TP%\%~3.zip" -C "%TP%\%~3"
if errorlevel 1 (echo [ERROR] Unpacking %~1 failed. & exit /b 1)
del "%TP%\%~3.zip"
echo %~2> "%TP%\%~3\.version-%~2"
exit /b 0

rem ---- tools\.venv: the build-time Python (PyTorch CPU, ONNX) ------------------------
:venv
if exist "%VENV%\.ready" exit /b 0
where %PYTHON% >nul 2>&1 || (echo [ERROR] Python not found ^(set PYTHON to python.exe^). & exit /b 1)
echo [INFO] Setting up the build-time Python environment in tools\.venv ^(about 1 GB^) ...
if not exist "%VENV%\Scripts\python.exe" %PYTHON% -m venv "%VENV%"
if errorlevel 1 (echo [ERROR] Creating tools\.venv failed. & exit /b 1)
"%VENV%\Scripts\python.exe" -m pip install -q --upgrade pip
"%VENV%\Scripts\python.exe" -m pip install -q -r "%ROOT%\tools\requirements-export.txt"
if errorlevel 1 (echo [ERROR] Installing the Python packages failed. & exit /b 1)
echo ready> "%VENV%\.ready"
exit /b 0

:fail
echo.
echo [ERROR] Build failed.
exit /b 1
