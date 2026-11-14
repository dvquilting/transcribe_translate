#include "speakers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>

namespace pk {

namespace {

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> f;
    size_t b = 0;
    while (true) {
        const size_t t = line.find('\t', b);
        f.push_back(line.substr(b, t == std::string::npos ? std::string::npos : t - b));
        if (t == std::string::npos) break;
        b = t + 1;
    }
    return f;
}

bool number(const std::string& s, double* v) {
    if (s.empty()) return false;
    char* end = nullptr;
    *v = std::strtod(s.c_str(), &end);
    return end == s.c_str() + s.size() && std::isfinite(*v);
}

bool integer(const std::string& s, int* v) {
    double d = 0;
    if (!number(s, &d) || d != std::floor(d) || std::fabs(d) > 1e6) return false;
    *v = (int)d;
    return true;
}

// A name on one line without tabs.
std::string one_line(std::string s) {
    for (char& c : s)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    const size_t b = s.find_first_not_of(' '), e = s.find_last_not_of(' ');
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

std::string fixed(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.3f", v);
    return b;
}

double overlap(double a0, double a1, double b0, double b1) { return std::min(a1, b1) - std::max(a0, b0); }

// A subtitle part on one line (its own line breaks become spaces).
std::string flat(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\r') continue;
        out += c == '\n' ? ' ' : c;
    }
    const size_t b = out.find_first_not_of(' '), e = out.find_last_not_of(' ');
    return b == std::string::npos ? std::string() : out.substr(b, e - b + 1);
}

}  // namespace

const SpeakerInfo* SpeakerMap::find(int id) const {
    for (const auto& s : speakers)
        if (s.id == id) return &s;
    return nullptr;
}

const SpeakerCue* SpeakerMap::at(double start, double end, bool nearest) const {
    const SpeakerCue* best = nullptr;
    double best_overlap = 0;
    for (const auto& c : cues) {
        const double o = overlap(start, end, c.start, c.end);
        if (o > best_overlap + 1e-9) best_overlap = o, best = &c;
    }
    if (best || !nearest) return best;
    double best_gap = 1e300;
    for (const auto& c : cues) {
        const double gap = std::max(c.start - end, start - c.end);
        if (gap < best_gap) best_gap = gap, best = &c;
    }
    return best;
}

std::string format_speakers(const SpeakerMap& map) {
    std::string out = "# speakers 1\n";
    for (const auto& s : map.speakers)
        out += "speaker\t" + std::to_string(s.id) + "\t" + one_line(s.name) + "\t" + fixed(s.seconds) + "\t" +
               fixed(s.sample_start) + "\t" + fixed(s.sample_end) + "\n";
    for (const auto& c : map.cues)
        out += "cue\t" + fixed(c.start) + "\t" + fixed(c.end) + "\t" + std::to_string(c.speaker) + "\t" +
               std::to_string(c.group) + "\n";
    return out;
}

