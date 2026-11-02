// engine.h - thin RAII wrapper over the CrispASR session ABI plus the model table.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "model_catalog.h"
#include "transcript.h"

struct crispasr_session;

namespace tmx {

// The model table is shared with Speech Translate's menus (../common/src/model_catalog.h).
using ModelInfo = pk::AsrModel;
inline const std::vector<ModelInfo>& model_table() { return pk::asr_models(); }
inline const ModelInfo* find_model(const std::string& key) { return pk::find_asr_model(key); }
using pk::aligner_model;
using pk::punctuation_model;  // PCS: punctuation + capitals + sentence breaks

// Return a local path for the model, downloading it into the cache on first use.
// `models_dir` empty = CrispASR's default cache (%USERPROFILE%\.cache\crispasr).
std::string ensure_model(const ModelInfo& m, const std::string& models_dir);

struct EngineOptions {
    std::string device = "auto";  // auto | cuda | vulkan | cpu
    int threads = 0;               // 0 = hardware concurrency
};

using ProgressFn = std::function<void(int done, int total)>;

class AsrSession {
public:
    AsrSession(const std::string& model_path, const std::string& backend, const EngineOptions& opts);
    ~AsrSession();
    AsrSession(const AsrSession&) = delete;
    AsrSession& operator=(const AsrSession&) = delete;

    // Long-form transcription of a whole file (Parakeet). `progress` receives
    // processed/total samples. Language may be nullptr for auto.
    std::vector<Segment> transcribe_long(const std::vector<float>& pcm, const char* language, ProgressFn progress);

    // Replace the model's transcription instruction (audio-LLM backends such as
    // ARK). Empty = the backend's default instruction.
    void set_instruction(const std::string& instruction);

    // One short chunk; timestamps are shifted by `offset` seconds.
    std::vector<Segment> transcribe_chunk(const float* pcm, size_t n, double offset, const char* language);

private:
    crispasr_session* s_ = nullptr;
    ProgressFn progress_;
    static void progress_trampoline(int processed, int total, void* user);
};

// Punctuation restoration with the PCS model (XLM-R, 47 languages). Expects
// plain lowercase text; restore() prepares the input itself.
class PuncModel {
public:
    explicit PuncModel(const std::string& model_path);
    ~PuncModel();
    PuncModel(const PuncModel&) = delete;
    PuncModel& operator=(const PuncModel&) = delete;
    // Returns the text with punctuation and capitals restored (unchanged on failure).
    std::string restore(const std::string& text);

private:
    void* ctx_ = nullptr;
};

// Word timings for `text` spoken in `pcm` (offset seconds added) using the
// Qwen3 forced aligner GGUF at `aligner_path`. Empty on failure.
// Free the forced-aligner model CrispASR keeps loaded after align_words.
// Returns false if this CrispASR build can't (it is then freed at exit).
bool free_aligner();

std::vector<Word> align_words(const std::string& aligner_path, const std::string& text, const float* pcm, size_t n,
                              double offset, int threads);

int default_threads();

// English name of a language code ("fr" -> "French"); the code itself if unknown.
std::string language_name(const std::string& code);

// Resolve --device. "auto" stays "auto" (CrispASR picks the best GPU: CUDA, then
// Vulkan) except in a static CUDA build whose cuBLAS DLLs are missing, where it
// becomes "vulkan". `note` receives a human-readable explanation (may be empty).
// Throws if "cuda" is requested but cannot work.
std::string choose_device(const std::string& requested, std::string* note);

// The compute device CrispASR uses for a given --device choice, determined from
// ggml's device registry with the same rules CrispASR applies (preferred GPU by
// name, otherwise the first discrete GPU, then integrated GPU, then CPU).
// Call after an AsrSession is open, so dynamically loaded GPU backends are registered.
struct EngineInfo {
    bool known = false;
    std::string backend;  // "CUDA", "Vulkan", "CPU", ...
    std::string device;   // e.g. "NVIDIA GeForce RTX 5060 Ti"
    double memory_gb = 0; // dedicated memory of a GPU (0 if n/a)
};
EngineInfo active_engine(const std::string& device);

// "CUDA (NVIDIA GeForce RTX 5060 Ti, 16.0 GB)"
std::string describe_engine(const EngineInfo& e);

// Decode with CrispASR's built-in decoder (WAV/MP3/FLAC/OGG/Opus/AAC/M4A...).
std::vector<float> load_audio_builtin(const std::string& path);

}  // namespace tmx
