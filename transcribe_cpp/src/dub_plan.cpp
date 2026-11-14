// dub_plan.cpp - the model-free parts of dubbing (subtitle reading, line
// grouping, timing, reference choice); unit-tested in tests/test_helpers.cpp.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <stdexcept>

#include "dub.h"
#include "fileio.h"
#include "pathutil.h"
#include "srt.h"
#include "transcript.h"

namespace fs = std::filesystem;

namespace tmx {

// ---------------------------------------------------------------------------
// Subtitles
// ---------------------------------------------------------------------------

namespace {

bool ends_sentence(const std::string& t) {
    std::string s = t;
    while (!s.empty() && (s.back() == ' ' || s.back() == '"' || s.back() == '\'' || s.back() == ')')) s.pop_back();
    if (s.empty()) return false;
    const char c = s.back();
    if (c == '.' || c == '?' || c == '!') return true;
    for (const char* e : {"\xE2\x80\xA6", "\xE3\x80\x82", "\xEF\xBC\x81", "\xEF\xBC\x9F"})
        if (s.size() >= 3 && s.compare(s.size() - 3, 3, e) == 0) return true;
    return false;
}

}  // namespace

std::string read_text_file(const std::string& path) { return pk::clean_input_text(pk::read_file(path_from_utf8(path))); }

std::vector<SubCue> read_srt_cues(const std::string& path) {
    // The shared reader starts a cue at every timing line, so a file with a
    // missing blank line between two cues doesn't make the dub speak the next
    // cue's number and timing line as text of the one before.
    std::vector<SubCue> cues;
    for (const auto& c : pk::parse_srt(read_text_file(path))) {
        if (!c.timed) continue;
        SubCue s;
        s.start = c.start;
        s.end = c.end;
        s.text = normalize_space(pk::strip_srt_tags(c.text));
        if (!s.text.empty()) cues.push_back(std::move(s));
    }
    std::stable_sort(cues.begin(), cues.end(), [](const SubCue& a, const SubCue& b) { return a.start < b.start; });
    return cues;
}

std::vector<SpeechLine> group_cues(const std::vector<SubCue>& cues, double max_seconds) {
    std::vector<SpeechLine> lines;
    for (size_t i = 0; i < cues.size(); ++i) {
        const auto& c = cues[i];
        if (!lines.empty()) {
            SpeechLine& l = lines.back();
            const bool continues = l.speaker == c.speaker && !ends_sentence(l.text) && c.start - l.end < 0.35 &&
                                   c.end - l.start <= max_seconds;
            if (continues) {
                l.end = c.end;
                l.text += " " + c.text;
                ++l.cue_count;
                continue;
            }
        }
        lines.push_back({c.start, c.end, c.text, i, 1, c.speaker});
    }
    return lines;
}

std::vector<Placement> place_lines(const std::vector<SpeechLine>& lines, const std::vector<double>& lengths,
                                   double max_speed, double total_seconds) {
    std::vector<Placement> out(lines.size());
    double cursor = 0;  // end of the previous placed line
    for (size_t i = 0; i < lines.size(); ++i) {
        const double next = i + 1 < lines.size() ? lines[i + 1].start : std::max(total_seconds, lines[i].end + 2.0);
        // The line may use its own slot plus the pause before the next line.
        const double slot = std::max(0.3, next - lines[i].start - 0.08);
        double speed = 1.0;
        if (lengths[i] > slot) speed = std::min(max_speed, lengths[i] / slot);
        const double len = lengths[i] / speed;
        const double at = std::max(lines[i].start, cursor + 0.05);
        out[i] = {at, speed, std::max(0.0, at + len - next)};
        cursor = at + len;
    }
    return out;
}

std::vector<SubCue> retime_cues(const std::vector<SubCue>& cues, const std::vector<SpeechLine>& lines,
                              const std::vector<Placement>& placements, const std::vector<double>& durations,
                              double offset) {
    if (lines.size() != placements.size() || lines.size() != durations.size())
        throw std::runtime_error("inconsistent dub timing");
    std::vector<SubCue> result;
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& l = lines[i];
        if (l.first_cue > cues.size() || l.cue_count > cues.size() - l.first_cue)
            throw std::runtime_error("inconsistent dub cue mapping");
        size_t weight = 0, before = 0;
        for (size_t k = 0; k < l.cue_count; ++k) weight += std::max<size_t>(1, utf8_length(cues[l.first_cue + k].text));
        // A line that couldn't be spoken (no audio) keeps its subtitles at their
        // original times rather than flashing them for a millisecond.
        if (durations[i] < 0.05) {
            for (size_t k = 0; k < l.cue_count; ++k) {
                SubCue c = cues[l.first_cue + k];
                c.start += offset, c.end += offset;
                result.push_back(std::move(c));
            }
            continue;
        }
        for (size_t k = 0; k < l.cue_count; ++k) {
            SubCue c = cues[l.first_cue + k];
            const size_t n = std::max<size_t>(1, utf8_length(c.text));
            const double length = std::max(0.001, durations[i]);
            c.start = offset + placements[i].at + length * before / weight;
            c.end = offset + placements[i].at + length * (before + n) / weight;
            before += n;
            result.push_back(std::move(c));
        }
    }
    return result;
}

