@echo off
rem ===========================================================================
rem  Build the Android app (Transcribe ^& Translate) on Windows:
rem    dist\TranscribeTranslate.apk
rem  (build_apk.sh does the same on Linux and macOS.)
rem
rem  Needs: the Android SDK (Android Studio installs it), a JDK 17-24 (Android Studio's
rem  own, or any other), and git.
rem  The SDK packages below (platform, build tools, NDK, CMake) are installed with
rem  sdkmanager when missing. The sources it needs next to this folder:
rem    ..\transcribe_cpp\third_party\CrispASR  (fetched here at the pinned commit)
rem    ..\translate_cpp\third_party\miniz      (fetched here at the pinned commit)
rem
rem  Usage:  build_apk.bat              release APK (signed with the debug key, for sideloading)
rem          build_apk.bat compatible   for phones without i8mm (before Snapdragon 8 Gen 1)
rem          build_apk.bat install      also install it on the phone connected with USB debugging
rem          (both words can be given)
rem ===========================================================================
setlocal
cd /d "%~dp0"

rem Pinned versions: the same as ..\transcribe_cpp\build_static.bat (CrispASR) and
rem ..\translate_cpp\build_static.bat (miniz). Keep them in step.
set "CRISPASR_COMMIT=199de52d9068aeba366dd95d09a96bb45b2a7a14"
set "MINIZ_COMMIT=2948eebafc5b9f1c69bebed80333e403a09bd62d"

set "COMPATIBLE="
set "INSTALL="
for %%a in (%*) do (
    if /i "%%a"=="compatible" set "COMPATIBLE=1"
    if /i "%%a"=="install" set "INSTALL=1"
)

where git >nul 2>&1 || (echo [ERROR] git not found on PATH. & exit /b 1)

rem ---- Java 17 to 24 -------------------------------------------------------------
rem The Android Gradle plugin needs 17 or newer, and this project's Gradle (8.14)
rem runs on Java 24 at the most: on Java 25 it stops with just "25.0.x".
rem Looked for in this order: JAVA_HOME, Android Studio's own Java (jbr), then the
rem usual install folders of JDKs (Android, Microsoft, Adoptium, Oracle, Zulu).
set "JDK="
if defined JAVA_HOME call :try_java "%JAVA_HOME%"
call :try_java "%ProgramFiles%\Android\Android Studio\jbr"
call :try_java "%LOCALAPPDATA%\Programs\Android Studio\jbr"
for %%p in ("%ProgramFiles%\Android\openjdk" "%ProgramFiles%\Android\jdk" "%ProgramFiles%\Microsoft" "%ProgramFiles%\Eclipse Adoptium" "%ProgramFiles%\Java" "%ProgramFiles%\Zulu") do (
    if exist "%%~p\" for /d %%d in ("%%~p\*") do call :try_java "%%~d"
)
if not defined JDK (
    echo [ERROR] No Java 17 to 24 was found ^(JAVA_HOME, Android Studio's jbr folder and the usual
    echo         JDK folders were checked^). Install a JDK 21 ^(for example: winget install
    echo         Microsoft.OpenJDK.21^), or set JAVA_HOME to a JDK 17, 21 or 24 folder.
    exit /b 1
)
set "JAVA_HOME=%JDK%"
set "PATH=%JAVA_HOME%\bin;%PATH%"
echo [INFO] Java: %JAVA_HOME%

rem ---- sources -------------------------------------------------------------------
call :fetch ..\transcribe_cpp\third_party\CrispASR https://github.com/CrispStrobe/CrispASR %CRISPASR_COMMIT% || exit /b 1
call :fetch ..\translate_cpp\third_party\miniz https://github.com/richgel999/miniz %MINIZ_COMMIT% || exit /b 1

rem ---- Android SDK -----------------------------------------------------------------
if not defined ANDROID_HOME if defined ANDROID_SDK_ROOT set "ANDROID_HOME=%ANDROID_SDK_ROOT%"
if not defined ANDROID_HOME set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
if not exist "%ANDROID_HOME%\" (
    echo [ERROR] No Android SDK at "%ANDROID_HOME%". Install Android Studio, or set ANDROID_HOME.
    exit /b 1
)
echo [INFO] Android SDK: %ANDROID_HOME%
set "MISSING="
if not exist "%ANDROID_HOME%\platforms\android-36\" set "MISSING=%MISSING% "platforms;android-36""
if not exist "%ANDROID_HOME%\build-tools\36.0.0\" set "MISSING=%MISSING% "build-tools;36.0.0""
if not exist "%ANDROID_HOME%\ndk\28.2.13676358\" set "MISSING=%MISSING% "ndk;28.2.13676358""
if not exist "%ANDROID_HOME%\cmake\3.31.6\" set "MISSING=%MISSING% "cmake;3.31.6""
if not exist "%ANDROID_HOME%\platform-tools\" set "MISSING=%MISSING% "platform-tools""
if defined MISSING call :sdk_install || exit /b 1
rem local.properties: forward slashes (backslashes would need escaping there).
set "SDK_FWD=%ANDROID_HOME:\=/%"
> local.properties echo sdk.dir=%SDK_FWD%

