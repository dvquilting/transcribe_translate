@echo off
rem clean.bat      - delete this project's build outputs, but keep the compiled
rem                  CrispASR/CUDA libraries (those take a long time to rebuild)
rem clean.bat all  - delete every build folder, including CrispASR/CUDA
setlocal
cd /d "%~dp0"

if /i "%~1"=="all" (
    for %%d in (build build-gui build-static build-tests dist) do if exist %%d rmdir /s /q %%d
    echo Removed all build folders. The next build_static.bat recompiles everything.
    exit /b 0
)

for %%d in (build build-gui build-tests dist) do if exist %%d rmdir /s /q %%d
rem Only the transcribe_multi objects inside the static build, not CrispASR's.
if exist build-static\CMakeFiles\transcribe_multi.dir rmdir /s /q build-static\CMakeFiles\transcribe_multi.dir
if exist build-static\bin\transcribe_multi.exe del /q build-static\bin\transcribe_multi.exe
echo Removed the GUI and transcribe_multi build files (CrispASR/CUDA kept).
echo Now run build_static.bat (or build.bat).
