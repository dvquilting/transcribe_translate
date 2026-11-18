# Enhance Audio (C++ / Qt6, ONNX Runtime)

Speech denoising and enhancement on your own computer, with
[Resemble Enhance](https://github.com/resemble-ai/resemble-enhance) (Resemble AI, MIT
licence) ported to C++. No Python is used at run time: the five networks run in
[ONNX Runtime](https://onnxruntime.ai) on the graphics card through DirectML (any
DirectX 12 GPU: NVIDIA, AMD, Intel), or on the CPU; everything around them (STFT,
mel spectrograms, resampling, the ODE solver, chunking) is C++.

| Program | What it does |
|---|---|
| `enhance_audio_gui.exe` | Qt6 window: pick files, choose the mode, follow the progress, play original and result |
| `enhance_audio.exe` | Command-line program (does the work; the window runs it) |

Two modes, as in Resemble Enhance:

- **Remove noise only** (`--denoise-only`): a UNet separates the voice from noise, music
  and hum and keeps the voice as recorded. About 45x real time on an RTX 5060 Ti.
- **Remove noise and enhance** (default): after the denoiser, a latent conditional flow
  matching model regenerates the voice at 44.1 kHz and a UnivNet vocoder turns it into
  sound. It restores bandwidth and repairs muffled or distorted speech, but it can
  change the voice slightly and invent detail. About 3x real time at full quality (64
  solver evaluations) on an RTX 5060 Ti; the CPU is roughly 10x slower.

The output is mono, 44.1 kHz. Input is anything ffmpeg reads; output is WAV (32-bit
float), FLAC, MP3, M4A, OGG, Opus, or a video with its picture copied and the new sound.

## Build

Needs Visual Studio 2022 or newer (C++ workload), Qt 6 (MinGW kit, with Qt
Multimedia), Git, Python 3.10+ (build time only) and an internet connection once.

```bat
cd C:\Users\David\parakeet\transcribe_translate\enhance_cpp
build_all.bat
```

The script:

