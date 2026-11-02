// docx.h - translate Word documents paragraph by paragraph.
//
// Every paragraph in the body, tables, headers, footers, footnotes and
// endnotes is one unit, except that hyperlinks are separate semantic segments,
// and so is the text on either side of a page or column break (text never moves
// across such a break). The translation goes into the paragraph's first piece
// of real text (so the paragraph keeps its style, list numbering, alignment and
// that run's character formatting); the other pieces are emptied. Formatting
// that changes inside a paragraph (one bold word, say) is therefore not kept.
//
// Line breaks (w:br, w:cr) and tabs (w:tab) between pieces of text are "\n"
// and "\t" in the unit. They are written back as elements inside the first
// run, where the translation has them; if the model dropped or added some, the
// translation's own "\n"/"\t" become plain breaks/tabs (or there are none).
// Consecutive breaks are one "\n" in the unit (a blank line would split the
// unit in two), and written back as the original group when the counts match.
//
// A paragraph that anchors a text box keeps its own text; the text box's
// paragraphs are units of their own. Word stores a text box twice (mc:Choice
// for current readers, mc:Fallback for old ones); the fallback copy gets the
// same translation instead of being sent to the model again.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace tl {

class DocxDocument {
public:
    // Throws if the file can't be read or isn't a Word document.
    explicit DocxDocument(const std::string& path);

    const std::vector<std::string>& units() const { return units_; }

    // Write a copy with translations[i] in place of units()[i].
    void save(const std::string& path, const std::vector<std::string>& translations) const;

    // Exposed for tests: XML of one part -> units, and back.
    struct TextRun {
        size_t tag_begin, tag_end;          // the <w:t ...> start tag
        size_t content_begin, content_end;  // text between <w:t> and </w:t>
    };
    struct Paragraph {
        std::vector<TextRun> runs;
        size_t main_run = 0;  // the run that gets the translation (the first with real text)
        int unit = -1;
        std::string leading, trailing;  // spaces at hyperlink/ordinary-text boundaries
        // Breaks/tabs that are "\n"/"\t" in the unit, in order: the XML to write
        // for each (e.g. "<w:br/><w:br/>"), and the original elements' [begin, end)
        // positions, which are removed when the paragraph is rewritten.
        std::vector<std::string> seps;
        std::vector<std::pair<size_t, size_t>> moved;
    };
    static std::vector<Paragraph> scan_part(const std::string& xml, std::vector<std::string>& units);
    static std::string rewrite_part(const std::string& xml, const std::vector<Paragraph>& paras,
                                    const std::vector<std::string>& translations);

private:
    struct Part {
        std::string name;
        std::string xml;
        std::vector<Paragraph> paras;
    };
    std::string zip_bytes_;
    std::vector<Part> parts_;
    std::vector<std::string> units_;
};

// XML text escaping (&, <, > and quotes; control characters XML 1.0 doesn't
// allow are dropped) and unescaping (named and numeric entities).
std::string xml_escape(const std::string& s);
std::string xml_unescape(const std::string& s);

}  // namespace tl
