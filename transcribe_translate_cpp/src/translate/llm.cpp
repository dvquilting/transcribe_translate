#include "llm.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <type_traits>

#ifndef TL_NO_GPU_FIT  // the Android app runs on the processor and has no llama.cpp common library
#include "fit.h"  // llama.cpp common: fit the model + context into free GPU memory
#include "speculative.h"  // llama.cpp common: can a model draft for another?
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

namespace {
// Speculative decoding's cost model (see Llm::generate).
constexpr double kVerifyCostPerToken = 0.03;  // until measured: a step with k more tokens costs (1 + 0.03 k) of one
constexpr double kForget = 0.97;              // the guess counts fade, so the length follows the text
constexpr int kProbeEvery = 24;               // steps without drafting before a short draft checks again
constexpr float kDraftMinMargin = 1.0f;       // logits: the draft stops at a token this close to the runner-up
double ema(double old, double now) { return old <= 0 ? now : 0.85 * old + 0.15 * now; }
}  // namespace

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

uint64_t gpu_free_memory(const std::string& device) {
    llama_log_set(log_callback, nullptr);
    llama_backend_init();
    std::string backend;
    std::vector<ggml_backend_dev_t> devs;
    try {
        devs = choose_devices(device, backend, nullptr);
    } catch (const std::exception&) {
        return 0;
    }
    uint64_t sum = 0;
    for (auto d : devs) {
        size_t free_b = 0, total_b = 0;
        ggml_backend_dev_memory(d, &free_b, &total_b);
        sum += free_b;
    }
    return sum;
}

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

    // The draft model (speculative decoding) runs on the same GPUs with the
    // same context size; it is used only when the model itself still fits as
    // well as it would alone, and only on a graphics card - on the processor the
    // draft model's own work eats most of the gain.
    llama_model_params dmp = llama_model_default_params();
    dmp.devices = dev_list.data();
    llama_context_params dcp = cp;
    dcp.n_ubatch = 256;  // a smaller compute buffer: more room on the graphics card for both models
    bool use_draft = !opts.draft_model_path.empty();
#ifdef TL_NO_GPU_FIT
    if (use_draft) {
        use_draft = false;
        draft_info_ = "not supported in this build";
    }
#endif
    if (use_draft && devs.empty() && !opts.draft_on_cpu) {
        use_draft = false;
        draft_info_ = "not used: it only helps on a graphics card";
    }

    // Fit the weights + KV cache into free GPU memory: when the model is too big,
    // keep some layers (dense models) or expert weights (MoE models) on the CPU.
    std::vector<float> tensor_split(llama_max_devices(), 0.0f);
    std::vector<llama_model_tensor_buft_override> overrides(llama_max_tensor_buft_overrides(), {nullptr, nullptr});
