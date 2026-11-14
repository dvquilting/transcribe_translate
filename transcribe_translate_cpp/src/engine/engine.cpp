#include "engine.h"

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <stdexcept>
#include <thread>

#include "crispasr_api.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace tmx {

PuncModel::PuncModel(const std::string& model_path) {
    ctx_ = crispasr().crispasr_punc_init(model_path.c_str());
    if (!ctx_) throw std::runtime_error("could not load the punctuation model '" + model_path + "'");
}

PuncModel::~PuncModel() {
    if (ctx_) crispasr().crispasr_punc_free(ctx_);
}

std::string PuncModel::restore(const std::string& text) {
    const std::string plain = strip_punctuation(text);
    if (plain.empty()) return text;
    const char* out = crispasr().crispasr_punc_process(ctx_, plain.c_str());
    if (!out) return text;
    std::string r = normalize_space(out);
    crispasr().crispasr_punc_free_text(out);
    // Sanity check: the model only adds punctuation and changes case, so the
    // words must be the same (same_words: apostrophes, hyphens and the symbols
    // inside words count); otherwise keep what the recognizer wrote.
    if (!same_words(r, plain)) return text;
    return r;
}

std::string ensure_model(const ModelInfo& m, const std::string& models_dir) {
    char path[4096];
    const int rc = crispasr().crispasr_cache_ensure_file_abi(m.file.c_str(), m.url.c_str(), /*quiet=*/0,
                                                  models_dir.empty() ? nullptr : models_dir.c_str(), path,
                                                  (int32_t)sizeof(path));
    if (rc != 0) throw std::runtime_error("could not download " + m.display + " from " + m.url);
    return path;
}

namespace {

#if defined(_WIN32) && defined(TMX_CUBLAS_DLL)
bool cublas_available() {
    // The .exe delay-loads cuBLAS; check it can actually be found before letting CUDA run.
    return LoadLibraryA(TMX_CUBLAS_DLL) != nullptr && LoadLibraryA(TMX_CUBLASLT_DLL) != nullptr;
}
#define TMX_CHECK_CUBLAS 1
#endif

}  // namespace

std::string choose_device(const std::string& requested, std::string* note) {
    std::string dev = requested.empty() ? "auto" : requested;
    if (note) note->clear();
#ifdef TMX_CHECK_CUBLAS
    if ((dev == "auto" || dev == "cuda") && !cublas_available()) {
        if (dev == "cuda")
            throw std::runtime_error(std::string("CUDA needs ") + TMX_CUBLAS_DLL + " and " + TMX_CUBLASLT_DLL +
                                     " next to tt_engine.exe (or use --device vulkan)");
        if (note) *note = std::string(TMX_CUBLAS_DLL) + " not found, so using Vulkan instead of CUDA";
        dev = "vulkan";
    }
#endif
    if (dev == "vulkan" || dev == "cpu") {
        // Hide CUDA devices from this process so the language model (llama.cpp
        // inside CrispASR, which picks GPUs on its own) can't select CUDA either.
        // Must happen before anything initialises CUDA.
#ifdef _WIN32
        _putenv_s("CUDA_VISIBLE_DEVICES", "-1");
#else
        setenv("CUDA_VISIBLE_DEVICES", "-1", 1);
#endif
    }
    return dev;
}

std::vector<float> load_audio_builtin(const std::string& path) {
    float* pcm = nullptr;
    int n = 0, sr = 0;
    const int rc = crispasr().crispasr_audio_load(path.c_str(), &pcm, &n, &sr);
    if (rc != 0 || !pcm || n <= 0) {
        if (pcm) crispasr().crispasr_audio_free(pcm);
        throw std::runtime_error("could not decode '" + path +
                                 "' with the built-in decoder; install ffmpeg (on PATH) for video and other formats");
    }
    std::vector<float> out(pcm, pcm + n);
    crispasr().crispasr_audio_free(pcm);
    if (sr != 16000) throw std::runtime_error("built-in decoder returned unexpected sample rate");
    return out;
}

namespace {

bool starts_with_ci(const char* s, const std::string& prefix) {
    if (!s) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (!s[i] || std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)prefix[i])) return false;
    }
    return true;
}

constexpr int kDevCpu = 0, kDevGpu = 1, kDevIgpu = 2;

}  // namespace

