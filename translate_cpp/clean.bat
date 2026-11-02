@echo off
rem clean.bat      - delete this project's build outputs, but keep the compiled
rem                  llama.cpp/CUDA libraries (those take a long time to rebuild)
rem clean.bat all  - delete every build folder, including llama.cpp/CUDA
setlocal
cd /d "%~dp0"

if /i "%~1"=="all" (
    for %%d in (build build-gui build-static build-tests dist) do if exist %%d rmdir /s /q %%d
    echo Removed all build folders. The next build_static.bat recompiles everything.
    exit /b 0
)

for %%d in (build build-gui build-tests dist) do if exist %%d rmdir /s /q %%d
rem Only translate_text's own objects inside the static build, not llama.cpp's.
for %%d in (translate_text.dir translate_tests.dir tl_miniz.dir) do if exist build-static\CMakeFiles\%%d rmdir /s /q build-static\CMakeFiles\%%d
if exist build-static\bin\translate_text.exe del /q build-static\bin\translate_text.exe
echo Removed the GUI and translate_text build files (llama.cpp/CUDA kept).
echo Now run build_static.bat.
