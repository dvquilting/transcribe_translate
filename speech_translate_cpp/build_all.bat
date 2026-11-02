@echo off
rem ===========================================================================
rem  Builds all three programs and puts them in ONE folder, dist\, ready for the
rem  combined installer (make_installer.bat):
rem    transcribe_multi(.exe/_gui)  from ..\transcribe_cpp   (speech to text)
rem    translate_text / translate_gui from ..\translate_cpp  (text translation)
rem    speech_translate_gui         from this folder         (audio -> translated subtitles)
rem  They share one copy of the Qt DLLs and of the cuBLAS DLLs.
rem
rem  Usage:  build_all.bat          build everything (each part only recompiles what changed)
rem          build_all.bat quick    reuse the existing ..\transcribe_cpp\dist and
rem                                 ..\translate_cpp\dist, only rebuild this GUI
rem
rem  Needs everything the two other projects need (see their README.md files).
rem ===========================================================================
setlocal
set "QT_DIR=C:\Qt\6.11.0\mingw_64"
set "QT_TOOLS=C:\Qt\Tools"
rem Never let an RC variable reach CMake (it would be taken as the resource compiler).
set "RC="

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "TRANSCRIBE=%ROOT%\..\transcribe_cpp"
set "TRANSLATE=%ROOT%\..\translate_cpp"
cd /d "%ROOT%"
if not exist "%TRANSCRIBE%\build_static.bat" (echo [ERROR] %TRANSCRIBE% not found. & exit /b 1)
if not exist "%TRANSLATE%\build_static.bat" (echo [ERROR] %TRANSLATE% not found. & exit /b 1)

if /i not "%~1"=="quick" (
    echo [INFO] ===== 1/3  transcribe_cpp =====
    call "%TRANSCRIBE%\build_static.bat" || goto :fail
    echo [INFO] ===== 2/3  translate_cpp =====
    call "%TRANSLATE%\build_static.bat" || goto :fail
)
cd /d "%ROOT%"
if not exist "%TRANSCRIBE%\dist\transcribe_multi.exe" (echo [ERROR] %TRANSCRIBE%\dist is not built. & exit /b 1)
if not exist "%TRANSLATE%\dist\translate_text.exe" (echo [ERROR] %TRANSLATE%\dist is not built. & exit /b 1)

echo [INFO] ===== 3/3  speech_translate_gui =====
setlocal
set "PATH=%QT_TOOLS%\mingw1310_64\bin;%QT_TOOLS%\CMake_64\bin;%QT_TOOLS%\Ninja;%QT_DIR%\bin;%PATH%"
cmake -S . -B build-gui -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_CXX_COMPILER=g++ -DCMAKE_PREFIX_PATH="%QT_DIR%"
if errorlevel 1 goto :fail
cmake --build build-gui
if errorlevel 1 goto :fail
rem All test suites: this project's, the GUI regressions and those of the shared
rem code in ..\common. The GUI ones need no screen (CMakeLists.txt also sets this per test),
rem and take the offscreen plugin from Qt itself (windeployqt copies only the Windows one).
set "QT_QPA_PLATFORM=offscreen"
set "QT_PLUGIN_PATH=%QT_DIR%\plugins"
ctest --test-dir build-gui --output-on-failure
if errorlevel 1 goto :testfail
endlocal

echo [INFO] Assembling dist\ ...
if exist dist rmdir /s /q dist
mkdir dist
rem Order matters only for identical files (Qt and cuBLAS DLLs), which are the same.
xcopy /e /i /q /y "%TRANSCRIBE%\dist" dist >nul
xcopy /e /i /q /y "%TRANSLATE%\dist" dist >nul
xcopy /e /i /q /y build-gui\bin dist >nul
rem No test programs in dist\ (common_tests, common_qt_tests, common_models_tests,
rem speech_translate_cpp_gui_tests, speech_translate_tests, and any from the others).
del /q dist\*_tests.exe 2>nul
rem The programs use an ffmpeg.exe next to them before any on PATH (which may
rem lack the MP3/AAC encoders), so put the installer's copy in dist\ too.
if exist "%TRANSCRIBE%\installer\redist\ffmpeg.exe" (
    copy /y "%TRANSCRIBE%\installer\redist\ffmpeg.exe" dist\ >nul
    echo [INFO] Copied ffmpeg.exe into dist\
) else (
    echo [WARN] No %TRANSCRIBE%\installer\redist\ffmpeg.exe - ffmpeg from PATH will be used
)

echo.
echo [SUCCESS] %ROOT%\dist
echo   speech_translate_gui.exe   audio/video -^> translated subtitles
echo   transcribe_multi_gui.exe   speech to text
echo   translate_gui.exe          text and document translation
echo Next: make_installer.bat
exit /b 0

:testfail
echo [ERROR] Unit tests failed.
:fail
echo.
echo [ERROR] Build failed.
exit /b 1
