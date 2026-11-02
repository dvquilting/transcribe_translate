// formats.h - split documents into units of text to translate, and put the
// translations back in place. Plain text, Markdown and SubRip subtitles here;
// Word documents in docx.h.
#pragma once

#include <string>
#include <vector>

#include "srt.h"  // ../common

namespace tl {

// A document as a list of pieces: literal text kept as is, or a unit to translate.
struct Layout {
    struct Part {
        std::string before;  // literal text (markup, line breaks) before the unit
        int unit = -1;       // index into units, or -1 for a literal-only part
        std::string after;   // literal text after the unit
    };
    std::vector<Part> parts;
    std::vector<std::string> units;

    // Rebuild the document with translations[i] in place of units[i].
    std::string render(const std::vector<std::string>& translations) const;
};

// Plain text: paragraphs separated by blank lines.
Layout parse_plain_text(const std::string& text);

// Markdown: headings, list items, quotes, table cells and paragraphs are units;
// code blocks, front matter, HTML lines and link targets are left alone.
Layout parse_markdown(const std::string& text);

// ---- SubRip subtitles ------------------------------------------------------
// Read with the reader shared by all three programs (../common/src/srt.h):
// tolerant of BOM, CRLF, Windows-1252, a missing blank line between cues,
// blank lines inside a cue's text (dropped) and missing numbers.
using Cue = pk::SrtCue;  // index and timing line as written, start/end, text (lines joined with '\n')
using pk::parse_srt;

// Group cues into sentence-sized units: a group ends at a cue ending in
// sentence punctuation, before a pause of over 2 s, or after 6 cues.
struct SrtPlan {
    std::vector<Cue> cues;
    std::vector<std::vector<size_t>> groups;  // cue indices per unit
    std::vector<std::string> units;           // the text of each group (formatting tags removed)
    std::vector<bool> italic;                 // per cue: whole cue was in <i>...</i>
};
SrtPlan plan_srt(const std::vector<Cue>& cues);

// Write the translated subtitles: each group's translation is divided over its
// cues in proportion to the source text length, then wrapped to 42 characters.
std::string render_srt(const SrtPlan& plan, const std::vector<std::string>& translations,
                       const std::string& target_lang);

// Divide `text` into `weights.size()` consecutive parts with lengths roughly
// proportional to `weights`, breaking between words (or characters for
// languages written without spaces). Never returns an empty part if it can avoid it.
std::vector<std::string> split_proportionally(const std::string& text, const std::vector<size_t>& weights,
                                              bool by_characters);

}  // namespace tl