namespace {
// Length in "Latin characters": a CJK/kana/hangul character takes about as long
// to say as three Latin letters.
size_t cp_count(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = (unsigned char)s[i];
        if ((c & 0xC0) == 0x80) continue;
        n += (c >= 0xE2 && c <= 0xED && !(c == 0xE2 && i + 1 < s.size() && (unsigned char)s[i + 1] < 0xBA)) ? 3 : 1;
    }
    return n;
}
// Split after any of `ends` (followed by a space or the end), keeping the punctuation.
std::vector<std::string> split_after(const std::string& text, const std::vector<std::string>& ends) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < text.size();) {
        bool cut = false;
        for (const auto& e : ends)
            if (text.compare(i, e.size(), e) == 0) {
                cur += e;
                i += e.size();
                // ASCII punctuation must be followed by a space (not "3.5" or "U.S."); CJK needs none.
                const bool ascii = e.size() == 1;
                if (!ascii || i >= text.size() || text[i] == ' ') cut = true;
                break;
            }
        if (!cut) {
            cur += text[i];
            ++i;
            continue;
        }
        out.push_back(normalize_space(cur));
        cur.clear();
    }
    if (!normalize_space(cur).empty()) out.push_back(normalize_space(cur));
    return out;
}
}  // namespace

std::vector<std::string> split_for_speech(const std::string& text, size_t max_chars) {
    const std::string t = normalize_space(text);
    if (cp_count(t) <= max_chars) return {t};
    std::vector<std::string> pieces;
    for (const auto& sentence : split_after(t, {".", "!", "?", "\xE2\x80\xA6", "\xE3\x80\x82", "\xEF\xBC\x81",
                                                "\xEF\xBC\x9F"})) {
        if (cp_count(sentence) <= max_chars) {
            pieces.push_back(sentence);
            continue;
        }
        for (const auto& clause : split_after(sentence, {",", ";", ":", "\xEF\xBC\x8C", "\xE3\x80\x81", "\xEF\xBC\x9B"})) {
            if (cp_count(clause) <= max_chars) {
                pieces.push_back(clause);
                continue;
            }
            // No punctuation to use: between words, in pieces of about equal length.
            const size_t n = (cp_count(clause) + max_chars - 1) / max_chars;
            const size_t target = (cp_count(clause) + n - 1) / n;
            std::string cur;
            std::istringstream words(clause);
            for (std::string w; words >> w;) {
                if (!cur.empty() && (cp_count(cur) >= target || cp_count(cur) + 1 + cp_count(w) > max_chars)) {
                    // A short last word ("a", "the", "de") belongs with what follows.
                    const size_t sp = cur.rfind(' ');
                    std::string carry;
                    if (sp != std::string::npos && cp_count(cur.substr(sp + 1)) <= 3)
                        carry = cur.substr(sp + 1), cur.erase(sp);
                    pieces.push_back(cur);
                    cur = carry;
                }
                cur += (cur.empty() ? "" : " ") + w;
            }
            if (!cur.empty()) pieces.push_back(cur);
        }
    }
    // Join neighbours back up (short sentences sound better together) - into parts
    // of about equal length rather than filling each to the limit, which leaves a
    // long part and a stub ("... a place of holiness:" + "the house of the Lord.").
    auto pack = [&](size_t limit) {
        std::vector<std::string> out;
        for (const auto& p : pieces) {
            if (out.empty()) {
                out.push_back(p);
                continue;
            }
            // A stub of a word or two ("Sometimes,") joins its neighbour even a little over.
            const size_t a = cp_count(out.back()), b = cp_count(p);
            const std::string& shorter = a <= b ? out.back() : p;
            const bool stub = std::min(a, b) <= 12 && !ends_sentence(shorter) && a + 1 + b <= max_chars + 12;
            if (a + 1 + b <= limit || stub) out.back() += " " + p;
            else out.push_back(p);
        }
        return out;
    };
    std::vector<std::string> out = pack(max_chars);
    size_t total = 0;
    for (const auto& p : pieces) total += cp_count(p) + 1;
    for (size_t limit = total / out.size(); limit < max_chars; limit += std::max<size_t>(1, max_chars / 40)) {
        auto even = pack(limit);
        if (even.size() <= out.size()) return even;
    }
    return out;
}

