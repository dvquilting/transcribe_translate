#include "transcript.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <sstream>

namespace tmx {

static bool is_space(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

static std::vector<std::string> split_words(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (unsigned char c : s) {
        if (is_space(c)) {
            if (!cur.empty()) out.push_back(std::move(cur)), cur.clear();
        } else {
            cur += (char)c;
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

// Closing quotes and brackets that may follow a sentence end: " ' ) ] and
// U+201D U+2019 U+00BB U+300D U+300F U+FF09
static const char* const kClosers[] = {"\"", "'", ")", "]", "\xE2\x80\x9D", "\xE2\x80\x99", "\xC2\xBB",
                                       "\xE3\x80\x8D", "\xE3\x80\x8F", "\xEF\xBC\x89"};
// Full-width (CJK) sentence ends: U+3002 U+FF1F U+FF01
static const char* const kWideEnds[] = {"\xE3\x80\x82", "\xEF\xBC\x9F", "\xEF\xBC\x81"};

static bool has_at(const std::string& s, size_t i, const char* m) { return s.compare(i, std::strlen(m), m) == 0; }

static bool has_before(const std::string& s, size_t n, const char* m) {
    const size_t len = std::strlen(m);
    return n >= len && s.compare(n - len, len, m) == 0;
}

// Length of `w` without the closing quotes and brackets at its end.
static size_t without_closers(const std::string& w) {
    size_t n = w.size();
    for (bool more = true; more;) {
        more = false;
        for (const char* c : kClosers)
            if (has_before(w, n, c)) {
                n -= std::strlen(c);
                more = true;
                break;
            }
    }
    return n;
}

static bool ends_wide(const std::string& w) {
    const size_t n = without_closers(w);
    for (const char* e : kWideEnds)
        if (has_before(w, n, e)) return true;
    return false;
}

// A word ending in . ? ! an ellipsis (U+2026) or a full-width sentence end
// (closing quotes may follow).
static bool ends_sentence(const std::string& w) {
    const size_t n = without_closers(w);
    if (n == 0) return false;
    const char c = w[n - 1];
    return c == '.' || c == '?' || c == '!' || has_before(w, n, "\xE2\x80\xA6") || ends_wide(w.substr(0, n));
}

// Chinese and Japanese put no space between sentences: cut a word after each
// full-width sentence end (and the closing quotes after it).
static std::vector<std::string> split_wide_sentences(const std::string& w) {
    auto wide_end_at = [&](size_t i) {
        for (const char* e : kWideEnds)
            if (has_at(w, i, e)) return std::strlen(e);
        return size_t(0);
    };
    std::vector<std::string> out;
    size_t from = 0;
    for (size_t i = 0; i < w.size();) {
        const size_t len = wide_end_at(i);
        if (!len) {
            ++i;
            continue;
        }
        i += len;
        for (bool more = true; more;) {
            more = false;
            for (const char* c : kClosers)
                if (has_at(w, i, c)) {
                    i += std::strlen(c);
                    more = true;
                    break;
                }
        }
        if (i < w.size() && !wide_end_at(i)) out.push_back(w.substr(from, i - from)), from = i;  // U+FF01 U+FF1F stays together
    }
    out.push_back(w.substr(from));
    return out;
}

static std::string join_words(const std::vector<Word>& ws) {
    std::string s;
    for (const auto& w : ws) {
        if (!s.empty()) s += ' ';
        s += w.text;
    }
    return s;
}

std::vector<Segment> words_to_cues(const std::vector<Word>& words) {
    std::vector<Segment> cues;
    std::vector<Word> cur;
    size_t cur_len = 0;

    auto flush = [&] {
        if (cur.empty()) return;
        Segment s;
        s.start = cur.front().start;
        s.end = cur.back().end;
        s.text = join_words(cur);
        s.words = cur;
        cues.push_back(std::move(s));
        cur.clear();
        cur_len = 0;
    };

    for (const auto& w : words) {
        if (w.text.empty()) continue;
        const size_t wlen = utf8_length(w.text);
        if (!cur.empty()) {
            const double gap = w.start - cur.back().end;
            if (gap > kCueMaxGap || cur_len + 1 + wlen > kCueMaxChars || w.end - cur.front().start > kCueMaxSeconds)
                flush();
        }
        cur_len += (cur.empty() ? 0 : 1) + wlen;
        cur.push_back(w);
        if (ends_sentence(w.text) && cur.back().end - cur.front().start >= 1.0) flush();
    }
    flush();
    return cues;
}

std::vector<std::string> wrap_text(const std::string& text, size_t width) {
    std::vector<std::string> lines;
    std::string line;
    size_t line_len = 0;
    for (const auto& w : split_words(text)) {
        const size_t wl = utf8_length(w);
        if (!line.empty() && line_len + 1 + wl > width) {
            lines.push_back(std::move(line));
            line.clear();
            line_len = 0;
        }
        if (!line.empty()) line += ' ', ++line_len;
        line += w;
        line_len += wl;
    }
    if (!line.empty()) lines.push_back(std::move(line));
    return lines;
}

std::string format_srt(const std::vector<Segment>& cues) {
    std::vector<pk::SrtCue> out;
    for (const auto& cue : cues) {
        // Line breaks in the text are kept (one speaker per line, a note above the
        // text); each line is wrapped on its own.
        pk::SrtCue c;
        size_t b = 0;
        while (b <= cue.text.size()) {
            const size_t e = std::min(cue.text.find('\n', b), cue.text.size());
            for (const auto& l : wrap_text(normalize_space(cue.text.substr(b, e - b)), kCueLineWidth))
                if (!l.empty()) c.text += (c.text.empty() ? "" : "\n") + l;
            b = e + 1;
        }
        if (c.text.empty()) continue;
        c.index = std::to_string(out.size() + 1);
        c.timing = pk::srt_timing(cue.start, std::max(cue.end, cue.start + 0.2));
        out.push_back(std::move(c));
    }
    return pk::format_srt(out);
}

std::string space_before_numbers(const std::string& text) {
    static const char* const kept[] = {"ipv", "utf", "sha", "base", "web", "crc", "rfc", "vp", "md"};
    std::string out;
    out.reserve(text.size() + 8);
    size_t letters = 0;      // letters in the run that ends here
    size_t run_start = 0;    // where that run starts in `out`
    bool small_last = false; // ... and it ends in a small letter
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = (unsigned char)text[i];
        // Letters: ASCII, and the two-byte ones (Latin with accents, Greek, Cyrillic).
        const bool ascii_letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool wide_letter = c >= 0xC3 && c <= 0xD3 && i + 1 < text.size() && ((unsigned char)text[i + 1] & 0xC0) == 0x80;
        if (ascii_letter || wide_letter) {
            if (letters == 0) run_start = out.size();
            ++letters;
            small_last = wide_letter || (c >= 'a' && c <= 'z');
            out.append(text, i, wide_letter ? 2 : 1);
            i += wide_letter ? 2 : 1;
            continue;
        }
        if (c >= '0' && c <= '9' && letters >= 3 && small_last) {
            // The digits must end the word: "2024", "2024.", "12)" - not "64bit".
            size_t e = i;
            while (e < text.size() && ((text[e] >= '0' && text[e] <= '9') ||
                                       ((text[e] == '.' || text[e] == ',' || text[e] == ':') && e + 1 < text.size() &&
                                        text[e + 1] >= '0' && text[e + 1] <= '9')))
                ++e;
            const unsigned char after = e < text.size() ? (unsigned char)text[e] : ' ';
            const bool ends_word = !((after >= 'a' && after <= 'z') || (after >= 'A' && after <= 'Z') || after >= 0x80);
            std::string word = out.substr(run_start);
            for (char& ch : word)
                if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
            bool keep = false;
            for (const char* k : kept) keep = keep || word == k;
            // Only a whole word: not the letters after other characters ("x-ray2").
            const bool whole = run_start == 0 || out[run_start - 1] == ' ' || out[run_start - 1] == '\n' ||
                               out[run_start - 1] == '(' || out[run_start - 1] == '"' || out[run_start - 1] == '\'';
            if (ends_word && !keep && whole) out += ' ';
        }
        letters = 0;
        out += (char)c;
        ++i;
    }
    return out;
}

std::vector<std::string> split_sentences(const std::string& text, int max_words) {
    std::vector<std::string> sentences;
    std::string cur;
    int n = 0;
    for (const auto& word : split_words(text)) {
        bool first = true;
        for (const auto& w : split_wide_sentences(word)) {
            if (!cur.empty() && first) cur += ' ';
            first = false;
            cur += w;
            ++n;
            if (ends_sentence(w) || (max_words > 0 && n >= max_words))
                sentences.push_back(std::move(cur)), cur.clear(), n = 0;
        }
    }
    if (!cur.empty()) sentences.push_back(std::move(cur));
    return sentences;
}

namespace {
size_t visible_chars(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s)
        if (!is_space(c) && (c & 0xC0) != 0x80) ++n;
    return n;
}
}  // namespace

std::vector<double> sentence_pauses(const std::vector<std::string>& sentences, const std::vector<Word>& words) {
    if (sentences.empty() || words.empty()) return {};
    // Cumulative character offsets (spaces ignored) of each timed word's end.
    std::vector<size_t> word_end(words.size());
    size_t total_w = 0;
    for (size_t i = 0; i < words.size(); ++i) word_end[i] = (total_w += visible_chars(words[i].text));
    size_t total_s = 0;
    for (const auto& s : sentences) total_s += visible_chars(s);
    // Different text (e.g. alignment failed for part of it): don't guess.
    if (total_w == 0 || std::fabs((double)total_w - (double)total_s) > 0.03 * (double)total_s + 2) return {};
    const double scale = (double)total_w / (double)total_s;

    // Index of the timed word containing character `c` (0-based, scaled).
    auto word_at = [&](double c) {
        const size_t pos = (size_t)std::max(0.0, c * scale);
        const auto it = std::upper_bound(word_end.begin(), word_end.end(), pos);
        return (size_t)std::min<std::ptrdiff_t>(it - word_end.begin(), (std::ptrdiff_t)words.size() - 1);
    };
    std::vector<double> pauses(sentences.size(), -1.0);
    size_t offset = 0;
    for (size_t i = 0; i < sentences.size(); ++i) {
        if (i > 0) {
            const size_t first = word_at((double)offset);
            const size_t prev = word_at((double)offset - 1);
            pauses[i] = first > prev ? std::max(0.0, words[first].start - words[prev].end) : 0.0;
        }
        offset += visible_chars(sentences[i]);
    }
    return pauses;
}

std::string join_paragraphs(const std::vector<std::string>& sentences, const std::vector<int>& breaks) {
    std::string out;
    size_t b = 0;
    for (size_t i = 0; i < sentences.size(); ++i) {
        while (b < breaks.size() && breaks[b] < (int)i) ++b;
        if (i > 0 && b < breaks.size() && breaks[b] == (int)i)
            out += "\n\n";
        else if (i > 0 && !(ends_wide(sentences[i - 1]) && !sentences[i].empty() && (unsigned char)sentences[i][0] >= 0x80))
            out += ' ';  // but no space between CJK sentences
        out += sentences[i];
    }
    return out + "\n";
}

std::string join_speaker_paragraphs(const std::vector<std::string>& sentences, const std::vector<int>& breaks,
                                    const std::vector<std::pair<int, std::string>>& turns) {
    std::string out;
    size_t b = 0, t = 0;
    for (size_t i = 0; i < sentences.size(); ++i) {
        while (b < breaks.size() && breaks[b] < (int)i) ++b;
        while (t < turns.size() && turns[t].first < (int)i) ++t;
        const bool turn = t < turns.size() && turns[t].first == (int)i;
        if (i > 0 && (turn || (b < breaks.size() && breaks[b] == (int)i)))
            out += "\n\n";
        else if (i > 0 && !(ends_wide(sentences[i - 1]) && !sentences[i].empty() && (unsigned char)sentences[i][0] >= 0x80))
            out += ' ';
        if (turn && !turns[t].second.empty()) out += turns[t].second + ": ";
        out += sentences[i];
    }
    return out + "\n";
}

bool keeps_speaker_labels(const std::string& original, const std::string& corrected,
                          const std::vector<std::string>& labels) {
    const std::vector<std::string> a = split_paragraphs(original), b = split_paragraphs(corrected);
    if (a.size() != b.size()) return false;
    auto label_of = [&](const std::string& p) -> const std::string* {
        const std::string* best = nullptr;  // the longest ("Anna B:" before "Anna:")
        for (const auto& l : labels)
            if (!l.empty() && p.compare(0, l.size(), l) == 0 && (!best || l.size() > best->size())) best = &l;
        return best;
    };
    for (size_t i = 0; i < a.size(); ++i)
        if (label_of(a[i]) != label_of(b[i])) return false;
    return true;
}

std::vector<int> parse_number_list(const std::string& reply) {
    std::vector<int> out;
    long long cur = -1;
    for (unsigned char c : reply + " ") {
        if (c >= '0' && c <= '9') {
            cur = (cur < 0 ? 0 : cur) * 10 + (c - '0');
            if (cur > 1000000) cur = 1000000;
        } else if (cur >= 0) {
            out.push_back((int)cur);
            cur = -1;
        }
    }
    return out;
}

std::vector<int> tidy_paragraph_breaks(std::vector<int> breaks, const std::vector<std::string>& sentences,
                                       const std::vector<double>& pauses, int max_sentences, int max_words) {
    const int n = (int)sentences.size();
    std::vector<int> words(n);
    for (int i = 0; i < n; ++i) words[i] = (int)split_words(sentences[i]).size();
    auto span_words = [&](int a, int b) {
        int w = 0;
        for (int i = a; i < b; ++i) w += words[i];
        return w;
    };
    constexpr int kShortParagraphWords = 25;

    std::sort(breaks.begin(), breaks.end());
    breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());
    // Drop breaks that leave a paragraph of a single short sentence.
    std::vector<int> kept;
    int last = 0;
    for (int b : breaks) {
        if (b <= 0 || b >= n) continue;
        if (b - last < 2 && span_words(last, b) < kShortParagraphWords) continue;
        kept.push_back(b);
        last = b;
    }
    if (!kept.empty() && n - kept.back() < 2 && span_words(kept.back(), n) < kShortParagraphWords) kept.pop_back();

    // Split paragraphs that are too long.
    std::vector<int> out;
    std::function<void(int, int)> split = [&](int a, int b) {  // paragraph [a, b)
        if ((b - a <= max_sentences && span_words(a, b) <= max_words) || b - a < 4) return;
        int best = (a + b) / 2;
        if (!pauses.empty()) {
            double best_score = -1e9;
            for (int i = a + 2; i <= b - 2; ++i) {
                // Prefer long pauses, then positions near the middle.
                const double centre = 1.0 - std::fabs((i - a) / double(b - a) - 0.5);
                const double score = std::max(0.0, pauses[i]) + 0.3 * centre;
                if (score > best_score) best_score = score, best = i;
            }
        }
        split(a, best);
        out.push_back(best);
        split(best, b);
    };
    int start = 0;
    for (int b : kept) {
        split(start, b);
        out.push_back(b);
        start = b;
    }
    split(start, n);
    std::sort(out.begin(), out.end());
    return out;
}

bool lacks_punctuation(const std::string& text) {
    const auto words = split_words(text);
    if (words.size() < 12) return false;
    int ends = 0;
    // "No capital letter at all" only means something in scripts that have
    // capitals (Latin, Greek, Cyrillic). Arabic, Hebrew, CJK, Devanagari, ...
    // have none, so their text is judged by its sentence ends alone.
    bool cased = false, capital = false;
    for (const auto& w : words) {
        if (ends_sentence(w)) ++ends;
        for (size_t i = 0; i < w.size(); ++i) {
            const unsigned char c = (unsigned char)w[i];
            const unsigned char d = i + 1 < w.size() ? (unsigned char)w[i + 1] : 0;
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                cased = true;
                capital |= c <= 'Z';
            } else if (c >= 0xC3 && c <= 0xC9 && (d & 0xC0) == 0x80) {
                // Latin-1 Supplement and Latin Extended (U+00C0..U+024F); the
                // capitals checked are Latin-1's U+00C0..U+00DE except U+00D7 (x).
                cased = true;
                capital |= c == 0xC3 && d <= 0x9E && d != 0x97;
            } else if ((c == 0xCE || c == 0xCF) && (d & 0xC0) == 0x80) {
                cased = true;                                 // Greek U+0380..U+03FF
                capital |= c == 0xCE && d >= 0x86 && d <= 0xAB;  // U+0386..U+03AB
            } else if ((c == 0xD0 || c == 0xD1) && (d & 0xC0) == 0x80) {
                cased = true;                     // Cyrillic U+0400..U+047F
                capital |= c == 0xD0 && d <= 0xAF;  // U+0400..U+042F
            }
        }
    }
    return (cased && !capital) || ends * 40 < (int)words.size();
}

namespace {
// Length of the punctuation mark at s[i] that the punctuation model writes
// itself (sentence punctuation, quotes, brackets, dashes), 0 if none.
size_t punctuation_mark(const std::string& s, size_t i) {
    const char c = s[i];
    if (c != '\0' && std::strchr(".,!?;:\"'()[]{}-", c)) return 1;
    // U+00AB U+00BB U+00A1 U+00BF, U+2018 U+2019 U+201C U+201D, U+2013 U+2014 U+2026,
    // and the full-width U+3001 U+3002 U+FF01 U+FF0C U+FF1A U+FF1B U+FF1F
    static const char* const marks[] = {"\xC2\xAB",     "\xC2\xBB",     "\xC2\xA1",     "\xC2\xBF",
                                        "\xE2\x80\x98", "\xE2\x80\x99", "\xE2\x80\x9C", "\xE2\x80\x9D",
                                        "\xE2\x80\x93", "\xE2\x80\x94", "\xE2\x80\xA6", "\xE3\x80\x81",
                                        "\xE3\x80\x82", "\xEF\xBC\x81", "\xEF\xBC\x8C", "\xEF\xBC\x9A",
                                        "\xEF\xBC\x9B", "\xEF\xBC\x9F"};
    for (const char* m : marks)
        if (has_at(s, i, m)) return std::strlen(m);
    return 0;
}

// The word without the punctuation marks at its start and end, lowercased.
// Everything inside is kept: d'abord, well-known, 3.5, 10:30, AT&T, 24/7, C++.
std::string word_core(const std::string& w) {
    size_t b = 0;
    while (b < w.size()) {
        // a sign or decimal point belongs to the number: -5, .5
        if ((w[b] == '-' || w[b] == '.') && b + 1 < w.size() && w[b + 1] >= '0' && w[b + 1] <= '9') break;
        const size_t m = punctuation_mark(w, b);
        if (!m) break;
        b += m;
    }
    size_t e = b;
    for (size_t i = b; i < w.size();) {
        // Marks start with an ASCII or lead byte, so stepping byte-wise never
        // matches inside a multi-byte character.
        const size_t m = punctuation_mark(w, i);
        i += m ? m : 1;
        if (!m) e = i;
    }
    std::string word;
    for (size_t i = b; i < e; ++i) {
        const unsigned char c = (unsigned char)w[i];
        if (c >= 'A' && c <= 'Z') {
            word += (char)(c + 32);
        } else if (c == 0xC3 && i + 1 < e) {
            // Latin-1 capitals U+00C0..U+00DE (except U+00D7) are C3 80..9E.
            unsigned char d = (unsigned char)w[i + 1];
            if (d >= 0x80 && d <= 0x9E && d != 0x97) d += 0x20;
            word += (char)c;
            word += (char)d;
            ++i;
        } else {
            word += (char)c;
        }
    }
    return word;
}

// Words compared ignoring punctuation and letter case. Non-ASCII bytes are
// masked: a case change keeps their UTF-8 length in the scripts PCS handles.
// Apostrophes and hyphens inside words count (U+2019 as '), so a reply that
// dropped or moved one does not pass as the same words.
std::string word_key(const std::string& text) {
    const std::string s = strip_punctuation(text);
    std::string k;
    for (size_t i = 0; i < s.size(); ++i) {
        if (has_at(s, i, "\xE2\x80\x99")) {
            k += '\'';
            i += 2;
            continue;
        }
        const unsigned char c = (unsigned char)s[i];
        k += c >= 0x80 ? '*' : (char)c;
    }
    return k;
}
}  // namespace

std::string strip_punctuation(const std::string& text) {
    std::string out;
    for (const auto& w : split_words(text)) {
        const std::string word = word_core(w);
        if (word.empty()) continue;  // only punctuation (-- ...)
        if (!out.empty()) out += ' ';
        out += word;
    }
    return out;
}

bool same_words(const std::string& a, const std::string& b) { return word_key(a) == word_key(b); }

bool apply_restored_text(std::vector<Word>& words, const std::string& text) {
    // Restored words; a token of punctuation only (a dash the model put between
    // words) goes with the word before it, or the first word.
    std::vector<std::string> tokens;
    std::string lead;
    for (const auto& t : split_words(text)) {
        if (!strip_punctuation(t).empty())
            tokens.push_back(lead.empty() ? t : lead + " " + t), lead.clear();
        else if (!tokens.empty())
            tokens.back() += " " + t;
        else
            lead += (lead.empty() ? "" : " ") + t;
    }
    std::vector<size_t> spoken;  // timed words that are not punctuation only
    for (size_t i = 0; i < words.size(); ++i)
        if (!strip_punctuation(words[i].text).empty()) spoken.push_back(i);
    if (tokens.empty() || tokens.size() != spoken.size()) return false;
    for (size_t k = 0; k < tokens.size(); ++k)
        if (!same_words(tokens[k], words[spoken[k]].text)) return false;
    for (auto& w : words) w.text.clear();  // punctuation-only words: the model re-punctuated
    for (size_t k = 0; k < tokens.size(); ++k) words[spoken[k]].text = tokens[k];
    return true;
}

std::string format_paragraphs(const std::string& text, int sentences_per_paragraph) {
    if (sentences_per_paragraph < 1) sentences_per_paragraph = 1;
    const std::vector<std::string> sentences = split_sentences(text);
    std::vector<int> breaks;
    for (int i = sentences_per_paragraph; i < (int)sentences.size(); i += sentences_per_paragraph) breaks.push_back(i);
    return join_paragraphs(sentences, breaks);
}

std::vector<std::string> split_paragraphs(const std::string& text) {
    std::vector<std::string> paras;
    std::string cur;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (normalize_space(line).empty()) {
            if (!cur.empty()) paras.push_back(normalize_space(cur)), cur.clear();
        } else {
            cur += (cur.empty() ? "" : " ") + line;
        }
    }
    if (!cur.empty()) paras.push_back(normalize_space(cur));
    return paras;
}