#ifndef TL_NO_GPU_FIT
    if (!devs.empty()) {
        std::vector<size_t> margins(llama_max_devices(), (size_t)768 * 1024 * 1024);  // keep 768 MB free
        const auto level = opts.verbose ? GGML_LOG_LEVEL_DEBUG : GGML_LOG_LEVEL_ERROR;
        // With the draft model: fit both, then the model alone, and keep the draft
        // model only if the model's placement stays the same. Every layer the
        // draft model pushes onto the processor slows each step, drafted or not,
        // and the processor works through a draft's tokens one by one, so a split
        // model (TranslateGemma 27B on 16 GB) loses more than drafting saves.
        // Both models' runtime buffers on top of the estimate: a second context
        // needs some memory of its own (an extra 512 MB of headroom).
        std::vector<size_t> margins_d = margins;
        for (size_t& m : margins_d) m += (size_t)512 * 1024 * 1024;
        llama_model_params mp_d = mp;
        llama_context_params cp_d = cp;
        std::vector<float> split_d(tensor_split.size(), 0.0f);
        std::vector<llama_model_tensor_buft_override> overrides_d(overrides.size(), {nullptr, nullptr});
        if (use_draft) {
            common_fit_extra_model extra{opts.draft_model_path.c_str(), &dmp, &dcp, false};
            const auto st = common_fit_params(model_path.c_str(), &mp_d, &cp_d, split_d.data(), overrides_d.data(),
                                              margins_d.data(), cp.n_ctx, &extra, level);
            if (st == COMMON_PARAMS_FIT_STATUS_ERROR) throw std::runtime_error("could not read model file " + model_path);
        }
        const auto st = common_fit_params(model_path.c_str(), &mp, &cp, tensor_split.data(), overrides.data(),
                                          margins.data(), cp.n_ctx, nullptr, level);
        if (st == COMMON_PARAMS_FIT_STATUS_ERROR) throw std::runtime_error("could not read model file " + model_path);
        if (use_draft) {
            auto n_overrides = [](const std::vector<llama_model_tensor_buft_override>& v) {
                return std::count_if(v.begin(), v.end(), [](const auto& o) { return o.pattern != nullptr; });
            };
            const bool same = mp_d.n_gpu_layers == mp.n_gpu_layers && split_d == tensor_split &&
                              n_overrides(overrides_d) == n_overrides(overrides);
            if (cp_d.n_ctx == cp.n_ctx && same) {
                // (mp_d points into split_d / overrides_d; swapping keeps the buffers.)
                mp = mp_d;
                cp = cp_d;
                tensor_split.swap(split_d);
                overrides.swap(overrides_d);
            } else {
                use_draft = false;
                draft_info_ = "not used: the model and the draft model don't both fit in the graphics card's memory "
                              "(it would push part of the model onto the processor, which is slower)";
            }
        }
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

#ifndef TL_NO_GPU_FIT
    if (use_draft) {
        draft_model_ = llama_model_load_from_file(opts.draft_model_path.c_str(), dmp);
        if (!draft_model_) {
            // The estimate said both fit, but loading didn't: almost always graphics
            // memory (llama.cpp's own error line is in the log just above).
            draft_info_ = "not used: it could not be loaded next to the model (not enough graphics memory?) - "
                          "translating at the usual speed";
        } else if (!common_speculative_are_compatible(model_, draft_model_)) {
            draft_info_ = "not used: its vocabulary differs from the model's";
        } else {
            dcp.n_ctx = (uint32_t)n_ctx_;
            dcp.n_batch = (uint32_t)n_ctx_;
            draft_ctx_ = llama_init_from_model(draft_model_, dcp);
            if (!draft_ctx_)
                draft_info_ = "not used: not enough graphics memory for its context - translating at the usual speed";
        }
        if (draft_ctx_) {
            llama_set_abort_callback(draft_ctx_, [](void* p) { return abort_trampoline(p); }, this);
            draft_max_ = std::clamp(opts.draft_max, 1, 32);
            spec_.t_step.assign((size_t)draft_max_ + 1, 0.0);
        } else if (draft_model_) {
            llama_model_free(draft_model_);
            draft_model_ = nullptr;
        }
    }
#endif

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
    if (draft_ctx_) llama_free(draft_ctx_);
    if (draft_model_) llama_model_free(draft_model_);
    if (ctx_) llama_free(ctx_);
    if (model_) llama_model_free(model_);
}

double Llm::SpecTiming::step_time(int k) const {
    if (k < (int)t_step.size() && t_step[k] > 0) return t_step[k];
    // The nearest measured lengths below and above k.
    int lo = -1, hi = -1;
    for (int j = std::min(k, (int)t_step.size()) - 1; j >= 0; --j)
        if (t_step[j] > 0) { lo = j; break; }
    for (int j = k + 1; j < (int)t_step.size(); ++j)
        if (t_step[j] > 0) { hi = j; break; }
    if (lo >= 0 && hi >= 0) return t_step[lo] + (t_step[hi] - t_step[lo]) * (k - lo) / (hi - lo);
    if (lo < 0 && hi < 0) return -1;
    const int at = lo >= 0 ? lo : hi;
    // Beyond the measured lengths: the cost per drafted token between the two
    // nearest measured lengths, or the usual small one until there are two.
    double per_token = kVerifyCostPerToken * t_step[at];
    for (int j = at + (lo >= 0 ? -1 : 1); j >= 0 && j < (int)t_step.size(); j += lo >= 0 ? -1 : 1)
        if (t_step[j] > 0) {
            per_token = std::max(0.0, (t_step[at] - t_step[j]) / (at - j));
            break;
        }
    return std::max(1e-6, t_step[at] + per_token * (k - at));
}