EngineInfo active_engine(const std::string& device) {
    EngineInfo info;
    const GgmlApi* g = ggml_api();
    if (!g) return info;

    ggml_backend_dev_t chosen = nullptr;
    auto first_of_type = [&](int type) -> ggml_backend_dev_t {
        for (size_t i = 0; i < g->ggml_backend_dev_count(); ++i) {
            ggml_backend_dev_t d = g->ggml_backend_dev_get(i);
            if (g->ggml_backend_dev_type(d) == type) return d;
        }
        return nullptr;
    };

    if (device == "cpu") {
        chosen = first_of_type(kDevCpu);
    } else {
        if (!device.empty() && device != "auto") {
            for (size_t i = 0; i < g->ggml_backend_dev_count() && !chosen; ++i) {
                ggml_backend_dev_t d = g->ggml_backend_dev_get(i);
                const int t = g->ggml_backend_dev_type(d);
                if ((t == kDevGpu || t == kDevIgpu) && starts_with_ci(g->ggml_backend_dev_name(d), device)) chosen = d;
            }
        }
        if (!chosen) chosen = first_of_type(kDevGpu);
        if (!chosen) chosen = first_of_type(kDevIgpu);
        if (!chosen) chosen = first_of_type(kDevCpu);
    }
    if (!chosen) return info;

    info.known = true;
    const char* reg = g->ggml_backend_reg_name(g->ggml_backend_dev_backend_reg(chosen));
    info.backend = reg && *reg ? reg : (g->ggml_backend_dev_name(chosen) ? g->ggml_backend_dev_name(chosen) : "?");
    const char* desc = g->ggml_backend_dev_description(chosen);
    info.device = desc ? normalize_space(desc) : "";
    if (g->ggml_backend_dev_type(chosen) == kDevGpu) {
        size_t free_b = 0, total_b = 0;
        g->ggml_backend_dev_memory(chosen, &free_b, &total_b);
        info.memory_gb = (double)total_b / (1024.0 * 1024.0 * 1024.0);
    }
    return info;
}

std::string describe_engine(const EngineInfo& e) {
    if (!e.known) return "unknown";
    std::string s = e.backend;
    std::string extra = e.device;
    if (e.memory_gb > 0.05) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f GB", e.memory_gb);
        extra += (extra.empty() ? "" : ", ") + std::string(buf);
    }
    if (!extra.empty()) s += " (" + extra + ")";
    return s;
}

int default_threads() {
    const unsigned n = std::thread::hardware_concurrency();
    return n ? (int)std::min(n, 16u) : 4;
}

AsrSession::AsrSession(const std::string& model_path, const std::string& backend, const EngineOptions& opts) {
    const bool cpu = opts.device == "cpu";
    // Set on every open (an earlier session may have chosen another device).
    // CrispASR treats "" as automatic; "auto" would be looked up as a device name.
    crispasr().crispasr_set_gpu_backend(opts.device == "auto" ? "" : opts.device.c_str());

    crispasr_open_params_v1 p{};
    p.abi_version = 2;
    p.n_threads = opts.threads > 0 ? opts.threads : default_threads();
    p.use_gpu = cpu ? 0 : 1;
    p.verbosity = 0;
    p.flash_attn = 1;
    p.n_gpu_layers = -1;
    s_ = crispasr().crispasr_session_open_with_params(model_path.c_str(), backend.c_str(), &p);
    if (!s_) throw std::runtime_error("failed to load model '" + model_path + "' with backend '" + backend + "'");
}

AsrSession::~AsrSession() {
    if (s_) crispasr().crispasr_session_close(s_);
}

void AsrSession::progress_trampoline(int processed, int total, void* user) {
    auto* self = static_cast<AsrSession*>(user);
    if (self && self->progress_) self->progress_(processed, total);
}

