#include "docx.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "fileio.h"
#include "miniz.h"
#include "pathutil.h"
#include "text.h"

namespace tl {

std::string xml_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        default:
            // XML 1.0 allows no control characters but tab, LF and CR; one in
            // the model's output would make Word refuse the whole document.
            if ((unsigned char)c < 0x20 && c != '\t' && c != '\n' && c != '\r') break;
            o += c;
        }
    }
    return o;
}

namespace {
void append_utf8(std::string& o, unsigned long cp) {
    if (cp < 0x80) {
        o += (char)cp;
    } else if (cp < 0x800) {
        o += (char)(0xC0 | (cp >> 6));
        o += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        o += (char)(0xE0 | (cp >> 12));
        o += (char)(0x80 | ((cp >> 6) & 0x3F));
        o += (char)(0x80 | (cp & 0x3F));
    } else {
        o += (char)(0xF0 | (cp >> 18));
        o += (char)(0x80 | ((cp >> 12) & 0x3F));
        o += (char)(0x80 | ((cp >> 6) & 0x3F));
        o += (char)(0x80 | (cp & 0x3F));
    }
}
}  // namespace

std::string xml_unescape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            o += s[i];
            continue;
        }
        const size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10) {
            o += s[i];
            continue;
        }
        const std::string ent = s.substr(i + 1, semi - i - 1);
        if (ent == "amp") o += '&';
        else if (ent == "lt") o += '<';
        else if (ent == "gt") o += '>';
        else if (ent == "quot") o += '"';
        else if (ent == "apos") o += '\'';
        else if (!ent.empty() && ent[0] == '#') {
            const unsigned long cp = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')
                                         ? std::strtoul(ent.c_str() + 2, nullptr, 16)
                                         : std::strtoul(ent.c_str() + 1, nullptr, 10);
            append_utf8(o, cp);
        } else {
            o += s.substr(i, semi - i + 1);
        }
        i = semi;
    }
    return o;
}

namespace {

// Is there an element named exactly `name` starting at xml[pos] ("<w:p" must not match "<w:pPr")?
bool tag_at(const std::string& xml, size_t pos, const char* name) {
    const size_t n = std::strlen(name);
    if (xml.compare(pos, n, name) != 0) return false;
    const char c = pos + n < xml.size() ? xml[pos + n] : '\0';
    return c == '>' || c == ' ' || c == '/' || c == '\t' || c == '\r' || c == '\n';
}

bool self_closing(const std::string& xml, size_t tag_begin, size_t* tag_end) {
    const size_t e = xml.find('>', tag_begin);
    if (tag_end) *tag_end = e == std::string::npos ? xml.size() : e + 1;
    return e != std::string::npos && e > 0 && xml[e - 1] == '/';
}

}  // namespace

