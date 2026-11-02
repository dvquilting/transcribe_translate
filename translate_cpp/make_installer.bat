@echo off
rem ===========================================================================
rem  Builds the Windows installer: installer\output\Translate-<version>-setup.exe
rem
rem  Needs Inno Setup 6 or 7 (free):  winget install JRSoftware.InnoSetup
rem  Runs build_static.bat first if dist\ hasn't been built yet.
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

if not exist dist\translate_text.exe (
    echo [INFO] dist\ not built yet - running build_static.bat ...
    call build_static.bat || exit /b 1
)
if not exist dist\translate_gui.exe (
    echo [ERROR] dist\translate_gui.exe is missing - check the GUI part of build_static.bat.
    exit /b 1
)

echo [INFO] Compiling the installer (compressing about 1 GB takes a few minutes) ...
"%ISCC%" /Qp installer\translate.iss
if errorlevel 1 (echo [ERROR] Inno Setup failed. & exit /b 1)

echo.
for %%f in (installer\output\*.exe) do echo [SUCCESS] %%~ff  (%%~zf bytes)