double max_plausible_seconds(const std::string& text) {
    size_t codepoints = 0;
    for (unsigned char c : text) codepoints += (c & 0xC0) != 0x80;
    return std::max(4.0, 1.0 + 0.35 * (double)codepoints);
}

std::vector<RefClip> choose_references(const std::vector<SubCue>& source, double want_seconds, int speaker) {
    struct Cand {
        double score, start, end;
        std::string text;
    };
    std::vector<Cand> cands;
    for (size_t i = 0; i < source.size(); ++i) {
        if (speaker >= 0 && source[i].speaker != speaker) continue;
        std::string t = source[i].text;
        for (size_t j = i; j < source.size(); ++j) {
            if (j > i) {
                if (source[j].start - source[j - 1].end > 0.8) break;  // a pause/cut: another take
                if (speaker >= 0 && source[j].speaker != speaker) break;  // someone else speaks
                t += " " + source[j].text;
            }
            const double dur = source[j].end - source[i].start;
            if (dur > 14.0) break;
            if (dur < 3.0) continue;
            double score = -std::fabs(dur - 10.0);
            if (ends_sentence(t)) score += 1.0;
            const double words_per_s = std::count(t.begin(), t.end(), ' ') / dur;
            if (words_per_s < 0.8) score -= 3.0;
            cands.push_back({score, source[i].start, source[j].end, t});
        }
    }
    // Best first; ties go to the earlier stretch (stable, deterministic).
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) { return x.score > y.score; });
    std::vector<RefClip> out;
    double have = 0;
    for (const auto& c : cands) {
        bool overlaps = false;
        for (const auto& o : out)
            if (c.start < o.end + 0.5 && c.end > o.start - 0.5) overlaps = true;
        if (overlaps || (c.score < -6.0 && !out.empty())) continue;  // short stretches only if nothing better
        out.push_back({c.start, c.end, c.text});
        have += c.end - c.start;
        if (have >= want_seconds) break;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Several speakers
// ---------------------------------------------------------------------------

size_t text_split_point(const std::string& text, double frac) {
    auto is_space = [](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; };
    // The codepoint starts, and whether the text has word gaps at all (Chinese
    // and Japanese have none: there any character boundary will do).
    std::vector<size_t> starts;
    for (size_t i = 0; i < text.size(); ++i)
        if (((unsigned char)text[i] & 0xC0) != 0x80) starts.push_back(i);
    const size_t first = text.find_first_not_of(" \n\r\t"), last = text.find_last_not_of(" \n\r\t");
    if (first == std::string::npos || starts.size() < 2) return 0;
    bool gaps = false;
    for (size_t i = first; i < last; ++i) gaps = gaps || is_space(text[i]);
    const std::vector<std::string> sentence_ends = {".", "?", "!", "\xE2\x80\xA6", "\xE3\x80\x82", "\xEF\xBC\x9F",
                                                    "\xEF\xBC\x81"};
    const std::vector<std::string> clause_ends = {",", ";", ":", "\xEF\xBC\x8C", "\xE3\x80\x81", "\xEF\xBC\x9B",
                                                  "\xEF\xBC\x9A"};
    auto ends_with_any = [&](size_t end, const std::vector<std::string>& set) {  // text[..end) ends with one?
        for (const auto& e : set)
            if (end >= e.size() && text.compare(end - e.size(), e.size(), e) == 0) return true;
        return false;
    };
    const double total = (double)starts.size();
    size_t best = 0;
    double best_cost = 1e9;
    // CJK (from U+2E80): written without spaces, so it splits between characters.
    auto cjk = [&](size_t i) {
        const unsigned char c = (unsigned char)text[i];
        return c >= 0xE3 || (c == 0xE2 && i + 1 < text.size() && (unsigned char)text[i + 1] >= 0xBA);
    };
    for (size_t k = 1; k < starts.size(); ++k) {
        const size_t at = starts[k];
        if (at <= first || at > last) continue;
        size_t prev_end = at;  // the end of the text before the split, without its spaces
        if (gaps) {
            if (is_space(text[at]) || !is_space(text[at - 1])) continue;  // only where a word starts
            while (prev_end > 0 && is_space(text[prev_end - 1])) --prev_end;
        } else if (!cjk(at) && !cjk(starts[k - 1])) {
            continue;  // inside a word
        }
        double cost = std::fabs((double)k / total - frac);
        if (ends_with_any(prev_end, sentence_ends)) cost -= 0.3;
        else if (ends_with_any(prev_end, clause_ends)) cost -= 0.12;
        if (cost < best_cost) best_cost = cost, best = at;
    }
    return best;
}

std::string format_words(const std::vector<Word>& words) {
    std::string out;
    char b[64];
    for (const auto& w : words) {
        const std::string t = normalize_space(w.text);
        if (t.empty() || t.find('\t') != std::string::npos) continue;
        std::snprintf(b, sizeof(b), "%.3f\t%.3f\t", w.start, w.end);
        out += b + t + "\n";
    }
    return out;
}

std::vector<Word> parse_words(const std::string& text) {
    std::vector<Word> out;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1);
        if (b == std::string::npos) continue;
        char* e1 = nullptr;
        char* e2 = nullptr;
        const std::string s0 = line.substr(0, a), s1 = line.substr(a + 1, b - a - 1);
        const double t0 = std::strtod(s0.c_str(), &e1), t1 = std::strtod(s1.c_str(), &e2);
        if (e1 == s0.c_str() || e2 == s1.c_str() || !std::isfinite(t0) || !std::isfinite(t1)) continue;
        Word w;
        w.start = t0, w.end = std::max(t0, t1), w.text = normalize_space(line.substr(b + 1));
        if (!w.text.empty()) out.push_back(std::move(w));
    }
    std::stable_sort(out.begin(), out.end(), [](const Word& x, const Word& y) { return x.start < y.start; });
    return out;
}

