# Speech Translate (C++ / Qt6)

Audio or video in one language -> subtitles in another and, optionally, a dubbed
soundtrack in the speaker's own voice, on your own computer.
`speech_translate_gui` runs existing programs one after the other:

1. `transcribe_multi` (../transcribe_cpp) -> subtitles in the spoken language;
   with **Several speakers** ticked, then `transcribe_multi speakers` finds who
   speaks when and the **Speakers** window shows what it found: listen to each
   speaker, name them, mark two that are the same person, or detect again with a
   given number of speakers
2. `translate_text` (../translate_cpp) translates that `.srt`, keeping the timing
3. optional: `transcribe_multi dub` speaks the translation with a text-to-speech
   model (Qwen3-TTS, CosyVoice3 or OmniVoice), cloning the speaker's voice, fits each
   line to the original timing and mixes it over the music/effects
   (see ../transcribe_cpp/README.md, "Dubbing")

Each runs as its own process, so the speech model has left the GPU before the
translation model loads. Afterwards the GUI writes the outputs:

| File | Content |
|---|---|
| `NAME.<lang>.srt` | translated subtitles (always) |
| `NAME.srt` | original subtitles (optional) |
| `NAME.<lang>.bilingual.srt` | original line above the translation (optional) |
| `NAME.<lang>.txt` | translated text in paragraphs (optional) |
| `NAME.<lang>.dubbed.mp4/.mkv/.mp3/.m4a/.wav` | dubbed video or audio (step 3) |

With several speakers, a subtitle in which the speaker changes is saved with each
speaker's part on its own line, starting with a dash (the usual subtitle style),
and the dub gives each speaker their own cloned voice (a built-in voice for a
speaker with too little clear speech).

Cloning needs the "speaker agreed" box ticked for each file (it is never
remembered); otherwise choose "Built-in voice". Cloned output carries an AI
notice (spoken, or as the first subtitle of a video) and an inaudible watermark.

The window shows audio length, time per step, total and speed (x real time).

## Build (Windows)

The three projects must sit side by side (`parakeet\transcribe_cpp`,
`parakeet\translate_cpp`, `parakeet\speech_translate_cpp`).

```bat
build_all.bat            & rem builds all three (each only recompiles what changed) -> dist\
build_all.bat quick      & rem only this GUI; reuses the other two dist\ folders
make_installer.bat       & rem ONE installer for all three -> installer\output\
clean.bat                & rem clean all three (keeps compiled CUDA kernels)
```

`dist\` contains all three programs with one copy of the Qt and cuBLAS DLLs.
The installer adds Start-menu entries for all three, right-click menu entries
(audio/video: Transcribe, Translate speech to subtitles; .txt/.md/.srt/.docx:
Translate) and optionally FFmpeg (from `..\transcribe_cpp\installer\redist`).

## Linux

```bash
./build_all.sh           # needs what the other two build_static.sh scripts need
```

## Layout

```
src/mainwindow.cpp   the three-step pipeline, outputs, timing
src/subtitles.cpp    bilingual .srt and text from subtitles
                     (+ ../translate_cpp/src formats/text/languages/models)
tests/               unit tests (no model needed)
```

The shared code in `../common` (subtitle reader, model list, running the tools,
safe file writing) must sit next to this folder; its tests run with this project's
`ctest`. See `../common/README.md`.