namespace {

using Paragraph = DocxDocument::Paragraph;

// A paragraph found while scanning, before the units are numbered.
struct Found {
    size_t begin;
    Paragraph para;
    std::string text;  // the unit
    int twin = -1;     // a text box's mc:Fallback copy: index of the mc:Choice paragraph it mirrors
};

// A <w:p> being read. Its pieces are text and break/tab elements in order.
struct OpenPara {
    size_t begin;
    std::vector<DocxDocument::TextRun> runs;
    struct Piece {
        char kind;     // 't' text, '\n' line break, '\t' tab, ' ' break outside the text
        size_t b, e;   // the element (for breaks and tabs)
        std::string text;
    };
    std::vector<Piece> pieces;
    bool segmented = false;
};

bool all_space(const std::string& s, size_t from) {
    for (size_t i = from; i < s.size(); ++i)
        if (s[i] != ' ' && s[i] != '\t') return false;
    return true;
}

// Turn a finished paragraph into a unit. Breaks and tabs between the first
// and the last piece of text become "\n" / "\t"; the ones before or after all
// text (a line break before the first word, say) stay where they are, and so
// does the translation: it goes into the run of the first piece of real text,
// not into a run of spaces in front of such a break.
bool finish_paragraph(OpenPara& op, Found& f) {
    if (op.runs.empty()) return false;
    size_t first = op.pieces.size(), last = 0, first_run = 0, text_pieces = 0;
    for (size_t i = 0; i < op.pieces.size(); ++i) {
        if (op.pieces[i].kind != 't') continue;
        if (!trim(op.pieces[i].text).empty()) {
            if (i < first) first = i, first_run = text_pieces;
            last = i;
        }
        ++text_pieces;  // the pieces of text and op.runs are in step
    }
    std::string text;
    size_t nl_text = std::string::npos, nl_sep = 0;  // where the last "\n" is, in text and in seps
    for (size_t i = 0; i < op.pieces.size(); ++i) {
        const auto& pc = op.pieces[i];
        if (pc.kind == 't') {
            text += pc.text;
        } else if ((pc.kind == '\n' || pc.kind == '\t') && i > first && i < last) {
            f.para.moved.push_back({pc.b, pc.e});
            const std::string xml = pc.kind == '\n' ? "<w:br/>" : "<w:tab/>";
            if (pc.kind == '\n' && nl_text != std::string::npos && all_space(text, nl_text)) {
                // Another break with nothing but spaces/tabs since the last one: join
                // them, since an empty line in the unit would split it in two.
                text.resize(nl_text);
                for (size_t k = nl_sep + 1; k < f.para.seps.size(); ++k) f.para.seps[nl_sep] += f.para.seps[k];
                f.para.seps.resize(nl_sep + 1);
                f.para.seps[nl_sep] += xml;
                continue;
            }
            text += pc.kind;
            f.para.seps.push_back(xml);
            if (pc.kind == '\n') nl_text = text.size(), nl_sep = f.para.seps.size() - 1;
        } else if (!text.empty() && text.back() != ' ') {
            text += ' ';  // other breaks, and breaks/tabs outside the text (trimmed below)
        }
    }
    f.text = trim(text);
    if (op.segmented && !f.text.empty()) {
        const size_t begin = text.find(f.text);
        f.para.leading = text.substr(0, begin);
        f.para.trailing = text.substr(begin + f.text.size());
    }
    if (!has_letters(f.text)) return false;
    f.begin = op.begin;
    f.para.runs = std::move(op.runs);
    f.para.main_run = first_run;
    return true;
}

// Position of the "</mc:Fallback>" that closes the element whose start tag ends at `from`.
size_t fallback_end(const std::string& xml, size_t from, size_t limit) {
    int depth = 1;
    for (size_t pos = xml.find('<', from); pos != std::string::npos && pos < limit; pos = xml.find('<', pos + 1)) {
        if (tag_at(xml, pos, "<mc:Fallback") && !self_closing(xml, pos, nullptr)) ++depth;
        else if (xml.compare(pos, 14, "</mc:Fallback>") == 0 && --depth == 0) return pos;
    }
    return limit;
}

// Collect the paragraphs in xml[b, e). Text belongs to the innermost open
// paragraph, so a paragraph holding a text box keeps its own text and the text
// box's paragraphs are found separately.
void collect(const std::string& xml, size_t b, size_t e, std::vector<Found>& found) {
    std::vector<OpenPara> stack;
    struct Alt {
        size_t choice_begin = std::string::npos, choice_end = std::string::npos;  // indices into found
    };
    std::vector<Alt> alts;  // open mc:AlternateContent elements
    for (size_t pos = xml.find('<', b); pos != std::string::npos && pos < e; pos = xml.find('<', pos + 1)) {
        const char c1 = pos + 1 < xml.size() ? xml[pos + 1] : '\0';
        if (c1 == 'w') {
            size_t te;
            if (tag_at(xml, pos, "<w:p")) {
                if (!self_closing(xml, pos, &te)) stack.push_back({pos, {}, {}});
            } else if (tag_at(xml, pos, "<w:hyperlink")) {
                if (stack.empty() || self_closing(xml, pos, &te)) continue;
                OpenPara& op = stack.back();
                op.segmented = true;
                Found f;
                if (finish_paragraph(op, f)) found.push_back(std::move(f));
                op.runs.clear(); op.pieces.clear(); op.begin = pos;
            } else if (tag_at(xml, pos, "<w:t")) {
                if (self_closing(xml, pos, &te)) continue;
                const size_t close = xml.find("</w:t>", te);
                if (close == std::string::npos || close > e) break;
                if (!stack.empty()) {
                    stack.back().runs.push_back({pos, te, te, close});
                    std::string t = xml_unescape(xml.substr(te, close - te));
                    for (char& ch : t)  // stray line breaks/tabs in the text are just spaces in Word
                        if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
                    stack.back().pieces.push_back({'t', pos, close, std::move(t)});
                }
                pos = close;
            } else if (tag_at(xml, pos, "<w:br") || tag_at(xml, pos, "<w:cr") || tag_at(xml, pos, "<w:tab")) {
                self_closing(xml, pos, &te);
                if (stack.empty()) continue;
                const std::string elem = xml.substr(pos, te - pos);
                char kind = ' ';
                if (xml.compare(pos, 6, "<w:tab") == 0) {
                    // Only a bare <w:tab/> is a tab character (w:tabs/w:tab with
                    // attributes in the paragraph properties are tab stops).
                    if (elem.find('=') == std::string::npos) kind = '\t';
                    else continue;
                } else if (elem.find("w:type=") == std::string::npos || elem.find("textWrapping") != std::string::npos) {
                    kind = '\n';  // a line break; page and column breaks are left where they are
                } else {
                    // A page or column break: the text before it and the text
                    // after it are separate units, so no text moves across the
                    // break (a heading after a page break stays on the new page).
                    OpenPara& op = stack.back();
                    for (const auto& pc : op.pieces)
                        if (pc.kind == 't' && has_letters(pc.text)) op.segmented = true;  // keep the spaces at the cut
                    Found f;
                    if (finish_paragraph(op, f)) found.push_back(std::move(f));
                    op.runs.clear(); op.pieces.clear(); op.begin = te;
                    continue;
                }
                stack.back().pieces.push_back({kind, pos, te, {}});
            }
        } else if (xml.compare(pos, 14, "</w:hyperlink>") == 0) {
            if (stack.empty()) continue;
            OpenPara& op = stack.back();
            Found f;
            if (finish_paragraph(op, f)) found.push_back(std::move(f));
            op.runs.clear(); op.pieces.clear(); op.begin = pos + 14;
        } else if (c1 == '/' && xml.compare(pos, 6, "</w:p>") == 0) {
            if (stack.empty()) continue;
            Found f;
            if (finish_paragraph(stack.back(), f)) found.push_back(std::move(f));
            stack.pop_back();
        } else if (c1 == 'm' || (c1 == '/' && xml.compare(pos, 5, "</mc:") == 0)) {
            size_t te;
            if (tag_at(xml, pos, "<mc:AlternateContent")) {
                if (!self_closing(xml, pos, &te)) alts.push_back({});
            } else if (xml.compare(pos, 22, "</mc:AlternateContent>") == 0) {
                if (!alts.empty()) alts.pop_back();
            } else if (tag_at(xml, pos, "<mc:Choice")) {
                if (!alts.empty() && !self_closing(xml, pos, &te)) alts.back().choice_begin = found.size();
            } else if (xml.compare(pos, 12, "</mc:Choice>") == 0) {
                if (!alts.empty()) alts.back().choice_end = found.size();
            } else if (tag_at(xml, pos, "<mc:Fallback")) {
                if (self_closing(xml, pos, &te)) continue;
                const size_t fe = fallback_end(xml, te, e);
                std::vector<Found> fb;
                collect(xml, te, fe, fb);
                // The same paragraphs with the same text as the mc:Choice copy:
                // mirror its translation instead of translating it again.
                const size_t base = found.size();
                bool mirror = !alts.empty() && alts.back().choice_end != std::string::npos &&
                              alts.back().choice_begin <= alts.back().choice_end &&
                              fb.size() == alts.back().choice_end - alts.back().choice_begin && !fb.empty();
                for (size_t k = 0; mirror && k < fb.size(); ++k)
                    mirror = fb[k].text == found[alts.back().choice_begin + k].text;
                for (size_t k = 0; k < fb.size(); ++k) {
                    if (fb[k].twin >= 0) fb[k].twin += (int)base;
                    if (mirror) fb[k].twin = (int)(alts.back().choice_begin + k);
                    found.push_back(std::move(fb[k]));
                }
                if (fe >= e) break;
                pos = fe;
            }
        }
    }
}

}  // namespace