namespace {

// Who speaks during [s, e]: the turn overlapping it most, else the nearest one
// within a second (-1: none).
int speaker_at(const std::vector<SpeakerTurn>& turns, double s, double e) {
    int best = -1;
    double best_overlap = 0, best_gap = 1.0;
    for (const auto& t : turns) {
        const double o = std::min(e, t.end) - std::max(s, t.start);
        if (o > best_overlap) best_overlap = o, best = t.speaker;
    }
    if (best >= 0) return best;
    for (const auto& t : turns) {
        const double gap = t.start > e ? t.start - e : s > t.end ? s - t.end : 0.0;
        if (gap < best_gap) best_gap = gap, best = t.speaker;
    }
    return best;
}

// One subtitle split by speaker with the transcription's word timings. False
// when there is nothing to go on (fewer than two of its words, or no turns).
// The speaker of each word (-1: nobody heard), with the detection's slips
// taken out: a few words given to someone else in the middle of a speaker's
// sentence ("... I went [home] and ..."). Speaker detection listens in windows
// of about a second, and one window that sounds different puts a word or two
// with another speaker. A real remark in between ("Yes.") is a sentence of its
// own - the words before it end a sentence and so does it - and stays, as does
// anything longer than `max_slip` seconds.
std::vector<int> word_speakers(const std::vector<Word>& words, const std::vector<SpeakerTurn>& turns,
                               double max_slip = 1.5) {
    std::vector<int> who(words.size(), -1);
    if (turns.empty()) return who;
    std::vector<size_t> order;  // the words that are spoken text, in time
    for (size_t k = 0; k < words.size(); ++k)
        if (!normalize_space(words[k].text).empty()) order.push_back(k);
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return words[a].start < words[b].start; });
    for (size_t k : order) who[k] = speaker_at(turns, words[k].start, words[k].end);
    struct Run {
        size_t a, b;  // order[a, b)
        int speaker;
    };
    for (;;) {
        std::vector<Run> runs;
        for (size_t k = 0; k < order.size(); ++k) {
            const int s = who[order[k]];
            if (s < 0) continue;  // (filled from the neighbours by the caller)
            if (!runs.empty() && runs.back().speaker == s) runs.back().b = k + 1;
            else runs.push_back({k, k + 1, s});
        }
        // The shortest slip first: taking one out can join its neighbours.
        size_t pick = runs.size();
        double pick_len = max_slip;
        for (size_t r = 1; r + 1 < runs.size(); ++r) {
            if (runs[r - 1].speaker != runs[r + 1].speaker) continue;
            const Word& first = words[order[runs[r].a]];
            const Word& last = words[order[runs[r].b - 1]];
            const double len = last.end - first.start;
            if (len >= pick_len) continue;
            const bool own_sentence = ends_sentence(normalize_space(words[order[runs[r].a - 1]].text)) &&
                                      ends_sentence(normalize_space(last.text));
            if (!own_sentence) pick = r, pick_len = len;
        }
        if (pick == runs.size()) break;
        for (size_t k = runs[pick].a; k < runs[pick].b; ++k) who[order[k]] = runs[pick - 1].speaker;
    }
    return who;
}

