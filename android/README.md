# Transcribe & Translate for Android

The phone version of `transcribe_cpp` (speech to text) and `translate_cpp`
(translation), in one app with four tabs: **Transcribe**, **Translate**,
**History** and **Models**. Everything runs on the phone; nothing is uploaded.

| | Desktop | Phone |
|---|---|---|
| Speech models | Parakeet Ultra, ARK, Qwen3-ASR 1.7B, Granite, Cohere (8-bit) | Parakeet Ultra, Qwen3-ASR 0.6B and 1.7B (4-bit) |
| Translation models | TranslateGemma 4–27B, Hy-MT2 7B, Qwen 3.5 9B/35B | Hy-MT2 1.8B, TranslateGemma 4B, Qwen 3.5 2B (4-bit) |
| AI post-processing | corrected copy, summary, smart paragraphs | corrected copy (Qwen 3.5 2B) |
| Compute | CUDA, Vulkan, CPU | the phone's processor (Arm dot-product + int8 matrix instructions) |
| Audio decoding | ffmpeg | Android's own decoders (every format the phone plays, video too) |
| Output | .txt .srt .docx .pdf | the same, saved in Documents/Transcribe (or a folder you choose) |

Also: record from the microphone (screen off is fine), share audio, video,
text or documents into the app from other apps, a history of past jobs, and
model downloads that resume (Wi-Fi only unless allowed in Settings).

## How it is built

```
android/
  app/src/main/cpp/          the native engine, libpkengine.so
    engine_api.h/.cpp        catalog / transcribe / translate, JSON in and out
    jni_bridge.cpp           the Kotlin side's NativeEngine
    android_overlay.cmake    injects pk_engine.cmake into CrispASR's own CMake build
  app/src/main/java/...      the app (Kotlin, Jetpack Compose)
    engine/EngineService     runs jobs in a separate process with a notification
    audio/AudioDecoder       MediaCodec -> 16 kHz mono float (Resampler)
    audio/RecorderService    microphone -> 16 kHz WAV
    ui/                      the four tabs
  app/src/test/              Kotlin unit tests (gradlew test) and the engine's host test
```

The engine is compiled from the desktop sources, not a copy of them:
`transcribe_cpp/src/pipeline.cpp` (speech to text, shared with
`transcribe_multi`), `llm.cpp` (the corrected copy), and
`translate_cpp/src/translator.cpp`, `formats.cpp`, `docx.cpp` (translation).
CrispASR's built-in llama.cpp runs the translation models, so there is one
ggml library for everything.

Jobs run in the `:engine` process. Cancel asks the engine to stop; when a model
is in the middle of a long step and doesn't stop within 4 seconds, that process
is ended (the app itself is not affected), as the desktop programs end their
child process.

## Build

```bash
./build_apk.sh              # -> dist/TranscribeTranslate.apk
INSTALL=1 ./build_apk.sh    # and install it on the phone (USB debugging on)
COMPATIBLE_CPU=1 ./build_apk.sh   # for phones older than Snapdragon 8 Gen 1 (no i8mm)
```

Needs the Android SDK (Android Studio installs it; `ANDROID_HOME`), git and a
JDK 17+. The script installs the SDK packages it needs (platform 36, NDK r28c,
CMake 3.31) and fetches CrispASR and miniz at the commits pinned by the desktop
builds. The release APK is signed with the debug key so it installs directly
(Settings > Apps > Special access > Install unknown apps, for the app you open
it with). For a Play Store release, use a signing key of your own.

### Host test of the engine (no phone)

```bash
cmake -S ../transcribe_cpp/third_party/CrispASR -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PROJECT_crispasr_INCLUDE=$PWD/app/src/main/cpp/android_overlay.cmake \
      -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DGGML_OPENMP=OFF \
      -DCRISPASR_BUILD_TESTS=OFF -DCRISPASR_BUILD_EXAMPLES=OFF -DCRISPASR_BUILD_SERVER=OFF
cmake --build build-host --target pk_engine_host_test -j 8
./build-host/bin/pk_engine_host_test
# with real models: PK_TEST_MODELS=<folder with the .gguf files> PK_TEST_AUDIO=<16 kHz float32 .raw>
```

## Speed on a Galaxy S23 (Snapdragon 8 Gen 2)

The models run on 4 of the processor's fast cores (Settings > Processor
threads). As a guide, a 2B language model generates about 15 tokens/s and
reads about 70 tokens/s on this phone, so translating a page takes about a
minute with Hy-MT2 1.8B; the corrected copy of a one-hour talk takes roughly as
long as the talk. Long jobs warm the phone; it then slows down a little.

The phone's GPU (OpenCL, Adreno) and NPU (Hexagon) backends of llama.cpp are
not used yet: they are experimental and only speed up some 4-bit model types.
