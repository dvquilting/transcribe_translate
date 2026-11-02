# transcribe_multi (C++ / Qt6)

Native C++ version of `transcribe_multi.py` and its GUI. No Python is used at
build time or run time.

| Program | What it does |
|---|---|
| `transcribe_multi.exe` | Command-line transcriber (same options as the Python script) |
| `transcribe_multi_gui.exe` | Qt6 window; runs `transcribe_multi.exe` as a child process |

The models run on [CrispASR](https://github.com/CrispStrobe/CrispASR), a C++/ggml
runtime with CUDA support. Models are GGUF conversions, downloaded on first use
into `%USERPROFILE%\.cache\crispasr`:

| `-m` | Model | File (Q8_0) | Size |
|---|---|---|---|
| `parakeet-ultra` (default) | moondream/parakeet-ultra | parakeet-ultra-q8_0.gguf | 674 MB |
| `ark` | AutoArk-AI/ARK-ASR-3B | ark-asr-3b-q8_0.gguf | 4.3 GB |
| `qwen` | Qwen/Qwen3-ASR-1.7B | qwen3-asr-1.7b-q8_0.gguf | 2.5 GB |
| `granite` | ibm-granite/granite-speech-4.1-2b-plus | granite-speech-4.1-2b-plus-q4_k.gguf (no Q8_0 published; encoder kept F32) | 3.0 GB |
| `cohere` | CohereLabs/cohere-transcribe-03-2026 | cohere-transcribe-q8_0.gguf | 2.4 GB |
| `parakeet-redux` | moondream/parakeet-redux | parakeet-redux-q4_k.gguf | 402 MB |
| (SRT word timing for ark/qwen/granite/cohere) | Qwen3-ForcedAligner-0.6B | qwen3-forced-aligner-0.6b-q8_0.gguf | 986 MB |
| (punctuation for ark; see below) | PCS (XLM-R base, punctuation + capitals + sentence boundaries) | pcs-xlmr-base-q4_k.gguf | 163 MB |
| (`--polish` / `--summary`) | Qwen/Qwen3-4B-Instruct-2507 | Qwen3-4B-Instruct-2507-Q4_K_M.gguf | 2.5 GB |

CrispASR's ARK-ASR-3B support is marked experimental upstream. ARK writes no
punctuation or capitals, so its chunks go through the PCS punctuation model
(`--punctuation auto`, the default). For the other models PCS is used only on a
chunk that comes back unpunctuated (12+ words and under one sentence end per 40
words, or no capitals). `--punctuation on` applies it to every chunk, `off`
never. It runs before forced alignment, so SRT cues get the punctuation too
(except Parakeet SRT, whose cues come from its own word timings). If PCS can't
be loaded (e.g. no internet on first use) transcription continues without it.

`--ark-punctuation` (GUI: "Ask ARK to write punctuation") is the experimental
alternative: ARK's instruction becomes "Transcribe the audio in LANGUAGE, with
punctuation and capitalization." instead of its default, and PCS then only
handles chunks that still come back unpunctuated. ARK was trained with a fixed
instruction, so a chunk that comes back empty is redone with the default one. Granite 4.1 Plus is
used rather than the base 4.1 model because only Plus writes punctuation and
capitals, which the paragraphs, subtitles and AI options rely on. It covers en, fr,
de, es, pt. Cohere Transcribe covers 14 languages but cannot detect the language:
with `--language auto` it uses English (the GUI doesn't offer Auto-detect for it).
The forced aligner covers en, zh, yue, fr, de, it, ja, ko, pt, ru, es; for other
languages SRT cues fall back to whole chunks.

There are two ways to build it:

| | Shareable build (`build_static.bat`) | Quick build (`build.bat`) |
|---|---|---|
| CrispASR | compiled **into** `transcribe_multi.exe` | `crispasr.dll` from the CrispASR release, loaded at run time |
| GPU | CUDA, else Vulkan, else CPU | whatever the release package supports (CUDA) |
| Compilers | Visual Studio (CLI) + Qt MinGW (GUI) | Qt MinGW only |
| Extra installs | Visual Studio, CUDA Toolkit, Vulkan SDK, Git | none |

## Shareable build

### Install (once)

1. **Visual Studio 2022** (Community is free) with the **Desktop development with C++** workload.
2. **CUDA Toolkit 12.8 or a newer 12.x** (12.8+ is required for the RTX 50 series). Its
   installer sets `CUDA_PATH`.
3. **Vulkan SDK** from <https://vulkan.lunarg.com> (sets `VULKAN_SDK`).
4. **Git for Windows**, if you don't already have it.
5. **Qt 6 MinGW kit**, which you already have. The GUI is built with it.

Open a new command prompt after installing so the environment variables are picked up.

### Build

```bat
cd C:\Users\David\parakeet\transcribe_cpp
build_static.bat
```

The script downloads CrispASR (with its submodules) into `third_party\CrispASR`, builds
the GUI with MinGW, builds the static `transcribe_multi.exe` with Visual Studio, and
assembles everything in **`dist\`**.

CrispASR is pinned to the version this program was tested with (`CRISPASR_COMMIT` at
the top of `build_static.bat` / `.sh`); an existing checkout at another version is
switched to it. Two checks keep a build from silently going wrong when that pin is
changed: the source changes in `cmake\patch_crispasr.cmake` stop the configuration if
they no longer apply (`-DTMX_ALLOW_UNPATCHED=ON` to build without them), and
`src\crispasr_abi_check.cpp` compares the CrispASR declarations this program copies
(`src\crispasr_api.h`) with CrispASR's real headers at compile time.

The shared code in `..\common` (subtitle reader, safe file writing, model list,
running the tools from the GUIs) must sit next to this folder, as in `parakeet\`. The first build takes a long time (the CUDA
kernels dominate; remove architectures from `CUDA_ARCHS` at the top of the script to
speed it up, e.g. just `120-real` for your own card).

### What to share

Zip the `dist\` folder:

| File | Notes |
|---|---|
| `transcribe_multi.exe` | Self-contained: CrispASR, ggml (CPU, Vulkan, CUDA), static C runtime |
| `cublas64_12.dll`, `cublasLt64_12.dll` | ~800 MB, only needed for CUDA. Windows has no static cuBLAS. Leave them out for a small package: the program then uses Vulkan, also on NVIDIA cards |
| `transcribe_multi_gui.exe` + Qt DLLs | The window (Qt is LGPL, so it stays as DLLs) |
| `licenses\` | CrispASR/ggml (MIT) and Qt licence texts |

The people you share it with need Windows 10/11 x64, a CPU with AVX2 (2013+), and a
graphics driver installed (that is what provides Vulkan; NVIDIA drivers also provide
CUDA). ffmpeg is optional: without it the built-in decoder handles WAV, MP3, FLAC and
OGG/Opus; M4A/AAC and video files need ffmpeg on `PATH`. Models download on first use
(internet needed once), into `%USERPROFILE%\.cache\crispasr`.

The program prints the engine it uses (`Engine: CUDA (NVIDIA GeForce RTX 5060 Ti, 16.0 GB)`),
and the window shows it as a coloured "Compute" badge next to the status line.

`--device auto` (the default) uses CUDA when an NVIDIA GPU and the cuBLAS DLLs are
present, otherwise Vulkan, otherwise the CPU. `--device vulkan` / `cuda` / `cpu` force one.

## Quick build (uses the CrispASR release DLLs)

Needs only Qt (MinGW kit, with its bundled compiler, CMake and Ninja), the unzipped
CrispASR CUDA package (e.g. `C:\local\crispasr-windows-x86_64-cuda`) and ffmpeg.
Edit the paths at the top of `build.bat` if needed, then run `build.bat`. Everything,
including about 1 GB of CrispASR/CUDA DLLs, ends up in `build\bin\`.

## Windows installer

`make_installer.bat` packages `dist\` into a normal Windows installer,
`installer\output\Transcribe-1.0.0-setup.exe`, using the free
[Inno Setup](https://jrsoftware.org/isinfo.php) (`winget install JRSoftware.InnoSetup`).
It runs `build_static.bat` first if `dist\` hasn't been built.

The installer:

- installs per user or for all users (it asks), on 64-bit Windows 10/11;
- adds Start-menu (and optionally desktop) shortcuts and an uninstaller;
- adds **Transcribe (speech to text)** to the right-click menu of audio/video files;
- offers the cuBLAS DLLs as an optional "NVIDIA CUDA support" component, pre-selected
  only when an NVIDIA driver is present (without it the program uses Vulkan or the CPU);
- offers FFmpeg as a component if you put `ffmpeg.exe` in `installer\redist\`
  (e.g. the "essentials" build from gyan.dev, with its licence as `ffmpeg-LICENSE.txt`);
  the program uses an `ffmpeg.exe` next to itself before looking on `PATH`;
- can optionally add the install folder to `PATH` for the command-line program.

Speech models are not bundled (0.4 to 4.3 GB each); they download on first use and are
kept on uninstall. Edit `AppVersion` at the top of `installer\transcribe.iss` for new
releases, and keep its `AppId` unchanged so new versions upgrade the old one.

## Linux

Two scripts mirror the Windows ones (run them with `bash` if the files lost
their executable bit on the way from Windows):

```bash
# packages (Debian/Ubuntu)
sudo apt install build-essential cmake ninja-build git ffmpeg qt6-base-dev
sudo apt install libvulkan-dev glslc        # optional: Vulkan backend
# optional: CUDA Toolkit 12.8+ from NVIDIA's repository for the CUDA backend

./build_static.sh                            # CrispASR compiled in -> dist/
CRISPASR_DIR=~/crispasr-linux ./build.sh     # quick build using libcrispasr.so -> build/bin/
```

`build_static.sh` detects CUDA (`nvcc`) and Vulkan (`glslc` + headers) and builds
whichever it finds, plus the CPU backend. The CUDA runtime and cuBLAS are linked
in statically, so `dist/transcribe_multi` only needs the usual system libraries
(glibc, libstdc++, libgomp), plus `libvulkan.so.1` when built with Vulkan and the
NVIDIA driver for CUDA. Override detection with `CUDA=off` / `VULKAN=off`, and set
`CUDA_ARCHS=120-real` to compile only for an RTX 50 card (much faster). The GUI in
`dist/` uses the system's Qt 6 libraries.

## Use

```bat
build\bin\transcribe_multi_gui.exe
build\bin\transcribe_multi.exe talk.mp3                     & rem parakeet-ultra -> talk.txt
build\bin\transcribe_multi.exe talk.mp3 -m ark -f both      & rem talk.txt + talk.srt
build\bin\transcribe_multi.exe lecture.mp4 -m qwen -f srt
build\bin\transcribe_multi.exe talk.mp3 -f txt,docx      & rem talk.txt + talk.docx (Word)
build\bin\transcribe_multi.exe talk.mp3 --polish --summary & rem + talk.clean.txt, talk.summary.txt
build\bin\transcribe_multi.exe --help
```

### Word and PDF

`-f docx` (or a list such as `-f txt,srt,docx`) writes the transcript as a Word
document, `NAME.docx`: the file name as its title, then the paragraphs. With
`--polish` / `--summary` the corrected copy and the summary are written as `.docx`
too, with the AI note in italics under the title. The `.docx` is written by
`common/src/docx_writer.cpp` (no extra library) and opens in Word, LibreOffice and
Google Docs.

PDFs are made by the GUI (tick **PDF** under *Save as*): Qt lays out the text with
the system's fonts and embeds them, so every script prints. The GUI asks
`transcribe_multi` for plain-text copies in a temporary folder (`--text-dir DIR`,
with `-f none` when only PDF is ticked) and turns each into `NAME.pdf`,
`NAME.clean.pdf` and `NAME.summary.pdf` next to the other outputs: A4, or Letter
in the US and Canada, with 2.5 cm margins and page numbers.

### AI post-processing (optional)

`--smart-paragraphs` breaks the transcript into paragraphs by topic and pauses,
`--polish` writes `NAME.clean.txt`, a copy with spelling, punctuation and grammar
corrected; `--summary` writes `NAME.summary.txt`, an overview plus key points. All three
run a local chat model (Qwen3-4B-Instruct-2507, Q4_K_M) through the llama.cpp that
is already built into CrispASR, in the same process, after the speech model has been
unloaded. Nothing is uploaded. The original `NAME.txt` is always written unchanged.

- `--smart-paragraphs` lets the model choose the paragraph breaks in `NAME.txt`
  (and in the corrected copy) instead of every N sentences. The sentences are
  numbered and the model replies only with the numbers that start a paragraph, so
  the wording cannot change. Pauses of 1 s or more (from word timestamps: always
  for Parakeet, for the chunked models when word-timed subtitles are made) are shown to it as
  hints. Paragraphs of one short sentence are merged and ones over
  max(12, 2N) sentences or 250 words are split at the longest pause.
- Corrections are done in ~300-word sections at temperature 0. A section whose
  correction changes the word count by more than 12 % is kept as it was.
- Long transcripts are summarised part by part and the notes are then combined.
- `--llm-file model.gguf` uses another instruction-tuned GGUF chat model;
  `--llm-ctx N` sets its context size (default 12288 tokens; about 1.8 GB of VRAM
  for Qwen3-4B on top of its 2.5 GB of weights).
- If post-processing fails, the transcript is still saved and the exit code is 3.
- The word-timing model (forced aligner, ~1 GB) is unloaded before the chat model
  loads. That needs a small addition to CrispASR's C API, which the static build
  applies automatically (`cmake/patch_crispasr.cmake`); with the release DLLs it
  stays loaded and a note is printed.

### Dubbing: translated speech in the speaker's voice (`dub`)

```bat
transcribe_multi.exe dub talk.mp4 --srt talk.en.srt --source-srt talk.srt -s es -t en --i-have-rights -o talk.en.dubbed.mp4
transcribe_multi.exe dub talk.mp3 --srt talk.de.srt -t de --no-clone -o talk.de.mp3
```

Speaks the translated subtitles with a text-to-speech model and fits each line to
the original timing (what Speech Translate's step 3 runs):

1. **Background** (`--background keep`, default): the htdemucs model (38 MB)
   separates the voice from music and effects; the music/effects are kept under the
   new voice. `duck` keeps the whole original quietly underneath, `none` the voice only.
2. **Voice**: a 3–14 s stretch of clean speech (chosen from `--source-srt`, with its
   transcript) is used to clone the voice, or `--voice-file WAV --voice-text TEXT`.
   **Accent:** by default the new language is spoken with a native accent: Qwen3-TTS
   clones only the voice (speaker embedding, "x-vector" mode, via a small CrispASR
   patch in `cmake/patch_crispasr.cmake` applied by the static build), CosyVoice3
   uses its cross-language mode. `--keep-accent` copies the speaker's way of
   speaking too, accent included. OmniVoice always keeps some of the accent.
   **Likeness:** with a native accent only the sound of the voice is cloned, and
   that improves with more speech, so Qwen3-TTS and CosyVoice3 get ~30 s taken from
   several of the best stretches (`--sample-seconds 5-60`). With the original accent
   (and OmniVoice) one ~10 s stretch with its transcript is used, since longer
   prompts leak into the output. Afterwards the dub's tone (bass, warmth, presence,
   air; 7 octave bands, at most +-6 dB) is matched to the speaker's voice in the
   sample (`--no-tone-match` to skip).
3. **Lines**: subtitles that continue a sentence are spoken together; each line is
   sped up (without changing the pitch) by up to `--max-speed` (1.35) to fit until the
   next line; a line that still doesn't fit delays the following ones until a pause.
   A line whose audio is far longer than its text could take (a "runaway", where
   the model doesn't stop) is synthesized again with another seed, up to 3 times,
   and otherwise left silent with a warning.
4. The voice is matched to the original speech level, mixed and peak-limited at 44.1 kHz.
   `.wav/.mp3/.m4a/.flac` = audio; `.mp4/.mkv` = the video (copied, not re-encoded)
   with the new soundtrack and the subtitles as a track.

| `--tts` | Download | Languages |
|---|---|---|
| `qwen3-tts` (default) | 2.0 GB | en zh ja ko de fr ru pt es it |
| `cosyvoice3` | 1.2 GB | en zh ja ko de es fr it ru; drops the original accent when cloning across languages |
| `omnivoice` | 1.6 GB | 600+ |

### Several speakers (`speakers`, `dub --speakers`)

```bat
transcribe_multi.exe speakers talk.mp4 --srt talk.srt -o talk.speakers.txt --split-srt talk.split.srt
rem translate talk.split.srt to talk.en.srt (keeping the timing), then:
transcribe_multi.exe dub talk.mp4 --srt talk.en.srt --source-srt talk.split.srt --speakers talk.speakers.txt -s es -t en --i-have-rights -o talk.en.dubbed.mp4
```

`speakers` finds who speaks when with CrispASR's FoxNose speaker detection
(WeSpeaker ResNet34-LM voice embeddings, 24 MB, any language; speaker counting,
clustering and smoothing) and gives each subtitle its speaker. It counts the
speakers itself (up to 8); `--num-speakers N` sets the number. A subtitle in
which the speaker changes is split there (`--split-srt`): its text at the sentence
end, else the comma, else the word gap nearest to where the change falls; changes
under a second (a "yes" in between) are ignored. `--samples-dir DIR` writes a few
seconds of each speaker (`speaker_1.wav`, ...) to listen to. The speakers file
(one tab-separated record per line, see `common/src/speakers.h`) can be edited:
names, and two speakers who are one person (Speech Translate does this in a window).

`dub --speakers FILE` gives each speaker their own voice: each one's voice sample
is taken only from their own subtitles, and their tone is matched separately. A
speaker with no 3–14 s stretch of clear speech gets the model's built-in voice
instead (said in the log). With `--i-have-rights` the user states that every
speaker agreed. Lines by different speakers are never spoken as one, and in a
video's subtitle track the parts of a split subtitle are shown together again,
each speaker's part on its own line starting with a dash. The WeSpeaker weights
are CC-BY-4.0 (Wespeaker/wespeaker-voxceleb-resnet34-LM).

**Consent and disclosure.** A voice is only cloned with `--i-have-rights`, the user's
statement that the speaker agreed (logged as `[CONSENT]`); otherwise the model's
built-in voice is used. Every synthesized clip carries CrispASR's inaudible AI
watermark. Cloned output also carries the notice "This audio was generated by
artificial intelligence.": spoken in a neutral voice at the start of audio files;
for videos spoken before the first line when there is room, otherwise shown as the
first subtitle. The notice is also written to the file's `comment` tag.

Differences from the Python version:

- `--language` takes ISO codes (`en`, `fr`, `de`, ... or `auto`) instead of names.
- `--batch-size` is gone (CrispASR decodes one chunk at a time); `--threads` was added.
- `--model-id` is now `--model-file` (a local `.gguf`); `--models-dir` moves the cache.
- `--device` accepts `auto`, `cuda`, `vulkan` or `cpu`.

## Layout

```
CMakeLists.txt
src/crispasr_api.h     CrispASR C-ABI functions used here (speech + chat)
src/llm.cpp            spelling/grammar cleanup and summaries with a local LLM
src/crispasr_loader.cpp  binds them: linked directly (static) or from crispasr.dll
src/pathutil.h         UTF-8 file-name helpers
cmake/crispasr_overlay.cmake, cmake/static_cli.cmake   static build glue
build_static.bat / build_static.sh   shareable build (Windows / Linux)
build.bat / build.sh                 quick build (Windows / Linux)
make_installer.bat, installer/       Windows installer (Inno Setup)
clean.bat                            delete build outputs
src/audio.*            ffmpeg decoding + silence-aware chunking
src/transcript.*       subtitle cues, SRT and paragraph formatting
src/engine.*           model table, downloads, CrispASR session wrapper
src/cli_main.cpp       transcribe_multi.exe
src/gui/               transcribe_multi_gui.exe (Qt6 Widgets)
tests/test_helpers.cpp unit tests (no model needed)
```