std::vector<DocxDocument::Paragraph> DocxDocument::scan_part(const std::string& xml, std::vector<std::string>& units) {
    std::vector<Found> found;
    collect(xml, 0, xml.size(), found);
    // Number the units in document order (a paragraph before the text box it
    // anchors, which closes first); mirrored copies share their original's unit.
    std::vector<size_t> order;
    for (size_t i = 0; i < found.size(); ++i)
        if (found[i].twin < 0) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return found[a].begin < found[b].begin; });
    for (size_t i : order) {
        units.push_back(found[i].text);
        found[i].para.unit = (int)units.size() - 1;
    }
    std::vector<Paragraph> out;
    for (size_t i = 0; i < found.size(); ++i) {
        size_t r = i;
        while (found[r].twin >= 0) r = (size_t)found[r].twin;  // twins point backwards, so this ends
        found[i].para.unit = found[r].para.unit;
    }
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.begin < b.begin; });
    for (auto& f : found) out.push_back(std::move(f.para));
    return out;
}

std::string DocxDocument::rewrite_part(const std::string& xml, const std::vector<Paragraph>& paras,
                                       const std::vector<std::string>& translations) {
    struct Edit {
        size_t begin, end;
        std::string text;
    };
    std::vector<Edit> edits;
    for (const auto& p : paras) {
        if (p.unit < 0 || p.unit >= (int)translations.size()) continue;
        const std::string tr = p.leading + translations[p.unit] + p.trailing;
        for (size_t k = 0; k < p.runs.size(); ++k) {
            const auto& r = p.runs[k];
            if (k != p.main_run) {
                edits.push_back({r.content_begin, r.content_end, ""});
                continue;
            }
            // The run of the first real text gets the whole translation; make sure spaces survive.
            std::string tag = xml.substr(r.tag_begin, r.tag_end - r.tag_begin);
            if (tag.find("xml:space") == std::string::npos) tag = "<w:t xml:space=\"preserve\">";
            // "\n" / "\t" in the translation: close the w:t, write the break or tab,
            // open a new w:t. The original elements are used when the translation
            // has the same breaks and tabs in the same order as the source.
            std::string kinds_tr, kinds_src;
            for (char c : tr)
                if (c == '\n' || c == '\t') kinds_tr += c;
            for (const auto& sx : p.seps) kinds_src += sx.compare(0, 6, "<w:tab") == 0 ? '\t' : '\n';
            const bool same = kinds_tr == kinds_src;
            std::string body = tag, seg;
            size_t n = 0;
            for (char c : tr) {
                if (c == '\r') continue;
                if (c != '\n' && c != '\t') {
                    seg += c;
                    continue;
                }
                body += xml_escape(seg) + "</w:t>" + (same ? p.seps[n] : c == '\n' ? "<w:br/>" : "<w:tab/>") + tag;
                seg.clear();
                ++n;
            }
            body += xml_escape(seg);
            edits.push_back({r.tag_begin, r.content_end, std::move(body)});
        }
        for (const auto& [b, e] : p.moved) edits.push_back({b, e, ""});
    }
    // One forward pass (replacing in place, edit by edit, was quadratic on big documents).
    std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.begin < b.begin; });
    std::string out;
    out.reserve(xml.size() + xml.size() / 8);
    size_t at = 0;
    for (const auto& e : edits) {
        if (e.begin < at) continue;  // overlapping edit: can't happen with well-formed XML
        out.append(xml, at, e.begin - at);
        out += e.text;
        at = e.end;
    }
    out.append(xml, at, std::string::npos);
    return out;
}

