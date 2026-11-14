# Transcribe & Translate

Speech to text, translation and speech translation in **one program**, running
entirely on your own computer. It does what `transcribe_cpp`, `translate_cpp`
and `speech_translate_cpp` do, in one window with four tabs:

| Tab | What it does |
|---|---|
| **Transcribe** | audio/video → .txt, .srt, Word, PDF; several speakers with their names in the transcript ("Anna: ..."); optional AI post-processing (smart paragraphs, corrected copy, summary). **Translate...** hands the transcript to the Translate tab. |
| **Translate** | typed text, or .txt / .md / .srt / .docx / .pdf files, keeping the layout (a PDF is rebuilt as headings and paragraphs; .txt and .md can also be saved as PDF) |
| **Speech translation** | audio/video → translated subtitles (original, bilingual, text, PDF), several speakers, and an optional dub in the speaker's (cloned) voice |
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

The Translate tab runs `tt_engine translate --serve` (`src/gui/translate_engine.*`):
one engine for all its jobs, which keeps the model (and the faster mode's helper)
loaded, so a second translation starts without loading 10 GB again. Jobs go in
on stdin (`job<TAB>args`, `cancel`, `unload`, `quit`); each ends with a
`tt-job-done <code>` line, and `tt-model <name>` says what stays loaded. Another
tab's job, **Free GPU memory**, **Model files** and closing the window end the
engine. The tab also loads the model ahead (a `job` line ending in `--preload`:
the engine loads the model and its draft model for such jobs and translates
nothing) a moment after it is shown or its model, faster mode or device
changes, unless a job runs or the model would have to be downloaded first; a
job started meanwhile waits for the load in the engine. A model kept loaded
keeps its draft model for the next job asking the same: choosing again would
measure the graphics memory that model now takes. A cancelled job stops at once; if it doesn't within 4 s (a download,
loading), the engine is ended.

PDFs never reach the engine. The window writes them with Qt's `QPdfWriter`
(system fonts embedded, every script shaped). It reads them with PDFium
(`pdfium.dll` / `libpdfium.so` from [pdfium-binaries](https://github.com/bblanchon/pdfium-binaries),
downloaded by `build_all` into `third_party/pdfium` and copied next to the
window; loaded at run time, so the window starts without it). The text of each
page is rebuilt into headings (by font size), paragraphs (by spacing, indents
and short last lines; hyphenation undone), page breaks and page size; page
numbers and running headers are dropped. That becomes a Markdown file
(`# heading`, one paragraph per line, `<!-- page -->`), which the engine
translates like any Markdown, and the result is laid out as a new PDF. A
scanned PDF has no text layer and is refused (no OCR). `TT_PDF_DEBUG=1` prints
the lines as read.

```
src/gui/        the window: mainwindow (tabs, Help, Model files, Quit),
                transcribe_page, translate_page, speech_page, history_page,
                jobgate (one job at a time, the engine's path), pdf_files (PDFs
                are written with Qt and read with PDFium here, not in the engine)
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
make_installer.bat     :: -> installer\output\TranscribeTranslate-1.1.0-setup.exe
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

A transcript with the speakers' names (what the Transcribe tab's "Several speakers" does, with the
Speakers window between the second and third command): the speech once, who speaks when, then the
outputs. Names can be changed in `speakers.txt` (`speaker <id> <name> ...` lines) before the last step.

```
tt_engine transcribe talk.mp4 -f srt -o work/source --words work/words.tsv
tt_engine speakers talk.mp4 --srt work/source.srt --words work/words.tsv -o work/speakers.txt --split-srt work/split.srt
tt_engine transcribe talk.mp4 --from-srt work/split.srt --from-words work/words.tsv --speakers-file work/speakers.txt -f txt,srt,docx
```

`tt_engine COMMAND --help` lists each command's options (they are those of
`transcribe_multi` and `translate_text`).

### Faster TranslateGemma 12B and 27B (speculative decoding)

`tt_engine translate ... --draft auto` (the window's default, **Faster mode**
under Advanced on the Translate and Speech translation tabs) lets a small helper
model guess a few tokens ahead: `--draft translategemma-4b` (TranslateGemma 4B
Q4_K_M, 2.5 GB, the best guesser), `--draft gemma-3-1b` (Gemma 3 1B instruct
Q8_0, 1.1 GB: guesses less well, but fits beside TranslateGemma 12B on a 16 GB
card) or `auto` (the 4B if it fits beside the model by a rough estimate -
model + helper + 3 GB - else the 1B). The model list also has
`translategemma-12b-q4`, TranslateGemma 12B as a 7.3 GB file, which leaves room
for the 4B helper on 16 GB; the large model checks all of them in one step and keeps only the
tokens it would have chosen itself, so the translation is the same, only
sooner. How far it drafts is chosen step by step from measured costs (the time
of a step of the model for each number of drafted tokens - a step without a
draft is measured first - of a token of the draft model, and how often the
guesses are right) to write the most tokens per second; where no draft pays off it
writes without one, trying a short draft now and then. The model's choices are
read as the largest logit (no sampler pass over the 262K vocabulary) and the
draft stops at a token it is unsure of (logit margin under 1). (The one exception: the model's numbers for one token checked alone and
for several checked at once can differ in the last decimal place, which can
change a word where two were exactly as likely.) It is used only on a graphics
card, and only if both models fit on it whole: a layer of the large model
pushed onto the processor slows every step, and the processor works through a
draft's tokens one by one, so a split model loses more than drafting saves
(measured with 27B on 16 GB). 12B with the 4B draft needs a little more than
16 GB (a 16 GB card is usually just too small), 27B about 24 GB. Otherwise the log says why it was skipped, and if loading the draft model
fails after all (graphics memory), the translation goes on at the usual speed.
The helper is downloaded to the models folder on first use (for the 4B, an
existing Q8_0 download is used when it fits). The log ends with how many of
the drafted tokens were accepted and the speed-up (the writing time against an
estimate for one token per step); the window shows that next to Compute.
`--draft PATH` uses another draft model with the same vocabulary, `--draft-max N`
caps how far it drafts ahead (default 7: with the token being checked, 8 per
step, the largest batch llama.cpp's CUDA backend handles with its
matrix-vector kernels).

## Tests

`ctest --test-dir build-gui` (run by both build scripts):

- `common`, `engine_helpers`, `translate_formats`, `translate_partial_exit_contract`,
  `subtitles`, `child_process`, `model_files` - the three programs' unit tests
- `pdf` - writing PDFs, reading them back with PDFium (headings, paragraphs,
  page breaks, page size, no page numbers), the Markdown in between, a scanned PDF
- `gui` - the window with a stand-in engine (`tests/fake_engine.cpp`): a
  transcription handed on to Translate, a file translation, a PDF translated
  into a PDF, a speech translation (with its PDF), one job at a time, the
  History and the settings

For development, `-DTT_BUILD_ENGINE_DYNAMIC=ON` also builds a `bin/dynamic/tt_engine`
that loads CrispASR (`libcrispasr.so` / `crispasr.dll`) at run time.
