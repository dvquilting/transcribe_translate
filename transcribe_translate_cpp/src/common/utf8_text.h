// utf8_text.h - the basic UTF-8 text helpers that the subtitle code needs and
// that transcribe_cpp and translate_cpp used to define each for themselves.
#pragma once

#include <cstddef>
#include <string>

namespace pk {

// Trim spaces, tabs and line breaks at both ends.
std::string trim(const std::string& s);

// Collapse runs of whitespace to single spaces and trim.
std::string normalize_space(const std::string& s);

// Number of UTF-8 code points.
size_t utf8_length(const std::string& s);

// Text read from a file as the user saved it: a UTF-8 byte-order mark is
// removed and CRLF / CR become LF. Invalid UTF-8 (e.g. a Windows-1252 subtitle
// file from an old editor) is converted from Windows-1252: the whole text if it
// has no valid multi-byte UTF-8 at all, otherwise only the invalid bytes (a
// UTF-8 file with a few stray Windows-1252 characters keeps its UTF-8).
std::string clean_input_text(const std::string& raw);

}  // namespace pk
