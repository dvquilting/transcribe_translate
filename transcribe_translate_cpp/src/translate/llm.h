// llm.h - a thin wrapper around llama.cpp: pick the GPU, fit the model into
// its memory (splitting with the CPU when needed), and generate text.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct llama_model;
struct llama_context;

namespace tl {

struct EngineOptions {
    std::string device = "auto";  // auto | cuda | vulkan | cpu
    int threads = 0;              // 0 = automatic
    int n_ctx = 8192;
    bool verbose = false;         // show llama.cpp's own log
    // Speculative decoding: a small model of the same family (the same
    // vocabulary) guesses a few tokens ahead and the model checks them all in
    // one step, keeping the ones it would have written itself. Greedy output is
    // the same as without it, only sooner. Empty = no draft model.
    std::string draft_model_path;
    int draft_max = 7;            // tokens drafted ahead at most (the length is chosen below that)
    bool draft_on_cpu = false;    // use the draft model without a graphics card too
};

// How the draft model did (speculative decoding).
struct DraftStats {
    long long drafted = 0;   // tokens the draft model proposed
    long long accepted = 0;  // ... that the model kept
    long long steps = 0;     // verification steps (one llama_decode of the model each)
    long long tokens = 0;    // tokens written in those steps
    long long plain_steps = 0;  // steps without a draft (drafting didn't pay off then)
    double seconds = 0;         // spent writing (the prompts' processing not included)
    double plain_seconds = 0;   // estimated for the same tokens one step each, without the draft model
};

struct EngineInfo {
    std::string backend = "CPU";  // "CUDA", "Vulkan" or "CPU"
    std::string device;           // e.g. "NVIDIA GeForce RTX 5060 Ti"
    double memory_gb = 0;         // GPU memory (0 = n/a)
    std::string placement;        // e.g. "all on GPU", "30 of 46 layers on GPU"
};

// "CUDA (NVIDIA GeForce RTX 5060 Ti, 15.9 GB)"
std::string describe_engine(const EngineInfo& e);

struct Sampling {
    float temperature = 0.0f;  // 0 = greedy (deterministic)
    int top_k = 0;
    float top_p = 1.0f;
    float repeat_penalty = 1.0f;
    uint32_t seed = 42;
};

// A prompt in the model's chat format, in pieces. Control tokens such as
// <start_of_turn> or <|im_end|> are recognised only in `special` pieces; the
// text to translate (and its context) goes in plain pieces, so a document that
// happens to contain "<end_of_turn>" can't end the turn or inject other turns.
struct Prompt {
    struct Piece {
        std::string text;
        bool special = false;
    };
    std::vector<Piece> pieces;

    Prompt& special(const std::string& s) { pieces.push_back({s, true}); return *this; }
    Prompt& text(const std::string& s) { pieces.push_back({s, false}); return *this; }
    std::string str() const {  // for debug output and tests
        std::string r;
        for (const auto& p : pieces) r += p.text;
        return r;
    }
};

struct Generation {
    std::string text;
    bool truncated = false;  // hit max_tokens
    bool looped = false;     // stopped because the output started repeating itself
    int prompt_tokens = 0;
    int generated_tokens = 0;
};

class Llm {
public:
    // `note` receives a human-readable remark about device choice, if any.
    Llm(const std::string& model_path, const EngineOptions& opts, std::string* note = nullptr);
    ~Llm();
    Llm(const Llm&) = delete;
    Llm& operator=(const Llm&) = delete;

    // Tokens in `text`, read as plain text (control-token names are not special).
    int count_tokens(const std::string& text) const;
    int count_prompt_tokens(const Prompt& prompt, bool add_bos) const;
    // The prompt as generate() feeds it to the model. Hand the tokens back to
    // generate() so a prompt that was measured isn't tokenized a second time.
    std::vector<int> tokenize_prompt(const Prompt& prompt, bool add_bos) const;

