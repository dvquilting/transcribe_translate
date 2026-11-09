// translator.h - turn units of text (paragraphs, subtitle sentences, Word
// paragraphs) into translations, one model call per piece of a few hundred
// tokens, with checks and retries.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "llm.h"
#include "models.h"

namespace tl {

struct TranslateOptions {
    std::string source = "auto";  // language code, or "auto"
    std::string target;           // language code
    bool use_context = true;      // give the model the preceding text (Hy-MT2, Qwen)
};

struct TranslateStats {
    int pieces = 0;          // model calls planned
    int split_retries = 0;   // pieces redone unit by unit (paragraph count changed)
    int problems = 0;        // failed units retained verbatim in the source language
    int prompt_tokens = 0;
    int generated_tokens = 0;
};

// Builds the exact prompt each model family was trained with.
struct PromptBuilder {
    Family family;
    std::string source;  // language code or "auto"
    std::string target;

    // The prompt in the model's chat format. `context` = preceding source
    // text (may be empty; TranslateGemma has no slot for it).
    Prompt translation(const std::string& text, const std::string& context) const;
    // Does generate() need to prepend the begin-of-text token?
    bool add_bos() const { return family == Family::TranslateGemma; }
    // Prompt asking which language `sample` is in (answer: ISO code).
    Prompt language_question(const std::string& sample) const;
};

// Clean a raw model answer: drop <think> blocks, wrapper tags, chatty first
// lines such as "Here is the translation:", and surrounding whitespace.
std::string clean_output(const std::string& raw, const std::string& source_text);

// The most times a word or a phrase (up to 64 words) occurs back to back in
// `text` (1 = nothing repeats), ignoring case and punctuation; each CJK
// character counts as a word. A chorus sung four times gives 4. The model's
// output may repeat that often without being taken for a loop.
int source_repetitions(const std::string& text);

// The language a model named in its answer to PromptBuilder::language_question
// ("sv", "en-US", "Swedish", "Svenska (sv)" ...), or "" if not recognised.
std::string language_from_answer(const std::string& answer);

class Translator {
public:
    Translator(Llm& llm, const ModelInfo& model, const TranslateOptions& opts);

    // Ask the model which language the text is in. Returns a language code, or
    // "" if the answer wasn't recognised.
    std::string detect_language(const std::string& sample);

    using Progress = std::function<void(int done, int total)>;
    using UnitDone = std::function<void(size_t index, const std::string& translation)>;

    // Translate each unit. A unit is a paragraph-like block without blank
    // lines; the result has exactly one translation per unit, in order. Units
    // without letters (numbers, punctuation) are returned unchanged.
    std::vector<std::string> translate_units(const std::vector<std::string>& units, const Progress& progress,
                                             const UnitDone& unit_done = nullptr);

    const TranslateStats& stats() const { return stats_; }
    // During translate_units: the share of the text translated so far (0..1,
    // never decreasing), updated as the model writes, not only per piece.
    // Pieces count by the tokens of their source text; within a piece, the
    // output written so far is compared with the source's length (capped
    // below 100% until the piece is done).
    using Fraction = std::function<void(double done)>;
    void set_fraction_progress(Fraction f) { fraction_ = std::move(f); }
    void set_source(const std::string& code) { prompts_.source = code; opts_.source = code; }

private:
    struct Result {
        std::string text;
        bool ok = true;
    };
    // What fits() measured, so run() needn't tokenize the same text and prompt again.
    struct Tokenized {
        int text = 0;             // tokens in the text to translate
        std::vector<int> prompt;  // the whole prompt, as generate() takes it
    };
    Result run(const std::string& text, const std::string& context, const Tokenized* tok = nullptr);
    Result translate_long_unit(const std::string& unit, const std::string& context);
    bool fits(const std::string& text, const std::string& context, Tokenized* tok = nullptr);
    int tokens(const std::string& text);

    Llm& llm_;
    const ModelInfo& model_;
    TranslateOptions opts_;
    PromptBuilder prompts_;
    TranslateStats stats_;
    bool debug_ = false;

    void report(double weight_done);  // to fraction_, as a share of total_w_
    Fraction fraction_;
    double total_w_ = 0;       // source tokens of all pieces
    double done_w_ = 0;        // ... of the pieces finished
    double piece_w_ = 0;       // ... of the piece being translated
    double piece_done_w_ = 0;  // ... of the runs of that piece finished
    double reported_ = -1;
};

}  // namespace tl
