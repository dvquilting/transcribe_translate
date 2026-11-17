@echo off
rem clean.bat      - remove the build folders and dist\ (the converted models in
rem                  build-models\ and the downloads are kept)
rem clean.bat all  - also the converted models, the build-time Python environment,
rem                  the downloaded weights and third_party\ (a full rebuild downloads
rem                  and converts everything again)
setlocal
cd /d "%~dp0"
for %%d in (build-cli build-gui dist) do if exist %%d rmdir /s /q %%d
if /i "%~1"=="all" (
    for %%d in (build-models third_party tools\.venv tools\model_repo test_data) do if exist %%d rmdir /s /q %%d
)
echo Now run build_all.bat.