namespace {

bool translatable_part(const std::string& name) {
    if (name == "word/document.xml" || name == "word/footnotes.xml" || name == "word/endnotes.xml") return true;
    auto starts = [&](const char* p) { return name.compare(0, std::strlen(p), p) == 0; };
    return (starts("word/header") || starts("word/footer")) && name.size() > 4 &&
           name.compare(name.size() - 4, 4, ".xml") == 0;
}

}  // namespace

DocxDocument::DocxDocument(const std::string& path) {
    zip_bytes_ = pk::read_file(path_from_utf8(path));
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, zip_bytes_.data(), zip_bytes_.size(), 0))
        throw std::runtime_error(path + " is not a Word document (.docx); old .doc files must be saved as .docx first");
    bool has_document = false;
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; ++i) {
        char name[512];
        mz_zip_reader_get_filename(&zip, i, name, sizeof(name));
        if (!translatable_part(name)) continue;
        size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (!data) continue;
        Part part{name, std::string(static_cast<char*>(data), size), {}};
        mz_free(data);
        part.paras = scan_part(part.xml, units_);
        if (part.name == "word/document.xml") has_document = true;
        parts_.push_back(std::move(part));
    }
    mz_zip_reader_end(&zip);
    if (!has_document) throw std::runtime_error(path + " is not a Word document (no word/document.xml)");
}

