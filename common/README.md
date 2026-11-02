# common - code shared by the three programs

`transcribe_cpp`, `translate_cpp` and `speech_translate_cpp` each compile these
files into their own targets (there is no library to build first). The folder
must sit next to them:

    parakeet\
      common\                  <- this folder
      transcribe_cpp\
      translate_cpp\
      speech_translate_cpp\

Each project's `CMakeLists.txt` includes `../common/common.cmake` (and stops with
an error if it is missing); transcribe_cpp's static build does the same in
`cmake/static_cli.cmake`. The build scripts need nothing else.

## src/ - plain C++17, no dependencies (no Qt, CrispASR or llama.cpp)

| File | What | Used by |
|---|---|---|
| `srt.h/.cpp` | The SubRip reader (`parse_srt`: BOM, CRLF, Windows-1252, missing blank lines, blank lines inside a cue) and writer (`format_srt`), `srt_time`, `strip_srt_tags` | transcribe (`.srt` output, dub cue reader), translate (subtitle translation), Speech Translate (bilingual subtitles, via translate's `formats.cpp`) |
| `speakers.h/.cpp` | Who speaks when: the speakers file (`format_speakers`, `parse_speakers`), `merge_speakers` (two speakers who are one person), `join_speaker_parts` (subtitles split by speaker shown as one again, a dash before each speaker's part) | transcribe (`speakers`, `dub --speakers`), Speech Translate (the Speakers window, the saved subtitles) |
| `docx_writer.h/.cpp` | `make_docx`: a Word document (title, an italic note, paragraphs with their line breaks) written as a stored zip, no library needed; `text_paragraphs` | transcribe (`-f docx`) |
| `fileio.h/.cpp` | `write_file_atomic` (temporary file + replace, never a truncated output), `replace_file` (on Windows retried for about half a second while another program briefly holds the target), `read_file` | transcribe (outputs, dub commit), translate (outputs, `.docx`) |
| `utf8_path.h` | `path_from_utf8` / `path_to_utf8` | transcribe, translate (their `pathutil.h` brings them into `tmx` / `tl`) |
| `utf8_text.h/.cpp` | `trim`, `normalize_space`, `utf8_length`, `clean_input_text` (invalid UTF-8 is read as Windows-1252: only the stray bytes in an otherwise UTF-8 text, the whole text when it has no valid multi-byte UTF-8) | all three |
| `model_catalog.h/.cpp` | The speech-recognition and voice models of `transcribe_multi` (keys, names, sizes, files, descriptions, languages) | transcribe (`engine.cpp`, `dub.cpp`), Speech Translate (model menus) |

Everything is in namespace `pk`.

## qt/ - Qt 6, compiled into the GUIs only

| File | What |
|---|---|
| `child_process.h/.cpp` | `pk::ChildProcess`: runs one command-line tool for a GUI - working directory, UTF-8 decoding and line splitting of its output, cancelling the whole process tree, a single `finished(exitCode, cancelled)` / `failedToStart()` (a crash reports `kCrashExitCode` (-1) and the output line "The program crashed.", never a signal number that could pass for a tool's own exit code), and `stop()` for a closing window. The tools stay separate processes so each step's GPU memory is freed when it exits. |
| `process_tree.h` | `pk::ProcessTree`: Windows job object / POSIX process group so a cancel also stops ffmpeg and other descendants. |
| `model_files.h/.cpp` | Where the downloaded models are (`crispasr_models_dir`, `translate_models_dir`, matching the tools' own defaults and the GUIs' "Models folder" setting), listing them (`*.gguf`), telling the programs' own downloads ("ours": in CrispASR's cache a model with CrispASR's `<model>.src` sidecar, in Translate's folder one of Translate's model file names, passed in by the GUI) from other programs' models in the same folder, and deleting a model with its download sidecars (`.src`, `.part`, `.lock`), never a download still in progress (a `.part.<pid>.<n>` whose process runs, a held `.lock`). Nothing else in a folder is touched. |
| `models_dialog.h/.cpp` | `pk::ModelsDialog`, the GUIs' **Model files** window: every model in the model folders with its full path and size and a **Delete** button; models these programs did not download are marked "(not downloaded by these programs)". **Clean All models** deletes only the programs' own downloads (and their leftovers) and names each file in its confirmation. Disabled while a tool runs. |
| `help_window.h/.cpp` | `pk::show_help_window`: the GUIs' **Help** window (an HTML page each program writes; asking again brings the open window forward), and `help_row` for its tables. |

Used by `transcribe_multi_gui`, `translate_gui` and `speech_translate_gui`.

## tests/

`test_common.cpp` (src/), `test_child_process.cpp` and `test_models.cpp` (qt/) are built and run by
speech_translate_cpp's `ctest` (`pk_add_common_tests()` in `common.cmake`).
