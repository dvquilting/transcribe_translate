#include "subtitles.h"

#include <cmath>
#include <sstream>

#include "text.h"

namespace st {

std::string bilingual_srt(const std::vector<tl::Cue>& original, const std::vector<tl::Cue>& translated) {
    std::vector<tl::Cue> out;
    size_t j = 0;
    for (size_t i = 0; i < translated.size(); ++i) {
        const tl::Cue& t = translated[i];
        // Same file timing, so normally original[i]; search nearby in case cues differ.
        const tl::Cue* o = nullptr;
        for (size_t k = j; k < original.size() && k < j + 5; ++k) {
            if (std::fabs(original[k].start - t.start) < 0.01 && std::fabs(original[k].end - t.end) < 0.01) {
                o = &original[k];
                j = k + 1;
                break;
            }
        }
        tl::Cue c = t;
        if (c.index.empty()) c.index = std::to_string(i + 1);
        c.text = (o && !tl::trim(o->text).empty() ? tl::trim(o->text) + "\n" : std::string()) + tl::trim(t.text);
        out.push_back(std::move(c));
    }
    return pk::format_srt(out);
}

std::string subtitles_to_text(const std::vector<tl::Cue>& cues, int sentences_per_paragraph) {
    std::string all;
    for (const auto& c : cues) {
        const std::string t = tl::normalize_space(pk::strip_srt_tags(c.text));
        if (t.empty()) continue;
        if (!all.empty()) all += ' ';
        all += t;
    }
    const auto sentences = tl::split_sentences(all);
    std::string out, para;
    int n = 0;
    for (const auto& s : sentences) {
        para += (para.empty() ? "" : " ") + s;
        if (++n >= std::max(1, sentences_per_paragraph)) {
            out += (out.empty() ? "" : "\n\n") + para;
            para.clear();
            n = 0;
        }
    }
    if (!para.empty()) out += (out.empty() ? "" : "\n\n") + para;
    return out + "\n";
}

std::string with_suffix(const std::string& base, const std::string& suffix) {
    const size_t slash = base.find_last_of("/\\");
    const size_t dot = base.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) return base.substr(0, dot) + suffix;
    return base + suffix;
}

}  // namespace st
