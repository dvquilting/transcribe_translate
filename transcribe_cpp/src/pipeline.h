// pipeline.h - speech to text once the audio is decoded: the speech model,
// punctuation restoration and word timings, giving the text, the subtitle cues
// and the word timings. Shared by the command-line program (cli_main.cpp) and
// the Android app (android/app/src/main/cpp), which differ only in how they
// decode audio, find model files and report progress.
#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine.h"
#include "transcript.h"

namespace tmx {

struct PipelineOptions {
    const ModelInfo* model = nullptr;  // the speech model (required)
    std::string model_path;            // its local file
    std::string language;              // ISO code; empty = detect (models that can)
    EngineOptions engine;
    std::string punctuation = "auto";  // PCS punctuation restoration: auto | on | off
    bool ark_punctuation = false;      // ask ARK itself to write punctuation (experimental)
    double chunk_seconds = 30.0;       // chunk length for the chunked models
    bool want_cues = true;             // subtitle cues
    bool align = true;                 // word timings for the chunked models' cues (forced aligner)
    const ModelInfo* aligner = nullptr;  // the forced aligner (nullptr: aligner_model())
    bool more_models_follow = false;   // a language model runs afterwards (memory note)
};

struct PipelineHooks {
    std::function<void(const std::string&)> say;  // a line for the log / status
    // "progress: <phase> <done>/<total> <unit>" for the command line.
    std::function<void(const char* phase, long long done, long long total, const char* unit)> progress;
    // The local file of a helper model (punctuation, aligner), downloaded if needed. Throws on failure.
    std::function<std::string(const ModelInfo&)> model_file;
    // Asked between chunks; true stops the run with PipelineCancelled.
    std::function<bool()> cancelled;
};

struct PipelineCancelled : std::runtime_error {
    PipelineCancelled() : std::runtime_error("cancelled") {}
};

struct PipelineResult {
    std::string text;           // the whole transcript, spaces normalized (empty: no speech)
    std::vector<Segment> cues;  // subtitle cues (when want_cues)
    std::vector<Word> words;    // word timings, when there are any
};

// `wav` is 16 kHz mono; it is released as soon as it is no longer needed.
// Throws std::runtime_error on failure (and PipelineCancelled).
PipelineResult transcribe_audio(std::vector<float> wav, const PipelineOptions& opts, const PipelineHooks& hooks);

}  // namespace tmx
