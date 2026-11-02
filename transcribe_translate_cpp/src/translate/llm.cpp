#include "llm.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <type_traits>

#ifndef TL_NO_GPU_FIT  // the Android app runs on the processor and has no llama.cpp common library
#include "fit.h"  // llama.cpp common: fit the model + context into free GPU memory
#endif
#include "ggml-backend.h"
#include "llama.h"
#include "text.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace tl {

int default_threads() {
    const unsigned n = std::thread::hardware_concurrency();
    // Token generation is memory-bound; beyond ~8 threads there is little gain.
    return n ? (int)std::clamp(n / 2, 1u, 8u) : 4;
}

std::string describe_engine(const EngineInfo& e) {
    std::string extra = e.device;
    if (e.memory_gb > 0.05) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f GB", e.memory_gb);
        extra += (extra.empty() ? "" : ", ") + std::string(buf);
    }
    std::string s = e.backend;
    if (!extra.empty()) s += " (" + extra + ")";
    return s;
}

namespace {

bool g_verbose = false;

void log_callback(ggml_log_level level, const char* text, void*) {
    if (g_verbose || level == GGML_LOG_LEVEL_ERROR) std::fputs(text, stderr);
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string reg_name(ggml_backend_dev_t d) {
    const char* n = ggml_backend_reg_name(ggml_backend_dev_backend_reg(d));
    return n ? n : "";
}

#if defined(_WIN32) && defined(TL_CUBLAS_DLL)
// The .exe delay-loads cuBLAS (Windows has no static cuBLAS); CUDA can only be
// used when the DLLs are next to the program or on PATH.
bool cublas_available() {
    return LoadLibraryA(TL_CUBLAS_DLL) != nullptr && LoadLibraryA(TL_CUBLASLT_DLL) != nullptr;
}
#else
bool cublas_available() { return true; }
#endif

// GPUs of one backend ("CUDA" / "Vulkan"), discrete before integrated.
std::vector<ggml_backend_dev_t> gpus_of(const std::string& backend) {
    std::vector<ggml_backend_dev_t> discrete, integrated;
    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        ggml_backend_dev_t d = ggml_backend_dev_get(i);
        if (lower(reg_name(d)) != lower(backend)) continue;
        const auto t = ggml_backend_dev_type(d);
        if (t == GGML_BACKEND_DEVICE_TYPE_GPU) discrete.push_back(d);
        else if (t == GGML_BACKEND_DEVICE_TYPE_IGPU) integrated.push_back(d);
    }
    return discrete.empty() ? integrated : discrete;
}

// Choose the GPUs to use. Both the CUDA and the Vulkan backend see an NVIDIA
// card, so an explicit list is needed or llama.cpp would use the card twice.
std::vector<ggml_backend_dev_t> choose_devices(const std::string& requested, std::string& backend,
                                               std::string* note) {
    const std::string want = lower(requested.empty() ? "auto" : requested);
    if (want == "cpu") {
        backend = "CPU";
        return {};
    }
    if (want == "cuda" || want == "auto") {
        auto cuda = gpus_of("CUDA");
        if (!cuda.empty() && cublas_available()) {
            backend = "CUDA";
            return cuda;
        }
        if (want == "cuda") {
            if (cuda.empty()) throw std::runtime_error("no CUDA graphics card found (try --device vulkan)");
#ifdef TL_CUBLAS_DLL
            throw std::runtime_error(std::string("CUDA needs ") + TL_CUBLAS_DLL + " and " + TL_CUBLASLT_DLL +
                                     " next to the program (or use --device vulkan)");
#endif
        }
        if (!cuda.empty() && note) *note = "the cuBLAS DLLs were not found, so using Vulkan instead of CUDA";
    }
    if (want == "vulkan" || want == "auto" || want == "cuda") {
        auto vk = gpus_of("Vulkan");
        if (!vk.empty()) {
            backend = "Vulkan";
            return vk;
        }
        if (want == "vulkan") throw std::runtime_error("no Vulkan graphics card found (try --device cpu)");
    }
    if (want != "auto" && want != "cuda" && want != "vulkan")
        throw std::runtime_error("unknown device '" + requested + "' (use auto, cuda, vulkan or cpu)");
    if (note && note->empty()) *note = "no supported graphics card found; running on the CPU (slow)";
    backend = "CPU";
    return {};
}

std::string cpu_description() {
    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        ggml_backend_dev_t d = ggml_backend_dev_get(i);
        if (ggml_backend_dev_type(d) == GGML_BACKEND_DEVICE_TYPE_CPU) {
            const char* s = ggml_backend_dev_description(d);
            return s ? s : "";
        }
    }
    return "";
}

}  // namespace