void DocxDocument::save(const std::string& path, const std::vector<std::string>& translations) const {
    mz_zip_archive in{}, out{};
    if (!mz_zip_reader_init_mem(&in, zip_bytes_.data(), zip_bytes_.size(), 0))
        throw std::runtime_error("cannot reopen the Word document");
    if (!mz_zip_writer_init_heap(&out, 0, zip_bytes_.size() + 65536)) {
        mz_zip_reader_end(&in);
        throw std::runtime_error("cannot create the output document");
    }
    bool ok = true;
    const mz_uint n = mz_zip_reader_get_num_files(&in);
    for (mz_uint i = 0; i < n && ok; ++i) {
        char name[512];
        mz_zip_reader_get_filename(&in, i, name, sizeof(name));
        const Part* part = nullptr;
        for (const auto& p : parts_)
            if (p.name == name) part = &p;
        if (part) {
            const std::string xml = rewrite_part(part->xml, part->paras, translations);
            ok = mz_zip_writer_add_mem(&out, name, xml.data(), xml.size(), MZ_DEFAULT_COMPRESSION);
        } else {
            ok = mz_zip_writer_add_from_zip_reader(&out, &in, i);  // copy unchanged, still compressed
        }
    }
    void* buf = nullptr;
    size_t size = 0;
    if (ok) ok = mz_zip_writer_finalize_heap_archive(&out, &buf, &size);
    std::string bytes = ok ? std::string(static_cast<char*>(buf), size) : std::string();
    if (buf) mz_free(buf);  // the finalized buffer belongs to us
    mz_zip_writer_end(&out);
    mz_zip_reader_end(&in);
    if (!ok) throw std::runtime_error("could not build the translated Word document");

    pk::write_file_atomic(path_from_utf8(path), bytes);  // never leaves a half-written .docx
}

}  // namespace tl
