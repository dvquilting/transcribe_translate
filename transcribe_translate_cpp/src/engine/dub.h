// dub.h - speak translated subtitles in the original speaker's voice and fit
// them to the original timing ("tt_engine dub ...").
//
// Responsible use is built in:
//   * cloning a voice needs an explicit statement that the speaker agreed
//     (--i-have-rights); without it a built-in voice is used;
//   * every synthesized clip carries CrispASR's inaudible AI watermark;
//   * cloned output starts with a short spoken AI disclosure (in a neutral,
//     not the cloned, voice) - for videos, where that would shift the timing,
//     it is placed before the first line when it fits and is otherwise shown as
//     the first subtitle of the video.
#pragma once

#include <string>
#include <vector>

#include "model_catalog.h"
#include "speakers.h"  // ../common
#include "srt.h"
#include "transcript.h"  // Word

namespace tmx {

// The voice models are shared with Speech Translate's menus (../common/src/model_catalog.h).
using TtsModel = pk::TtsModel;
using pk::find_tts_model;
using pk::tts_models;

// A subtitle cue to speak (times in seconds).
struct SubCue {
    double start = 0, end = 0;
    std::string text;  // tags removed, lines joined with spaces
    int speaker = -1;  // who says it (--speakers), -1 = unknown / the only speaker
    int group = -1;    // the original subtitle, when it was split by speaker (--speakers)
};
// The cues of an .srt file, read with the shared SubRip reader (../common/src/srt.h),
// sorted by start time. Cues without text or with an unreadable timing line are skipped.
std::vector<SubCue> read_srt_cues(const std::string& path);
// A text file as UTF-8 without BOM, line ends normalized to \n (Windows-1252 is converted).
std::string read_text_file(const std::string& path);

// Groups of consecutive cues spoken as one utterance: joined across short gaps
// while the sentence continues, at most `max_seconds` long.
struct SpeechLine {
    double start = 0, end = 0;  // from the subtitles
    std::string text;
    size_t first_cue = 0, cue_count = 0;
    int speaker = -1;
};
// Cues by different speakers are never joined.
std::vector<SpeechLine> group_cues(const std::vector<SubCue>& cues, double max_seconds = 20.0);

// Where each synthesized line goes. `lengths` are the natural lengths of the
// synthesized lines (s). Lines may be sped up to `max_speed`; a line that is
// still too long starts later and pushes the following lines back until a
// pause absorbs the delay.
struct Placement {
    double at = 0;      // start time in the output (s)
    double speed = 1;   // tempo factor applied (>= 1)
    double overrun = 0; // seconds the line is late or runs past its slot
};
std::vector<Placement> place_lines(const std::vector<SpeechLine>& lines, const std::vector<double>& lengths,
                                   double max_speed, double total_seconds);

// Preserve original cue text, allocating each grouped utterance's actual duration
// among its cues by text length. Exact word timings are not supplied by the TTS.
std::vector<SubCue> retime_cues(const std::vector<SubCue>& cues, const std::vector<SpeechLine>& lines,
                              const std::vector<Placement>& placements, const std::vector<double>& durations,
                              double offset = 0);

// Split a line into pieces the speech model handles well: whole sentences,
// joined up to about `max_chars` characters; a longer sentence is split at
// clause punctuation (, ; :) and, failing that, between words.
std::vector<std::string> split_for_speech(const std::string& text, size_t max_chars = 140);

// Longest believable length (s) of speech for `text`. TTS models sometimes
// "run away" (never stop, babbling for many seconds); a clip longer than this
// is treated as such. Healthy speech is ~0.1-0.2 s per character/codepoint.
double max_plausible_seconds(const std::string& text);

// Several stretches of the original speech to take the voice from, best first,
// not overlapping, adding up to at least `want_seconds` (or as much as there is).
// Each is 3-14 s of consecutive cues; with want_seconds <= 0 only the best one.
// With `speaker` >= 0 only that speaker's cues count, and a stretch never runs
// across someone else's cue.
struct RefClip {
    double start = 0, end = 0;
    std::string text;
};
std::vector<RefClip> choose_references(const std::vector<SubCue>& source, double want_seconds, int speaker = -1);

// ---- several speakers ("tt_engine speakers") ----

// A stretch of one speaker's speech, as speaker detection found it.
struct SpeakerTurn {
    double start = 0, end = 0;
    int speaker = 0;
};
// A subtitle, or the part of one, spoken by one speaker.
struct SpeakerPart {
    double start = 0, end = 0;
    std::string text;
    int speaker = -1;
    int group = 0;  // the subtitle it is (part of): its position in `cues`
};
// The subtitles with each one's speaker; a subtitle in which the speaker changes
// is split there - its text at the sentence end, else the comma, else the word
// gap nearest to where the change falls by length. `labels` is the speaker
// detection gave each whole subtitle (-1 = none), `turns` who speaks when.
// Changes shorter than `min_part` seconds are ignored (a "yes" in between
// isn't worth a subtitle of its own). Subtitles without a speaker get the one
// before (or after) them.
//
// With the word timings of the transcription (`words`), each word gets the
// speaker who speaks at that moment and a subtitle is split exactly between
// two words where the speaker changes; only changes shorter than
// `min_word_part` seconds are ignored there. Without them the split is
// estimated from the text's length, which is much less exact when people
// speak in quick turns.
std::vector<SpeakerPart> split_by_speaker(const std::vector<pk::SrtCue>& cues, const std::vector<int>& labels,
                                          const std::vector<SpeakerTurn>& turns, double min_part = 1.0,
                                          const std::vector<Word>& words = {}, double min_word_part = 0.45);

// The word timings file of "tt_engine transcribe --words": one word per line,
// "start<TAB>end<TAB>word" (seconds). Unreadable lines are skipped.
std::string format_words(const std::vector<Word>& words);
std::vector<Word> parse_words(const std::string& text);
// Where to split `text` so that about `frac` (0-1) of it comes first: the byte
// position where the second part starts (0 = nowhere to split).
size_t text_split_point(const std::string& text, double frac);
// Entry point for "tt_engine speakers ARGS" (argv[0] = "speakers").
int speakers_main(const std::vector<std::string>& args);

// Pick the best stretch of the original speech to clone: 5-14 s of
// consecutive cues (ideally ~10 s). Returns false if there is nothing usable.
bool choose_reference(const std::vector<SubCue>& source, double* start, double* end, std::string* text);

// Entry point for "tt_engine dub ARGS" (argv[0] = "dub").
int dub_main(const std::vector<std::string>& args);

}  // namespace tmx
