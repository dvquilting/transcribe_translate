@echo off
rem clean.bat      - delete the window's build and dist\, but keep the compiled
rem                  engine and translation library (CUDA kernels take long to rebuild)
rem clean.bat all  - delete every build folder (the next build_all.bat recompiles everything)
setlocal
cd /d "%~dp0"
rem The translation library's build folder (see build_all.bat).
if not defined TT_TRANSLATE_BUILD set "TT_TRANSLATE_BUILD=%SystemDrive%\tt-build\translate"
if /i "%~1"=="all" (
    for %%d in (build-gui build-engine build-translate build-dyn dist) do if exist %%d rmdir /s /q %%d
    if exist "%TT_TRANSLATE_BUILD%" rmdir /s /q "%TT_TRANSLATE_BUILD%"
    echo Removed all build folders.
    exit /b 0
)
for %%d in (build-gui build-dyn dist) do if exist %%d rmdir /s /q %%d
rem Only our own objects inside the engine and library builds, not CrispASR's or llama.cpp's.
if exist build-engine\CMakeFiles\tt_engine.dir rmdir /s /q build-engine\CMakeFiles\tt_engine.dir
if exist build-engine\bin\tt_engine.exe del /q build-engine\bin\tt_engine.exe
if exist "%TT_TRANSLATE_BUILD%\CMakeFiles\tt_translate.dir" rmdir /s /q "%TT_TRANSLATE_BUILD%\CMakeFiles\tt_translate.dir"
if exist "%TT_TRANSLATE_BUILD%\bin\tt_translate.dll" del /q "%TT_TRANSLATE_BUILD%\bin\tt_translate.dll"
echo Removed the window's build and our own engine objects (CrispASR, llama.cpp and CUDA kept).
echo Now run build_all.bat.
