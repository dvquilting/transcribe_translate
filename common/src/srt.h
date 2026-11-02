// srt.h - the one SubRip (.srt) reader and writer of all three programs:
// transcribe_multi writes subtitles and reads them back to dub them,
// translate_text translates them, and Speech Translate combines them.
#pragma once

#include <string>
#include <vector>

namespace pk {

struct SrtCue {
    std::string index;   // as written ("12"); empty when the file has no number
    std::string timing;  // the timing line as written: "00:00:01,000 --> 00:00:03,500"
    double start = 0, end = 0;  // seconds; 0 when a time can't be read (see `timed`)
    std::string text;    // the cue's lines joined with '\n', formatting tags kept
    bool timed = false;  // both times of the timing line could be read
};

// Tolerant of real-world files: BOM, CRLF, Windows-1252, a missing blank line
// between cues (a line containing "-->" always starts a new cue), blank lines
// inside a cue's text (dropped), missing cue numbers. Cues stay in file order.
std::vector<SrtCue> parse_srt(const std::string& text);

// The cues as an .srt document: per cue its number (when it has one), the
// timing line, the text and a blank line. Writes exactly what it is given.
std::string format_srt(const std::vector<SrtCue>& cues);

// "HH:MM:SS,mmm" (also accepts '.' before the milliseconds) -> seconds.
bool parse_srt_time(const std::string& s, double* seconds);

// Seconds -> "HH:MM:SS,mmm" (rounded to the millisecond, negative -> 0).
std::string srt_time(double seconds);

// "HH:MM:SS,mmm --> HH:MM:SS,mmm"
std::string srt_timing(double start, double end);

// Remove formatting: HTML-like tags (<i>, <font ...>) and ASS overrides ({\an8}).
std::string strip_srt_tags(const std::string& s);

}  // namespace pk
