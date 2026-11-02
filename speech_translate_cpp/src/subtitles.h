// subtitles.h - turn the translated subtitles into the other outputs.
#pragma once

#include <string>
#include <vector>

#include "formats.h"  // tl::Cue, tl::parse_srt (from translate_cpp)

namespace st {

// One .srt with the original line(s) above the translation, cue by cue.
// Cues are matched by timing; a translated cue with no original keeps its own text.
std::string bilingual_srt(const std::vector<tl::Cue>& original, const std::vector<tl::Cue>& translated);

// The subtitle text as plain paragraphs (formatting tags removed, cue line
// breaks joined, `sentences_per_paragraph` sentences per paragraph).
std::string subtitles_to_text(const std::vector<tl::Cue>& cues, int sentences_per_paragraph);

// Replace the extension of `base` (or add one): ("talk.mp3", ".de.srt") -> "talk.de.srt".
std::string with_suffix(const std::string& base, const std::string& suffix);

}  // namespace st
