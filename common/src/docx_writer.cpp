#include "docx_writer.h"

#include <cstdint>
#include <vector>

namespace pk {

namespace {

// ---- a minimal zip writer (stored entries) -------------------------------------

uint32_t crc32(const std::string& data) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : data) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void put16(std::string& s, uint32_t v) {
    s += (char)(v & 0xFF);
    s += (char)((v >> 8) & 0xFF);
}
void put32(std::string& s, uint32_t v) {
    put16(s, v & 0xFFFF);
    put16(s, v >> 16);
}

std::string zip(const std::vector<std::pair<std::string, std::string>>& files) {
    std::string out, central;
    const uint32_t dos_time = 0, dos_date = (1 << 5) | 1;  // 1980-01-01: no clock needed
    for (const auto& [name, data] : files) {
        const uint32_t offset = (uint32_t)out.size();
        const uint32_t crc = crc32(data), size = (uint32_t)data.size();
        // local file header
        put32(out, 0x04034b50);
        put16(out, 20);  // version needed
        put16(out, 0x0800);  // UTF-8 names
        put16(out, 0);   // stored
        put16(out, dos_time);
        put16(out, dos_date);
        put32(out, crc);
        put32(out, size);
        put32(out, size);
        put16(out, (uint32_t)name.size());
        put16(out, 0);
        out += name;
        out += data;
        // central directory entry
        put32(central, 0x02014b50);
        put16(central, 20);  // version made by
        put16(central, 20);
        put16(central, 0x0800);
        put16(central, 0);
        put16(central, dos_time);
        put16(central, dos_date);
        put32(central, crc);
        put32(central, size);
        put32(central, size);
        put16(central, (uint32_t)name.size());
        put16(central, 0);  // extra
        put16(central, 0);  // comment
        put16(central, 0);  // disk
        put16(central, 0);  // internal attributes
        put32(central, 0);  // external attributes
        put32(central, offset);
        central += name;
    }
    const uint32_t central_offset = (uint32_t)out.size();
    out += central;
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, (uint32_t)files.size());
    put16(out, (uint32_t)files.size());
    put32(out, (uint32_t)central.size());
    put32(out, central_offset);
    put16(out, 0);
    return out;
}

// ---- WordprocessingML -----------------------------------------------------------

// XML text: escapes, and drops the control characters XML can't hold.
std::string xml(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else if (c < 0x20 && c != '\t' && c != '\n') continue;
        else out += (char)c;
    }
    return out;
}

// One paragraph; '\n' inside becomes a line break. Its first `bold` bytes are bold.
std::string paragraph(const std::string& text, const char* style, size_t bold = 0) {
    std::string p = "<w:p>";
    if (style) p += std::string("<w:pPr><w:pStyle w:val=\"") + style + "\"/></w:pPr>";
    size_t start = 0;
    while (true) {
        const size_t nl = text.find('\n', start);
        const std::string whole = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        const size_t b = start == 0 && bold <= whole.size() ? bold : 0;
        if (b > 0)
            p += "<w:r><w:rPr><w:b/><w:bCs/></w:rPr><w:t xml:space=\"preserve\">" + xml(std::string(whole, 0, b)) +
                 "</w:t></w:r>";
        const std::string line(whole, b);
        p += "<w:r><w:t xml:space=\"preserve\">" + xml(line) + "</w:t></w:r>";
        if (nl == std::string::npos) break;
        p += "<w:r><w:br/></w:r>";
        start = nl + 1;
    }
    return p + "</w:p>";
}

const char* kContentTypes =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
    "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
    "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
    "<Override PartName=\"/word/document.xml\" "
    "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
    "<Override PartName=\"/word/styles.xml\" "
    "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
    "</Types>";

const char* kRels =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" "
    "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
    "Target=\"word/document.xml\"/></Relationships>";

const char* kDocumentRels =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" "
    "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
    "Target=\"styles.xml\"/></Relationships>";

std::string styles(const std::string& language) {
    const std::string lang = language.empty() ? "" : "<w:lang w:val=\"" + xml(language) + "\"/>";
    return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
           "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
           "<w:docDefaults><w:rPrDefault><w:rPr>"
           "<w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\" w:eastAsia=\"Calibri\" w:cs=\"Calibri\"/>"
           "<w:sz w:val=\"22\"/><w:szCs w:val=\"22\"/>" +
           lang +
           "</w:rPr></w:rPrDefault>"
           "<w:pPrDefault><w:pPr><w:spacing w:after=\"160\" w:line=\"276\" w:lineRule=\"auto\"/></w:pPr>"
           "</w:pPrDefault></w:docDefaults>"
           "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/>"
           "<w:qFormat/></w:style>"
           "<w:style w:type=\"paragraph\" w:styleId=\"Title\"><w:name w:val=\"Title\"/>"
           "<w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:qFormat/>"
           "<w:pPr><w:spacing w:after=\"240\"/></w:pPr>"
           "<w:rPr><w:sz w:val=\"40\"/><w:szCs w:val=\"40\"/></w:rPr></w:style>"
           "<w:style w:type=\"paragraph\" w:styleId=\"Subtitle\"><w:name w:val=\"Subtitle\"/>"
           "<w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:qFormat/>"
           "<w:rPr><w:i/><w:color w:val=\"595959\"/></w:rPr></w:style>"
           "</w:styles>";
}

}  // namespace

std::vector<std::string> text_paragraphs(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        // trim spaces and line breaks at both ends
        size_t b = cur.find_first_not_of(" \t\r\n"), e = cur.find_last_not_of(" \t\r\n");
        if (b != std::string::npos) out.push_back(cur.substr(b, e - b + 1));
        cur.clear();
    };
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find_first_not_of(" \t") == std::string::npos) flush();
        else cur += (cur.empty() ? "" : "\n") + line;
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    flush();
    return out;
}

std::string make_docx(const DocxText& doc) {
    std::string body;
    if (!doc.title.empty()) body += paragraph(doc.title, "Title");
    if (!doc.note.empty()) body += paragraph(doc.note, "Subtitle");
    for (const auto& p : text_paragraphs(doc.body)) {
        size_t bold = 0;  // the longest name it starts with ("Anna B:" before "Anna:")
        for (const auto& s : doc.bold_starts)
            if (s.size() > bold && p.compare(0, s.size(), s) == 0) bold = s.size();
        body += paragraph(p, nullptr, bold);
    }
    const std::string document =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>" +
        body +
        // No page size: Word uses the reader's default (Letter or A4); 1 inch margins.
        "<w:sectPr><w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\" "
        "w:header=\"720\" w:footer=\"720\" w:gutter=\"0\"/></w:sectPr></w:body></w:document>";
    return zip({{"[Content_Types].xml", kContentTypes},
                {"_rels/.rels", kRels},
                {"word/document.xml", document},
                {"word/_rels/document.xml.rels", kDocumentRels},
                {"word/styles.xml", styles(doc.language)}});
}

}  // namespace pk
