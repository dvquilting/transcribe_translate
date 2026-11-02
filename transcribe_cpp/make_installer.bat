@echo off
rem ===========================================================================
rem  Builds the Windows installer: installer\output\Transcribe-<version>-setup.exe
rem
rem  Needs Inno Setup 6 or 7 (free):  winget install JRSoftware.InnoSetup
rem                         or  https://jrsoftware.org/isdl.php
rem  Runs build_static.bat first if dist\ hasn't been built yet.
rem
rem  Optional: put ffmpeg.exe (and its licence as ffmpeg-LICENSE.txt) in
rem  installer\redist\ to offer FFmpeg as an installer component.
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

if not exist dist\transcribe_multi.exe (
    echo [INFO] dist\ not built yet - running build_static.bat ...
    call build_static.bat || exit /b 1
)
if not exist dist\transcribe_multi_gui.exe (
    echo [ERROR] dist\transcribe_multi_gui.exe is missing - check the GUI part of build_static.bat.
    exit /b 1
)
if exist installer\redist\ffmpeg.exe (echo [INFO] Including FFmpeg from installer\redist) else (echo [INFO] No installer\redist\ffmpeg.exe - FFmpeg will not be offered)

echo [INFO] Compiling the installer (compressing about 1 GB takes a few minutes) ...
"%ISCC%" /Qp installer\transcribe.iss
if errorlevel 1 (echo [ERROR] Inno Setup failed. & exit /b 1)

echo.
for %%f in (installer\output\*.exe) do echo [SUCCESS] %%~ff  (%%~zf bytes)
