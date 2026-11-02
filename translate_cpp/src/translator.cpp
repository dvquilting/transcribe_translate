#include "translator.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "languages.h"
#include "text.h"

namespace tl {

// ---------------------------------------------------------------------------
// Prompts
// ---------------------------------------------------------------------------

namespace {

std::string lang_name(const std::string& code) {
    const Language* l = find_language(code);
    return l ? l->name : code;
}

std::string lang_chinese(const std::string& code) {
    const Language* l = find_language(code);
    return l ? l->chinese : code;
}

bool is_chinese(const std::string& code) { return code == "zh" || code == "zh-Hant" || code == "yue"; }

// The chat templates. Control tokens go in special pieces, everything else
// (template text and the user's text) in plain pieces - see Prompt.
Prompt gemma_turn(const std::string& user) {
    Prompt p;
    p.special("<start_of_turn>").text("user\n" + user).special("<end_of_turn>").text("\n");
    p.special("<start_of_turn>").text("model\n");
    return p;
}

Prompt qwen_turn(const std::string& user) {
    // Qwen 3.5 chat format with thinking switched off (the empty think block is
    // what the official template emits for enable_thinking=false).
    Prompt p;
    p.special("<|im_start|>").text("user\n" + user).special("<|im_end|>").text("\n");
    p.special("<|im_start|>").text("assistant\n").special("<think>").text("\n\n").special("</think>").text("\n\n");
    return p;
}

Prompt hy_turn(const std::string& user) {
    // Hy-MT2 chat template: no system prompt, no newlines.
    Prompt p;
    p.special("<|startoftext|>").text(user).special("<|extra_0|>");
    return p;
}

}  // namespace

Prompt PromptBuilder::translation(const std::string& text, const std::string& context) const {
    const bool auto_src = source.empty() || source == "auto";
    switch (family) {
    case Family::TranslateGemma: {
        // The prompt TranslateGemma was trained with (technical report, fig. 3).
        // It needs the source language; "auto" is resolved by detect_language().
        const std::string s = lang_name(source), t = lang_name(target);
        const std::string user = "You are a professional " + s + " (" + source + ") to " + t + " (" + target +
                                 ") translator. Your goal is to accurately convey the meaning and nuances of the "
                                 "original " + s + " text while adhering to " + t +
                                 " grammar, vocabulary, and cultural sensitivities.\nProduce only the " + t +
                                 " translation, without any additional explanations or commentary. Please translate "
                                 "the following " + s + " text into " + t + ":\n\n\n" + text;
        return gemma_turn(user);
    }
    case Family::HyMT: {
        // Hy-MT2's templates: Chinese wording when Chinese is involved, English otherwise.
        const bool zh = is_chinese(target) || (!auto_src && is_chinese(source));
        std::string user;
        if (zh) {
            const std::string t = lang_chinese(target);
            if (!context.empty())
                user = "【背景信息】\n" + context + "\n\n请结合背景信息将以下文本翻译为" + t +
                       "，注意只需要输出翻译后的结果，不要额外解释：\n\n" + text;
            else
                user = "将以下文本翻译为" + t + "，注意只需要输出翻译后的结果，不要额外解释：\n\n" + text;
        } else {
            const std::string t = lang_name(target);
            if (!context.empty())
                user = "[Background information]\n" + context + "\n\nPlease translate the following text into " + t +
                       ", taking the provided background information into consideration. Only output the "
                       "translated result without any additional explanation:\n\n" + text;
            else
                user = "Translate the following text into " + t +
                       ", only output the translated result without any additional explanation:\n\n" + text;
        }
        return hy_turn(user);
    }
    case Family::Qwen: {
        const std::string t = lang_name(target);
        std::string user = auto_src ? "Translate the text below into " + t + ".\n"
                                    : "Translate the text below from " + lang_name(source) + " into " + t + ".\n";
        user +=
            "- Translate all of it, faithfully and naturally, keeping the meaning, tone and register.\n"
            "- Keep the formatting: line breaks, blank lines between paragraphs, Markdown symbols, numbers and "
            "names.\n"
            "- Reply with the translation only: no notes, explanations or quotation marks around it.\n";
        if (!context.empty())
            user += "\nFor context only (do not translate it), this is the text that comes just before:\n"
                    "<context>\n" + context + "\n</context>\n";
        user += "\nText to translate:\n<text>\n" + text + "\n</text>";
        return qwen_turn(user);
    }
    }
    return Prompt().text(text);
}

Prompt PromptBuilder::language_question(const std::string& sample) const {
    const std::string q =
        "Which language is the following text written in? Reply with only its ISO 639-1 code (for example en, fr, "
        "de, zh, ja).\n\n" + sample;
    switch (family) {
    case Family::TranslateGemma: return gemma_turn(q);
    case Family::HyMT: return hy_turn(q);
    case Family::Qwen: return qwen_turn(q);
    }
    return Prompt().text(q);
}

// ---------------------------------------------------------------------------
// Output clean-up
// ---------------------------------------------------------------------------

namespace {

bool ends_with_colon(const std::string& line) {
    return (!line.empty() && line.back() == ':') ||
           (line.size() >= 3 && line.compare(line.size() - 3, 3, "\xEF\xBC\x9A") == 0);  // ：
}

// Does the line start like a model's preamble ("Here is the translation:")?
bool looks_like_preamble(const std::string& line) {
    std::string l = line;
    for (char& c : l) c = (char)std::tolower((unsigned char)c);  // ASCII only; enough for these openings
    while (!l.empty() && (l[0] == '*' || l[0] == '#' || l[0] == '"' || l[0] == ' ')) l.erase(0, 1);  // **Translation:**
    static const char* const openings[] = {
        "here is", "here's", "here\xE2\x80\x99s", "here are", "sure", "certainly", "of course", "okay", "ok,",
        "translation", "translated", "the translation", "the translated", "below is", "english translation",
        "\xE4\xBB\xA5\xE4\xB8\x8B\xE6\x98\xAF",              // 以下是
        "\xE7\xBF\xBB\xE8\xAF\x91", "\xE7\xBF\xBB\xE8\xAD\xAF",  // 翻译 翻譯
        "\xE8\xAF\x91\xE6\x96\x87", "\xE8\xAD\xAF\xE6\x96\x87",  // 译文 譯文
        "voici", "voil\xC3\xA0", "traduction", "aqu\xC3\xAD", "aqui", "traducci\xC3\xB3n", "tradu\xC3\xA7\xC3\xA3o",
        "hier ist", "hier sind", "\xC3\xBC" "bersetzung", "ecco", "traduzione",
    };
    for (const char* o : openings)
        if (l.compare(0, std::strlen(o), o) == 0) return true;
    return false;
}

}  // namespace

std::string clean_output(const std::string& raw, const std::string& source_text) {
    std::string r = raw;
    // <think>...</think> (Qwen in case thinking is on anyway)
    for (size_t a; (a = r.find("<think>")) != std::string::npos;) {
        const size_t b = r.find("</think>", a);
        r.erase(a, b == std::string::npos ? std::string::npos : b + 8 - a);
    }
    // Chat-format tokens that may leak through.
    for (const char* tok : {"<end_of_turn>", "<|im_end|>", "<|eos|>", "<eos>"}) {
        for (size_t p; (p = r.find(tok)) != std::string::npos;) r.erase(p);
    }
    r = trim(r);
    // Wrapper tags from the Qwen prompt.
    for (const char* tag : {"<text>", "</text>"}) {
        for (size_t p; (p = r.find(tag)) != std::string::npos;) r.erase(p, std::string(tag).size());
    }
    r = trim(r);
    // A chatty first line ("Here is the translation:") that isn't in the source.
    // Only lines that start like such a preamble are dropped, and only when
    // the source doesn't itself begin with a line ending in a colon - a real
    // first line such as "Ingredients:" -> "Zutaten:" must stay.
    const size_t nl = r.find('\n');
    if (nl != std::string::npos) {
        const std::string first = trim(r.substr(0, nl));
        const std::string src_first = trim(source_text.substr(0, source_text.find('\n')));
        if (ends_with_colon(first) && !ends_with_colon(src_first) && utf8_length(first) <= 80 &&
            looks_like_preamble(first) && source_text.find(first) == std::string::npos)
            r = trim(r.substr(nl + 1));
    }
    // Quotes wrapped around the whole answer when the source had none.
    if (r.size() >= 2 && r.front() == '"' && r.back() == '"' && !source_text.empty() && source_text.front() != '"' &&
        std::count(r.begin(), r.end(), '"') == 2)
        r = trim(r.substr(1, r.size() - 2));
    return r;
}

// ---------------------------------------------------------------------------
// Loops
// ---------------------------------------------------------------------------

size_t trailing_loop_length(const std::vector<int>& t, int allowed_repeats) {
    // A period-k pattern repeated R times at the end. Short patterns must repeat
    // more often (a word repeated 3x can be legitimate; 16x is not), and every
    // pattern more often than the source text repeats itself (a song's chorus).
    const size_t n = t.size();
    for (size_t k = 1; k <= 64 && k * 3 <= n; ++k) {
        const size_t reps_needed = std::max<size_t>({3, 48 / k, (size_t)std::max(0, allowed_repeats) + 1});
        if (k * reps_needed > n) continue;
        size_t reps = 1;
        while ((reps + 1) * k <= n) {
            bool same = true;
            for (size_t i = 0; i < k && same; ++i) same = t[n - 1 - i] == t[n - 1 - i - reps * k];
            if (!same) break;
            ++reps;
        }
        if (reps >= reps_needed) return (reps - 1) * k;  // keep one copy
    }
    return 0;
}

size_t reusable_prompt_tokens(const std::vector<int>& cached, const std::vector<int>& prompt) {
    return prompt.size() >= 2 && cached == prompt ? prompt.size() - 1 : 0;
}

int source_repetitions(const std::string& text) {
    // The words as numbers: lower-case ASCII letters and digits, and any other
    // characters, up to spaces or ASCII punctuation; CJK characters one by one
    // (CJK punctuation separates).
    std::vector<int> words;
    std::unordered_map<std::string, int> ids;
    std::string w;
    auto flush = [&] {
        if (w.empty()) return;
        words.push_back(ids.emplace(w, (int)ids.size()).first->second);
        w.clear();
    };
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = (unsigned char)text[i];
        if (c < 0x80) {
            if (std::isalnum(c)) w += (char)std::tolower(c);
            else flush();
            ++i;
            continue;
        }
        const size_t len = std::min<size_t>(c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1, text.size() - i);
        unsigned long cp = 0;
        if (len == 3) cp = ((c & 0x0Ful) << 12) | ((text[i + 1] & 0x3Ful) << 6) | (text[i + 2] & 0x3Ful);
        else if (len == 4) cp = 0x10000;  // CJK extensions and the like
        if (cp >= 0x2E80) {
            flush();
            const bool punct = (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0xFF00 && cp <= 0xFF20);
            if (!punct) w.assign(text, i, len), flush();
        } else {
            w.append(text, i, len);
        }
        i += len;
    }
    flush();
    // A run of L words equal to the word k further on is a period-k stretch
    // of L + k words: L / k + 1 copies of the pattern.
    int best = 1;
    const size_t n = words.size();
    for (size_t k = 1; k <= 64 && 2 * k <= n; ++k) {
        size_t run = 0;
        for (size_t i = 0; i + k < n; ++i) {
            run = words[i] == words[i + k] ? run + 1 : 0;
            best = std::max(best, (int)(run / k) + 1);
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Translator
// ---------------------------------------------------------------------------

Translator::Translator(Llm& llm, const ModelInfo& model, const TranslateOptions& opts)
    : llm_(llm), model_(model), opts_(opts), prompts_{model.family, opts.source, opts.target} {
    debug_ = std::getenv("TRANSLATE_DEBUG") != nullptr;  // print every prompt and raw answer to stderr
    if (!find_language(opts.target)) throw std::runtime_error("unknown target language '" + opts.target + "'");
    if (opts.source != "auto" && !find_language(opts.source))
        throw std::runtime_error("unknown source language '" + opts.source + "'");
    if (const Language* t = find_language(opts.target)) prompts_.target = opts_.target = t->code;
    if (opts.source != "auto") prompts_.source = opts_.source = find_language(opts.source)->code;
}

int Translator::tokens(const std::string& text) { return llm_.count_tokens(text); }

std::string Translator::detect_language(const std::string& sample) {
    std::string s = sample.substr(0, std::min<size_t>(sample.size(), 600));
    while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) s.pop_back();  // don't cut a character
    if (!s.empty() && (unsigned char)s.back() >= 0xC0) s.pop_back();
    Generation g = llm_.generate(prompts_.language_question(s), prompts_.add_bos(), 12, Sampling{});
    return language_from_answer(clean_output(g.text, ""));
}

std::string language_from_answer(const std::string& answer) {
    const std::string a = trim(answer);
    std::string low = a;
    for (char& c : low) c = (char)std::tolower((unsigned char)c);  // ASCII only: native names keep their accents
    auto word_at = [&](size_t p, size_t n) {  // a whole word, not "English" inside "Englishman"
        auto letter = [](unsigned char c) { return std::isalpha(c) || c >= 0x80; };
        return (p == 0 || !letter((unsigned char)low[p - 1])) && (p + n >= low.size() || !letter((unsigned char)low[p + n]));
    };
    // 1. A language name, English or native ("Swedish", "Svenska"), or a common
    //    other name ("Brazilian Portuguese", "Chinese"), anywhere in the answer;
    //    the first one mentioned wins, the longer one on a tie ("Portuguese
    //    (Brazil)" over "Portuguese").
    static const std::pair<const char*, const char*> kOtherNames[] = {
        {"Brazilian Portuguese", "pt-BR"}, {"European Portuguese", "pt-PT"}, {"Portuguese", "pt-BR"},
        {"Simplified Chinese", "zh"},      {"Traditional Chinese", "zh-Hant"}, {"Chinese", "zh"},
        {"Mandarin", "zh"},                {"Castilian", "es-ES"},             {"European Spanish", "es-ES"}};
    std::vector<std::pair<std::string, std::string>> names;  // (name, code)
    for (const auto& l : languages()) names.push_back({l.name, l.code}), names.push_back({l.native, l.code});
    for (const auto& [n, c] : kOtherNames) names.push_back({n, c});
    std::string best;
    size_t best_pos = std::string::npos, best_len = 0;
    for (auto [name, code] : names) {
        for (char& c : name) c = (char)std::tolower((unsigned char)c);
        for (size_t p = low.find(name); p != std::string::npos; p = low.find(name, p + 1)) {
            if (!word_at(p, name.size())) continue;
            if (p < best_pos || (p == best_pos && name.size() > best_len)) best = code, best_pos = p, best_len = name.size();
            break;
        }
    }
    if (!best.empty()) return best;
    // 2. Otherwise only an answer that is a code: "sv", "en-US", "(zh_CN)", "pt."
    std::string code;
    for (char c : a) {
        if (std::isalpha((unsigned char)c) || c == '-' || c == '_') code += c;
        else if (!code.empty()) break;
        else if (c != '(' && c != '"' && c != '\'' && c != '`' && c != '*' && c != ' ') return "";
    }
    const size_t dash = code.find_first_of("-_");
    const std::string base = code.substr(0, dash);
    if (base.size() < 2 || base.size() > 3) return "";  // a word such as "Swedish", not a code
    if (const Language* l = find_language(code)) return l->code;
    if (dash != std::string::npos)
        if (const Language* l = find_language(base)) return l->code;  // en-US -> en
    return "";
}

Translator::Result Translator::run(const std::string& text, const std::string& context, const Tokenized* tok) {
    const Prompt prompt = prompts_.translation(text, context);
    Tokenized own;
    if (!tok) {
        own.text = tokens(text);
        own.prompt = llm_.tokenize_prompt(prompt, prompts_.add_bos());
        tok = &own;
    }
    // Room for languages that need more tokens than the source (e.g. English -> Hindi).
    const int max_tokens = std::max(64, tok->text * 3 + 64);
    // A source that repeats a line or phrase three times or more (a song's
    // chorus) is translated with the same repetition: that is not a loop.
    // (One more copy is allowed, for where the token pattern lines up.)
    const int src_reps = source_repetitions(text);
    const int allowed = src_reps >= 3 ? src_reps + 1 : 0;

    Sampling greedy;
    if (model_.family == Family::HyMT) greedy.repeat_penalty = 1.05f;  // Tencent's recommendation
    Generation g = llm_.generate(prompt, prompts_.add_bos(), max_tokens, greedy, allowed, &tok->prompt);
    if (debug_)
        std::fprintf(stderr, "----- prompt (%d tokens) -----\n%s\n----- answer (%d tokens%s%s) -----\n%s\n-----\n",
                     g.prompt_tokens, prompt.str().c_str(), g.generated_tokens, g.looped ? ", looped" : "",
                     g.truncated ? ", truncated" : "", g.text.c_str());
    stats_.prompt_tokens += g.prompt_tokens;
    stats_.generated_tokens += g.generated_tokens;
    std::string out = clean_output(g.text, text);
    if (!out.empty() && !g.looped && !g.truncated) return {out, true};

    // Second attempt with a little randomness, which usually breaks a loop.
    // (The same prompt tokens: generate() keeps the prompt in the KV cache.)
    Sampling warm;
    warm.temperature = 0.3f;
    warm.top_k = 20;
    warm.top_p = 0.9f;
    warm.repeat_penalty = 1.1f;
    Generation g2 = llm_.generate(prompt, prompts_.add_bos(), max_tokens, warm, allowed, &tok->prompt);
    stats_.prompt_tokens += g2.prompt_tokens;
    stats_.generated_tokens += g2.generated_tokens;
    std::string out2 = clean_output(g2.text, text);
    if (!out2.empty() && !g2.looped && !g2.truncated) return {out2, true};
    return {text, false};  // Both attempts failed: preserve the complete source verbatim.
}

bool Translator::fits(const std::string& text, const std::string& context, Tokenized* tok) {
    const int t = tokens(text);
    if (t > model_.chunk_tokens) return false;
    std::vector<int> prompt = llm_.tokenize_prompt(prompts_.translation(text, context), prompts_.add_bos());
    if ((int)prompt.size() + std::max(64, t * 3 + 64) + 16 > llm_.context_size()) return false;
    if (tok) *tok = {t, std::move(prompt)};
    return true;
}

Translator::Result Translator::translate_long_unit(const std::string& unit, const std::string& context) {
    // Bound even a single huge sentence by its actual tokens and the full prompt.
    // UTF-8 boundaries ensure a split cannot corrupt a character.
    const std::string sep = is_cjk_language(opts_.target) ? "" : " ";
    std::string out, ctx = tail_at_word(context, 800);
    // Most units that come here (the fallback paths) fit in one piece: one
    // check instead of a search that tokenizes the whole prompt every step.
    Tokenized tok;
    if (fits(unit, ctx, &tok)) return run(unit, ctx, &tok);
    // The search below asks the same as fits(), but tokenizes the prompt
    // around the text only once per context and then just the text at each
    // step. Tokens can merge across the two joins (whitespace at the ends of
    // the text most of all), so the sum decides only when it is clear of the
    // limit by a margin; near the limit, fits() counts exactly.
    std::string frame_ctx;
    int frame = -1;  // prompt tokens without the text, for frame_ctx
    auto probe = [&](const std::string& text) {
        const int t = tokens(text);
        if (t > model_.chunk_tokens) return false;
        if (frame < 0 || frame_ctx != ctx) {
            frame = llm_.count_prompt_tokens(prompts_.translation("", ctx), prompts_.add_bos());
            frame_ctx = ctx;
        }
        int margin = 8;
        size_t a = 0, b = text.size();
        while (a < b && std::isspace((unsigned char)text[a])) ++a, ++margin;
        while (b > a && std::isspace((unsigned char)text[b - 1])) --b, ++margin;
        const int need = frame + t + std::max(64, t * 3 + 64) + 16;
        if (need + margin <= llm_.context_size()) return true;
        if (need - margin > llm_.context_size()) return false;
        return fits(text, ctx);
    };
    std::vector<size_t> ends;
    for (size_t i = 1; i <= unit.size(); ++i)
        if (i == unit.size() || ((unsigned char)unit[i] & 0xC0) != 0x80) ends.push_back(i);
    size_t at = 0;
    while (at < unit.size()) {
        const size_t first = (size_t)(std::upper_bound(ends.begin(), ends.end(), at) - ends.begin());
        // Context is optional; don't let it prevent even one character fitting.
        if (!probe(unit.substr(at, ends[first] - at))) ctx.clear();
        // Grow the search window instead of tokenizing half of the remaining
        // document for every chunk (quadratic work on very long paragraphs).
        size_t span = 1;
        while (span < ends.size() - first && probe(unit.substr(at, ends[first + span - 1] - at)))
            span = std::min(span * 2, ends.size() - first);
        size_t lo = first, hi = first + span;
        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            if (probe(unit.substr(at, ends[mid] - at))) lo = mid + 1;
            else hi = mid;
        }
        if (lo == first) throw std::runtime_error("model context is too small for the translation prompt");
        size_t end = ends[lo - 1];
        // Prefer a word boundary near the limit, including for punctuation-free text.
        if (end < unit.size()) {
            const size_t word = unit.find_last_of(" \t\n", end - 1);
            if (word != std::string::npos && word > at + (end - at) / 2) end = word + 1;
        }
        std::string chunk = unit.substr(at, end - at);
        // Token counts are not strictly monotonic at merges; verify the chosen
        // boundary (exactly - this also tokenizes the prompt for run()).
        while (!fits(chunk, ctx, &tok) && end > at) {
            --end;
            while (end > at && ((unsigned char)unit[end] & 0xC0) == 0x80) --end;
            chunk = unit.substr(at, end - at);
        }
        if (end == at) throw std::runtime_error("model context is too small for the translation prompt");
        Result r = run(chunk, ctx, &tok);
        if (!r.ok) return {unit, false};  // Never retain half of a long unit.
        out += (out.empty() ? "" : sep) + r.text;
        if (opts_.use_context && model_.family != Family::TranslateGemma) ctx = tail_at_word(chunk, 800);
        at = end;
    }
    return {out, true};
}

std::vector<std::string> Translator::translate_units(const std::vector<std::string>& units, const Progress& progress,
                                                     const UnitDone& unit_done) {
    std::vector<std::string> result(units.size());
    const bool want_context = opts_.use_context && model_.family != Family::TranslateGemma;

    // Plan the pieces: consecutive translatable units up to chunk_tokens each.
    struct Piece {
        std::vector<size_t> idx;
        bool long_unit = false;
    };
    std::vector<Piece> pieces;
    Piece cur;
    int cur_tokens = 0;
    for (size_t i = 0; i < units.size(); ++i) {
        if (!has_letters(units[i])) {
            result[i] = units[i];  // numbers, punctuation, empty: as they are
            continue;
        }
        const int t = tokens(units[i]);
        if (t > model_.chunk_tokens) {
            if (!cur.idx.empty()) pieces.push_back(cur), cur = {}, cur_tokens = 0;
            pieces.push_back({{i}, true});
            continue;
        }
        if (!cur.idx.empty() && cur_tokens + t > model_.chunk_tokens) pieces.push_back(cur), cur = {}, cur_tokens = 0;
        cur.idx.push_back(i);
        cur_tokens += t + 2;
    }
    if (!cur.idx.empty()) pieces.push_back(cur);
    stats_.pieces += (int)pieces.size();

    // Untranslated units can be reported right away.
    if (unit_done)
        for (size_t i = 0; i < units.size(); ++i)
            if (!has_letters(units[i])) unit_done(i, result[i]);

    std::string context;
    auto tail = [](const std::string& s) { return tail_at_word(s, 800); };  // last ~800 bytes, from a word

    for (size_t p = 0; p < pieces.size(); ++p) {
        const Piece& piece = pieces[p];
        if (piece.long_unit) {
            const size_t i = piece.idx[0];
            Result r = translate_long_unit(units[i], want_context ? context : "");
            result[i] = r.text;
            if (!r.ok) ++stats_.problems;
            if (unit_done) unit_done(i, result[i]);
        } else {
            std::string joined;
            for (size_t i : piece.idx) joined += (joined.empty() ? "" : "\n\n") + units[i];
            Tokenized tok;
            if (!fits(joined, want_context ? context : "", &tok)) {
                for (size_t i : piece.idx) {
                    Result r = translate_long_unit(units[i], want_context ? context : "");
                    result[i] = r.text;
                    if (!r.ok) ++stats_.problems;
                    if (unit_done) unit_done(i, result[i]);
                    if (want_context) context = tail(units[i]);
                }
                if (progress) progress((int)p + 1, (int)pieces.size());
                continue;
            }
            Result r = run(joined, want_context ? context : "", &tok);
            std::vector<std::string> parts = split_blank_lines(r.text);
            if (piece.idx.size() == 1) {
                // One unit: blank lines the model added are not paragraph breaks here.
                std::string one;
                for (const auto& s : parts) one += (one.empty() ? "" : "\n") + s;
                parts = {one};
            }
            if (parts.size() == piece.idx.size() && (r.ok || piece.idx.size() == 1)) {
                if (!r.ok) ++stats_.problems;
                for (size_t k = 0; k < parts.size(); ++k) {
                    result[piece.idx[k]] = r.ok ? parts[k] : units[piece.idx[k]];
                    if (unit_done) unit_done(piece.idx[k], result[piece.idx[k]]);
                }
            } else {
                // The model merged or split paragraphs, or a batch of several failed
                // (looped or cut off): redo this piece unit by unit, so one bad
                // paragraph doesn't cost the others their translation.
                ++stats_.split_retries;
                std::string ctx = want_context ? context : "";
                for (size_t i : piece.idx) {
                    Result u = translate_long_unit(units[i], ctx);
                    if (!u.ok) ++stats_.problems;
                    std::string one;
                    for (const auto& s : split_blank_lines(u.text)) one += (one.empty() ? "" : "\n") + s;
                    result[i] = u.ok ? one : units[i];
                    if (unit_done) unit_done(i, result[i]);
                    if (want_context) ctx = tail(units[i]);
                }
            }
        }
        if (want_context) {
            std::string src;
            for (size_t i : piece.idx) src += (src.empty() ? "" : "\n\n") + units[i];
            context = tail(src);
        }
        if (progress) progress((int)p + 1, (int)pieces.size());
    }
    return result;
}

}  // namespace tl
