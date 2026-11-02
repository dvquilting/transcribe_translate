#include "llm.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "crispasr_api.h"
#include "transcript.h"

namespace tmx {

const ModelInfo& default_llm_model() {
    // Qwen3-4B-Instruct-2507: Apache-2.0, non-thinking (answers directly), strong
    // multilingual editing for its size; Q4_K_M fits alongside nothing else in ~4 GB.
    static const ModelInfo m = {
        "qwen3-4b", "Qwen3-4B-Instruct-2507", "", "Qwen3-4B-Instruct-2507-Q4_K_M.gguf",
        "https://huggingface.co/unsloth/Qwen3-4B-Instruct-2507-GGUF/resolve/main/Qwen3-4B-Instruct-2507-Q4_K_M.gguf",
        "2.5 GB", false, 0.0, false, "Spelling/grammar correction, summaries and paragraph breaks."};
    return m;
}

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------

LlmSession::LlmSession(const std::string& model_path, const LlmOptions& opts) {
    const CrispApi& api = crispasr();
    crispasr_chat_open_params p;
    api.crispasr_chat_open_params_default(&p);
    p.n_ctx = opts.n_ctx;  // never leave at 0: the model default can be 256k tokens
    if (opts.threads > 0) p.n_threads = p.n_threads_batch = opts.threads;
    p.n_gpu_layers = opts.device == "cpu" ? 0 : -1;
    crispasr_chat_error err{};
    s_ = api.crispasr_chat_open(model_path.c_str(), &p, &err);
    if (!s_)
        throw std::runtime_error("could not load the language model '" + model_path + "': " +
                                 (err.message[0] ? err.message : "unknown error"));
    n_ctx_ = api.crispasr_chat_n_ctx(s_);
}

LlmSession::~LlmSession() {
    if (s_) crispasr().crispasr_chat_close(s_);
}

std::string LlmSession::ask(const std::string& system, const std::string& user, int max_tokens, float temperature) {
    const CrispApi& api = crispasr();
    // The prompt plus the answer must fit in the context window, or llama.cpp
    // fails mid-prefill. Callers keep prompts small; this is the safety net.
    const int n_prompt = count_tokens(system, user);
    if (n_prompt + 32 > n_ctx_)
        throw std::runtime_error("the text sent to the language model is too long (" + std::to_string(n_prompt) +
                                 " tokens, context " + std::to_string(n_ctx_) + ")");
    max_tokens = std::min(max_tokens, n_ctx_ - n_prompt - 8);
    const crispasr_chat_message msgs[2] = {{"system", system.c_str()}, {"user", user.c_str()}};
    crispasr_chat_generate_params g;
    api.crispasr_chat_generate_params_default(&g);
    g.max_tokens = max_tokens;
    g.temperature = temperature;
    g.repeat_penalty = 1.0f;  // a penalty would push an editor away from copying the text
    g.seed = 42;
    crispasr_chat_error err{};
    char* out = api.crispasr_chat_generate(s_, msgs, 2, &g, &err);
    if (!out) throw std::runtime_error(std::string("language model failed: ") + err.message);
    std::string r(out);
    api.crispasr_chat_string_free(out);
    return strip_reasoning(r);
}

int LlmSession::count_tokens(const std::string& system, const std::string& user) {
    const crispasr_chat_message msgs[2] = {{"system", system.c_str()}, {"user", user.c_str()}};
    crispasr_chat_error err{};
    const int n = crispasr().crispasr_chat_count_tokens(s_, msgs, 2, &err);
    // Fallback estimate if counting fails: ~1.4 tokens per word.
    return n >= 0 ? n : (int)(count_words(system + " " + user) * 1.4) + 32;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string strip_reasoning(const std::string& s) {
    std::string r = s;
    for (size_t a; (a = r.find("<think>")) != std::string::npos;) {
        const size_t b = r.find("</think>", a);
        r.erase(a, b == std::string::npos ? std::string::npos : b + 8 - a);
    }
    const size_t first = r.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const size_t last = r.find_last_not_of(" \t\r\n");
    return r.substr(first, last - first + 1);
}

int count_words(const std::string& s) {
    int n = 0;
    bool in = false;
    for (unsigned char c : s) {
        const bool sp = c == ' ' || c == '\t' || c == '\n' || c == '\r';
        if (!sp && !in) ++n;
        in = !sp;
    }
    return n;
}

namespace {

// Split a long run of words into pieces of at most `max_words`.
std::vector<std::string> split_words(const std::string& text, int max_words) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string w, cur;
    int n = 0;
    while (in >> w) {
        cur += (cur.empty() ? "" : " ") + w;
        if (++n >= max_words) out.push_back(cur), cur.clear(), n = 0;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// If the model opened with a chatty preface ("Here is the corrected text:"), drop it.
std::string drop_preface(const std::string& out, const std::string& original) {
    const size_t nl = out.find('\n');
    if (nl == std::string::npos) return out;
    const std::string first = normalize_space(out.substr(0, nl));
    if (!first.empty() && first.back() == ':' && original.find(first) == std::string::npos)
        return strip_reasoning(out.substr(nl + 1));
    return out;
}

const char* kPolishSystem =
    "You are a meticulous copy editor for speech transcripts produced by speech recognition. "
    "Correct spelling, punctuation, capitalization and clear grammatical errors, and fix words the "
    "recognizer obviously misheard when the intended word is certain from context.\n"
    "Rules:\n"
    "- Keep the speaker's own words and word order. Do not paraphrase, summarize, shorten, expand, "
    "translate or add anything.\n"
    "- Keep names, numbers and quotations as written unless they are clearly misspelled.\n"
    "- Keep the paragraph breaks (blank lines) exactly as in the input.\n"
    "- Reply with the corrected text only, with no introduction, notes or comments.";

const char* kSummarySystem =
    "You summarize transcripts of recorded speech. Write in the same language as the transcript. "
    "Be accurate: include only what the transcript says, and do not invent names, numbers or facts.";

const char* kSummaryFormat =
    "Write a summary of this transcript: first a short overview of two to four sentences, then a line "
    "\"Key points:\" followed by up to ten bullet points, each starting with \"- \". "
    "Reply with the summary only.";

const char* kPartFormat =
    "This is one part of a longer transcript. List its main points as concise bullet points, each "
    "starting with \"- \", keeping important names, numbers and conclusions. Reply with the bullet "
    "points only.";

// Correct one piece of text. If the prompt plus the expected answer would not
// fit in the context window, the piece is halved and each half corrected.
std::string polish_piece(ChatModel& llm, const std::string& input, PolishStats& st) {
    const int w_in = count_words(input);
    const int n_in = llm.count_tokens(kPolishSystem, input);
    const int n_text = n_in - llm.count_tokens(kPolishSystem, "");
    const int max_tokens = std::max((int)(w_in * 2.2), (int)(n_text * 1.3)) + 96;
    if (n_in + max_tokens > llm.context_size() && w_in > 8) {
        // Split between paragraphs when there are several, else between words.
        const auto paras = split_paragraphs(input);
        std::vector<std::string> halves;
        if (paras.size() > 1) {
            std::string a, b;
            for (size_t i = 0; i < paras.size(); ++i)
                (i < paras.size() / 2 ? a : b) += ((i < paras.size() / 2 ? a : b).empty() ? "" : "\n\n") + paras[i];
            halves = {a, b};
        } else {
            halves = split_words(input, (w_in + 1) / 2);
        }
        const char* sep = paras.size() > 1 ? "\n\n" : " ";
        std::string r;
        for (const auto& h : halves) r += (r.empty() ? "" : sep) + polish_piece(llm, h, st);
        return r;
    }
    std::string out = drop_preface(llm.ask(kPolishSystem, input, max_tokens, 0.0f), input);
    const int w_out = count_words(out);
    // Reject edits that changed the amount of content: a cleanup should keep
    // almost every word. Fall back to the original text for this piece.
    const int allowed = std::max(4, (int)std::lround(w_in * 0.12));
    if (w_out == 0 || std::abs(w_out - w_in) > allowed) {
        out = input;
        ++st.kept_original;
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Spelling / grammar cleanup
// ---------------------------------------------------------------------------

std::string polish_transcript(ChatModel& llm, const std::string& text, LlmProgress progress, PolishStats* stats) {
    constexpr int kChunkWords = 300;  // small chunks keep the model faithful and output bounded

    // Group paragraphs into chunks of up to ~kChunkWords words (a very long paragraph
    // is split on its own, and its pieces are joined again with spaces).
    const std::vector<TextChunk> chunks = chunk_paragraphs(text, kChunkWords);

    PolishStats st;
    st.chunks = (int)chunks.size();
    std::vector<std::string> outputs;
    for (size_t i = 0; i < chunks.size(); ++i) {
        PolishStats piece;  // counts rejected pieces; report each chunk at most once
        outputs.push_back(polish_piece(llm, chunks[i].text, piece));
        if (piece.kept_original > 0) ++st.kept_original;
        if (progress) progress((int)i + 1, (int)chunks.size());
    }
    if (stats) *stats = st;
    return join_chunks(chunks, outputs) + "\n";
}

// ---------------------------------------------------------------------------
// Paragraph breaks
// ---------------------------------------------------------------------------

namespace {

const char* kParagraphSystem =
    "You divide transcripts of recorded speech into paragraphs so they are easy to read.";

const char* kParagraphTask =
    "Below are numbered sentences from a transcript. Decide where new paragraphs should start, as a careful "
    "editor would: where the topic changes, a new speaker or a new step of the argument begins, or the speaker "
    "starts something new. Paragraphs are usually three to eight sentences long. A note such as "
    "\"(pause 2.3 s)\" marks a silence before that sentence; a long pause often, but not always, means a new "
    "paragraph.\n"
    "Reply with only the numbers of the sentences that should START a new paragraph, separated by commas, "
    "for example: 5, 9, 16. Do not include sentence 1.";

constexpr double kPauseHint = 1.0;  // seconds; shorter pauses are not shown to the model

}  // namespace

std::vector<int> smart_paragraph_breaks(ChatModel& llm, const std::vector<std::string>& sentences,
                                        const std::vector<double>& pauses, int fallback_every, LlmProgress progress,
                                        ParagraphStats* stats) {
    constexpr int kWindowSentences = 50;
    constexpr int kOverlap = 4;  // sentences of context shared with the neighbouring window
    const int n = (int)sentences.size();
    const bool have_pauses = (int)pauses.size() == n;
    fallback_every = std::max(1, fallback_every);
    ParagraphStats st;
    std::vector<int> breaks;
    if (n < 3) {
        if (stats) *stats = st;
        return breaks;
    }

    // Window size: at most 900 words, and what fits in the context window next
    // to the task and the answer. Sentences are measured in tokens one by one, so
    // text without spaces (Chinese, Japanese) is measured correctly too.
    constexpr int kWindowWords = 900;
    const int base = llm.count_tokens(kParagraphSystem, "");
    const int room = (int)((llm.context_size() - llm.count_tokens(kParagraphSystem, kParagraphTask) -
                            (kWindowSentences * 4 + 32) - 64) * 0.9);
    std::vector<int> words(n), tokens(n);
    for (int i = 0; i < n; ++i) {
        words[i] = count_words(sentences[i]);
        // + the "[12] (pause 2.3 s) " numbering and note
        tokens[i] = std::max(1, llm.count_tokens(kParagraphSystem, sentences[i]) - base) + 10;
    }

    // Plan windows first so progress can show a total.
    struct Window { int a, b, keep_from, keep_to; };
    std::vector<Window> windows;
    // Each window keeps the breaks in [keep_from, keep_to); the kept ranges tile
    // the transcript, and a few sentences either side are shown as context.
    for (int keep_from = 0, a = 0;;) {
        // (A sentence too long for a window on its own gets a window of its own.)
        int b = a, w_sum = 0, t_sum = 0;
        while (b < n && b - a < kWindowSentences &&
               (b == a || (w_sum + words[b] <= kWindowWords && t_sum + tokens[b] <= room)))
            w_sum += words[b], t_sum += tokens[b++];
        const bool last = b >= n;
        const int keep_to = last ? n : std::max(keep_from + 1, b - std::min(kOverlap, (b - a) / 4));
        windows.push_back({a, b, keep_from, keep_to});
        if (last) break;
        keep_from = keep_to;
        a = std::max(0, keep_to - kOverlap);
    }

    for (size_t w = 0; w < windows.size(); ++w) {
        const Window& win = windows[w];
        std::string prompt = std::string(kParagraphTask) + "\n\n";
        for (int i = win.a; i < win.b; ++i) {
            prompt += "[" + std::to_string(i - win.a + 1) + "] ";
            if (have_pauses && i > win.a && pauses[i] >= kPauseHint) {
                char note[32];
                std::snprintf(note, sizeof(note), "(pause %.1f s) ", pauses[i]);
                prompt += note;
            }
            prompt += sentences[i] + "\n";
        }
        const int count = win.b - win.a;
        const int answer = count * 4 + 32;
        // A prompt that doesn't fit the context window (a very long sentence) is
        // not sent: the fixed paragraph size is used there instead of failing.
        // A single sentence has no break to ask about.
        const bool too_long = llm.count_tokens(kParagraphSystem, prompt) + answer + 8 > llm.context_size();
        if (too_long) ++st.skipped;
        const std::vector<int> nums = count >= 2 && !too_long
                                          ? parse_number_list(llm.ask(kParagraphSystem, prompt, answer, 0.0f))
                                          : std::vector<int>{};

        std::vector<int> found;
        for (int k : nums) {
            const int i = win.a + k - 1;  // back to a global sentence index
            if (k >= 2 && k <= count && i >= win.keep_from && i < win.keep_to) found.push_back(i);
        }
        // A window with no usable answer, though long enough to need one, falls
        // back to the fixed split (tidying below still applies).
        if (found.empty() && (too_long || count >= 2 * fallback_every) &&
            std::none_of(nums.begin(), nums.end(), [&](int k) { return k >= 2 && k <= count; })) {
            if (!too_long) ++st.fallback;
            for (int i = win.keep_from; i < win.keep_to; ++i)
                if (i > 0 && i % fallback_every == 0) found.push_back(i);
        }
        breaks.insert(breaks.end(), found.begin(), found.end());
        ++st.windows;
        if (progress) progress((int)w + 1, (int)windows.size());
    }
    const int max_sentences = std::max(12, 2 * fallback_every);
    breaks = tidy_paragraph_breaks(breaks, sentences, have_pauses ? pauses : std::vector<double>{}, max_sentences, 250);
    if (stats) *stats = st;
    return breaks;
}

// ---------------------------------------------------------------------------
// Summary
// ---------------------------------------------------------------------------

std::string summarize_transcript(ChatModel& llm, const std::string& text, LlmProgress progress) {
    constexpr int kAnswer = 1000;     // tokens allowed for the final summary
    constexpr int kPartAnswer = 600;  // tokens allowed for the notes on one part
    const std::string final_intro = std::string(kSummaryFormat) + "\n\nTranscript:\n";
    const std::string notes_intro = std::string(kSummaryFormat) +
                                    "\n\nThe transcript was long, so here are notes taken from each part of it, "
                                    "in order:\n";
    const std::string part_intro = std::string(kPartFormat) + "\n\nTranscript part:\n";
    // Tokens left for transcript text once the instructions and the answer are counted.
    auto room = [&](const std::string& intro, int answer) {
        return llm.context_size() - llm.count_tokens(kSummarySystem, intro) - answer - 64;
    };
    const int final_room = room(final_intro, kAnswer);
    const int notes_room = room(notes_intro, kAnswer);
    const int part_room = room(part_intro, kPartAnswer);
    if (std::min({final_room, notes_room, part_room}) < 256)
        throw std::runtime_error("the language model's context window is too small for a summary");

    const std::string flat = normalize_space(text);
    const int intro_tokens = llm.count_tokens(kSummarySystem, final_intro);
    const int text_tokens = llm.count_tokens(kSummarySystem, final_intro + flat) - intro_tokens;
    if (text_tokens <= final_room) {
        if (progress) progress(0, 1);
        std::string s = llm.ask(kSummarySystem, final_intro + flat, kAnswer, 0.2f);
        if (progress) progress(1, 1);
        return s + "\n";
    }

    // Too long for one pass: take notes on each part, then summarise the notes
    // (reducing the notes again while they are still too long).
    auto split_to_fit = [&](const std::string& t, int t_tokens) {
        const double per_word = std::max(1.0, (double)t_tokens / std::max(1, count_words(t)));
        return split_words(t, std::max(50, (int)(part_room * 0.9 / per_word)));
    };
    std::vector<std::string> parts = split_to_fit(flat, text_tokens);
    int done = 0;
    const int planned = (int)parts.size() + 1;
    std::string notes;
    for (int round = 0;; ++round) {
        notes.clear();
        for (const auto& part : parts) {
            const std::string n = llm.ask(kSummarySystem, part_intro + part, kPartAnswer, 0.2f);
            notes += (notes.empty() ? "" : "\n") + n;
            if (progress) progress(std::min(++done, planned - 1), planned);
        }
        const int notes_tokens = llm.count_tokens(kSummarySystem, notes_intro + notes) -
                                 llm.count_tokens(kSummarySystem, notes_intro);
        if (notes_tokens <= notes_room) break;
        if (round >= 4) {  // notes are not shrinking: keep what fits
            notes = split_to_fit(notes, notes_tokens).front();
            break;
        }
        parts = split_to_fit(notes, notes_tokens);
    }

    std::string s = llm.ask(kSummarySystem, notes_intro + notes, kAnswer, 0.2f);
    if (progress) progress(planned, planned);
    return s + "\n";
}

}  // namespace tmx
