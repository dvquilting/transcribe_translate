# Transcribe & Translate

Speech to text, translation and speech translation in **one program**, running
entirely on your own computer. It does what `transcribe_cpp`, `translate_cpp`
and `speech_translate_cpp` do, in one window with four tabs:

| Tab | What it does |
|---|---|
| **Transcribe** | audio/video → .txt, .srt, Word, PDF; optional AI post-processing (smart paragraphs, corrected copy, summary). **Translate...** hands the transcript to the Translate tab. |
| **Translate** | typed text, or .txt / .md / .srt / .docx files, keeping the layout |
| **Speech translation** | audio/video → translated subtitles (original, bilingual, text), several speakers, and an optional dub in the speaker's (cloned) voice |
| **History** | every job with the files it saved: open the file or its folder, or use the same input again (kept in `history.json` in the program's data folder) |

**Help** (F1), **Model files** and **Quit** sit next to the tabs and serve all
of them. One job runs at a time (each model has the graphics card to itself);
the other tabs can be set up meanwhile.

The models are **shared** with the three separate programs: speech and voice
models in CrispASR's cache (`%USERPROFILE%\.cache\crispasr`, `~/.cache/crispasr`),
translation models in `%LOCALAPPDATA%\Translate\models` (`~/.cache/translate` on
Linux). Settings are stored under `DavidV/TranscribeTranslate`, one group per tab.

This project has its own copy of all its code; it does not use `../common` or
the other projects' folders, which are left as they are.

## How it is put together

```
transcribe_translate(.exe)   the window (Qt 6). Runs the engine as a child process,
                             one command per job, and reads its output lines
tt_engine(.exe)              the engine: one program with four commands
    transcribe INPUT ...     speech to text            (CrispASR, linked in)
    speakers MEDIA ...       who speaks when           (CrispASR)
    dub MEDIA ...            speak translated subtitles (CrispASR voices)
    translate INPUT -t LANG  text/document translation (loads tt_translate)
tt_translate.dll / libtt_translate.so
                             llama.cpp (a newer one than CrispASR's built-in
                             copy) and the translator
```

Translation uses a newer llama.cpp than the one inside CrispASR, so the
process holds two copies of ggml when it translates. They are kept apart:
the library is built with hidden symbols and exports only `tt_translate_main`;
on Linux its static libraries' symbols stay local (`--exclude-libs,ALL`,
`-Bsymbolic`) and the engine loads it with `RTLD_LOCAL`; on Windows a DLL has
its own symbol table anyway. `build_all.sh` checks that the library exports
no `ggml_`/`llama_` symbols. The library is loaded only by `tt_engine translate`.

```
src/gui/        the window: mainwindow (tabs, Help, Model files, Quit),
                transcribe_page, translate_page, speech_page, history_page,
                jobgate (one job at a time, the engine's path)
src/engine/     tt_engine: main.cpp (the commands) + the speech/dub engine
src/translate/  the translation library (its own CMakeLists.txt)
src/common/     plain C++ shared by all parts (subtitles, files, Word writer,
                model tables, speakers)
src/qt/         Qt helpers (child processes, the Model files window, Help window)
cmake/          engine_overlay.cmake / engine.cmake (the engine is built inside
                CrispASR's own CMake build), patch_crispasr.cmake, sources.cmake
tests/          unit tests, and the window's test with a stand-in engine
installer/      Inno Setup script, licence and read-me
```

## Build on Windows

Needs Visual Studio 2022 (C++ workload), the CUDA Toolkit 12.8+, the Vulkan SDK,
Git and Qt 6 (MinGW kit) - the same as the three separate programs.

```bat
build_all.bat          :: everything -> dist\   (the first build takes long: CUDA kernels)
build_all.bat gui      :: only the window and the tests (the engine is kept)
make_installer.bat     :: -> installer\output\TranscribeTranslate-1.0.0-setup.exe
clean.bat [all]
```

`build_all.bat` downloads CrispASR, llama.cpp and miniz at the tested commits
into `third_party\`, builds the window with MinGW and runs the tests, then the
engine and the translation library with MSVC (CUDA + Vulkan, static C runtime),
and assembles `dist\`. Edit `QT_DIR` at the top if your Qt is elsewhere.
FFmpeg: put `ffmpeg.exe` into `installer\redist\` (otherwise the one from
`..\transcribe_cpp\installer\redist\` is used, if there).

The installer has the same options as the others: CUDA libraries (optional),
FFmpeg, a desktop shortcut, the right-click menu (Transcribe / Translate speech
to subtitles for media, Translate for documents) and `tt_engine` on PATH.

## Build on Linux

```bash
sudo apt install build-essential cmake ninja-build git pkg-config ffmpeg qt6-base-dev
# optional: libvulkan-dev glslc (Vulkan), the CUDA Toolkit (NVIDIA)
./build_all.sh                 # -> dist/
./dist/transcribe_translate
```

`CUDA=on|off`, `VULKAN=on|off`, `QT_DIR`, `JOBS` and `CUDA_ARCHS` work as for
the other programs.

## Command line

```
tt_engine transcribe talk.mp4 -f txt,srt,docx
tt_engine translate report.docx -t de
tt_engine speakers talk.mp4 --srt talk.srt -o speakers.txt
tt_engine dub talk.mp4 --srt talk.de.srt -t de -o talk.de.dubbed.mp4
transcribe_translate --speech talk.mp4      # the window, on a tab, with a file
```

`tt_engine COMMAND --help` lists each command's options (they are those of
`transcribe_multi` and `translate_text`).

## Tests

`ctest --test-dir build-gui` (run by both build scripts):

- `common`, `engine_helpers`, `translate_formats`, `translate_partial_exit_contract`,
  `subtitles`, `child_process`, `model_files` - the three programs' unit tests
- `gui` - the window with a stand-in engine (`tests/fake_engine.cpp`): a
  transcription handed on to Translate, a file translation, a speech
  translation, one job at a time, the History and the settings

For development, `-DTT_BUILD_ENGINE_DYNAMIC=ON` also builds a `bin/dynamic/tt_engine`
that loads CrispASR (`libcrispasr.so` / `crispasr.dll`) at run time.
