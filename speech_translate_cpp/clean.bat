@echo off
rem clean.bat      - remove this folder's build files and the build files of the two
rem                  other projects (their compiled CUDA kernels are kept)
rem clean.bat all  - the same, but remove the CUDA/library builds too (slow rebuild)
setlocal
cd /d "%~dp0"
for %%d in (build build-gui dist) do if exist %%d rmdir /s /q %%d
call ..\transcribe_cpp\clean.bat %1
call ..\translate_cpp\clean.bat %1
echo Now run build_all.bat.
