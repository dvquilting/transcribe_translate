// speakers.h - who speaks when: the file "transcribe_multi speakers" writes
// (and Speech Translate edits: names, two speakers who are one person) and
// "transcribe_multi dub --speakers" reads, plus marking speaker changes in
// subtitles.
//
// The file is plain text, one record per line, fields separated by tabs:
//
//   # speakers 1
//   speaker <id> <name> <seconds of speech> <sample start> <sample end>
//   cue <start> <end> <speaker id> <group>
//
// Times are seconds. A "cue" is one subtitle of the (split) subtitles: a
// subtitle in which the speaker changes is split into one cue per speaker, and
// those cues share their `group` (the number of the original subtitle), so
// they can be shown as one subtitle again with a dash before each speaker.
#pragma once

#include <string>
#include <vector>

#include "srt.h"

namespace pk {

struct SpeakerInfo {
    int id = 0;
    std::string name;                      // "Speaker 1", or what the user called them
    double seconds = 0;                    // how long they speak
    double sample_start = 0, sample_end = 0;  // a stretch of their speech to listen to
};

struct SpeakerCue {
    double start = 0, end = 0;
    int speaker = -1;
    int group = -1;  // the original subtitle (cues of one subtitle share it)
};

struct SpeakerMap {
    std::vector<SpeakerInfo> speakers;  // in order of first appearance
    std::vector<SpeakerCue> cues;       // in time order

    const SpeakerInfo* find(int id) const;
    // The cue record that overlaps [start, end] most (nullptr: none overlaps;
    // then the one nearest in time, when `nearest`).
    const SpeakerCue* at(double start, double end, bool nearest = true) const;
};

std::string format_speakers(const SpeakerMap& map);
// False (and *error) for a file that isn't one.
bool parse_speakers(const std::string& text, SpeakerMap* map, std::string* error = nullptr);

// Two speakers who are the same person: `into[k]` is the id speaker k becomes
// (itself when unchanged). The cues follow, merged speakers' times add up, and
// speakers that are gone are removed from the list.
void merge_speakers(SpeakerMap* map, const std::vector<std::pair<int, int>>& into);

// The speaker and group of each cue: of the map's cue with the same times
// (-1, -1 for a cue the map doesn't have).
struct CueSpeaker {
    int speaker = -1, group = -1;
};
std::vector<CueSpeaker> cue_speakers(const std::vector<SrtCue>& cues, const SpeakerMap& map);

// Subtitles split by speaker, back as the original subtitles: consecutive cues
// of one group become one subtitle again. When its parts are by different
// speakers, each part is on its own line, starting with "- " (the usual way of
// showing two speakers in one subtitle); parts by one speaker are simply joined.
// `who` has an entry per cue; cues with group -1 stay as they are.
std::vector<SrtCue> join_speaker_parts(const std::vector<SrtCue>& cues, const std::vector<CueSpeaker>& who);
// The same, with the speakers and groups of `map` (matched by the cues' times).
std::vector<SrtCue> join_speaker_parts(const std::vector<SrtCue>& cues, const SpeakerMap& map);

// As join_speaker_parts(), but with the speakers' names instead of dashes (a
// transcript's subtitles): "Anna: " starts the text where the speaker changes -
// the first subtitle of each turn, and each speaker's line in a subtitle with a
// change in it. Each speaker's text is on one line (wrap it afterwards). With
// fewer than two speakers in `map` no names are added.
std::vector<SrtCue> name_speaker_parts(const std::vector<SrtCue>& cues, const SpeakerMap& map);

// What was said as turns: the text of consecutive subtitles by one speaker,
// joined. A subtitle without a speaker belongs to the turn before it (the
// first: to the one after).
struct SpeakerTurnText {
    int speaker = -1;
    std::string name;
    std::string text;
};
std::vector<SpeakerTurnText> speaker_turn_texts(const std::vector<SrtCue>& cues, const SpeakerMap& map);

// `more` after `text`, with a space between - except between two CJK
// characters, which are written without one.
void append_spoken(std::string* text, const std::string& more);

}  // namespace pk