    // Run the prompt (already in the model's chat format). `add_bos` prepends
    // the model's begin-of-text token if the vocabulary has one. The returned
    // text is valid UTF-8 (see sanitize_utf8). `allowed_repeats`: the text to
    // translate itself repeats something this many times in a row (a chorus),
    // so output that does the same is not a loop (see trailing_loop_length).
    // `prompt_tokens`, if given, is tokenize_prompt(prompt, add_bos). When the
    // tokens are the same as the last call's (a retry), the prompt still in the
    // KV cache is reused instead of being processed again.
    Generation generate(const Prompt& prompt, bool add_bos, int max_tokens, const Sampling& s,
                        int allowed_repeats = 0, const std::vector<int>* prompt_tokens = nullptr);

    int context_size() const { return n_ctx_; }
    const EngineInfo& engine() const { return info_; }

    // The draft model, if one was asked for and could be used.
    bool has_draft() const { return draft_ctx_ != nullptr; }
    // Why a draft model that was asked for isn't used (empty when it is).
    const std::string& draft_info() const { return draft_info_; }
    const DraftStats& draft_stats() const { return draft_stats_; }
    void reset_draft_stats() { draft_stats_ = DraftStats(); }  // per job, for a model kept loaded

    // While *flag is true, generation stops as soon as possible (throws "cancelled").
    void set_cancel_flag(const std::atomic<bool>* flag) { cancel_ = flag; }
    // Called during generate() with the number of tokens written so far (for
    // progress); an empty function turns it off.
    void set_token_callback(std::function<void(int)> cb) { on_token_ = std::move(cb); }

private:
    // Up to n tokens the draft model expects after `sequence` (all tokens so
    // far: the prompt and the output). It stops early at an end of text or
    // when it is unsure of the next token.
    // `decoded` receives the tokens the draft model processed (catching up + drafting).
    std::vector<int> draft_tokens(const std::vector<int>& sequence, int n, int* decoded = nullptr);

    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    llama_model* draft_model_ = nullptr;
    llama_context* draft_ctx_ = nullptr;
    std::vector<int> draft_kv_;  // the tokens in the draft model's KV cache, in order
    int draft_max_ = 8;
    std::string draft_info_;
    DraftStats draft_stats_;
    struct SpecTiming {      // measured while generating (see generate())
        std::vector<double> t_step;  // [k]: seconds for a step of the model with k drafted tokens (0 = not yet)
        double t_draft = 0;          // seconds per token of the draft model
        double hits = 7, misses = 3; // draft guesses right / runs ended by a wrong one (fading)
        int since_probe = 0;
        int probe_every = 24;        // steps between probes; doubles while they find nothing
        double step_time(int k) const;  // measured, or estimated from the lengths measured (-1 = nothing yet)
    } spec_;
    int n_ctx_ = 0;
    EngineInfo info_;
    const std::atomic<bool>* cancel_ = nullptr;
    std::function<void(int)> on_token_;
    std::vector<int> kv_prompt_;  // the prompt whose tokens are in the KV cache (empty = unknown)
    static bool abort_trampoline(void* self);
};

// True if the token sequence ends in a short pattern repeated many times
// (and more than `allowed_repeats` times). Returns the number of trailing
// tokens to drop (0 = no loop). Defined in translator.cpp, so the unit tests,
// which have no llama.cpp, can check it.
size_t trailing_loop_length(const std::vector<int>& tokens, int allowed_repeats = 0);

// How many tokens of the prompt `cached` (in the KV cache) generate() can keep
// for the next prompt: all but the last when the two are the same - the last
// is processed again, for its logits - and none otherwise. Defined in
// translator.cpp for the unit tests, like trailing_loop_length.
size_t reusable_prompt_tokens(const std::vector<int>& cached, const std::vector<int>& prompt);

int default_threads();

// Free memory of the graphics card(s) the model would run on with this
// --device choice, in bytes (0 = it would run on the processor).
uint64_t gpu_free_memory(const std::string& device);

}  // namespace tl