std::vector<int> Llm::draft_tokens(const std::vector<int>& seq, int n, int* decoded) {
    std::vector<int> d;
    int dummy = 0;
    if (!decoded) decoded = &dummy;
    *decoded = 0;
    if (!draft_ctx_ || seq.empty() || n <= 0) return d;
    llama_memory_t mem = llama_get_memory(draft_ctx_);
    auto fail = [&]() {
        draft_kv_.clear();
        llama_memory_clear(mem, true);
        if (cancel_ && cancel_->load()) throw std::runtime_error("cancelled");
    };

    // Keep what the draft model's cache shares with the sequence, but process
    // the last token again for its logits.
    size_t keep = 0;
    while (keep < draft_kv_.size() && keep < seq.size() && draft_kv_[keep] == seq[keep]) ++keep;
    keep = std::min(keep, seq.size() - 1);
    if (!llama_memory_seq_rm(mem, 0, (llama_pos)keep, -1)) {
        llama_memory_clear(mem, true);
        keep = 0;
    }
    draft_kv_.assign(seq.begin(), seq.begin() + (std::ptrdiff_t)keep);
    *decoded += (int)(seq.size() - keep);
    const size_t n_batch = std::max<uint32_t>(1, llama_n_batch(draft_ctx_));
    for (size_t i = keep; i < seq.size(); i += n_batch) {
        const size_t m = std::min(n_batch, seq.size() - i);
        // (llama_decode doesn't write to the tokens.)
        if (llama_decode(draft_ctx_, llama_batch_get_one(const_cast<llama_token*>(seq.data()) + i, (int32_t)m)) != 0) {
            fail();
            return d;
        }
    }
    draft_kv_ = seq;

    const llama_vocab* vocab = llama_model_get_vocab(draft_model_);
    const int n_vocab = llama_vocab_n_tokens(vocab);
    // (The vocabularies may differ by a few padding tokens at the end.)
    const int n_target = llama_vocab_n_tokens(llama_model_get_vocab(model_));
    for (int j = 0; j < n; ++j) {
        const float* logits = llama_get_logits_ith(draft_ctx_, -1);
        if (!logits) break;
        // The best token and how far ahead of the runner-up it is: a guess the draft
        // model is unsure of is usually wrong, and checking it costs more than it saves.
        int best = 0, second = -1;
        for (int v = 1; v < n_vocab; ++v) {
            if (logits[v] > logits[best]) second = best, best = v;
            else if (second < 0 || logits[v] > logits[second]) second = v;
        }
        if (j > 0 && second >= 0 && logits[best] - logits[second] < kDraftMinMargin) break;
        if (best >= n_target) break;  // a token the model doesn't have
        d.push_back(best);
        if (llama_vocab_is_eog(vocab, best) || j + 1 == n) break;
        llama_token t = best;
        if (llama_decode(draft_ctx_, llama_batch_get_one(&t, 1)) != 0) {
            fail();
            break;
        }
        ++*decoded;
        draft_kv_.push_back(best);
    }
    return d;
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
    // Adds a token to the output; false = the output ends here.
    auto take = [&](llama_token t) {
        if (llama_vocab_is_eog(vocab, t)) return false;
        out.push_back(t);
        if (out.size() % 16 == 0) {
            if (const size_t drop = trailing_loop_length(out, allowed_repeats)) {
                out.resize(out.size() - drop);
                g.looped = true;
                return false;
            }
        }
        if ((int)out.size() >= max_tokens) {
            g.truncated = true;
            return false;
        }
        return true;
    };

    // Each step feeds the model the token it just chose and, with a draft
    // model, the tokens the draft model expects next, all in one llama_decode.
    // The model's choice after each fed token is then read in turn: while it
    // matches the next draft token, that token is written too and the next
    // logits are already there. At the first mismatch the model's own choice
    // is kept and the rest of the draft is dropped from the KV cache. Every
    // token written is the model's own choice given everything before it, so
    // the output is the same as without the draft model, only fewer steps.
    // How far to draft: the length that writes the most tokens per second, from
    // what each step has cost so far (a verification step of the model, a token
    // of the draft model) and how often the draft model's guesses are right.
    // When no length pays off, there is no drafting (0); a short draft now and
    // then checks whether that has changed. Kept from piece to piece.
    bool speculate = draft_ctx_ != nullptr;
    auto best_draft_length = [&]() -> int {
        // A step without a draft is measured first: a step with more tokens can cost
        // little more (a model on the graphics card) or much more (layers on the
        // processor, which works through each token), and only measuring tells.
        if (spec_.t_step[0] <= 0) return 0;
        if (spec_.t_draft <= 0) return std::min(3, draft_max_);
        const double a = std::clamp(spec_.hits / std::max(spec_.hits + spec_.misses, 1e-9), 0.0, 0.98);
        int best = 0;
        double best_rate = 1.0 / spec_.t_step[0];  // no drafting: one token per step
        for (int k = 1; k <= draft_max_; ++k) {
            const double tokens = (1.0 - std::pow(a, k + 1)) / (1.0 - a);  // expected, geometric
            const double t = spec_.step_time(k) + k * spec_.t_draft;
            if (t > 0 && tokens / t > best_rate * 1.02) best_rate = tokens / t, best = k;
        }
        return best;
    };
    // Greedy without penalties: the model's choice is the largest logit (the same
    // token llama.cpp's greedy sampler picks, the first of equal ones), without
    // sorting through the 262,000-token vocabulary of a sampler call each time.
    const bool plain_greedy = s.temperature <= 0.0f && s.repeat_penalty == 1.0f;
    const int n_vocab = llama_vocab_n_tokens(vocab);
    auto choose = [&](int i) -> llama_token {
        if (!plain_greedy) return llama_sampler_sample(chain, ctx_, i);
        const float* l = llama_get_logits_ith(ctx_, i);
        if (!l) return llama_sampler_sample(chain, ctx_, i);
        int best = 0;
        for (int v = 1; v < n_vocab; ++v)
            if (l[v] > l[best]) best = v;
        return best;
    };
    using clock = std::chrono::steady_clock;
    auto seconds = [](clock::time_point a, clock::time_point b) { return std::chrono::duration<double>(b - a).count(); };
    llama_batch batch = llama_batch_init(draft_max_ + 1, 0, 1);
    std::vector<int> sequence;  // prompt + output, for the draft model
    const auto t_writing = clock::now();
    try {
        llama_pos pos = (llama_pos)toks.size();  // where the next token goes in the KV cache
        llama_token cur = max_tokens > 0 ? choose(-1) : LLAMA_TOKEN_NULL;
        while (max_tokens > 0) {
            if (cancel_ && cancel_->load()) throw std::runtime_error("cancelled");
            if (!take(cur)) break;

            std::vector<int> draft;
            int n = 0;
            bool probing = false;
            if (speculate) {
                n = best_draft_length();
                probing = n == 0 && spec_.t_step[0] > 0 && ++spec_.since_probe >= spec_.probe_every;
                if (probing) n = std::min(2, draft_max_);  // worth it again?
                n = std::min({n, max_tokens - (int)out.size(), n_ctx_ - (int)pos - 2});
                if (n > 0) {
                    spec_.since_probe = 0;
                    sequence.assign(toks.begin(), toks.end());
                    sequence.insert(sequence.end(), out.begin(), out.end());
                    const auto t0 = clock::now();
                    int decoded = 0;
                    draft = draft_tokens(sequence, n, &decoded);
                    // Per token the draft model processed (catching up included); a long
                    // catch-up after a pause in drafting is a batch, not counted.
                    if (decoded > 0 && decoded <= draft_max_ + 2)
                        spec_.t_draft = ema(spec_.t_draft, seconds(t0, clock::now()) / decoded);
                }
            }

            batch.n_tokens = 0;
            auto add = [&](llama_token t, llama_pos p) {
                const int i = batch.n_tokens++;
                batch.token[i] = t;
                batch.pos[i] = p;
                batch.n_seq_id[i] = 1;
                batch.seq_id[i][0] = 0;
                batch.logits[i] = 1;
            };
            add(cur, pos);
            for (size_t i = 0; i < draft.size(); ++i) add(draft[i], pos + 1 + (llama_pos)i);
            const auto t_verify = clock::now();
            if (llama_decode(ctx_, batch) != 0) {
                if (cancel_ && cancel_->load()) throw std::runtime_error("cancelled");
                throw std::runtime_error("the model failed while writing the translation");
            }

            size_t accepted = 0;
            bool end = false;
            llama_token next = LLAMA_TOKEN_NULL;
            for (int i = 0;; ++i) {
                next = choose(i);  // (reading the logits waits for the decode)
                if (i < (int)draft.size() && next == draft[i]) {
                    ++accepted;
                    if (!take(next)) {
                        end = true;
                        break;
                    }
                    continue;
                }
                break;
            }
            if (speculate) {
                double& t = spec_.t_step[draft.size()];
                t = ema(t, seconds(t_verify, clock::now()));
                if (!draft.empty()) {
                    // Guesses right and wrong (the guess after the first wrong one isn't tried).
                    spec_.hits = spec_.hits * kForget + (double)accepted;
                    spec_.misses = spec_.misses * kForget + (accepted < draft.size() ? 1.0 : 0.0);
                    // A probe that found nothing: wait twice as long for the next one.
                    if (probing) spec_.probe_every = accepted == 0 ? std::min(spec_.probe_every * 2, 512) : kProbeEvery;
                    draft_stats_.drafted += (long long)draft.size();
                    draft_stats_.accepted += (long long)accepted;
                    draft_stats_.steps += 1;
                    draft_stats_.tokens += 1 + (long long)accepted;
                } else {
                    draft_stats_.plain_steps += 1;
                }
            }
            if (end) break;
            if (on_token_) on_token_((int)out.size());

            pos += 1 + (llama_pos)accepted;
            if (accepted < draft.size() && !llama_memory_seq_rm(mem, 0, pos, -1)) {
                // This cache can't drop its tail: rebuild it, and go on without drafts.
                llama_memory_clear(mem, true);
                sequence.assign(toks.begin(), toks.end());
                sequence.insert(sequence.end(), out.begin(), out.end());
                if (llama_decode(ctx_, llama_batch_get_one(sequence.data(), (int32_t)sequence.size())) != 0) {
                    if (cancel_ && cancel_->load()) throw std::runtime_error("cancelled");
                    throw std::runtime_error("the model failed while writing the translation");
                }
                speculate = false;
            }
            cur = next;
        }
    } catch (...) {
        llama_batch_free(batch);
        llama_sampler_free(chain);
        kv_prompt_.clear();  // a failed or cancelled decode: don't trust the cache
        draft_kv_.clear();
        throw;
    }
    llama_batch_free(batch);
    llama_sampler_free(chain);
    if (draft_ctx_ && spec_.t_step[0] > 0) {
        draft_stats_.seconds += seconds(t_writing, clock::now());
        draft_stats_.plain_seconds += (double)out.size() * spec_.t_step[0];
    }
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
