# translate_text (C++ / Qt6)

Translate text, Markdown, subtitles (.srt) and Word documents (.docx) on your own
computer with a local language model, run through llama.cpp compiled into the
program. Nothing leaves the computer.

| `-m` | Model | File | Size | Notes |
|---|---|---|---|---|
| `translategemma-4b` | google/translategemma-4b-it | translategemma-4b-it.Q8_0.gguf (mradermacher) | 4.1 GB | 55 languages |
| `translategemma-12b` (default) | google/translategemma-12b-it | translategemma-12b-it.Q6_K.gguf | 9.7 GB | 55 languages |
| `translategemma-27b` | google/translategemma-27b-it | translategemma-27b-it.Q4_K_M.gguf | 16.5 GB | partly on the CPU with 16 GB of VRAM |
| `hy-mt2-7b` | tencent/Hy-MT2-7B | HY-MT2-7B-Q8_0.gguf (Tencent) | 8.0 GB | 38 languages, uses context |
| `qwen3.5-9b` | Qwen/Qwen3.5-9B | Qwen3.5-9B-Q6_K.gguf (Unsloth) | 7.5 GB | any language, uses context |
| `qwen3.5-35b-a3b` | Qwen/Qwen3.5-35B-A3B | Qwen3.5-35B-A3B-UD-Q4_K_XL.gguf | 22.2 GB | MoE; expert weights partly on the CPU |

Models download on first use into `%LOCALAPPDATA%\Translate\models`
(`~/.cache/translate` on Linux). `--model-file` uses any GGUF of the same family.

## How it works

- **GPU choice**: CUDA if an NVIDIA card and the cuBLAS DLLs are present, otherwise
  Vulkan, otherwise the CPU (`--device` to force one). An explicit device list is
  passed, because the CUDA and Vulkan backends both see an NVIDIA card.
- **Memory fitting**: llama.cpp's `common_fit_params` places the weights and KV
  cache in free GPU memory (leaving 768 MB); a model that doesn't fit keeps some
  layers (dense) or expert weights (MoE) on the CPU. The log shows the placement.
- **Prompts**: each family gets the exact format it was trained with, built in
  `translator.cpp` rather than through a generic chat template:
  - TranslateGemma: Gemma turns + the translator instruction from the technical
    report. It needs the source language, so "auto" asks the model first.
  - Hy-MT2: `<|startoftext|>…<|extra_0|>`, Tencent's templates (Chinese wording
    when Chinese is involved), including the background-context template.
  - Qwen 3.5: ChatML with thinking switched off (empty `<think>` block).
- **Pieces**: paragraphs are grouped into pieces of ~400 (TranslateGemma, 2K-token
  training window) or ~600 tokens. If the model merges or splits paragraphs, that
  piece is redone paragraph by paragraph. A paragraph that is too long is
  translated in groups of sentences.
- **Context** (Hy-MT2, Qwen): the preceding ~800 characters of source text are
  given as background (`--no-context` to turn off).
- **Decoding**: greedy (Hy-MT2 with repetition penalty 1.05). Output that loops,
  runs out of room or comes back empty is retried once with light sampling; if it
  still fails the part is kept and reported.
- **Formats**:
  - `.txt`: paragraphs at blank lines; blank lines kept exactly.
  - `.md`: headings, list items, quotes, table cells and paragraphs are
    translated; front matter, code blocks, HTML lines, link definitions are not.
  - `.srt`: cues are grouped into sentences (end punctuation, 2 s pause or 6
    cues), translated as a whole and divided back over the cues in proportion to
    the source length, then wrapped to 42 characters. Dialogue cues (`- …` lines)
    and italics are kept.
  - `.docx`: every paragraph (body, tables, headers, footers, footnotes) is a unit;
    the translation goes into its first text run, so paragraph styles, numbering
    and the first run's formatting are kept, but formatting changes inside a
    paragraph are not.
- `TRANSLATE_DEBUG=1` prints every prompt and raw answer to stderr.

## Shareable build (Windows)

Install once: Visual Studio 2022 (Desktop C++), CUDA Toolkit 12.8+, Vulkan SDK,
Git, and Qt 6 with the MinGW kit (paths at the top of `build_static.bat`).

```bat
build_static.bat          & rem clones llama.cpp + miniz (pinned commits), builds dist\
make_installer.bat        & rem Inno Setup installer -> installer\output\
clean.bat                 & rem remove our build files, keep the compiled CUDA kernels
```

`dist\` holds `translate_text.exe` (CPU + Vulkan + CUDA in one file), the cuBLAS
DLLs (optional; without them CUDA is skipped and Vulkan used) and
`translate_gui.exe` with the Qt DLLs.

## Linux

```bash
sudo apt install build-essential cmake ninja-build git curl qt6-base-dev libvulkan-dev glslc
./build_static.sh         # CUDA is used too if nvcc is found
dist/translate_gui
```

## Use

```bat
translate_text report.docx -t de                       & rem -> report.de.docx
translate_text notes.md -t fr -m hy-mt2-7b
translate_text film.srt -t es -s en -m qwen3.5-9b      & rem -> film.es.srt
translate_text --text "Guten Morgen" -t en
translate_text --list-models
translate_text --list-languages
```

## Layout

```
src/llm.cpp          llama.cpp wrapper: device choice, memory fitting, generation, loop detection
src/translator.cpp   prompts per model family, piece planning, retries, output clean-up
src/formats.cpp      txt / Markdown / SRT layouts; src/docx.cpp Word documents (miniz)
src/download.cpp     model download (WinHTTP on Windows, curl elsewhere), resumable
src/models.cpp       model table; src/languages.cpp language table
src/cli_main.cpp     translate_text
src/gui/             translate_gui (runs translate_text as a child process)
tests/               unit tests for the formats (no model needed)
```

The shared code in `../common` (subtitle reader, safe file writing, running the
tool from the GUI) must sit next to this folder, as in `parakeet\`.
