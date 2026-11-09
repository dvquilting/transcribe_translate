#include "pdf_files.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QLibrary>
#include <QLocale>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSet>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <type_traits>

namespace tt {

// ============================================================================ PDFium
namespace {

// The few PDFium functions used, looked up at run time: the program works
// without pdfium.dll (only reading PDFs needs it), and the DLL's C interface
// is the same for every compiler.
struct Pdfium {
    using Doc = void*;
    using Page = void*;
    using TextPage = void*;
    void (*InitLibrary)() = nullptr;
    Doc (*LoadMemDocument)(const void*, int, const char*) = nullptr;
    unsigned long (*GetLastError)() = nullptr;
    void (*CloseDocument)(Doc) = nullptr;
    int (*GetPageCount)(Doc) = nullptr;
    Page (*LoadPage)(Doc, int) = nullptr;
    void (*ClosePage)(Page) = nullptr;
    float (*GetPageWidthF)(Page) = nullptr;
    float (*GetPageHeightF)(Page) = nullptr;
    unsigned long (*GetMetaText)(Doc, const char*, void*, unsigned long) = nullptr;
    TextPage (*TextLoadPage)(Page) = nullptr;
    void (*TextClosePage)(TextPage) = nullptr;
    int (*TextCountChars)(TextPage) = nullptr;
    unsigned int (*TextGetUnicode)(TextPage, int) = nullptr;
    int (*TextIsGenerated)(TextPage, int) = nullptr;     // optional (experimental API)
    int (*TextHasUnicodeMapError)(TextPage, int) = nullptr;  // optional
    double (*TextGetFontSize)(TextPage, int) = nullptr;
    int (*TextGetFontWeight)(TextPage, int) = nullptr;  // optional
    unsigned long (*TextGetFontInfo)(TextPage, int, void*, unsigned long, int*) = nullptr;  // optional
    int (*TextGetCharBox)(TextPage, int, double*, double*, double*, double*) = nullptr;
    struct RectF {
        float left, top, right, bottom;
    };
    int (*TextGetLooseCharBox)(TextPage, int, RectF*) = nullptr;  // optional
    struct Matrix {
        float a, b, c, d, e, f;
    };
    int (*TextGetMatrix)(TextPage, int, Matrix*) = nullptr;  // optional