bool split_cue_by_words(const pk::SrtCue& c, const std::string& text, int index, int label,
                        const std::vector<Word>& words, const std::vector<int>& word_who,
                        const std::vector<SpeakerTurn>& turns, double min_part, std::vector<SpeakerPart>& out) {
    if (turns.empty()) return false;
    std::vector<const Word*> cw;
    std::vector<int> who;
    for (size_t k = 0; k < words.size(); ++k) {
        const Word& w = words[k];
        const double mid = 0.5 * (w.start + w.end);
        if (mid >= c.start - 0.01 && mid <= c.end + 0.01 && !normalize_space(w.text).empty())
            cw.push_back(&w), who.push_back(word_who[k]);
    }
    if (cw.size() < 2) return false;
    for (size_t k = 1; k < who.size(); ++k)  // words nobody was heard in: the speaker before (or after)
        if (who[k] < 0) who[k] = who[k - 1];
    for (size_t k = who.size(); k-- > 1;)
        if (who[k - 1] < 0) who[k - 1] = who[k];
    struct Run {
        size_t a, b;  // words [a, b)
        int speaker;
    };
    std::vector<Run> runs;
    for (size_t k = 0; k < who.size(); ++k)
        if (!runs.empty() && runs.back().speaker == who[k]) runs.back().b = k + 1;
        else runs.push_back({k, k + 1, who[k]});
    auto dur = [&](const Run& r) { return cw[r.b - 1]->end - cw[r.a]->start; };
    // Very short changes (a cough, a breath, one misheard window) join the
    // neighbour that speaks longer.
    while (runs.size() > 1) {
        size_t k = 0;
        for (size_t j = 1; j < runs.size(); ++j)
            if (dur(runs[j]) < dur(runs[k])) k = j;
        if (dur(runs[k]) >= min_part) break;
        const bool left = k > 0 && (k + 1 >= runs.size() || dur(runs[k - 1]) >= dur(runs[k + 1]));
        if (left) runs[k - 1].b = runs[k].b;
        else runs[k + 1].a = runs[k].a;
        runs.erase(runs.begin() + (long)k);
        for (size_t j = 1; j < runs.size();)
            if (runs[j].speaker == runs[j - 1].speaker) {
                runs[j - 1].b = runs[j].b;
                runs.erase(runs.begin() + (long)j);
            } else {
                ++j;
            }
    }
    if (runs.size() == 1) {
        out.push_back({c.start, c.end, text, runs[0].speaker >= 0 ? runs[0].speaker : label, index});
        return true;
    }
    // Where each word starts in the subtitle's text (they are its words, so they
    // are found in order); failing that, by the share of the characters before it.
    std::vector<size_t> pos(cw.size(), std::string::npos);
    {
        size_t from = 0;
        bool all = true;
        for (size_t k = 0; k < cw.size() && all; ++k) {
            const std::string t = normalize_space(cw[k]->text);
            const size_t at = text.find(t, from);
            if (at == std::string::npos) all = false;
            else pos[k] = at, from = at + t.size();
        }
        if (!all) {
            size_t total = 0;
            std::vector<size_t> before(cw.size());
            for (size_t k = 0; k < cw.size(); ++k) before[k] = total, total += utf8_length(normalize_space(cw[k]->text)) + 1;
            for (size_t k = 0; k < cw.size(); ++k) pos[k] = text_split_point(text, (double)before[k] / (double)total);
        }
    }
    size_t from = 0;
    double part_start = c.start;
    for (size_t r = 1; r < runs.size(); ++r) {
        size_t at = pos[runs[r].a];
        // The split goes before the word, after any punctuation that ends the
        // previous one (a word's position never points into a word before it).
        if (at == std::string::npos || at <= from || at >= text.size()) continue;
        const std::string first = normalize_space(text.substr(from, at - from));
        if (first.empty()) continue;
        const double change = 0.5 * (cw[runs[r].a - 1]->end + cw[runs[r].a]->start);
        out.push_back({part_start, change, first, runs[r - 1].speaker, index});
        from = at, part_start = change;
    }
    out.push_back({part_start, c.end, normalize_space(text.substr(from)), runs.back().speaker, index});
    // Two neighbouring parts that ended up with one speaker (a split that
    // couldn't be placed) are one part.
    for (size_t k = out.size(); k-- > 1;)
        if (out[k].group == index && out[k - 1].group == index && out[k].speaker == out[k - 1].speaker) {
            out[k - 1].text += " " + out[k].text;
            out[k - 1].end = out[k].end;
            out.erase(out.begin() + (long)k);
        }
    return true;
}

}  // namespace