rem ---- build -------------------------------------------------------------------------
set "ARGS="
if defined COMPATIBLE set "ARGS=-PcompatibleCpu=true"
echo [INFO] Building (the first build compiles CrispASR and takes a while) ...
call gradlew.bat --no-daemon %ARGS% :app:assembleRelease
if errorlevel 1 (echo [ERROR] The build failed. & exit /b 1)
if not exist dist mkdir dist
copy /y app\build\outputs\apk\release\app-release.apk dist\TranscribeTranslate.apk >nul
if errorlevel 1 (echo [ERROR] No APK was made. & exit /b 1)
for %%f in (dist\TranscribeTranslate.apk) do echo [SUCCESS] %%~ff  (%%~zf bytes)

if defined INSTALL (
    "%ANDROID_HOME%\platform-tools\adb.exe" install -r dist\TranscribeTranslate.apk
    if errorlevel 1 (echo [ERROR] Installing failed: is the phone connected, with USB debugging on? & exit /b 1)
)
exit /b 0

rem ---- use the Java in folder %1 if none was chosen yet and it is version 17-24 ----
rem (Asks java.exe itself: "openjdk version "21.0.8" ..." or "java version "1.8.0_302"".)
:try_java
if defined JDK exit /b 0
if not exist "%~1\bin\java.exe" exit /b 0
set "JV="
for /f "tokens=3" %%v in ('call "%~1\bin\java.exe" -version 2^>^&1 ^| findstr /i /c:" version "') do if not defined JV set "JV=%%~v"
if not defined JV exit /b 0
set "JMAJOR="
for /f "delims=." %%m in ("%JV%") do set "JMAJOR=%%m"
set /a JMAJOR=JMAJOR+0 2>nul
if %JMAJOR% GEQ 17 if %JMAJOR% LEQ 24 (
    set "JDK=%~1"
    exit /b 0
)
echo [INFO] Not using Java %JV% in %~1 ^(this build needs 17 to 24^)
exit /b 0

rem ---- missing SDK packages --------------------------------------------------------
rem (Each package name is in quotes: cmd splits arguments at ";" as well as at spaces.)
:sdk_install
set "SDKMANAGER="
for /d %%d in ("%ANDROID_HOME%\cmdline-tools\*") do if exist "%%d\bin\sdkmanager.bat" set "SDKMANAGER=%%d\bin\sdkmanager.bat"
if not defined SDKMANAGER (
    echo [ERROR] Missing SDK packages:%MISSING%
    echo         Install them in Android Studio ^(Tools ^> SDK Manager^; NDK and CMake are under
    echo         "SDK Tools", tick "Show Package Details"^), or install "Android SDK Command-line Tools".
    exit /b 1
)
echo [INFO] Installing%MISSING% ...
(for /l %%i in (1,1,30) do @echo y) | call "%SDKMANAGER%" --sdk_root="%ANDROID_HOME%" --licenses >nul 2>&1
call "%SDKMANAGER%" --sdk_root="%ANDROID_HOME%"%MISSING%
if errorlevel 1 (echo [ERROR] Installing the SDK packages failed. & exit /b 1)
for %%d in (platforms\android-36 build-tools\36.0.0 ndk\28.2.13676358 cmake\3.31.6 platform-tools) do (
    if not exist "%ANDROID_HOME%\%%d\" (
        echo [ERROR] %%d is still missing after the SDK manager ran. Install it in Android Studio:
        echo         Tools ^> SDK Manager ^> SDK Tools, tick "Show Package Details".
        exit /b 1
    )
)
exit /b 0

rem ---- clone URL at COMMIT into DIR, or move an existing clone to it ---------------
rem (No %ERRORLEVEL% inside parenthesized blocks.)
:fetch
if exist "%~1\.git" goto :fetch_update
echo [INFO] Downloading %~nx1 %~3 ...
mkdir "%~1" 2>nul
git -C "%~1" init -q
if errorlevel 1 goto :fetch_failed
git -C "%~1" remote add origin %~2
if errorlevel 1 goto :fetch_failed
git -C "%~1" fetch -q --depth 1 origin %~3
if errorlevel 1 goto :fetch_failed
git -C "%~1" checkout -q FETCH_HEAD
if errorlevel 1 goto :fetch_failed
git -C "%~1" submodule update -q --init --recursive --depth 1 2>nul
exit /b 0

:fetch_failed
echo [ERROR] Downloading %~2 failed.
rmdir /s /q "%~1" 2>nul
exit /b 1

:fetch_update
set "FETCH_NOW="
for /f "usebackq delims=" %%h in (`git -C "%~1" rev-parse HEAD`) do set "FETCH_NOW=%%h"
if /i "%FETCH_NOW%"=="%~3" exit /b 0
echo [INFO] Switching %~1 to the tested version %~3 ...
git -C "%~1" fetch -q --depth 1 origin %~3
if errorlevel 1 goto :fetch_update_failed
git -C "%~1" checkout -q -f FETCH_HEAD
if errorlevel 1 goto :fetch_update_failed
git -C "%~1" submodule update -q --init --recursive --depth 1 2>nul
exit /b 0

:fetch_update_failed
echo [ERROR] Updating %~1 failed. Delete that folder and run again.
exit /b 1