Llm::Llm(const std::string& model_path, const EngineOptions& opts, std::string* note) {
    g_verbose = opts.verbose;
    llama_log_set(log_callback, nullptr);
    llama_backend_init();

    std::string backend;
    std::vector<ggml_backend_dev_t> devs = choose_devices(opts.device, backend, note);
    std::vector<ggml_backend_dev_t> dev_list = devs;
    dev_list.push_back(nullptr);

    llama_model_params mp = llama_model_default_params();
    mp.devices = dev_list.data();
    if (devs.empty()) mp.n_gpu_layers = 0;

    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = (uint32_t)opts.n_ctx;
    cp.n_batch = (uint32_t)opts.n_ctx;  // a whole prompt fits in one llama_decode call
    cp.n_ubatch = 512;
    const int threads = opts.threads > 0 ? opts.threads : default_threads();
    cp.n_threads = threads;
    cp.n_threads_batch = devs.empty() ? (int)std::max(1u, std::thread::hardware_concurrency()) : threads;
    cp.no_perf = true;

    // Fit the weights + KV cache into free GPU memory: when the model is too big,
    // keep some layers (dense models) or expert weights (MoE models) on the CPU.
    std::vector<float> tensor_split(llama_max_devices(), 0.0f);
    std::vector<llama_model_tensor_buft_override> overrides(llama_max_tensor_buft_overrides(), {nullptr, nullptr});
#ifndef TL_NO_GPU_FIT
    if (!devs.empty()) {
        std::vector<size_t> margins(llama_max_devices(), (size_t)768 * 1024 * 1024);  // keep 768 MB free
        const auto st = common_fit_params(model_path.c_str(), &mp, &cp, tensor_split.data(), overrides.data(),
                                          margins.data(), cp.n_ctx, nullptr,
                                          opts.verbose ? GGML_LOG_LEVEL_DEBUG : GGML_LOG_LEVEL_ERROR);
        if (st == COMMON_PARAMS_FIT_STATUS_ERROR) throw std::runtime_error("could not read model file " + model_path);
    }
#endif

    model_ = llama_model_load_from_file(model_path.c_str(), mp);
    if (!model_) throw std::runtime_error("could not load the model '" + model_path + "'");

    ctx_ = llama_init_from_model(model_, cp);
    if (!ctx_) {
        llama_model_free(model_);
        model_ = nullptr;
        throw std::runtime_error("could not create a context of " + std::to_string(opts.n_ctx) +
                                 " tokens (not enough memory?)");
    }
    n_ctx_ = (int)llama_n_ctx(ctx_);
    llama_set_abort_callback(ctx_, [](void* p) { return abort_trampoline(p); }, this);

    // Describe where the model runs.
    info_.backend = backend;
    if (devs.empty()) {
        info_.device = cpu_description();
        info_.placement = "on the CPU";
    } else {
        const char* desc = ggml_backend_dev_description(devs[0]);
        info_.device = desc ? desc : "";
        size_t free_b = 0, total_b = 0;
        ggml_backend_dev_memory(devs[0], &free_b, &total_b);
        info_.memory_gb = (double)total_b / (1024.0 * 1024.0 * 1024.0);
        if (devs.size() > 1) info_.device += " + " + std::to_string(devs.size() - 1) + " more";
        const int n_layer = llama_model_n_layer(model_);
        int cpu_overrides = 0;
        for (const auto& o : overrides)
            if (o.pattern) ++cpu_overrides;
        if (cpu_overrides > 0)
            info_.placement = "partly on the GPU; some expert weights on the CPU";
        else if (mp.n_gpu_layers >= 0 && mp.n_gpu_layers <= n_layer)
            info_.placement = std::to_string(mp.n_gpu_layers) + " of " + std::to_string(n_layer + 1) +
                              " layers on the GPU, the rest on the CPU";
        else
            info_.placement = "all on the GPU";
    }
}

