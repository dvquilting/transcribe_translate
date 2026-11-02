@echo off
rem ===========================================================================
rem  Builds the combined Windows installer:
rem    installer\output\SpeechTranslate-<version>-setup.exe
rem  (Speech Translate + Transcribe + Translate in one installation.)
rem
rem  Needs Inno Setup 6 or 7 (free):  winget install JRSoftware.InnoSetup
rem  Runs build_all.bat first if dist\ hasn't been built yet.
rem  Optional: ..\transcribe_cpp\installer\redist\ffmpeg.exe is offered as a component.
rem ===========================================================================
setlocal
cd /d "%~dp0"

set "ISCC="
for %%p in ("%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" "%ProgramFiles%\Inno Setup 6\ISCC.exe" "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" "%ProgramFiles(x86)%\Inno Setup 7\ISCC.exe" "%ProgramFiles%\Inno Setup 7\ISCC.exe" "%LOCALAPPDATA%\Programs\Inno Setup 7\ISCC.exe") do (
    if exist "%%~p" set "ISCC=%%~p"
)
if not defined ISCC for /f "delims=" %%i in ('where ISCC.exe 2^>nul') do set "ISCC=%%i"
if not defined ISCC (
    echo [ERROR] Inno Setup not found. Install it with:  winget install JRSoftware.InnoSetup
    exit /b 1
)

if not exist dist\speech_translate_gui.exe (
    echo [INFO] dist\ not built yet - running build_all.bat ...
    call build_all.bat || exit /b 1
)
for %%f in (transcribe_multi.exe transcribe_multi_gui.exe translate_text.exe translate_gui.exe) do (
    if not exist dist\%%f (echo [ERROR] dist\%%f is missing - run build_all.bat. & exit /b 1)
)

echo [INFO] Compiling the installer (compressing takes a few minutes) ...
"%ISCC%" /Qp installer\speech_translate.iss
if errorlevel 1 (echo [ERROR] Inno Setup failed. & exit /b 1)

echo.
for %%f in (installer\output\*.exe) do echo [SUCCESS] %%~ff  (%%~zf bytes)
