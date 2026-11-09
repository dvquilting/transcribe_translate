// Stand-in for the llama.cpp wrapper, so translator.cpp's model-free parts
// (prompts, output clean-up, language answers) can be unit-tested.
#include "llm_stub.h"
#include <stdexcept>
#include <cstdlib>

namespace tl {

namespace {
std::vector<Generation> answers;
std::vector<Prompt> prompts;
std::vector<int> allowed;  // generate()'s allowed_repeats, call by call
size_t next = 0;
int tokenizations = 0;  // prompts tokenized (count_prompt_tokens, tokenize_prompt)
int decodes = 0;        // prompts processed by the "model"
std::vector<int> kv;  // the "KV cache": the last prompt's tokens
int prompt_size(const Prompt& prompt, bool add_bos) {
    int n = add_bos ? 1 : 0;
    for (const auto& piece : prompt.pieces) n += piece.special ? 1 : (int)(piece.text.size() + 3) / 4;
    return n;
}
std::vector<int> fake_tokens(const Prompt& prompt, bool add_bos) {
    // As many "tokens" as count_prompt_tokens says, numbered from a hash of
    // the text, so different prompts give different tokens.
    std::vector<int> t((size_t)prompt_size(prompt, add_bos));
    unsigned h = add_bos ? 1u : 0u;
    for (const auto& piece : prompt.pieces)
        for (char c : piece.text) h = h * 31u + (unsigned char)c;
    for (size_t i = 0; i < t.size(); ++i) t[i] = (int)((h + (unsigned)i) & 0x7fffffff);
    return t;
}
}
void reset_stub(const std::vector<Generation>& a) {
    answers = a; prompts.clear(); allowed.clear(); next = 0; tokenizations = decodes = 0; kv.clear();
}
const std::vector<Prompt>& stub_prompts() { return prompts; }
const std::vector<int>& stub_allowed_repeats() { return allowed; }
int stub_prompt_tokenizations() { return tokenizations; }
int stub_prompt_decodes() { return decodes; }
std::string describe_engine(const EngineInfo&) { return "test model"; }
int default_threads() { return 1; }
uint64_t gpu_free_memory(const std::string&) { return 0; }
Llm::Llm(const std::string&, const EngineOptions& opts, std::string*) : n_ctx_(opts.n_ctx) {}
Llm::~Llm() = default;
int Llm::count_tokens(const std::string& text) const { return (int)(text.size() + 3) / 4; }
int Llm::count_prompt_tokens(const Prompt& prompt, bool add_bos) const {
    ++tokenizations;
    return prompt_size(prompt, add_bos);
}
std::vector<int> Llm::tokenize_prompt(const Prompt& prompt, bool add_bos) const {
    ++tokenizations;
    return fake_tokens(prompt, add_bos);
}
Generation Llm::generate(const Prompt& p, bool bos, int max_tokens, const Sampling&, int allowed_repeats,
                         const std::vector<int>* given) {
    const std::vector<int> toks = given ? *given : tokenize_prompt(p, bos);
    if (given && toks != fake_tokens(p, bos)) throw std::runtime_error("test: tokens of another prompt");
    if ((int)toks.size() + max_tokens + 16 > n_ctx_)
        throw std::runtime_error("test: context overflow");
    // As in llm.cpp: a prompt still in the "KV cache" is not processed again.
    if (reusable_prompt_tokens(kv, toks) == 0) ++decodes;
    kv = toks;
    prompts.push_back(p);
    allowed.push_back(allowed_repeats);
    if (on_token_) on_token_(1), on_token_(2);  // as if two tokens were written
    if (std::getenv("TRANSLATE_TEST_INCOMPLETE")) {
        Generation failed; failed.text = "INCOMPLETE"; failed.truncated = true;
        return failed;
    }
    if (next < answers.size()) return answers[next++];
    Generation g;
    g.text = "complete";
    return g;
}

}  // namespace tl
