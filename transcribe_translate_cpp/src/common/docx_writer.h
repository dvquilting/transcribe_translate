// docx_writer.h - writes a simple Word document (.docx) from plain text: a
// title, an optional note in italics ("Subtitle" style), then the paragraphs (blank lines separate
// paragraphs, single line breaks are kept). No dependencies: the .docx (a zip
// archive of XML files) is written with stored, uncompressed entries, which
// Word, LibreOffice and Google Docs all open.
#pragma once

#include <string>
#include <vector>

namespace pk {

struct DocxText {
    std::string title;  // first line, in the "Title" style (empty: none)
    std::string note;   // a line in italics under the title (empty: none)
    std::string body;   // UTF-8 text: paragraphs separated by blank lines
    std::string language;  // document language for spell checking ("en", "de", ...); empty: none
    // Speakers' names ("Anna:"): a paragraph that starts with one has it in bold.
    std::vector<std::string> bold_starts;
};

// The .docx file's bytes.
std::string make_docx(const DocxText& doc);

// The paragraphs of `text`: split at blank lines, each trimmed; single line
// breaks inside a paragraph are kept as '\n'.
std::vector<std::string> text_paragraphs(const std::string& text);

}  // namespace pk