std::vector<TextChunk> chunk_paragraphs(const std::string& text, int chunk_words) {
    chunk_words = std::max(1, chunk_words);
    std::vector<TextChunk> chunks;
    TextChunk cur;
    int cur_words = 0;
    auto flush = [&] {
        if (!cur.text.empty()) chunks.push_back(std::move(cur));
        cur = TextChunk();
        cur_words = 0;
    };
    for (const auto& p : split_paragraphs(text)) {
        const std::vector<std::string> words = split_words(p);
        const int w = (int)words.size();
        if (w > 2 * chunk_words) {  // a very long paragraph is split on its own
            flush();
            for (int i = 0; i < w; i += chunk_words) {
                TextChunk piece;
                piece.continues = i > 0;
                for (int j = i; j < std::min(w, i + chunk_words); ++j)
                    piece.text += (piece.text.empty() ? "" : " ") + words[j];
                chunks.push_back(std::move(piece));
            }
            continue;
        }
        if (!cur.text.empty() && cur_words + w > chunk_words) flush();
        cur.text += (cur.text.empty() ? "" : "\n\n") + p;
        cur_words += w;
    }
    flush();
    return chunks;
}

std::string join_chunks(const std::vector<TextChunk>& chunks, const std::vector<std::string>& outputs) {
    std::string out;
    for (size_t i = 0; i < chunks.size() && i < outputs.size(); ++i) {
        // Re-flow paragraphs: collapse the model's line wrapping, keep blank-line breaks.
        std::string flowed;
        for (const auto& p : split_paragraphs(outputs[i])) flowed += (flowed.empty() ? "" : "\n\n") + p;
        if (flowed.empty()) continue;
        if (!out.empty()) out += chunks[i].continues ? " " : "\n\n";
        out += flowed;
    }
    return out;
}

}  // namespace tmx
