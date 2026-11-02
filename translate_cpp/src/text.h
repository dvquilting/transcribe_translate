// text.h - small UTF-8 text helpers shared by the formats and the translator.
#pragma once

#include <string>
#include <vector>

#include "utf8_text.h"  // ../common

namespace tl {

// Shared with the other programs (../common/src/utf8_text.h).
using pk::clean_input_text;  // BOM, CRLF / CR -> LF, Windows-1252 -> UTF-8
using pk::normalize_space;   // collapse runs of whitespace to single spaces and trim
using pk::trim;              // spaces, tabs and line breaks at both ends
using pk::utf8_length;       // number of UTF-8 code points

// True if the text contains at least one letter (anything that isn't ASCII
// punctuation, digits or whitespace) - pure numbers/punctuation are not sent
// to the model.
bool has_letters(const std::string& s);

// Split into sentences after . ? ! and the CJK 。！？ (keeps the punctuation).
std::vector<std::string> split_sentences(const std::string& text);

// Group sentences into pieces of at most `max_chars` characters (a single
// longer sentence becomes its own piece).
std::vector<std::string> group_sentences(const std::vector<std::string>& sentences, size_t max_chars);

// Languages written without spaces between words.
bool is_cjk_language(const std::string& code);

// Split text into paragraphs at blank lines (lines inside a paragraph are kept).
std::vector<std::string> split_blank_lines(const std::string& text);

// Make model output safe to write: an incomplete character at the very end
// (generation cut off by a token limit) is dropped, any other invalid UTF-8
// becomes U+FFFD.
std::string sanitize_utf8(const std::string& s);

// The last `max_bytes` bytes or fewer of `s`, starting after a space (or at a
// character boundary when there is no space, e.g. Chinese).
std::string tail_at_word(const std::string& s, size_t max_bytes);

// Word-wrap UTF-8 text to `width` code points (languages without spaces are
// broken between characters).
std::vector<std::string> wrap_text(const std::string& text, size_t width);

}  // namespace tl
