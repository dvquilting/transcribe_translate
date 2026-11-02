// transcript.h - transcript data types and .txt / .srt formatting.
#pragma once

#include <string>
#include <vector>

#include "srt.h"        // ../common: srt_time
#include "utf8_text.h"  // ../common: normalize_space, utf8_length

namespace tmx {

using pk::normalize_space;  // collapse runs of whitespace to single spaces and trim
using pk::srt_time;         // "HH:MM:SS,mmm"
using pk::utf8_length;      // number of UTF-8 code points

struct Word {
    std::string text;
    double start = 0.0;  // seconds
    double end = 0.0;
};

struct Segment {
    double start = 0.0;
    double end = 0.0;
    std::string text;
    std::vector<Word> words;
};

// Subtitle cue shaping
constexpr size_t kCueMaxChars = 84;   // ~2 lines of 42 characters
constexpr double kCueMaxSeconds = 6.0;
constexpr double kCueMaxGap = 0.8;    // start a new cue after a pause this long
constexpr size_t kCueLineWidth = 42;

// Group word timings into subtitle-sized cues.
std::vector<Segment> words_to_cues(const std::vector<Word>& words);

// Full SRT document for the given cues.
std::string format_srt(const std::vector<Segment>& cues);

// Collapse whitespace and group sentences into paragraphs separated by blank lines.
std::string format_paragraphs(const std::string& text, int sentences_per_paragraph);

// ---- Paragraphs ----------------------------------------------------------
// Split into sentences (a word ending in . ? ! or an ellipsis; also after the
// full-width CJK marks, which need no space after them). A run longer than
// `max_words` without sentence punctuation is cut into pieces of that size
// (0 = never cut).
std::vector<std::string> split_sentences(const std::string& text, int max_words = 0);

// Silence (seconds) before each sentence, from word timings; -1 where unknown.
// Sentences and timed words are matched by character position, so differences
// in how words are split do not matter. Empty if the timings don't match the text.
std::vector<double> sentence_pauses(const std::vector<std::string>& sentences, const std::vector<Word>& words);

// Join sentences into paragraphs; `breaks` holds the indices of the sentences
// that start a new paragraph (sorted, 0 is implied). Sentences are separated by
// a space, except CJK sentences after a full-width sentence end.
std::string join_paragraphs(const std::vector<std::string>& sentences, const std::vector<int>& breaks);

// All integers in a model reply such as "4, 11 and 19." (in order).
std::vector<int> parse_number_list(const std::string& reply);

// Make proposed paragraph breaks sane: drop breaks that would leave a
// paragraph of one short sentence, and split paragraphs longer than
// `max_sentences` sentences or `max_words` words (at the longest pause when
// `pauses` is given, else in the middle). Returns sorted break indices.
std::vector<int> tidy_paragraph_breaks(std::vector<int> breaks, const std::vector<std::string>& sentences,
                                       const std::vector<double>& pauses, int max_sentences, int max_words);

// ---- Punctuation ---------------------------------------------------------
// True when `text` looks unpunctuated: at least 12 words and fewer than one
// sentence end (. ? !) per 40 words, or no capital letters at all.
bool lacks_punctuation(const std::string& text);

// Lowercase and remove the punctuation the model writes itself, as the
// punctuation model expects: sentence punctuation, quotes, brackets and dashes
// at the start and end of words. What is inside a word stays (it's, l'ete,
// well-known, 3.5, 10:30, AT&T, 24/7), as do symbols such as % $ & + that are
// part of the words. ASCII and Latin-1 letters are lowercased.
std::string strip_punctuation(const std::string& text);

// True when `a` and `b` are the same words, ignoring the punctuation removed by
// strip_punctuation() and letter case (the punctuation model's sanity check).
bool same_words(const std::string& a, const std::string& b);

// Put `text` - the words of `words` with punctuation and capitals restored -
// onto the timed words, keeping their timings. Returns false and changes
// nothing if the words don't correspond one to one.
bool apply_restored_text(std::vector<Word>& words, const std::string& text);

// ---- Chunks for the language model ---------------------------------------
// Paragraphs (separated by blank lines), whitespace collapsed.
std::vector<std::string> split_paragraphs(const std::string& text);

struct TextChunk {
    std::string text;        // paragraphs separated by blank lines
    bool continues = false;  // continues the previous chunk's last paragraph
};

// Group paragraphs into chunks of up to about `chunk_words` words; a paragraph
// longer than twice that is cut into pieces of `chunk_words` words on its own.
std::vector<TextChunk> chunk_paragraphs(const std::string& text, int chunk_words);

// Join processed chunks (`outputs[i]` from `chunks[i]`): blank lines between
// paragraphs, a space where a chunk continues a paragraph.
std::string join_chunks(const std::vector<TextChunk>& chunks, const std::vector<std::string>& outputs);

// Word-wrap UTF-8 text to `width` characters (code points).
std::vector<std::string> wrap_text(const std::string& text, size_t width);

}  // namespace tmx
