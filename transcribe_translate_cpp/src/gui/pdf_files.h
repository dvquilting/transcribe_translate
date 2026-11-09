// pdf_files.h - PDFs in and out, for the window's tabs. The engine reads and
// writes text, Markdown, subtitles and Word; PDFs are made and read here:
//   - written with Qt (QPdfWriter): the system's fonts are embedded, and Qt
//     shapes every script (Arabic, Hindi, CJK, ...) and lays out right-to-left
//     text;
//   - read with PDFium (pdfium.dll / libpdfium.so next to the program, loaded
//     when first needed): the text of each page with its position and font
//     size, rebuilt into headings and paragraphs in reading order. The original's
//     columns, images and exact positions are not kept ("reflowed").
#pragma once

#include <QList>
#include <QSizeF>
#include <QString>

namespace tt {

struct PdfBlock {
    enum Kind { Heading1, Heading2, Heading3, Paragraph, PageBreak };
    Kind kind = Paragraph;
    QString text;  // one paragraph ('\n' = a line break in it); empty for a page break
};

struct PdfDocument {
    QString title;     // the PDF's title (metadata), and a first heading when `titleHeading`
    bool titleHeading = false;
    QString note;      // a line in italics under the title (empty: none)
    QSizeF pageSize;   // in points; empty = A4, or Letter in the US and Canada
    QString language;  // of the text ("de", "ja", ...): picks fonts for CJK; empty = any
    QList<PdfBlock> blocks;
    QString markdown;  // if set, rendered as Markdown instead of `blocks` (a translated .md)
};

// ---- reading ------------------------------------------------------------------
struct PdfReadResult {
    bool ok = false;
    QString error;            // why not, for the user (scanned, password, no PDFium, ...)
    PdfDocument doc;
    int pages = 0;
    QList<int> emptyPages;    // 1-based pages without text (images only)
};
PdfReadResult readPdf(const QString& path);

// "" when PDFium can be loaded, otherwise what is missing.
QString pdfiumProblem();

// ---- writing ------------------------------------------------------------------
// Atomically (a failure leaves no half-written file). Paragraphs flow over the
// pages; page breaks are kept.
bool writePdf(const PdfDocument& doc, const QString& path, QString* error);

// The text of a transcript result as a PDF: the title, an AI note in italics
// when the text starts with one ("[Summary written by ...]"), the paragraphs
// (blank lines separate them).
bool writeTextPdf(const QString& textPath, const QString& pdfPath, const QString& title, QString* error,
                  const QString& language = QString());

// ---- the Markdown the engine translates in place of a PDF ---------------------
// Headings as "#", one paragraph per line, page breaks and the page size as
// HTML comments (left alone by the engine), paragraphs that would read as
// Markdown structure escaped with a backslash.
QString pdfToMarkdown(const PdfDocument& doc);
// Back from that Markdown, translated. Also reads plain paragraphs.
PdfDocument pdfFromMarkdown(const QString& markdown);
// Plain text: paragraphs separated by blank lines.
PdfDocument pdfFromText(const QString& text);

}  // namespace tt
