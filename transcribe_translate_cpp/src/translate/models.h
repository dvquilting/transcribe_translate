// models.h - the translation models on offer and where to get them.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tl {

enum class Family {
    TranslateGemma,  // Google TranslateGemma (Gemma 3): fixed translator prompt, no context slot
    HyMT,            // Tencent Hy-MT2: its own prompt templates, background-context template
    Qwen,            // Qwen 3.5 instruct, non-thinking mode: general instruction prompt
};

struct ModelInfo {
    std::string key;          // command-line name
    std::string display;      // human-readable name
    Family family;
    std::string file;         // GGUF file name in the models folder
    std::string url;          // download URL
    std::string size;         // approximate download size, for messages
    uint64_t bytes = 0;       // exact download size (0 = unknown)
    std::string description;  // one line for the GUI
    std::vector<std::string> langs;  // language codes it was trained on (empty = all)
    int n_ctx = 8192;         // context window we open the model with
    int chunk_tokens = 600;   // target size of one piece of source text, in tokens
};

const std::vector<ModelInfo>& model_table();
// The translation models of the Android app (android/): 4-bit, up to ~2.5 GB.
const std::vector<ModelInfo>& phone_model_table();
const ModelInfo* find_phone_model(const std::string& key);
const ModelInfo* find_model(const std::string& key);

// Draft ("helper") models for TranslateGemma's faster mode (speculative
// decoding, --draft): "translategemma-4b" (Q4_K_M, the better guesser) and
// "gemma-3-1b" (Gemma 3 1B instruct, small enough to fit beside TranslateGemma
// 12B on a 16 GB card). Both share TranslateGemma's vocabulary.
const std::vector<ModelInfo>& draft_model_table();
const ModelInfo* find_draft_model(const std::string& key);
const ModelInfo& default_model();

// True if the model was trained on this language (Qwen: always).
bool model_supports(const ModelInfo& m, const std::string& lang_code);

}  // namespace tl