Llm::~Llm() {
    if (ctx_) llama_free(ctx_);
    if (model_) llama_model_free(model_);
}

bool Llm::abort_trampoline(void* self) {
    const auto* l = static_cast<const Llm*>(self);
    return l->cancel_ && l->cancel_->load();
}

int Llm::count_tokens(const std::string& text) const {
    const llama_vocab* vocab = llama_model_get_vocab(model_);
    const int n = -llama_tokenize(vocab, text.c_str(), (int32_t)text.size(), nullptr, 0, false, false);
    return std::max(0, n);
}

namespace {

std::vector<llama_token> tokenize(const llama_vocab* vocab, const std::string& s, bool parse_special) {
    std::vector<llama_token> toks(s.size() + 8);
    int n = llama_tokenize(vocab, s.c_str(), (int32_t)s.size(), toks.data(), (int32_t)toks.size(), false,
                           parse_special);
    if (n < 0) {
        toks.resize(-n);
        n = llama_tokenize(vocab, s.c_str(), (int32_t)s.size(), toks.data(), (int32_t)toks.size(), false,
                           parse_special);
    }
    toks.resize(std::max(0, n));
    return toks;
}

std::vector<llama_token> prompt_tokens(const llama_vocab* vocab, const Prompt& prompt, bool add_bos) {
    // Adjacent plain pieces are tokenized together: the template's own text
    // ("user\n") and the text to translate then tokenize exactly as one string,
    // as they did when the whole prompt was a single string split at the
    // control tokens. Only the control-token pieces are parsed as special.
    std::vector<llama_token> toks;
    for (size_t i = 0; i < prompt.pieces.size();) {
        const bool special = prompt.pieces[i].special;
        std::string run;
        for (; i < prompt.pieces.size() && prompt.pieces[i].special == special; ++i) run += prompt.pieces[i].text;
        const auto t = tokenize(vocab, run, special);
        toks.insert(toks.end(), t.begin(), t.end());
    }
    const llama_token bos = llama_vocab_bos(vocab);
    if (add_bos && bos != LLAMA_TOKEN_NULL && (toks.empty() || toks[0] != bos)) toks.insert(toks.begin(), bos);

    return toks;
}

}  // namespace

int Llm::count_prompt_tokens(const Prompt& prompt, bool add_bos) const {
    return (int)prompt_tokens(llama_model_get_vocab(model_), prompt, add_bos).size();
}

static_assert(std::is_same<llama_token, int>::value, "prompt tokens are passed around as std::vector<int>");

std::vector<int> Llm::tokenize_prompt(const Prompt& prompt, bool add_bos) const {
    return prompt_tokens(llama_model_get_vocab(model_), prompt, add_bos);
}