1. downloads, at pinned versions, Resemble Enhance's code (commit `8e97814`), ONNX
   Runtime 1.24.4 with DirectML, DirectML 1.15.4 and pocketfft into `third_party\`;
2. sets up a build-time Python environment (`tools\.venv`, PyTorch CPU, about 1 GB,
   versions in `tools\requirements-export.txt`), downloads the Resemble Enhance
   weights (713 MB, into `tools\model_repo\`) and converts them with
   `tools\export_onnx.py` into `build-models\` (1.4 GB of ONNX, float32). Every
   exported network is checked against the PyTorch original;
3. builds the GUI with Qt's MinGW and `enhance_audio.exe` with Visual Studio;
4. runs the tests (`ctest`) and assembles **`dist\`**: both programs, ONNX Runtime and
   DirectML DLLs, Qt, `models\` and `licenses\`.

ffmpeg must be on `PATH` or `ffmpeg.exe` next to the programs (put one in this folder
and the script copies it into `dist\`). MP3 output needs an ffmpeg built with
libmp3lame (most are; conda's is not).

`clean.bat` removes the build folders; `clean.bat all` also the converted models,
downloads and the Python environment.

## Command line

```bat
enhance_audio talk.mp3                              & rem -> talk.enhanced.wav
enhance_audio talk.mp3 --denoise-only -f flac       & rem -> talk.denoised.flac
enhance_audio *.wav --out-dir cleaned --nfe 32      & rem faster, several files
enhance_audio lecture.mp4 -f video                  & rem -> lecture.enhanced.mp4 (picture copied)
enhance_audio talk.mp3 --lambd 0.1 --tau 0.3        & rem a clean but muffled recording
enhance_audio --help
```

| Option | Resemble Enhance | Default |
|---|---|---|
| `--denoise-only` | `denoise()` | off |
| `--lambd` | denoise strength before enhancing | 0.9 (the Gradio app's "denoise" setting) |
| `--tau` | CFM prior temperature | 0.5 |
| `--nfe` | solver function evaluations | 64 |
| `--solver` | midpoint, rk4, euler | midpoint |
| `--chunk-seconds` | chunk length (1 s overlap) | 30 |
| `--seed` | the random numbers (prior and vocoder noise) | 0 |
| `--device` | auto (DirectML GPU, else CPU), gpu, cpu | auto |
| `--gentle` | (not in the original) the GPU rests after each run as long as it took: about half the load | off |
| `--log FILE` / `--no-log` | (not in the original) the run log, see below | a new file in `%LOCALAPPDATA%\EnhanceAudio\logs` |

## How the port works

| Python (resemble_enhance) | Here |
|---|---|
| `inference()`: resample, 30 s chunks, `merge_chunks` with `compute_offset` | `dsp.cpp` (`resample` = torchaudio's Kaiser sinc resampler, `merge_chunks`, `compute_offset`), `enhancer.cpp` |
| `MelSpectrogram` (pre-emphasis, STFT 2048/420, slaney mel, dB) | `dsp.cpp` `enhancer_mel`, the filterbank exported from torchaudio |
| `Denoiser` STFT / iSTFT (1680/420) | `dsp.cpp` `stft` / `istft` (pocketfft) |
| `Denoiser.net` + mask / phase rotation | `denoiser.onnx` |
| `IRMAE.encoder` / `decoder` | `encoder.onnx` / `decoder.onnx` |
| `CFM` velocity field (WaveNet, time embedding) | `velocity.onnx`; the midpoint / RK4 / Euler solver and its time mapping in `enhancer.cpp` |
| `UnivNet` vocoder | `vocoder.onnx` |

Changes that give the same result (checked in `export_onnx.py` against the original
code) so that ONNX Runtime and DirectML can run the networks: the vocoder's
location-variable convolution as MatMuls instead of `unfold`/`einsum`, its transposed
convolutions as zero insertion plus a convolution (DirectML rejects them), fixed channel
counts in its anti-aliasing filters, and explicit padding instead of `padding="same"`
with dilation.

Memory: the vocoder runs in windows of 512 frames (4.9 s) with 24 frames of context on
each side (its receptive field is under 16 frames, so the samples are those of one
run over the whole chunk); in one piece a 30 s chunk needs about 3 GB per vocoder block.
The chunks go through one stage at a time (denoiser, CFM, vocoder) and each stage's
networks are unloaded before the next, so the GPU holds one stage's memory, about 4 GB
at most, not the sum. Each chunk still gets the random numbers it gets in the original
order.

### Tests

- `ctest` (`tests/test_dsp.cpp`): the C++ signal processing against PyTorch /
  torchaudio / Resemble Enhance on data from `tools/make_test_data.py` (resampling,
  mel, STFT/iSTFT, chunk alignment and merging, solver time points, noise stream).
- `tools/reference.py` runs the original PyTorch pipeline with the same random numbers
  as the C++ program; `tools/compare.py` compares two outputs. On a 12 s clip the
  differences were 0.00 dB (denoise only) and 0.05-0.07 dB (enhance) mean over the
  audible spectrum. The waveforms differ more than that, because the generative part
  amplifies float rounding: the original itself moves further when its mel
  spectrogram is nudged by 4e-4.
- `enhance_audio --benchmark` times the enhancement network on this computer.
- `ENHANCE_PROFILE=1` prints the time and GPU memory of each stage; `ENHANCE_LOG_RUNS=1` writes
  every GPU run's length into the run log; `ENHANCE_ORT_VERBOSE=1`
  ONNX Runtime's own log.

Note: Resemble Enhance's `Solver.exponential_decay_mapping` fails with NumPy 2
(`float()` of a 1-element array); the build tools patch it with the same computation.

## Stability: the GPU and the run log

On 17 November 2026 a 65-minute file at full quality froze the development PC twice:
each time the NVIDIA driver timed out (Windows "LiveKernelEvent 141"), the desktop lost
its GPU and the system hung without a blue screen. Measured on an idle RTX 5060 Ti,
no single GPU run of this program comes near Windows' 2 s timeout (CFM evaluation
0.19 s at most, vocoder window 0.27 s, denoiser 0.13 s plus about 0.8 s the first time
a network is prepared), so the cause is open: another program loading the GPU
at the same time, the long full load (150-160 W, up to 77 C), or the hardware or driver.
What the program does about it:

- one Direct3D 12 device and one compute queue for all networks (not a device per
  network and per group of chunks), so the desktop's own GPU queue is left alone;
- before each stage it checks the GPU memory other programs leave free, warns when it
  is short, and uses shorter pieces (down to 10 s) when the denoiser would not fit;
- `--gentle` (the window: *Go easy on the graphics card*) halves the GPU load;
- a driver reset during a run is reported as such ("device removed"), with what to try;
- **the run log**: every run writes `%LOCALAPPDATA%\EnhanceAudio\logs\enhance-DATE-TIME.log`
  (the last 20 are kept). Once a second it records what is running (stage, chunk, the
  network and how long its current GPU run has taken), the GPU's temperature, power,
  clock, load, memory, fan and any throttling (NVIDIA cards, through `nvml.dll`; other
  cards: memory only), and GPU runs of 1 s or more as they end. Every line is forced to
  disk, so the last second before a freeze is kept. A log without its final `end` line
  belongs to a run that did not finish; the next run prints how it ended.

## Layout

```
CMakeLists.txt
build_all.bat, clean.bat
src/engine/dsp.*        signal processing (STFT, mel, resampling, chunk merging, noise)
src/engine/models.*     the ONNX networks in ONNX Runtime (DirectML / CPU), loaded per stage
src/engine/monitor.*    the run log (GPU state once a second, flushed to disk)
src/engine/enhancer.*   the inference pipeline
src/engine/cli_main.cpp enhance_audio.exe (decoding and encoding via ffmpeg)
src/gui/                enhance_audio_gui.exe (Qt6 Widgets + Multimedia)
tests/test_dsp.cpp      unit tests (no models needed, test data from the tools)
tools/                  build-time Python: export_onnx.py, re_model.py, reference.py,
                        compare.py, make_test_data.py, requirements-export.txt
```

Shared code: `../common` (file writing, UTF-8 paths, running the tool from the GUI, the
Help window) and `../transcribe_cpp/src/audio.cpp` (ffmpeg decoding).