    QString problem;  // empty = loaded
};

QString pdfiumFileName() {
#if defined(Q_OS_WIN)
    return QStringLiteral("pdfium.dll");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("libpdfium.dylib");
#else
    return QStringLiteral("libpdfium.so");
#endif
}

const Pdfium& pdfium() {
    static Pdfium p = [] {
        Pdfium f;
        static QLibrary lib;
        // Next to the program first (dist\), then TT_PDFIUM (tests), then the system.
        QStringList candidates{QDir(QCoreApplication::applicationDirPath()).filePath(pdfiumFileName())};
        const QString env = qEnvironmentVariable("TT_PDFIUM");
        if (!env.isEmpty()) candidates.prepend(env);
        candidates << QStringLiteral("pdfium");
        QString why;
        bool loaded = false;
        for (const QString& c : candidates) {
            if (c.contains(QLatin1Char('/')) || c.contains(QLatin1Char('\\'))) {
                if (!QFileInfo::exists(c)) continue;
            }
            lib.setFileName(c);
            if (lib.load()) {
                loaded = true;
                break;
            }
            why = lib.errorString();
        }
        if (!loaded) {
            f.problem = QCoreApplication::translate("Pdf", "Reading PDFs needs %1 next to this program (%2).")
                            .arg(pdfiumFileName(), why.isEmpty() ? QStringLiteral("not found") : why);
            return f;
        }
        bool all = true;
        auto get = [&](auto& fn, const char* name, bool required = true) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(lib.resolve(name));
            if (!fn && required) all = false;
        };
        get(f.InitLibrary, "FPDF_InitLibrary");
        get(f.LoadMemDocument, "FPDF_LoadMemDocument");
        get(f.GetLastError, "FPDF_GetLastError");
        get(f.CloseDocument, "FPDF_CloseDocument");
        get(f.GetPageCount, "FPDF_GetPageCount");
        get(f.LoadPage, "FPDF_LoadPage");
        get(f.ClosePage, "FPDF_ClosePage");
        get(f.GetPageWidthF, "FPDF_GetPageWidthF");
        get(f.GetPageHeightF, "FPDF_GetPageHeightF");
        get(f.GetMetaText, "FPDF_GetMetaText", false);
        get(f.TextLoadPage, "FPDFText_LoadPage");
        get(f.TextClosePage, "FPDFText_ClosePage");
        get(f.TextCountChars, "FPDFText_CountChars");
        get(f.TextGetUnicode, "FPDFText_GetUnicode");
        get(f.TextIsGenerated, "FPDFText_IsGenerated", false);
        get(f.TextHasUnicodeMapError, "FPDFText_HasUnicodeMapError", false);
        get(f.TextGetFontSize, "FPDFText_GetFontSize");
        get(f.TextGetFontWeight, "FPDFText_GetFontWeight", false);
        get(f.TextGetFontInfo, "FPDFText_GetFontInfo", false);
        get(f.TextGetCharBox, "FPDFText_GetCharBox");
        get(f.TextGetLooseCharBox, "FPDFText_GetLooseCharBox", false);
        get(f.TextGetMatrix, "FPDFText_GetMatrix", false);
        if (!all) {
            f.problem = QCoreApplication::translate("Pdf", "%1 is too old or not PDFium.").arg(lib.fileName());
            return f;
        }
        f.InitLibrary();
        return f;
    }();
    return p;
}

// ---------------------------------------------------------------- text layout
struct Line {
    QString text;
    double l = 1e9, r = -1e9, b = 1e9, t = -1e9;  // PDF points, y up
    double size = 0;      // font size (median of its characters)
    double bold = 0;      // share of characters in a bold font
    bool softHyphen = false;
    int chars = 0;        // visible characters
    int page = 0;
    double pageH = 0;
};

bool isNoSpaceScript(QChar c) {
    const ushort u = c.unicode();
    return (u >= 0x2E80 && u <= 0x9FFF) ||  // CJK radicals, kana, ideographs
           (u >= 0xF900 && u <= 0xFAFF) || (u >= 0xFF00 && u <= 0xFFEF) ||  // compatibility, full width
           (u >= 0x0E00 && u <= 0x0EFF) ||  // Thai, Lao
           (u >= 0x1000 && u <= 0x109F) ||  // Myanmar
           (u >= 0x1780 && u <= 0x17FF) || c.isLowSurrogate() || c.isHighSurrogate();
}

bool endsSentence(const QString& s) {
    static const QRegularExpression re(QStringLiteral("[.!?:;。！？।۔؟]['\"”’)\\]]*$"));
    return re.match(s).hasMatch();
}

bool startsListItem(const QString& s) {
    static const QRegularExpression re(QStringLiteral(
        "^(?:[•◦▪▫‣⁃●○■□–—∙·*\\-]"
        "|\\(?\\d{1,3}[.)]|\\(?[a-zA-Z][.)]|\\(?[ivxIVX]{1,4}[.)])\\s"));
    return re.match(s).hasMatch();
}

bool startsLower(const QString& s) { return !s.isEmpty() && s.at(0).isLower(); }

// Page numbers and running headers/footers: "12", "- 12 -", "Page 3", "3 / 10", "xii".
bool looksLikePageNumber(const QString& s) {
    static const QRegularExpression re(QStringLiteral(
        "^(?:[-–—\\s]*\\d{1,4}[-–—\\s]*|(?:page|seite|p\\.|pg\\.?)?\\s*\\d{1,4}\\s*(?:/|of|von|de)\\s*\\d{1,4}"
        "|(?:page|seite|página|pagina|p\\.)\\s*\\d{1,4}|[ivxlcdm]{1,7})$"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(s.trimmed()).hasMatch();
}

double median(QList<double> v, double fallback) {
    if (v.isEmpty()) return fallback;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// Two lines of one paragraph joined: hyphenation undone, a space between
// words except in scripts written without spaces.
void joinInto(QString& text, const Line& prev, const QString& next) {
    if (text.isEmpty()) {
        text = next;
        return;
    }
    const QChar last = text.back();
    const bool hyphen = prev.softHyphen || last == QChar(0x00AD) ||
                        (last == QLatin1Char('-') && text.size() >= 2 && text.at(text.size() - 2).isLetter());
    if (hyphen && startsLower(next)) {
        text.chop(1);
        text += next;
        return;
    }
    if (last == QChar(0x00AD)) text.chop(1);
    if (isNoSpaceScript(text.back()) && !next.isEmpty() && isNoSpaceScript(next.front())) text += next;
    else text += QLatin1Char(' ') + next;
}

struct Para {
    QList<Line> lines;
    QString text;
    double size = 0, bold = 0;
    int page = 0;
    bool list = false;
};

// 0 = not a heading, 1 = by its larger size, 2 = a short bold line in the body size.
int headingType(const Para& p, double body) {
    const bool shortText = p.text.size() <= 160 && p.lines.size() <= 3;
    if (!shortText || p.list || looksLikePageNumber(p.text)) return 0;
    if (p.size >= body * 1.12) return 1;
    if (p.bold > 0.8 && p.text.size() <= 100 && p.lines.size() <= 2 && !endsSentence(p.text) &&
        !p.text.endsWith(QLatin1Char(',')))
        return 2;
    return 0;
}

}  // namespace

QString pdfiumProblem() { return pdfium().problem; }

PdfReadResult readPdf(const QString& path) {
    PdfReadResult res;
    const Pdfium& f = pdfium();
    if (!f.problem.isEmpty()) {
        res.error = f.problem;
        return res;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        res.error = file.errorString();
        return res;
    }
    const QByteArray data = file.readAll();  // PDFium reads from it while the document is open
    if (data.size() > std::numeric_limits<int>::max() - 1) {
        res.error = QCoreApplication::translate("Pdf", "The PDF is too large.");
        return res;
    }
    Pdfium::Doc doc = f.LoadMemDocument(data.constData(), (int)data.size(), nullptr);
    if (!doc) {
        const unsigned long e = f.GetLastError();
        res.error = e == 4 ? QCoreApplication::translate("Pdf", "The PDF is password-protected.")
                  : e == 5 ? QCoreApplication::translate("Pdf", "The PDF uses an unsupported encryption.")
                  : e == 3 ? QCoreApplication::translate("Pdf", "This is not a PDF file, or it is damaged.")
                           : QCoreApplication::translate("Pdf", "The PDF could not be opened (error %1).").arg(e);
        return res;
    }
    struct Closer {
        const Pdfium& f;
        Pdfium::Doc d;
        ~Closer() { f.CloseDocument(d); }
    } closer{f, doc};

    if (f.GetMetaText) {
        const unsigned long n = f.GetMetaText(doc, "Title", nullptr, 0);
        if (n > 2) {
            QByteArray buf((int)n, '\0');
            f.GetMetaText(doc, "Title", buf.data(), n);
            res.doc.title = QString::fromUtf16(reinterpret_cast<const char16_t*>(buf.constData()), (n - 2) / 2).trimmed();
        }
    }

    res.pages = f.GetPageCount(doc);
    QList<QList<Line>> pages;
    long long visible = 0, unreadable = 0;
    for (int pi = 0; pi < res.pages; ++pi) {
        QList<Line> lines;
        Pdfium::Page page = f.LoadPage(doc, pi);
        if (!page) {
            pages << lines;
            res.emptyPages << pi + 1;
            continue;
        }
        const double pw = f.GetPageWidthF(page), ph = f.GetPageHeightF(page);
        if (pi == 0) res.doc.pageSize = QSizeF(pw, ph);
        Pdfium::TextPage tp = f.TextLoadPage(page);
        int pageVisible = 0;
        if (tp) {
            const int n = f.TextCountChars(tp);
            Line cur;
            QList<double> sizes;
            int boldChars = 0;
            auto flush = [&] {
                cur.text = cur.text.trimmed();
                if (!cur.text.isEmpty() && cur.chars > 0) {
                    cur.size = median(sizes, 10);
                    cur.bold = (double)boldChars / cur.chars;
                    cur.page = pi;
                    cur.pageH = ph;
                    lines << cur;
                }
                cur = Line();
                sizes.clear();
                boldChars = 0;
            };
            // The last visible character's box: a character on another row starts a
            // new line even where PDFium ran two rows together (a column's last line
            // and the next column's first, or the two halves of a hyphenated word).
            double lastB = 0, lastT = 0;
            bool haveLast = false;
            QHash<QByteArray, bool> boldFont;  // by font name
            for (int i = 0; i < n; ++i) {
                unsigned int u = f.TextGetUnicode(tp, i);
                if (u == '\r') continue;
                if (u == '\n') {
                    flush();
                    haveLast = false;
                    continue;
                }
                const bool generated = f.TextIsGenerated && f.TextIsGenerated(tp, i) == 1;
                if (u == ' ' || u == '\t' || u == 0xA0 || (generated && u != 2)) {
                    if (!cur.text.isEmpty() && !cur.text.endsWith(QLatin1Char(' '))) cur.text += QLatin1Char(' ');
                    continue;
                }
                if (u == 0 || u == 0xFFFD || (f.TextHasUnicodeMapError && f.TextHasUnicodeMapError(tp, i) == 1)) {
                    ++unreadable;
                    continue;
                }
                if (u < 0x20 && u != 2) continue;
                double l = 0, r = 0, b = 0, t = 0;
                const bool box = f.TextGetCharBox(tp, i, &l, &r, &b, &t) && t > b;
                if (box && haveLast && u != 2) {
                    const double h = std::max(t - b, lastT - lastB);
                    if (std::abs((b + t) / 2 - (lastB + lastT) / 2) > 0.9 * h) flush();
                }
                if (u == 2) {  // PDFium's mark for a hyphen that breaks a word at the end of a line
                    cur.text += QLatin1Char('-');
                    cur.softHyphen = true;
                    continue;
                }
                // UTF-16 on Windows (surrogate pairs as two characters), UTF-32 elsewhere.
                QString ch;
                if (u > 0xFFFF) {
                    const char32_t c32 = u;
                    ch = QString::fromUcs4(&c32, 1);
                } else {
                    ch = QChar((char16_t)u);
                }
                // Kangxi radicals some PDFs map CJK characters to (U+2F00-U+2FD5) and the Latin
                // ligatures (fi, fl, ...): the ordinary characters.
                if ((u >= 0x2F00 && u <= 0x2FDF) || (u >= 0xFB00 && u <= 0xFB06))
                    ch = ch.normalized(QString::NormalizationForm_KC);
                cur.text += ch;
                cur.softHyphen = u == 0xAD;
                if (QChar::isHighSurrogate(u)) continue;  // its box comes with the low half
                if (box) {
                    cur.l = std::min(cur.l, l);
                    cur.r = std::max(cur.r, r);
                    cur.b = std::min(cur.b, b);
                    cur.t = std::max(cur.t, t);
                    lastB = b;
                    lastT = t;
                    haveLast = true;
                }
                // The size as drawn: the font size PDFium reports leaves out the scaling of
                // the text matrix (Qt's PDFs set 183 pt fonts, scaled down), so it is scaled
                // by the character's matrix; without that, the height of the font's line box.
                double size = f.TextGetFontSize(tp, i);
                Pdfium::Matrix m{};
                Pdfium::RectF lb{};
                if (f.TextGetMatrix && f.TextGetMatrix(tp, i, &m)) {
                    const double scale = std::sqrt(std::abs((double)m.a * m.d - (double)m.b * m.c));
                    if (scale > 1e-4) size *= scale;
                } else if (f.TextGetLooseCharBox && f.TextGetLooseCharBox(tp, i, &lb) && lb.top - lb.bottom > 0.5f) {
                    size = (lb.top - lb.bottom) / 1.15;
                }
                sizes << size;
                bool bold = f.TextGetFontWeight && f.TextGetFontWeight(tp, i) >= 600;
                if (!bold && f.TextGetFontInfo) {
                    char name[256];
                    int flags = 0;
                    const unsigned long len = f.TextGetFontInfo(tp, i, name, sizeof(name), &flags);
                    const QByteArray font = len > 0 && len <= sizeof(name) ? QByteArray(name) : QByteArray();
                    const QByteArray key = font + '/' + QByteArray::number(flags & 0x40000);
                    auto it = boldFont.constFind(key);
                    if (it == boldFont.cend()) {
                        // "Arial-BoldMT", "CMBX10", "Calibri Bold", "Roboto-Black" ...; flag 19 = ForceBold
                        static const QRegularExpression re(QStringLiteral(
                            "bold|black|heavy|semibold|demi|cmbx|[-,+]bd\\b"), QRegularExpression::CaseInsensitiveOption);
                        it = boldFont.insert(key, (flags & 0x40000) || re.match(QString::fromUtf8(font)).hasMatch());
                    }
                    bold = it.value();
                }
                if (bold) ++boldChars;
                ++cur.chars;
                ++pageVisible;
            }
            flush();
            f.TextClosePage(tp);
        }
        f.ClosePage(page);
        visible += pageVisible;
        if (pageVisible == 0) res.emptyPages << pi + 1;
        pages << lines;
    }

    if (visible == 0) {
        res.error = res.pages == 0
                        ? QCoreApplication::translate("Pdf", "The PDF has no pages.")
                        : QCoreApplication::translate("Pdf",
                              "The PDF has no text to translate: its pages are images (a scanned document?). "
                              "Text recognition (OCR) is not supported.");
        return res;
    }
    if (unreadable > visible) {
        res.error = QCoreApplication::translate("Pdf",
            "The text in this PDF can't be read: its fonts don't say which letters they draw (the PDF would "
            "need text recognition).");
        return res;
    }

    // ---- running headers, footers and page numbers --------------------------------
    {
        auto inMargin = [](const Line& l) { return l.t > l.pageH * 0.92 || l.b < l.pageH * 0.08; };
        auto key = [](const QString& s) {
            QString k = s.toLower();
            k.remove(QRegularExpression(QStringLiteral("[0-9\\s]")));
            return k;
        };
        QHash<QString, QSet<int>> seen;
        for (const auto& lines : pages)
            for (const Line& l : lines)
                if (inMargin(l) && !key(l.text).isEmpty()) seen[key(l.text)].insert(l.page);
        const int need = res.pages >= 4 ? 3 : 2;
        for (auto& lines : pages) {
            // A page number as the lowest or the highest line of the page, wherever the margin is.
            if (!lines.isEmpty()) {
                auto low = std::min_element(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.b < b.b; });
                if (looksLikePageNumber(low->text)) lines.erase(low);
            }
            if (!lines.isEmpty()) {
                auto high = std::max_element(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.t < b.t; });
                if (looksLikePageNumber(high->text)) lines.erase(high);
            }
            lines.erase(std::remove_if(lines.begin(), lines.end(),
                                       [&](const Line& l) {
                                           const bool nearEdge = l.t > l.pageH * 0.88 || l.b < l.pageH * 0.12;
                                           if (nearEdge && looksLikePageNumber(l.text)) return true;
                                           if (!inMargin(l)) return false;
                                           const QString k = key(l.text);
                                           return res.pages >= 2 && !k.isEmpty() && seen.value(k).size() >= need;
                                       }),
                        lines.end());
        }
    }

    // ---- the body text's size: the size most characters have ------------------------
    double body = 10;
    {
        QHash<int, long long> bySize;  // in half points
        for (const auto& lines : pages)
            for (const Line& l : lines) bySize[qRound(l.size * 2)] += l.chars;
        long long best = -1;
        for (auto it = bySize.cbegin(); it != bySize.cend(); ++it)
            if (it.value() > best) best = it.value(), body = it.key() / 2.0;
        if (body <= 0) body = 10;
    }

    // TT_PDF_DEBUG=1: the lines as read, for tuning the paragraph rules.
    if (qEnvironmentVariableIsSet("TT_PDF_DEBUG"))
        for (const auto& lines : pages)
            for (const Line& l : lines)
                std::fprintf(stderr, "p%d l=%.1f r=%.1f b=%.1f t=%.1f size=%.1f bold=%.2f | %s\n", l.page + 1, l.l, l.r, l.b,
                             l.t, l.size, l.bold, qPrintable(l.text));

    // ---- lines into paragraphs, page by page ---------------------------------------
    QList<QList<Para>> pageParas;
    for (const auto& lines : pages) {
        QList<Para> paras;
        if (lines.isEmpty()) {
            pageParas << paras;
            continue;
        }
        // The usual space between two lines of a paragraph on this page.
        QList<double> gaps;
        for (int i = 1; i < lines.size(); ++i) {
            const Line &a = lines[i - 1], &b = lines[i];
            const double gap = a.b - b.t;
            if (gap >= -1 && gap < 2 * body && std::abs(a.size - body) < 0.6 && std::abs(b.size - body) < 0.6)
                gaps << gap;
        }
        const double g0 = median(gaps, 0.3 * body);
        // The right edge of a line's column: the widest line starting near it.
        auto columnRight = [&](const Line& a) {
            double r = a.r;
            for (const Line& o : lines)
                if (std::abs(o.l - a.l) < 4 * body && std::abs(o.size - a.size) < 1) r = std::max(r, o.r);
            return r;
        };
        Para cur;
        auto finish = [&] {
            if (cur.lines.isEmpty()) return;
            QList<double> sz;
            double boldSum = 0;
            int chars = 0;
            for (const Line& l : cur.lines) {
                for (int k = 0; k < std::max(1, l.chars / 8); ++k) sz << l.size;
                boldSum += l.bold * l.chars;
                chars += l.chars;
            }
            cur.size = median(sz, body);
            cur.bold = chars ? boldSum / chars : 0;
            cur.page = cur.lines.front().page;
            paras << cur;
            cur = Para();
        };
        for (int i = 0; i < lines.size(); ++i) {
            const Line& b = lines[i];
            bool split = cur.lines.isEmpty();
            if (!split) {
                const Line& a = cur.lines.back();
                const double size = std::max(a.size, b.size);
                const double gap = a.b - b.t;
                const bool sameRow = std::abs(a.b - b.b) < 0.3 * size;  // pieces of one row (a table cell)
                if (b.t > a.t + 0.5 * size && !sameRow)  // went up: the next column, or a box
                    split = endsSentence(a.text) || !startsLower(b.text) || std::abs(a.size - b.size) > 0.5 ||
                            looksLikePageNumber(a.text);
                else if (gap > g0 + std::max(0.4 * size, 2.0)) split = true;     // extra space before it
                else if (std::abs(a.size - b.size) > 0.12 * size) split = true;  // other font size
                else if ((a.bold > 0.7) != (b.bold > 0.7) && (a.bold < 0.3 || b.bold < 0.3)) split = true;
                else if (startsListItem(b.text)) split = true;                   // a list item
                else if (endsSentence(a.text) && a.r < columnRight(a) - 3 * body) split = true;  // short last line
                else if (!cur.list) {
                    // The next paragraph's first line indented from this one's left edge.
                    double left = a.l;
                    for (const Line& l : cur.lines) left = std::min(left, l.l);
                    if (b.l > left + 0.8 * body && std::abs(a.l - left) < 0.5 * body && b.l < a.r) split = true;
                }
                if (!split && sameRow && b.l > a.r + 2 * size) split = true;     // a separate cell
            }
            if (split) {
                finish();
                cur.list = startsListItem(b.text);
            }
            if (!cur.lines.isEmpty()) joinInto(cur.text, cur.lines.back(), b.text);
            else cur.text = b.text;
            cur.lines << b;
        }
        finish();
        pageParas << paras;
    }

    // ---- the document: headings, paragraphs, page breaks ------------------------
    QList<PdfBlock>& blocks = res.doc.blocks;
    Para* lastBody = nullptr;
    QList<Para> all;
    for (const auto& ps : pageParas) all << ps;
    // Heading levels by size: the largest heading size is level 1, the next 2, the
    // rest (and bold lines in the body size) 3.
    QList<int> headingSizes;  // half points, largest first
    for (const Para& p : all)
        if (headingType(p, body) == 1 && !headingSizes.contains(qRound(p.size * 2))) headingSizes << qRound(p.size * 2);
    std::sort(headingSizes.begin(), headingSizes.end(), std::greater<int>());
    auto headingKind = [&](const Para& p, double) {
        const int type = headingType(p, body);
        if (type == 0) return PdfBlock::Paragraph;
        const int rank = type == 1 ? (int)headingSizes.indexOf(qRound(p.size * 2)) : 2;
        return rank <= 0 ? PdfBlock::Heading1 : rank == 1 ? PdfBlock::Heading2 : PdfBlock::Heading3;
    };
    bool havePageContent = false;
    int lastPage = -1;
    for (int i = 0; i < all.size(); ++i) {
        Para& p = all[i];
        const PdfBlock::Kind kind = headingKind(p, body);
        if (p.page != lastPage && lastPage >= 0 && havePageContent) {
            // A paragraph that runs on over the page break stays one paragraph.
            if (kind == PdfBlock::Paragraph && lastBody && !blocks.isEmpty() && blocks.back().kind == PdfBlock::Paragraph &&
                !endsSentence(blocks.back().text) && startsLower(p.text)) {
                joinInto(blocks.back().text, lastBody->lines.back(), p.text);
                lastBody = &p;
                lastPage = p.page;
                continue;
            }
            blocks << PdfBlock{PdfBlock::PageBreak, QString()};
        }
        blocks << PdfBlock{kind, p.text};
        lastBody = kind == PdfBlock::Paragraph ? &p : nullptr;
        lastPage = p.page;
        havePageContent = true;
    }
    res.ok = true;
    return res;
}

// ============================================================================ writing
namespace {

// Families for scripts the default font may lack, in the language's own style
// (Han characters are drawn differently in Japanese, Chinese and Korean).
QStringList familiesFor(const QString& language) {
    const QString l = language.toLower();
    if (l.startsWith(QLatin1String("ja")))
        return {QStringLiteral("Yu Gothic"), QStringLiteral("Meiryo"), QStringLiteral("MS Gothic"),
                QStringLiteral("Noto Sans CJK JP"), QStringLiteral("Noto Sans JP")};
    if (l == QLatin1String("zh-tw") || l == QLatin1String("zh-hant") || l == QLatin1String("zh-hk"))
        return {QStringLiteral("Microsoft JhengHei"), QStringLiteral("PMingLiU"), QStringLiteral("Noto Sans CJK TC"),
                QStringLiteral("Noto Sans TC")};
    if (l.startsWith(QLatin1String("zh")))
        return {QStringLiteral("Microsoft YaHei"), QStringLiteral("SimSun"), QStringLiteral("Noto Sans CJK SC"),
                QStringLiteral("Noto Sans SC")};
    if (l.startsWith(QLatin1String("ko")))
        return {QStringLiteral("Malgun Gothic"), QStringLiteral("Noto Sans CJK KR"), QStringLiteral("Noto Sans KR")};
    return {};
}

void setPage(QPdfWriter& pdf, QSizeF size) {
    if (size.width() < 72 || size.height() < 72) {
        // Letter in the US and Canada, A4 elsewhere.
        const QLocale::Country c = QLocale().territory();
        const bool letter = c == QLocale::UnitedStates || c == QLocale::Canada;
        pdf.setPageLayout(QPageLayout(QPageSize(letter ? QPageSize::Letter : QPageSize::A4), QPageLayout::Portrait,
                                      QMarginsF(5, 5, 5, 5), QPageLayout::Millimeter));
        return;
    }
    const bool landscape = size.width() > size.height();
    if (landscape) size.transpose();
    // The nearest standard size when it is one (A4 rather than 595.3 x 841.9 points).
    const QPageSize ps(QPageSize::id(size, QPageSize::Point, QPageSize::FuzzyMatch) != QPageSize::Custom
                           ? QPageSize(QPageSize::id(size, QPageSize::Point, QPageSize::FuzzyMatch))
                           : QPageSize(size, QPageSize::Point));
    // QTextDocument::print adds 2 cm margins (and the page numbers) inside these 5 mm.
    pdf.setPageLayout(QPageLayout(ps, landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
                                  QMarginsF(5, 5, 5, 5), QPageLayout::Millimeter));
}

}  // namespace

bool writePdf(const PdfDocument& d, const QString& path, QString* error) {
    // Without fonts Qt writes a PDF without its text (no error): say so instead.
    if (QFontDatabase::families().isEmpty()) {
        if (error) *error = QCoreApplication::translate("Pdf", "no fonts were found to write the PDF with");
        return false;
    }
    QTextDocument doc;
    QFont font = doc.defaultFont();
    font.setPointSizeF(11);
    QStringList families{font.family()};
    families << familiesFor(d.language);
    font.setFamilies(families);
    doc.setDefaultFont(font);

    if (!d.markdown.isEmpty()) {
        doc.setMarkdown(d.markdown);
    } else {
        QTextCursor c(&doc);
        bool first = true, breakNext = false;
        auto addBlock = [&](const QString& text, int level, bool italic, const QColor& colour = QColor()) {
            QTextBlockFormat bf;
            QTextCharFormat cf;
            cf.setFont(font);
            if (level > 0) {
                bf.setHeadingLevel(level);
                bf.setTopMargin(level == 1 ? 14 : 10);
                bf.setBottomMargin(6);
                cf.setFontWeight(QFont::Bold);
                cf.setFontPointSize(level == 1 ? 18 : level == 2 ? 14.5 : 12.5);
            } else {
                bf.setBottomMargin(7);
            }
            if (italic) cf.setFontItalic(true);
            if (colour.isValid()) cf.setForeground(colour);
            if (breakNext) bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
            breakNext = false;
            if (first) {
                c.setBlockFormat(bf);
                c.setBlockCharFormat(cf);
                first = false;
            } else {
                c.insertBlock(bf, cf);
            }
            // Line breaks inside a paragraph stay line breaks (QChar::LineSeparator).
            QString t = text;
            t.replace(QLatin1Char('\n'), QChar::LineSeparator);
            c.insertText(t, cf);
        };
        if (d.titleHeading && !d.title.isEmpty()) addBlock(d.title, 1, false);
        if (!d.note.isEmpty()) addBlock(d.note, 0, true, QColor(0x59, 0x59, 0x59));
        for (const PdfBlock& b : d.blocks) {
            switch (b.kind) {
            case PdfBlock::PageBreak:
                if (!first) breakNext = true;
                break;
            case PdfBlock::Heading1: addBlock(b.text, 1, false); break;
            case PdfBlock::Heading2: addBlock(b.text, 2, false); break;
            case PdfBlock::Heading3: addBlock(b.text, 3, false); break;
            case PdfBlock::Paragraph: addBlock(b.text, 0, false); break;
            }
        }
    }

    QSaveFile out(path);  // never a half-written PDF
    if (!out.open(QIODevice::WriteOnly)) {
        if (error) *error = out.errorString();
        return false;
    }
    {
        QPdfWriter pdf(&out);
        pdf.setTitle(d.title);
        pdf.setCreator(QStringLiteral("Transcribe & Translate"));
        setPage(pdf, d.pageSize);
        doc.print(&pdf);
    }
    if (!out.commit()) {
        if (error) *error = out.errorString();
        return false;
    }
    return true;
}

PdfDocument pdfFromText(const QString& textIn) {
    PdfDocument d;
    const QString text = QString(textIn).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    for (const QString& p : text.split(QRegularExpression(QStringLiteral("\n\\s*\n"))))
        if (!p.trimmed().isEmpty()) d.blocks << PdfBlock{PdfBlock::Paragraph, p.trimmed()};
    return d;
}

bool writeTextPdf(const QString& textPath, const QString& pdfPath, const QString& title, QString* error,
                  const QString& language) {
    QFile in(textPath);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error) *error = in.errorString();
        return false;
    }
    PdfDocument d = pdfFromText(QString::fromUtf8(in.readAll()));
    d.title = title;
    d.titleHeading = true;
    d.language = language;
    if (!d.blocks.isEmpty() && d.blocks.first().text.startsWith(QLatin1Char('[')) &&
        d.blocks.first().text.endsWith(QLatin1Char(']')))
        d.note = d.blocks.takeFirst().text;
    return writePdf(d, pdfPath, error);
}

