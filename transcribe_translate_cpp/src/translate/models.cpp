#include "models.h"

#include "languages.h"

namespace tl {

namespace {

const char* kHf = "https://huggingface.co/";

const std::vector<std::string> kGemmaLangs = {
    "en", "ar", "bg", "bn", "ca", "cs", "da", "de", "el", "es", "et", "fa", "fi", "fil", "fr", "gu", "he", "hi", "hr",
    "hu", "id", "is", "it", "ja", "kn", "ko", "lt", "lv", "ml", "mr", "nl", "no", "pa", "pl", "pt-BR", "pt-PT", "ro",
    "ru", "sk", "sl", "sr", "sv", "sw", "ta", "te", "th", "tr", "uk", "ur", "vi", "zh", "zh-Hant", "zu"};

const std::vector<std::string> kHyLangs = {
    "zh", "en", "fr", "pt-BR", "pt-PT", "es", "ja", "tr", "ru", "ar", "ko", "th", "it", "de", "vi", "ms", "id", "fil",
    "hi", "zh-Hant", "pl", "cs", "nl", "km", "my", "fa", "gu", "ur", "te", "mr", "he", "bn", "ta", "uk", "bo", "kk",
    "mn", "ug", "yue"};

}  // namespace

const std::vector<ModelInfo>& model_table() {
    // TranslateGemma was trained with a 2K-token input window, so it gets small
    // pieces of text. Quantisations: the largest that leaves room on a 16 GB GPU;
    // bigger models are split between GPU and CPU automatically.
    static const std::vector<ModelInfo> models = {
        {"translategemma-4b", "TranslateGemma 4B", Family::TranslateGemma, "translategemma-4b-it.Q8_0.gguf",
         std::string(kHf) + "mradermacher/translategemma-4b-it-GGUF/resolve/main/translategemma-4b-it.Q8_0.gguf",
         "4.1 GB", 4130417920ull, "Google. Small and fast; good quality for its size. 55 languages.", kGemmaLangs,
         4096, 400},
        {"translategemma-12b", "TranslateGemma 12B", Family::TranslateGemma, "translategemma-12b-it.Q6_K.gguf",
         std::string(kHf) + "mradermacher/translategemma-12b-it-GGUF/resolve/main/translategemma-12b-it.Q6_K.gguf",
         "9.7 GB", 9660827392ull, "Google. Very good quality; fits a 12-16 GB graphics card. 55 languages.",
         kGemmaLangs, 4096, 400},
        {"translategemma-12b-q4", "TranslateGemma 12B (smaller)", Family::TranslateGemma,
         "translategemma-12b-it.Q4_K_M.gguf",
         std::string(kHf) + "mradermacher/translategemma-12b-it-GGUF/resolve/main/translategemma-12b-it.Q4_K_M.gguf",
         "7.3 GB", 7300794112ull,
         "Google. 12B in a smaller file (4-bit): slightly lower quality than the 9.7 GB one, but leaves room on a "
         "16 GB graphics card for the faster mode's helper model. 55 languages.",
         kGemmaLangs, 4096, 400},
        {"translategemma-27b", "TranslateGemma 27B", Family::TranslateGemma, "translategemma-27b-it.Q4_K_M.gguf",
         std::string(kHf) + "mradermacher/translategemma-27b-it-GGUF/resolve/main/translategemma-27b-it.Q4_K_M.gguf",
         "16.5 GB", 16546704480ull,
         "Google. Best quality; larger than most graphics cards, so part runs on the CPU (slower). 55 languages.",
         kGemmaLangs, 4096, 400},
        {"hy-mt2-7b", "Hy-MT2 7B", Family::HyMT, "HY-MT2-7B-Q8_0.gguf",
         std::string(kHf) + "tencent/Hy-MT2-7B-GGUF/resolve/main/HY-MT2-7B-Q8_0.gguf", "8.0 GB", 7981928896ull,
         "Tencent. Strong on Chinese and Asian languages; uses the surrounding text as context. 38 languages.",
         kHyLangs, 8192, 600},
        {"qwen3.5-9b", "Qwen 3.5 9B", Family::Qwen, "Qwen3.5-9B-Q6_K.gguf",
         std::string(kHf) + "unsloth/Qwen3.5-9B-GGUF/resolve/main/Qwen3.5-9B-Q6_K.gguf", "7.5 GB", 7458301152ull,
         "Alibaba. General-purpose model; handles any language pair and uses the surrounding text as context.",
         {}, 8192, 600},
        {"qwen3.5-35b-a3b", "Qwen 3.5 35B-A3B", Family::Qwen, "Qwen3.5-35B-A3B-UD-Q4_K_XL.gguf",
         std::string(kHf) + "unsloth/Qwen3.5-35B-A3B-GGUF/resolve/main/Qwen3.5-35B-A3B-UD-Q4_K_XL.gguf", "22.2 GB",
         22241950336ull,
         "Alibaba. Larger general-purpose model (mixture of experts); needs 32 GB of RAM, partly runs on the CPU.",
         {}, 8192, 600},
    };
    return models;
}

const std::vector<ModelInfo>& phone_model_table() {
    // Smaller pieces of text than on a computer: a phone reads a prompt at
    // ~70 tokens/s, so long contexts cost minutes.
    static const std::vector<ModelInfo> models = {
        {"hy-mt2-1.8b", "Hy-MT2 1.8B", Family::HyMT, "Hy-MT2-1.8B-Q4_K_M.gguf",
         std::string(kHf) + "tencent/Hy-MT2-1.8B-GGUF/resolve/main/Hy-MT2-1.8B-Q4_K_M.gguf", "1.1 GB", 0,
         "Tencent. Small and fast, made for translation; uses the surrounding text as context. 38 languages.",
         kHyLangs, 4096, 400},
        {"translategemma-4b", "TranslateGemma 4B", Family::TranslateGemma, "translategemma-4b-it.Q4_K_M.gguf",
         std::string(kHf) + "mradermacher/translategemma-4b-it-GGUF/resolve/main/translategemma-4b-it.Q4_K_M.gguf",
         "2.5 GB", 0, "Google. Best quality here, slower. 55 languages.", kGemmaLangs, 4096, 300},
        {"qwen3.5-2b", "Qwen 3.5 2B", Family::Qwen, "Qwen3.5-2B-Q4_K_M.gguf",
         std::string(kHf) + "unsloth/Qwen3.5-2B-GGUF/resolve/main/Qwen3.5-2B-Q4_K_M.gguf", "1.3 GB", 0,
         "Alibaba. General-purpose; for language pairs the others don't cover.", {}, 4096, 400},
    };
    return models;
}

const std::vector<ModelInfo>& draft_model_table() {
    static const std::vector<ModelInfo> models = {
        {"translategemma-4b", "TranslateGemma 4B", Family::TranslateGemma, "translategemma-4b-it.Q4_K_M.gguf",
         std::string(kHf) + "mradermacher/translategemma-4b-it-GGUF/resolve/main/translategemma-4b-it.Q4_K_M.gguf",
         "2.5 GB", 2489909760ull, "TranslateGemma 4B: guesses the larger models' words best.", kGemmaLangs, 4096, 400},
        {"gemma-3-1b", "Gemma 3 1B", Family::TranslateGemma, "gemma-3-1b-it-Q8_0.gguf",
         std::string(kHf) + "ggml-org/gemma-3-1b-it-GGUF/resolve/main/gemma-3-1b-it-Q8_0.gguf", "1.1 GB",
         1069306368ull, "Gemma 3 1B: smaller and faster, guesses less well (it isn't a translation model).",
         kGemmaLangs, 4096, 400},
    };
    return models;
}

const ModelInfo* find_draft_model(const std::string& key) {
    for (const auto& m : draft_model_table())
        if (m.key == key) return &m;
    return nullptr;
}

const ModelInfo* find_phone_model(const std::string& key) {
    for (const auto& m : phone_model_table())
        if (m.key == key) return &m;
    return nullptr;
}

const ModelInfo* find_model(const std::string& key) {
    for (const auto& m : model_table())
        if (m.key == key) return &m;
    return nullptr;
}

const ModelInfo& default_model() { return *find_model("translategemma-12b"); }

bool model_supports(const ModelInfo& m, const std::string& lang_code) {
    if (m.langs.empty()) return true;
    const Language* l = find_language(lang_code);
    if (!l) return false;
    for (const auto& c : m.langs)
        if (c == l->code) return true;
    // A regional variant the model has no entry for ("es-ES") counts as its
    // language ("es"); variants the models do list (pt-BR, zh-Hant) match above.
    const size_t dash = l->code.find('-');
    if (dash != std::string::npos)
        for (const auto& c : m.langs)
            if (c == l->code.substr(0, dash)) return true;
    return false;
}

}  // namespace tl
