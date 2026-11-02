// llm.h - optional transcript post-processing with a local text LLM, run through
// CrispASR's built-in llama.cpp (crispasr_chat_* ABI): spelling/grammar cleanup
// and summaries. Everything runs in-process; nothing leaves the computer.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "engine.h"

struct crispasr_chat_session;

namespace tmx {

// The language model offered for post-processing (downloaded on first use).
const ModelInfo& default_llm_model();

struct LlmOptions {
    std::string device = "auto";  // as chosen by choose_device(); "cpu" = no GPU offload
    int threads = 0;
    int n_ctx = 12288;            // context window in tokens (KV-cache memory scales with it)
};

// A chat model the post-processing below can use. LlmSession is the one of the
// command-line program; the Android app has its own (android/.../chat_model.cpp).
class ChatModel {
public:
    virtual ~ChatModel() = default;
    // One system + user exchange. temperature 0 = deterministic.
    virtual std::string ask(const std::string& system, const std::string& user, int max_tokens, float temperature) = 0;
    // Prompt length in tokens for this system + user pair.
    virtual int count_tokens(const std::string& system, const std::string& user) = 0;
    virtual int context_size() const = 0;
};

class LlmSession : public ChatModel {
public:
    LlmSession(const std::string& model_path, const LlmOptions& opts);
    ~LlmSession() override;
    LlmSession(const LlmSession&) = delete;
    LlmSession& operator=(const LlmSession&) = delete;

    std::string ask(const std::string& system, const std::string& user, int max_tokens, float temperature) override;
    int count_tokens(const std::string& system, const std::string& user) override;
    int context_size() const override { return n_ctx_; }

private:
    crispasr_chat_session* s_ = nullptr;
    int n_ctx_ = 0;
};

using LlmProgress = std::function<void(int done, int total)>;

struct PolishStats {
    int chunks = 0;
    int kept_original = 0;  // chunks where the model's edit was rejected as too different
};

// Correct spelling, punctuation, capitalisation and grammar without changing
// the wording. `text` is paragraph-formatted (blank lines between paragraphs);
// paragraph breaks are preserved. Chunks whose correction differs too much from
// the original (the model added, dropped or rewrote content) keep the original.
std::string polish_transcript(ChatModel& llm, const std::string& text, LlmProgress progress, PolishStats* stats);

struct ParagraphStats {
    int windows = 0;
    int fallback = 0;  // windows where the reply had no usable numbers
    int skipped = 0;   // windows too long for the context window (one very long sentence)
};

// Choose paragraph breaks with the model. Only break positions come from the
// model, so the wording cannot change. `pauses` (seconds before each sentence,
// may be empty) are shown to the model as hints. Returns sorted indices of the
// sentences that start a paragraph. `fallback_every` is used for a window
// whose reply can't be used, and for a sentence too long to show the model.
std::vector<int> smart_paragraph_breaks(ChatModel& llm, const std::vector<std::string>& sentences,
                                        const std::vector<double>& pauses, int fallback_every, LlmProgress progress,
                                        ParagraphStats* stats);

// A short overview plus key points. Long transcripts are summarised in parts
// first and the part summaries are then combined.
std::string summarize_transcript(ChatModel& llm, const std::string& text, LlmProgress progress);

// Remove <think>...</think> blocks and surrounding whitespace from model output.
std::string strip_reasoning(const std::string& s);

// Word count (whitespace separated) - used to sanity-check corrections.
int count_words(const std::string& s);

}  // namespace tmx
