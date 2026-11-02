# Transcribe & Translate

Local speech-to-text, translation and speech translation (with dubbing in the
speaker's voice), running entirely on your own computer or phone. Nothing is
uploaded; each model is downloaded once, the first time it is needed.

| Folder | What it is |
|---|---|
| [`transcribe_translate_cpp`](transcribe_translate_cpp) | **Transcribe & Translate**: one program with tabs for transcription, translation, speech translation and a job history (Qt 6 window + `tt_engine`) |
| [`transcribe_cpp`](transcribe_cpp) | Transcribe: speech to text (.txt, .srt, Word, PDF) - `transcribe_multi` + window |
| [`translate_cpp`](translate_cpp) | Translate: text, Markdown, subtitles and Word documents - `translate_text` + window |
| [`speech_translate_cpp`](speech_translate_cpp) | Speech Translate: audio/video to translated subtitles and dubbing (uses the two above) |
| [`common`](common) | Code shared by the three separate programs |
| [`android`](android) | The phone app (transcription and translation) |

Speech recognition and voices use [CrispASR](https://github.com/CrispStrobe/CrispASR),
translation uses [llama.cpp](https://github.com/ggml-org/llama.cpp); the build
scripts download both at tested versions. Each folder's README.md explains how
to build it (Windows: Visual Studio 2022, CUDA, Vulkan SDK and Qt 6; Linux:
`build_all.sh` / `build_static.sh`).
