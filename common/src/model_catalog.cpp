#include "model_catalog.h"

namespace pk {

namespace {
constexpr const char* kHf = "https://huggingface.co/";  // a literal: no static-initialisation order
}

const std::vector<AsrModel>& asr_models() {
    // Q8_0 quantisations: indistinguishable from F16 in practice, and all of
    // them fit comfortably in 16 GB of VRAM.
    static const std::vector<AsrModel> models = {
        {"parakeet-ultra", "Parakeet Ultra", "parakeet", "parakeet-ultra-q8_0.gguf",
         std::string(kHf) + "cstr/parakeet-ultra-GGUF/resolve/main/parakeet-ultra-q8_0.gguf", "674 MB", false, 0.0, false,
         "Fastest. Detects the language itself (25 European languages)."},
        {"ark", "ARK-ASR-3B", "ark-asr", "ark-asr-3b-q8_0.gguf",
         std::string(kHf) + "cstr/ark-asr-3b-GGUF/resolve/main/ark-asr-3b-q8_0.gguf", "4.3 GB", true, 30.0, false,
         "Most accurate; 19 languages; slower."},
        {"qwen", "Qwen3-ASR-1.7B", "qwen3", "qwen3-asr-1.7b-q8_0.gguf",
         std::string(kHf) + "cstr/qwen3-asr-1.7b-GGUF/resolve/main/qwen3-asr-1.7b-q8_0.gguf", "2.5 GB", true, 300.0, false,
         "Accurate, 52 languages; can detect the language."},
        // IBM Granite Speech 4.1 2B Plus: the variant with punctuation and
        // capitalisation (the base 4.1 model writes plain lowercase text). No
        // Q8_0 is published; Q4_K keeps the encoder in F32 (the recommended file).
        {"granite", "Granite Speech 4.1 Plus", "granite-4.1-plus", "granite-speech-4.1-2b-plus-q4_k.gguf",
         std::string(kHf) + "cstr/granite-speech-4.1-2b-plus-GGUF/resolve/main/granite-speech-4.1-2b-plus-q4_k.gguf", "3.0 GB",
         true, 30.0, false, "Very accurate; English, French, German, Spanish, Portuguese."},
        // Cohere Transcribe (03-2026): needs the spoken language; 14 languages.
        {"cohere", "Cohere Transcribe", "cohere", "cohere-transcribe-q8_0.gguf",
         std::string(kHf) + "cstr/cohere-transcribe-03-2026-GGUF/resolve/main/cohere-transcribe-q8_0.gguf", "2.4 GB", true, 30.0,
         true, "Very accurate, 14 languages. Choose the spoken language."},
        {"parakeet-redux", "Parakeet Redux", "parakeet", "parakeet-redux-q4_k.gguf",
         std::string(kHf) + "cstr/parakeet-redux-GGUF/resolve/main/parakeet-redux-q4_k.gguf", "402 MB", false, 0.0, false,
         "Small ternary Parakeet variant."},
    };
    return models;
}

const std::vector<AsrModel>& phone_asr_models() {
    // Q4_K: a third of the memory of Q8_0 and faster on a phone's processor, with
    // a small loss of accuracy. Chunks of 30 s keep the memory flat for long files.
    static const std::vector<AsrModel> models = {
        {"parakeet-ultra", "Parakeet Ultra", "parakeet", "parakeet-ultra-q4_k.gguf",
         std::string(kHf) + "cstr/parakeet-ultra-GGUF/resolve/main/parakeet-ultra-q4_k.gguf", "402 MB", false, 0.0,
         false, "Fastest. Detects the language itself (25 European languages)."},
        {"qwen-0.6b", "Qwen3-ASR 0.6B", "qwen3", "qwen3-asr-0.6b-q4_k.gguf",
         std::string(kHf) + "cstr/qwen3-asr-0.6b-GGUF/resolve/main/qwen3-asr-0.6b-q4_k.gguf", "500 MB", true, 30.0, false,
         "52 languages; can detect the language."},
        {"qwen", "Qwen3-ASR 1.7B", "qwen3", "qwen3-asr-1.7b-q4_k.gguf",
         std::string(kHf) + "cstr/qwen3-asr-1.7b-GGUF/resolve/main/qwen3-asr-1.7b-q4_k.gguf", "1.3 GB", true, 30.0, false,
         "More accurate, 52 languages; about three times slower than 0.6B."},
    };
    return models;
}

const AsrModel* find_phone_asr_model(const std::string& key) {
    for (const auto& m : phone_asr_models())
        if (m.key == key) return &m;
    return nullptr;
}

const AsrModel& phone_aligner_model() {
    static const AsrModel m = {
        "aligner", "Qwen3-ForcedAligner-0.6B", "qwen3", "qwen3-forced-aligner-0.6b-q4_k.gguf",
        std::string(kHf) + "cstr/qwen3-forced-aligner-0.6b-GGUF/resolve/main/qwen3-forced-aligner-0.6b-q4_k.gguf", "500 MB",
        false, 0.0, false, "Word timings for the Qwen models' subtitles."};
    return m;
}

const AsrModel* find_asr_model(const std::string& key) {
    for (const auto& m : asr_models())
        if (m.key == key) return &m;
    return nullptr;
}

const AsrModel& aligner_model() {
    // The file name must contain "forced-aligner": CrispASR picks the aligner type from it.
    static const AsrModel m = {
        "aligner", "Qwen3-ForcedAligner-0.6B", "qwen3", "qwen3-forced-aligner-0.6b-q8_0.gguf",
        std::string(kHf) + "cstr/qwen3-forced-aligner-0.6b-GGUF/resolve/main/qwen3-forced-aligner-0.6b-q8_0.gguf", "986 MB",
        false, 0.0, false, "Word timings for the chunked models' subtitles."};
    return m;
}

const AsrModel& punctuation_model() {
    // The file name must contain "pcs" for CrispASR to load it as a PCS model.
    static const AsrModel m = {
        "pcs", "PCS punctuation model", "", "pcs-xlmr-base-q4_k.gguf",
        std::string(kHf) + "cstr/pcs-xlmr-base-GGUF/resolve/main/pcs-xlmr-base-q4_k.gguf", "163 MB", false, 0.0, false,
        "Adds punctuation and capitals to text written without them."};
    return m;
}

const std::vector<TtsModel>& tts_models() {
    static const std::vector<TtsModel> m = {
        {"qwen3-tts", "Qwen3-TTS 1.7B", "qwen3-tts-1.7b-base", "2.0 GB",
         {"zh", "en", "ja", "ko", "de", "fr", "ru", "pt", "es", "it"},
         "Most natural voice cloning. English, Chinese, Japanese, Korean, German, French, Russian, Portuguese, "
         "Spanish, Italian."},
        {"cosyvoice3", "CosyVoice3 0.5B", "cosyvoice3-tts", "1.2 GB",
         {"zh", "en", "ja", "ko", "de", "es", "fr", "it", "ru"},
         "Made for cross-language cloning: keeps the voice, drops the original accent. English, Chinese, Japanese, "
         "Korean, German, Spanish, French, Italian, Russian."},
        {"omnivoice", "OmniVoice", "omnivoice", "1.6 GB", {},
         "Voice cloning in 600+ languages, for languages the others don't cover."},
    };
    return m;
}

const TtsModel* find_tts_model(const std::string& key) {
    for (const auto& m : tts_models())
        if (m.key == key) return &m;
    return nullptr;
}

}  // namespace pk