bool parse_speakers(const std::string& text, SpeakerMap* map, std::string* error) {
    SpeakerMap m;
    std::istringstream in(text);
    std::string line;
    int n = 0;
    bool header = false;
    auto bad = [&](const std::string& why) {
        if (error) *error = "line " + std::to_string(n) + ": " + why;
        return false;
    };
    while (std::getline(in, line)) {
        ++n;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (n == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        if (line.empty()) continue;
        if (line[0] == '#') {
            if (line.rfind("# speakers ", 0) == 0) header = true;
            continue;
        }
        const auto f = split_tabs(line);
        if (f[0] == "speaker") {
            SpeakerInfo s;
            if (f.size() < 6 || !integer(f[1], &s.id) || !number(f[3], &s.seconds) || !number(f[4], &s.sample_start) ||
                !number(f[5], &s.sample_end))
                return bad("a speaker needs: id, name, seconds, sample start, sample end");
            s.name = one_line(f[2]);
            if (m.find(s.id)) return bad("speaker " + f[1] + " is listed twice");
            m.speakers.push_back(s);
        } else if (f[0] == "cue") {
            SpeakerCue c;
            if (f.size() < 5 || !number(f[1], &c.start) || !number(f[2], &c.end) || !integer(f[3], &c.speaker) ||
                !integer(f[4], &c.group))
                return bad("a cue needs: start, end, speaker, group");
            m.cues.push_back(c);
        } else {
            return bad("unknown record '" + f[0] + "'");
        }
    }
    if (!header) {
        if (error) *error = "not a speakers file (no \"# speakers\" line)";
        return false;
    }
    for (const auto& c : m.cues)
        if (c.speaker >= 0 && !m.find(c.speaker)) {
            if (error) *error = "a cue names speaker " + std::to_string(c.speaker) + ", which is not listed";
            return false;
        }
    std::stable_sort(m.cues.begin(), m.cues.end(),
                     [](const SpeakerCue& a, const SpeakerCue& b) { return a.start < b.start; });
    *map = std::move(m);
    return true;
}

void merge_speakers(SpeakerMap* map, const std::vector<std::pair<int, int>>& into) {
    std::map<int, int> to;
    for (const auto& [from, target] : into) to[from] = target;
    // Follow chains (A -> B -> C), without looping forever on a cycle.
    auto final_id = [&](int id) {
        for (int guard = 0; guard < 64; ++guard) {
            const auto it = to.find(id);
            if (it == to.end() || it->second == id) break;
            id = it->second;
        }
        return id;
    };
    for (auto& c : map->cues)
        if (c.speaker >= 0) c.speaker = final_id(c.speaker);
    std::vector<SpeakerInfo> kept;
    for (const auto& s : map->speakers) {
        const int id = final_id(s.id);
        if (id == s.id) {
            kept.push_back(s);
            continue;
        }
        for (auto& k : map->speakers)
            if (k.id == id) {
                // The longer sample is kept for listening.
                if (s.sample_end - s.sample_start > k.sample_end - k.sample_start)
                    k.sample_start = s.sample_start, k.sample_end = s.sample_end;
                k.seconds += s.seconds;
            }
    }
    // `kept` copied the targets before their seconds were added: copy again.
    for (auto& k : kept)
        if (const SpeakerInfo* s = map->find(k.id)) k = *s;
    map->speakers = std::move(kept);
}

std::vector<CueSpeaker> cue_speakers(const std::vector<SrtCue>& cues, const SpeakerMap& map) {
    std::vector<CueSpeaker> who;
    for (const auto& c : cues) {
        const SpeakerCue* s = c.timed ? map.at(c.start, c.end, false) : nullptr;
        // Only an exact match (the same subtitle): the times of a cue and its record agree.
        if (s && std::fabs(s->start - c.start) < 0.02 && std::fabs(s->end - c.end) < 0.02)
            who.push_back({s->speaker, s->group});
        else
            who.push_back({});
    }
    return who;
}

std::vector<SrtCue> join_speaker_parts(const std::vector<SrtCue>& cues, const SpeakerMap& map) {
    return join_speaker_parts(cues, cue_speakers(cues, map));
}

std::vector<SrtCue> join_speaker_parts(const std::vector<SrtCue>& cues, const std::vector<CueSpeaker>& who) {
    std::vector<SrtCue> out;
    struct Part {
        const SrtCue* cue;
        int speaker, group;
    };
    std::vector<Part> parts;
    auto flush = [&] {
        if (parts.empty()) return;
        if (parts.size() == 1) {
            out.push_back(*parts[0].cue);
        } else {
            SrtCue c = *parts[0].cue;
            c.end = parts.back().cue->end;
            c.timing = srt_timing(c.start, c.end);
            bool several = false;
            for (const auto& p : parts) several = several || p.speaker != parts[0].speaker;
            std::string text;
            for (size_t i = 0; i < parts.size(); ++i) {
                const std::string t = flat(parts[i].cue->text);
                if (t.empty()) continue;
                if (several) {
                    // A part by the same speaker as the one before continues their line.
                    if (i > 0 && parts[i].speaker == parts[i - 1].speaker && !text.empty()) text += " " + t;
                    else text += (text.empty() ? "" : "\n") + std::string("- ") + t;
                } else {
                    text += (text.empty() ? "" : " ") + t;
                }
            }
            c.text = text;
            out.push_back(std::move(c));
        }
        parts.clear();
    };
    for (size_t i = 0; i < cues.size(); ++i) {
        const SrtCue& c = cues[i];
        const CueSpeaker w = i < who.size() ? who[i] : CueSpeaker{};
        if (w.group < 0) {
            flush();
            out.push_back(c);
            continue;
        }
        if (!parts.empty() && parts.back().group != w.group) flush();
        parts.push_back({&c, w.speaker, w.group});
    }
    flush();
    // Number them again when the file had numbers.
    int k = 0;
    for (auto& c : out)
        if (!c.index.empty()) c.index = std::to_string(++k);
    return out;
}

namespace {

// The code point that ends / starts `s` (0: none).
uint32_t last_code_point(const std::string& s) {
    size_t i = s.size();
    while (i > 0 && ((unsigned char)s[i - 1] & 0xC0) == 0x80) --i;
    if (i == 0) return 0;
    --i;
    uint32_t c = (unsigned char)s[i];
    if (c < 0x80) return c;
    c &= c >= 0xF0 ? 0x07 : c >= 0xE0 ? 0x0F : 0x1F;
    for (size_t j = i + 1; j < s.size(); ++j) c = (c << 6) | ((unsigned char)s[j] & 0x3F);
    return c;
}
uint32_t first_code_point(const std::string& s) {
    if (s.empty()) return 0;
    uint32_t c = (unsigned char)s[0];
    if (c < 0x80) return c;
    const size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
    c &= c >= 0xF0 ? 0x07 : c >= 0xE0 ? 0x0F : 0x1F;
    for (size_t j = 1; j < n && j < s.size(); ++j) c = (c << 6) | ((unsigned char)s[j] & 0x3F);
    return c;
}
// Chinese and Japanese (and their punctuation), written without spaces. Not Korean.
bool unspaced(uint32_t c) {
    return (c >= 0x2E80 && c <= 0x30FF) || (c >= 0x3400 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) ||
           (c >= 0xFF00 && c <= 0xFF60) || (c >= 0x20000 && c <= 0x2FFFF);
}

std::string name_of(const SpeakerMap& map, int id) {
    const SpeakerInfo* s = map.find(id);
    return s && !s->name.empty() ? s->name : "Speaker " + std::to_string(id + 1);
}

}  // namespace

void append_spoken(std::string* text, const std::string& more) {
    if (more.empty()) return;
    if (!text->empty() && !(unspaced(last_code_point(*text)) && unspaced(first_code_point(more)))) *text += ' ';
    *text += more;
}

std::vector<SrtCue> name_speaker_parts(const std::vector<SrtCue>& cues, const SpeakerMap& map) {
    const std::vector<CueSpeaker> who = cue_speakers(cues, map);
    const bool names = map.speakers.size() >= 2;
    std::vector<SrtCue> out;
    int last = -1;  // who spoke last
    size_t i = 0;
    while (i < cues.size()) {
        // The parts of one subtitle (a cue the map doesn't have is one on its own).
        size_t e = i + 1;
        if (who[i].group >= 0)
            while (e < cues.size() && who[e].group == who[i].group) ++e;
        SrtCue c = cues[i];
        c.end = cues[e - 1].end;
        if (e - i > 1) c.timing = srt_timing(c.start, c.end);
        // One line per speaker.
        std::vector<std::pair<int, std::string>> lines;
        for (size_t k = i; k < e; ++k) {
            const std::string t = flat(cues[k].text);
            if (t.empty()) continue;
            const int s = who[k].speaker >= 0 ? who[k].speaker : lines.empty() ? last : lines.back().first;
            if (!lines.empty() && lines.back().first == s) append_spoken(&lines.back().second, t);
            else lines.push_back({s, t});
        }
        std::string text;
        for (const auto& [s, t] : lines) {
            if (!text.empty()) text += '\n';
            if (names && s >= 0 && s != last) text += name_of(map, s) + ": ";
            text += t;
            if (s >= 0) last = s;
        }
        c.text = text;
        if (!text.empty()) out.push_back(std::move(c));
        i = e;
    }
    int k = 0;
    for (auto& c : out)
        if (!c.index.empty()) c.index = std::to_string(++k);
    return out;
}

std::vector<SpeakerTurnText> speaker_turn_texts(const std::vector<SrtCue>& cues, const SpeakerMap& map) {
    const std::vector<CueSpeaker> who = cue_speakers(cues, map);
    std::vector<SpeakerTurnText> turns;
    for (size_t i = 0; i < cues.size(); ++i) {
        const std::string t = flat(strip_srt_tags(cues[i].text));
        if (t.empty()) continue;
        const int s = who[i].speaker;
        if (turns.empty() || (s >= 0 && turns.back().speaker >= 0 && s != turns.back().speaker)) {
            turns.push_back({s, std::string(), t});
        } else {
            if (turns.back().speaker < 0) turns.back().speaker = s;  // the first subtitles had no speaker
            append_spoken(&turns.back().text, t);
        }
    }
    for (auto& t : turns) t.name = t.speaker >= 0 ? name_of(map, t.speaker) : std::string();
    return turns;
}

}  // namespace pk