Generation Llm::generate(const Prompt& prompt, bool add_bos, int max_tokens, const Sampling& s,
                         int allowed_repeats, const std::vector<int>* given_tokens) {
    const llama_vocab* vocab = llama_model_get_vocab(model_);
    std::vector<llama_token> own;
    if (!given_tokens) own = prompt_tokens(vocab, prompt, add_bos);
    const std::vector<llama_token>& toks = given_tokens ? *given_tokens : own;

    Generation g;
    g.prompt_tokens = (int)toks.size();
    if (g.prompt_tokens + 16 > n_ctx_)
        throw std::runtime_error("text piece too long for the model (" + std::to_string(g.prompt_tokens) +
                                 " tokens, context " + std::to_string(n_ctx_) + ")");
    max_tokens = std::min(max_tokens, n_ctx_ - g.prompt_tokens - 1);

    // A retry runs the same prompt again: keep it in the KV cache, drop only
    // what the last call generated, and process the last prompt token again
    // (its logits were overwritten). Caches that can't drop a tail (recurrent
    // layers) or that no longer hold the whole prompt (SWA) start afresh.
    llama_memory_t mem = llama_get_memory(ctx_);
    const size_t keep = reusable_prompt_tokens(kv_prompt_, toks);
    kv_prompt_.clear();  // until the prompt is in the cache again
    size_t done = 0;
    if (keep > 0 && llama_memory_seq_rm(mem, 0, (llama_pos)keep, -1) && llama_memory_seq_pos_min(mem, 0) == 0 &&
        llama_memory_seq_pos_max(mem, 0) == (llama_pos)keep - 1)
        done = keep;
    else
        llama_memory_clear(mem, true);
    // (llama_decode doesn't write to the tokens.)
    llama_token* rest = const_cast<llama_token*>(toks.data()) + done;
    if (llama_decode(ctx_, llama_batch_get_one(rest, (int32_t)(toks.size() - done))) != 0) {
        if (cancel_ && cancel_->load()) throw std::runtime_error("cancelled");
        throw std::runtime_error("the model failed to process the text");
    }
    kv_prompt_ = toks;

    llama_sampler_chain_params sp = llama_sampler_chain_default_params();
    sp.no_perf = true;
    llama_sampler* chain = llama_sampler_chain_init(sp);
#ifdef TL_CRISPASR_LLAMA  // the Android app: CrispASR's built-in llama.cpp, whose penalties take no vocabulary size
    if (s.repeat_penalty != 1.0f) llama_sampler_chain_add(chain, llama_sampler_init_penalties(64, s.repeat_penalty, 0, 0));
#else
    if (s.repeat_penalty != 1.0f) llama_sampler_chain_add(chain, llama_sampler_init_penalties(llama_vocab_n_tokens(vocab), 64, s.repeat_penalty, 0, 0));
#endif
    if (s.temperature <= 0.0f) {
        llama_sampler_chain_add(chain, llama_sampler_init_greedy());
    } else {
        if (s.top_k > 0) llama_sampler_chain_add(chain, llama_sampler_init_top_k(s.top_k));
        if (s.top_p < 1.0f) llama_sampler_chain_add(chain, llama_sampler_init_top_p(s.top_p, 1));
        llama_sampler_chain_add(chain, llama_sampler_init_temp(s.temperature));
        llama_sampler_chain_add(chain, llama_sampler_init_dist(s.seed));
    }

    std::vector<int> out;
    try {
        for (int i = 0; i < max_tokens; ++i) {
            if (cancel_ && cancel_->load()) throw std::runtime_error("cancelled");
            llama_token t = llama_sampler_sample(chain, ctx_, -1);
            if (llama_vocab_is_eog(vocab, t)) break;
            out.push_back(t);
            if (out.size() % 16 == 0) {
                if (const size_t drop = trailing_loop_length(out, allowed_repeats)) {
                    out.resize(out.size() - drop);
                    g.looped = true;
                    break;
                }
            }
            if ((int)out.size() >= max_tokens) {
                g.truncated = true;
                break;
            }
            if (llama_decode(ctx_, llama_batch_get_one(&t, 1)) != 0) {
                if (cancel_ && cancel_->load()) throw std::runtime_error("cancelled");
                throw std::runtime_error("the model failed while writing the translation");
            }
        }
    } catch (...) {
        llama_sampler_free(chain);
        kv_prompt_.clear();  // a failed or cancelled decode: don't trust the cache
        throw;
    }
    llama_sampler_free(chain);
    if (!g.looped && !out.empty()) {
        if (const size_t drop = trailing_loop_length(out, allowed_repeats)) {
            out.resize(out.size() - drop);
            g.looped = true;
        }
    }

    g.generated_tokens = (int)out.size();
    std::string text;
    char buf[256];
    for (int t : out) {
        int m = llama_token_to_piece(vocab, t, buf, sizeof(buf), 0, false);
        if (m < 0) {
            std::string big(-m, '\0');
            m = llama_token_to_piece(vocab, t, big.data(), (int32_t)big.size(), 0, false);
            text.append(big.data(), std::max(0, m));
        } else {
            text.append(buf, m);
        }
    }
    // Cutting at max_tokens or at a loop can stop in the middle of a character
    // (byte-level tokens): drop the fragment rather than write broken UTF-8.
    g.text = sanitize_utf8(text);
    return g;
}

}  // namespace tl