std::vector<SpeakerPart> split_by_speaker(const std::vector<pk::SrtCue>& cues, const std::vector<int>& labels,
                                          const std::vector<SpeakerTurn>& turns_in, double min_part,
                                          const std::vector<Word>& words, double min_word_part) {
    std::vector<SpeakerTurn> turns = turns_in;
    std::stable_sort(turns.begin(), turns.end(), [](const SpeakerTurn& a, const SpeakerTurn& b) { return a.start < b.start; });
    std::vector<SpeakerPart> out;
    struct Run {
        double start, end;
        int speaker;
    };
    const std::vector<int> word_who = words.empty() ? std::vector<int>() : word_speakers(words, turns);
    for (size_t i = 0; i < cues.size(); ++i) {
        const auto& c = cues[i];
        const std::string text = normalize_space(c.text);
        if (!c.timed || text.empty()) continue;
        const int label = i < labels.size() ? labels[i] : -1;
        if (!words.empty() && split_cue_by_words(c, text, (int)i, label, words, word_who, turns, min_word_part, out))
            continue;
        // Who speaks when inside this subtitle.
        std::vector<Run> runs;
        for (const auto& t : turns) {
            const double s = std::max(t.start, c.start), e = std::min(t.end, c.end);
            if (e <= s) continue;
            if (!runs.empty() && runs.back().speaker == t.speaker) runs.back().end = std::max(runs.back().end, e);
            else runs.push_back({s, e, t.speaker});
        }
        // Short interjections and overlaps go to the neighbour that speaks longer.
        while (runs.size() > 1) {
            size_t k = 0;
            for (size_t j = 1; j < runs.size(); ++j)
                if (runs[j].end - runs[j].start < runs[k].end - runs[k].start) k = j;
            if (runs[k].end - runs[k].start >= min_part) break;
            const bool left = k > 0 && (k + 1 >= runs.size() || runs[k - 1].end - runs[k - 1].start >=
                                                                     runs[k + 1].end - runs[k + 1].start);
            if (left) runs[k - 1].end = std::max(runs[k - 1].end, runs[k].end);
            else runs[k + 1].start = std::min(runs[k + 1].start, runs[k].start);
            runs.erase(runs.begin() + (long)k);
            for (size_t j = 1; j < runs.size();)  // neighbours that are now the same speaker
                if (runs[j].speaker == runs[j - 1].speaker) {
                    runs[j - 1].end = std::max(runs[j - 1].end, runs[j].end);
                    runs.erase(runs.begin() + (long)j);
                } else {
                    ++j;
                }
        }
        if (runs.size() <= 1) {
            const int who = label >= 0 ? label : runs.empty() ? -1 : runs[0].speaker;
            out.push_back({c.start, c.end, text, who, (int)i});
            continue;
        }
        // Split the text where the speaker changes.
        const double length = std::max(1e-3, c.end - c.start);
        size_t from = 0;
        double part_start = c.start, longest = runs[0].end - runs[0].start;
        int who = runs[0].speaker;
        for (size_t k = 1; k < runs.size(); ++k) {
            const double change = 0.5 * (runs[k - 1].end + runs[k].start);
            const size_t at = text_split_point(text, (change - c.start) / length);
            const double run_length = runs[k].end - runs[k].start;
            if (at > from && normalize_space(text.substr(at)).size() > 0 &&
                !normalize_space(text.substr(from, at - from)).empty()) {
                out.push_back({part_start, change, normalize_space(text.substr(from, at - from)), who, (int)i});
                from = at, part_start = change, who = runs[k].speaker, longest = run_length;
            } else if (run_length > longest) {  // nowhere to split: the part is the longer speaker's
                who = runs[k].speaker, longest = run_length;
            }
        }
        out.push_back({part_start, c.end, normalize_space(text.substr(from)), who, (int)i});
    }
    // Subtitles nobody was found speaking in: the speaker before (or after) them.
    for (size_t k = 1; k < out.size(); ++k)
        if (out[k].speaker < 0) out[k].speaker = out[k - 1].speaker;
    for (size_t k = out.size(); k-- > 1;)
        if (out[k - 1].speaker < 0) out[k - 1].speaker = out[k].speaker;
    return out;
}

bool choose_reference(const std::vector<SubCue>& source, double* start, double* end, std::string* text) {
    double best_score = -1e9;
    bool found = false;
    for (size_t i = 0; i < source.size(); ++i) {
        std::string t = source[i].text;
        for (size_t j = i; j < source.size(); ++j) {
            if (j > i) {
                if (source[j].start - source[j - 1].end > 0.8) break;  // a pause/cut: another take
                t += " " + source[j].text;
            }
            const double dur = source[j].end - source[i].start;
            if (dur > 14.0) break;
            if (dur < 3.0) continue;
            // Prefer ~10 s, whole sentences, and enough words for the duration.
            double score = -std::fabs(dur - 10.0);
            if (ends_sentence(t)) score += 1.0;
            const double words_per_s = std::count(t.begin(), t.end(), ' ') / dur;
            if (words_per_s < 0.8) score -= 3.0;  // probably music or silence under the cue
            if (score > best_score) {
                best_score = score;
                *start = source[i].start;
                *end = source[j].end;
                *text = t;
                found = true;
            }
        }
    }
    return found;
}

}  // namespace tmx