static std::vector<Segment> collect(crispasr_session_result* r, double offset) {
    std::vector<Segment> out;
    if (!r) return out;
    const int ns = crispasr().crispasr_session_result_n_segments(r);
    for (int i = 0; i < ns; ++i) {
        Segment seg;
        const char* t = crispasr().crispasr_session_result_segment_text(r, i);
        seg.text = space_before_numbers(normalize_space(t ? t : ""));
        seg.start = offset + crispasr().crispasr_session_result_segment_t0(r, i) / 100.0;
        seg.end = offset + crispasr().crispasr_session_result_segment_t1(r, i) / 100.0;
        const int nw = crispasr().crispasr_session_result_n_words(r, i);
        for (int j = 0; j < nw; ++j) {
            const char* wt = crispasr().crispasr_session_result_word_text(r, i, j);
            Word w;
            w.text = normalize_space(wt ? wt : "");
            if (w.text.empty()) continue;
            w.start = offset + crispasr().crispasr_session_result_word_t0(r, i, j) / 100.0;
            w.end = offset + crispasr().crispasr_session_result_word_t1(r, i, j) / 100.0;
            // "November2024" is two words, as in the segment's text: each gets
            // its share of the time, by its length.
            const std::string spaced = space_before_numbers(w.text);
            const size_t cut = spaced.size() != w.text.size() ? spaced.find(' ') : std::string::npos;
            if (cut != std::string::npos && cut > 0 && spaced.find(' ', cut + 1) == std::string::npos) {
                const double mid = w.start + (w.end - w.start) * (double)cut / (double)(spaced.size() - 1);
                seg.words.push_back({spaced.substr(0, cut), w.start, mid});
                seg.words.push_back({spaced.substr(cut + 1), mid, w.end});
                continue;
            }
            seg.words.push_back(std::move(w));
        }
        if (!seg.text.empty() || !seg.words.empty()) out.push_back(std::move(seg));
    }
    crispasr().crispasr_session_result_free(r);
    return out;
}

std::vector<Segment> AsrSession::transcribe_long(const std::vector<float>& pcm, const char* language,
                                                 ProgressFn progress) {
    progress_ = std::move(progress);
    crispasr().crispasr_session_set_progress_callback(s_, &AsrSession::progress_trampoline, this);
    // chunk_seconds = 0 / overlap = -1: CrispASR's per-model long-form defaults.
    crispasr_session_result* r =
        crispasr().crispasr_session_transcribe_chunked_lang(s_, pcm.data(), (int)pcm.size(), 0, -1, language);
    crispasr().crispasr_session_set_progress_callback(s_, nullptr, nullptr);
    progress_ = nullptr;
    if (!r) throw std::runtime_error("transcription failed");
    return collect(r, 0.0);
}

void AsrSession::set_instruction(const std::string& instruction) {
    crispasr().crispasr_session_set_ask(s_, instruction.c_str());
}

std::string language_name(const std::string& code) {
    static const std::pair<const char*, const char*> names[] = {
        {"en", "English"}, {"de", "German"},   {"fr", "French"},     {"es", "Spanish"},  {"it", "Italian"},
        {"pt", "Portuguese"}, {"nl", "Dutch"}, {"pl", "Polish"},     {"ro", "Romanian"}, {"hu", "Hungarian"},
        {"cs", "Czech"},   {"fi", "Finnish"},  {"hr", "Croatian"},   {"sk", "Slovak"},   {"sl", "Slovenian"},
        {"et", "Estonian"}, {"lt", "Lithuanian"}, {"el", "Greek"},   {"ar", "Arabic"},   {"vi", "Vietnamese"},
        {"ru", "Russian"}, {"zh", "Chinese"},  {"ja", "Japanese"},   {"ko", "Korean"}};
    for (const auto& [c, n] : names)
        if (code == c) return n;
    return code;
}

std::vector<Segment> AsrSession::transcribe_chunk(const float* pcm, size_t n, double offset, const char* language) {
    crispasr_session_result* r = crispasr().crispasr_session_transcribe_lang(s_, pcm, (int)n, language);
    if (!r) throw std::runtime_error("transcription failed");
    return collect(r, offset);
}

bool free_aligner() {
    const auto& opt = crispasr_optional();
    if (!opt.crispasr_align_free_cache_abi) return false;
    opt.crispasr_align_free_cache_abi();
    return true;
}

std::vector<Word> align_words(const std::string& aligner_path, const std::string& text, const float* pcm, size_t n,
                              double offset, int threads) {
    std::vector<Word> out;
    if (text.empty() || n == 0) return out;
    crispasr_align_result* r = crispasr().crispasr_align_words_abi(aligner_path.c_str(), text.c_str(), pcm, (int32_t)n,
                                                        (int64_t)(offset * 100.0 + 0.5),
                                                        threads > 0 ? threads : default_threads());
    if (!r) return out;
    const int nw = crispasr().crispasr_align_result_n_words(r);
    for (int i = 0; i < nw; ++i) {
        Word w;
        const char* t = crispasr().crispasr_align_result_word_text(r, i);
        w.text = t ? t : "";
        w.start = crispasr().crispasr_align_result_word_t0(r, i) / 100.0;
        w.end = crispasr().crispasr_align_result_word_t1(r, i) / 100.0;
        if (!w.text.empty()) out.push_back(std::move(w));
    }
    crispasr().crispasr_align_result_free(r);
    return out;
}

}  // namespace tmx