// ============================================================================ Markdown
namespace {
const QString kPageBreak = QStringLiteral("<!-- page -->");
const QString kPageSize = QStringLiteral("<!-- tt-pdf page %1x%2 -->");
}  // namespace

QString pdfToMarkdown(const PdfDocument& d) {
    static const QRegularExpression structural(QStringLiteral("^(?:[#>|<+=`~*_\\-]|\\d+[.)]|\\s)"));
    QStringList out;
    if (d.pageSize.isValid())
        out << kPageSize.arg(d.pageSize.width(), 0, 'f', 1).arg(d.pageSize.height(), 0, 'f', 1);
    for (const PdfBlock& b : d.blocks) {
        QString t = b.text;
        t.replace(QLatin1Char('\n'), QLatin1Char(' '));
        switch (b.kind) {
        case PdfBlock::PageBreak: out << kPageBreak; break;
        case PdfBlock::Heading1: out << QStringLiteral("# ") + t; break;
        case PdfBlock::Heading2: out << QStringLiteral("## ") + t; break;
        case PdfBlock::Heading3: out << QStringLiteral("### ") + t; break;
        case PdfBlock::Paragraph:
            // Text that would read as Markdown structure (a list, a quote, a table, a
            // code block ...) is escaped, so the engine translates it as a paragraph.
            out << (structural.match(t).hasMatch() ? QStringLiteral("\\") + t : t);
            break;
        }
    }
    return out.join(QStringLiteral("\n\n")) + QLatin1Char('\n');
}

