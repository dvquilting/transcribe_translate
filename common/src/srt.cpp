#include "srt.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "utf8_text.h"

namespace pk {

bool parse_srt_time(const std::string& s, double* seconds) {
    int h = 0, m = 0, sec = 0, used = 0;
    if (std::sscanf(s.c_str(), "%d:%d:%d%n", &h, &m, &sec, &used) < 3) return false;
    // The fraction is a decimal one, whatever its length: ",5" and ",500" are
    // both half a second (not 5 ms). Digits past the millisecond count too (up
    // to nanoseconds; more are ignored).
    long long num = 0, den = 1;
    size_t i = (size_t)used;
    if (i < s.size() && (s[i] == ',' || s[i] == '.'))
        for (++i; i < s.size() && s[i] >= '0' && s[i] <= '9' && den < 1000000000LL; ++i) {
            num = num * 10 + (s[i] - '0');
            den *= 10;
        }
    *seconds = h * 3600.0 + m * 60.0 + sec + (double)num / (double)den;
    return true;
}

std::string srt_time(double seconds) {
    long long ms = std::max(0LL, (long long)std::llround(seconds * 1000.0));
    const long long h = ms / 3600000;
    ms %= 3600000;
    const long long m = ms / 60000;
    ms %= 60000;
    const long long s = ms / 1000;
    ms %= 1000;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld,%03lld", h, m, s, ms);
    return buf;
}

std::string srt_timing(double start, double end) { return srt_time(start) + " --> " + srt_time(end); }

std::string strip_srt_tags(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '<') {
            const size_t e = s.find('>', i);
            if (e != std::string::npos && e - i < 40) {
                i = e;
                continue;
            }
        }
        if (s[i] == '{' && i + 1 < s.size() && s[i + 1] == '\\') {
            const size_t e = s.find('}', i);
            if (e != std::string::npos) {
                i = e;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

std::vector<SrtCue> parse_srt(const std::string& text) {
    // Line by line rather than block by block: a timing line ("-->") starts a
    // new cue even without a blank line before it, and a blank line inside a
    // cue's text doesn't end the cue (real-world files have both). Splitting at
    // blank lines instead would read a missing separator's number and timing
    // line as text of the previous cue (and the dub would speak them).
    auto numeric = [](const std::string& s) {
        const std::string t = trim(s);
        return !t.empty() && t.find_first_not_of("0123456789") == std::string::npos;
    };
    std::vector<SrtCue> cues;
    std::vector<std::vector<std::string>> body;  // text lines per cue, blank ones included
    std::istringstream in(clean_input_text(text));  // BOM, CRLF, Windows-1252
    std::string line, prev;
    bool have_prev = false;
    while (std::getline(in, line)) {
        if (line.find("-->") != std::string::npos) {
            SrtCue c;
            // The cue number is the line just before the timing; it was taken as
            // text of the previous cue (or skipped, before the first cue).
            if (have_prev && numeric(prev)) {
                c.index = trim(prev);
                if (!body.empty() && !body.back().empty()) body.back().pop_back();
            }
            c.timing = trim(line);
            const size_t arrow = c.timing.find("-->");
            const bool start_ok = parse_srt_time(trim(c.timing.substr(0, arrow)), &c.start);
            const bool end_ok = parse_srt_time(trim(c.timing.substr(arrow + 3)), &c.end);
            c.timed = start_ok && end_ok;
            cues.push_back(c);
            body.emplace_back();
            have_prev = false;
            continue;
        }
        if (!body.empty()) body.back().push_back(line);
        prev = line;
        have_prev = true;
    }
    for (size_t k = 0; k < cues.size(); ++k) {
        // Blank lines are dropped (a blank line inside a cue would end it for other players).
        for (const auto& l : body[k])
            if (!trim(l).empty()) cues[k].text += (cues[k].text.empty() ? "" : "\n") + l;
    }
    return cues;
}

std::string format_srt(const std::vector<SrtCue>& cues) {
    std::string out;
    for (const auto& c : cues) {
        if (!c.index.empty()) out += c.index + "\n";
        out += c.timing + "\n" + c.text + "\n\n";
    }
    return out;
}

}  // namespace pk
