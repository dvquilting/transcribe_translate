// test_pdf.cpp - PDFs (src/gui/pdf_files.cpp): the Markdown the engine
// translates in place of a PDF, writing PDFs, and reading them back with
// PDFium (TT_PDFIUM; those checks are skipped, with a note, without it).
#include <cstdio>

#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>

#include "pdf_files.h"

static int failures = 0;
#define CHECK(x)                                                              \
    do {                                                                      \
        if (!(x)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

using tt::PdfBlock;

namespace {

QString allText(const tt::PdfDocument& d) {
    QString s;
    for (const PdfBlock& b : d.blocks) s += b.text + QLatin1Char('\n');
    return s;
}

int count(const tt::PdfDocument& d, PdfBlock::Kind k) {
    int n = 0;
    for (const PdfBlock& b : d.blocks) n += b.kind == k;
    return n;
}

}  // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;

    // ---- Markdown: structure and escapes survive, page breaks and size too ----
    {
        tt::PdfDocument d;
        d.pageSize = QSizeF(612, 792);
        d.blocks << PdfBlock{PdfBlock::Heading1, QStringLiteral("Title")}
                 << PdfBlock{PdfBlock::Paragraph, QStringLiteral("- not a list, a dash")}
                 << PdfBlock{PdfBlock::Paragraph, QStringLiteral("1. not a numbered list")}
                 << PdfBlock{PdfBlock::PageBreak, QString()}
                 << PdfBlock{PdfBlock::Heading2, QStringLiteral("Second page")}
                 << PdfBlock{PdfBlock::Paragraph, QStringLiteral("# not a heading")};
        const QString md = tt::pdfToMarkdown(d);
        CHECK(md.contains(QStringLiteral("\n\\- not a list")));
        CHECK(md.contains(QStringLiteral("\n\\1. not")));
        CHECK(md.contains(QStringLiteral("\n\\# not a heading")));
        CHECK(md.contains(QStringLiteral("<!-- page -->")));
        const tt::PdfDocument back = tt::pdfFromMarkdown(md);
        CHECK(back.pageSize == QSizeF(612, 792));
        CHECK(back.blocks.size() == d.blocks.size());
        for (int i = 0; i < std::min(back.blocks.size(), d.blocks.size()); ++i)
            CHECK(back.blocks[i].kind == d.blocks[i].kind && back.blocks[i].text == d.blocks[i].text);
        // A model that broke a paragraph into lines: one paragraph again.
        const tt::PdfDocument broken = tt::pdfFromMarkdown(QStringLiteral("# T\n\nfirst half\nsecond half\n"));
        CHECK(broken.blocks.size() == 2 && broken.blocks[1].text == QStringLiteral("first half second half"));
        CHECK(tt::pdfFromText(QStringLiteral("a\nb\n\n\nc")).blocks.size() == 2);
    }

    // ---- writing, and reading back --------------------------------------------
    const bool pdfium = tt::pdfiumProblem().isEmpty();
    if (!pdfium) std::fprintf(stderr, "note: %s - reading PDFs not tested\n", qPrintable(tt::pdfiumProblem()));
    {
        tt::PdfDocument d;
        d.title = QStringLiteral("Report");
        d.pageSize = QSizeF(595.3, 841.9);
        d.blocks << PdfBlock{PdfBlock::Heading1, QStringLiteral("Annual report")};
        for (int i = 0; i < 3; ++i)
            d.blocks << PdfBlock{PdfBlock::Paragraph,
                                 QStringLiteral("Paragraph %1. The river carries every small boat towards the open sea, "
                                                "where the light changes slowly and nothing stays the same for long, "
                                                "because the weather and the tide decide together.").arg(i + 1)};
        d.blocks << PdfBlock{PdfBlock::Heading2, QStringLiteral("Details")}
                 << PdfBlock{PdfBlock::Paragraph, QStringLiteral("Short paragraph under the heading.")}
                 << PdfBlock{PdfBlock::PageBreak, QString()}
                 << PdfBlock{PdfBlock::Paragraph, QStringLiteral("Text on the second page.")}
                 << PdfBlock{PdfBlock::Paragraph, QStringLiteral("日本語の段落です。川は時間を運びます。")};
        d.language = QStringLiteral("en");
        const QString path = dir.filePath(QStringLiteral("report.pdf"));
        QString error;
        CHECK(tt::writePdf(d, path, &error));
        if (!error.isEmpty()) std::fprintf(stderr, "writePdf: %s\n", qPrintable(error));
        CHECK(QFile(path).size() > 1000);
        if (pdfium) {
            const tt::PdfReadResult r = tt::readPdf(path);
            CHECK(r.ok);
            CHECK(r.pages == 2);
            CHECK(r.doc.title == QStringLiteral("Report"));
            CHECK(r.emptyPages.isEmpty());
            CHECK(std::abs(r.doc.pageSize.width() - 595.3) < 1.5);
            const QString text = allText(r.doc);
            CHECK(text.contains(QStringLiteral("Paragraph 2. The river carries every small boat")));
            CHECK(text.contains(QStringLiteral("because the weather and the tide decide together.")));
            CHECK(text.contains(QStringLiteral("日本語の段落です")));
            CHECK(!text.contains(QStringLiteral("\n1\n")) && !text.contains(QStringLiteral("\n2\n")));  // page numbers
            CHECK(count(r.doc, PdfBlock::PageBreak) == 1);
            CHECK(count(r.doc, PdfBlock::Paragraph) == 6);
            CHECK(!r.doc.blocks.isEmpty() && r.doc.blocks.first().kind == PdfBlock::Heading1 &&
                  r.doc.blocks.first().text == QStringLiteral("Annual report"));
            CHECK(count(r.doc, PdfBlock::Heading2) == 1);
            // Through the engine's Markdown and back to a PDF.
            const tt::PdfDocument again = tt::pdfFromMarkdown(tt::pdfToMarkdown(r.doc));
            CHECK(allText(again) == text);
        }
    }

    // ---- a scanned PDF (only a picture): no text to translate ------------------
    {
        const QString path = dir.filePath(QStringLiteral("scan.pdf"));
        {
            QPdfWriter pdf(path);
            QPainter p(&pdf);
            QImage img(400, 300, QImage::Format_RGB32);
            img.fill(Qt::white);
            QPainter ip(&img);
            ip.drawText(20, 40, QStringLiteral("Only pixels here"));
            ip.end();
            p.drawImage(QRect(0, 0, 4000, 3000), img);
        }
        if (pdfium) {
            const tt::PdfReadResult r = tt::readPdf(path);
            CHECK(!r.ok && r.error.contains(QStringLiteral("scanned")));
        }
    }

    // ---- not a PDF -------------------------------------------------------------------
    if (pdfium) {
        const QString path = dir.filePath(QStringLiteral("fake.pdf"));
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("hello, this is not a PDF");
        f.close();
        const tt::PdfReadResult r = tt::readPdf(path);
        CHECK(!r.ok && !r.error.isEmpty());
    }

    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    else std::printf("PDF tests passed\n");
    return failures ? 1 : 0;
}