PdfDocument pdfFromMarkdown(const QString& mdIn) {
    PdfDocument d;
    static const QRegularExpression sizeRe(QStringLiteral("^<!-- tt-pdf page ([0-9.]+)x([0-9.]+) -->$"));
    static const QRegularExpression headingRe(QStringLiteral("^(#{1,6})\\s+(.*?)(?:\\s+#+)?\\s*$"));
    const QString md = QString(mdIn).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    bool breakNext = false;
    for (const QString& raw : md.split(QRegularExpression(QStringLiteral("\n\\s*\n")))) {
        const QString block = raw.trimmed();
        if (block.isEmpty()) continue;
        if (block == kPageBreak) {
            breakNext = true;
            continue;
        }
        if (const auto m = sizeRe.match(block); m.hasMatch()) {
            d.pageSize = QSizeF(m.captured(1).toDouble(), m.captured(2).toDouble());
            continue;
        }
        PdfBlock b;
        if (const auto m = headingRe.match(block); m.hasMatch() && !block.contains(QLatin1Char('\n'))) {
            const int level = m.captured(1).size();
            b.kind = level == 1 ? PdfBlock::Heading1 : level == 2 ? PdfBlock::Heading2 : PdfBlock::Heading3;
            b.text = m.captured(2);
        } else {
            b.text = block;
            if (b.text.startsWith(QLatin1Char('\\'))) b.text.remove(0, 1);
            // Lines the model broke: one paragraph again.
            QString joined;
            for (const QString& l : b.text.split(QLatin1Char('\n'))) {
                const QString t = l.trimmed();
                if (t.isEmpty()) continue;
                if (joined.isEmpty()) joined = t;
                else if (isNoSpaceScript(joined.back()) && isNoSpaceScript(t.front())) joined += t;
                else joined += QLatin1Char(' ') + t;
            }
            b.text = joined;
        }
        if (breakNext && !d.blocks.isEmpty()) d.blocks << PdfBlock{PdfBlock::PageBreak, QString()};
        breakNext = false;
        d.blocks << b;
    }
    return d;
}

}  // namespace tt
