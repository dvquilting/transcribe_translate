// test_gui.cpp - the window of Transcribe & Translate, offscreen, with the
// stand-in engine (fake_engine.cpp, built as tt_engine next to this test):
// the tabs, the Help page, a transcription handed on to Translate, a file
// translation, a speech translation, one job at a time, and the History.
#include <cstdio>
#include <functional>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>

#include "history_page.h"
#include "jobgate.h"
#include "pdf_files.h"
#include "mainwindow.h"
#include "speech_page.h"
#include "transcribe_page.h"
#include "translate_page.h"
#include "translate_engine.h"

static int failures = 0;
#define CHECK(x)                                                              \
    do {                                                                      \
        if (!(x)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

namespace {

bool waitFor(const std::function<bool()>& done, int ms = 15000) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) QApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

QPushButton* button(QWidget* in, const QString& text) {
    for (QPushButton* b : in->findChildren<QPushButton*>())
        if (b->text() == text) return b;
    std::fprintf(stderr, "no button '%s'\n", qPrintable(text));
    return nullptr;
}

void click(QWidget* in, const QString& text) {
    if (QPushButton* b = button(in, text)) b->click();
}

QString readText(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QStringList fakeRuns(const QString& log) {
    QStringList runs = readText(log).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    return runs;
}

}  // namespace

int main(int argc, char** argv) {
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_DATA_HOME", home.filePath(QStringLiteral("data")).toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    const bool pdfium = tt::pdfiumProblem().isEmpty();  // reading PDFs (TT_PDFIUM)
    const int jobs = pdfium ? 13 : 12;                     // in the History at the end
    QApplication::setOrganizationName(QStringLiteral("DavidV"));
    QApplication::setApplicationName(QStringLiteral("TranscribeTranslateTest"));
    QApplication::setApplicationDisplayName(QStringLiteral("Transcribe & Translate"));

    const QString log = home.filePath(QStringLiteral("engine.log"));
    qputenv("TT_FAKE_LOG", log.toUtf8());
    CHECK(QFileInfo::exists(tt::enginePath()));

    // Settings before the window reads them: translate into German.
    {
        QSettings s(QStringLiteral("DavidV"), QStringLiteral("TranscribeTranslate"));
        s.clear();
        s.setValue(QStringLiteral("Translate/to"), QStringLiteral("de"));
        s.setValue(QStringLiteral("SpeechTranslate/to"), QStringLiteral("de"));
        s.setValue(QStringLiteral("SpeechTranslate/from"), QStringLiteral("en"));
        s.setValue(QStringLiteral("SpeechTranslate/savePdf"), true);
    }

    const QString historyFile = home.filePath(QStringLiteral("history.json"));
    QTemporaryDir work;
    const QString media = work.filePath(QStringLiteral("talk.wav"));
    {
        QFile f(media);
        f.open(QIODevice::WriteOnly);
        f.write("RIFF fake audio");
    }

    {
        MainWindow w(historyFile);
        w.show();
        QTabWidget* tabs = w.tabs();
        CHECK(tabs->count() == 4);
        CHECK(tabs->tabText(0) == QStringLiteral("Transcribe") && tabs->tabText(3) == QStringLiteral("History"));
        CHECK(w.historyPage()->jobs().isEmpty());

        // ---- Help: one page, a part for each tab ----
        const QString help = MainWindow::helpHtml();
        for (const char* anchor : {"name=\"transcribe\"", "name=\"translate\"", "name=\"speech\""})
            CHECK(help.contains(QLatin1String(anchor)));
        CHECK(help.contains(QStringLiteral("Languages of each model")));
        CHECK(help.contains(QStringLiteral("Languages of the voices")));
        CHECK(help.count(QStringLiteral("<h2>Languages of each model</h2>")) == 2);  // Transcribe's and Translate's only

        // ---- Transcribe, then "Translate..." ----
        tabs->setCurrentIndex(0);
        TranscribePage* tp = w.transcribePage();
        tp->setInputFile(media);
        QPushButton* translateBtn = button(tp, QStringLiteral("Translate..."));
        CHECK(translateBtn && !translateBtn->isEnabled());
        click(tp, QStringLiteral("Transcribe"));
        CHECK(tt::JobGate::instance().busy() || !tp->isRunning());
        CHECK(waitFor([&] { return !tp->isRunning(); }));
        CHECK(!tt::JobGate::instance().busy());
        const QString txt = work.filePath(QStringLiteral("talk.txt"));
        CHECK(QFileInfo::exists(txt));
        QStringList runs = fakeRuns(log);
        CHECK(runs.size() == 1 && runs.value(0).startsWith(QStringLiteral("transcribe\t") + QDir::toNativeSeparators(media) + QLatin1Char('\t')));
        CHECK(w.historyPage()->jobs().size() == 1);
        if (!w.historyPage()->jobs().isEmpty()) {
            const tt::JobRecord& r = w.historyPage()->jobs().first();
            CHECK(r.kind == QStringLiteral("Transcribe") && r.ok && QFileInfo(r.input) == QFileInfo(media));
            CHECK(r.outputs.size() == 1 && QFileInfo(r.outputs.value(0)) == QFileInfo(txt));
        }
        CHECK(translateBtn && translateBtn->isEnabled());
        if (translateBtn) translateBtn->click();
        CHECK(tabs->currentIndex() == 1);

        // ---- Translate the transcript (File tab) ----
        TranslatePage* lp = w.translatePage();
        bool inputSet = false;
        for (QLineEdit* e : lp->findChildren<QLineEdit*>())
            if (QFileInfo(e->text()) == QFileInfo(txt)) inputSet = true;
        CHECK(inputSet);
        click(lp, QStringLiteral("Translate file"));
        CHECK(waitFor([&] { return !lp->isRunning(); }));
        const QString de = work.filePath(QStringLiteral("talk.de.txt"));
        CHECK(readText(de).startsWith(QStringLiteral("[de] Good morning")));
        runs = fakeRuns(log);
        CHECK(runs.size() == 2 && runs.value(1).startsWith(QStringLiteral("translate\t")));
        CHECK(runs.value(1).contains(QStringLiteral("\t-t\tde\t")));
        // TranslateGemma 12B (the default) is sped up by TranslateGemma 4B unless switched off.
        CHECK(runs.value(1).contains(QStringLiteral("\t--draft\tauto")));
        {
            // The faster mode's choices: auto, the two helpers, off.
            QStringList values;
            for (QComboBox* c : lp->findChildren<QComboBox*>())
                if (c->findData(QStringLiteral("gemma-3-1b")) >= 0)
                    for (int i = 0; i < c->count(); ++i) values << c->itemData(i).toString();
            CHECK(values == (QStringList{QStringLiteral("auto"), QStringLiteral("translategemma-4b"),
                                         QStringLiteral("gemma-3-1b"), QStringLiteral("off")}));
        }
        {
            // What the faster mode gave, next to Compute.
            bool shown = false;
            for (QLabel* l : lp->findChildren<QLabel*>())
                if (l->text().contains(QStringLiteral("Compute:")) && l->text().contains(QStringLiteral("1.50x"))) shown = true;
            CHECK(shown);
        }
        CHECK(w.historyPage()->jobs().size() == 2 && w.historyPage()->jobs().value(0).kind == QStringLiteral("Translate"));
        // The engine stays running with its model, for the next translation.
        CHECK(tt::TranslateEngine::instance().loadedModel() == QStringLiteral("Fake model"));
        QPushButton* freeGpu = button(&w, QStringLiteral("Free GPU memory"));
        CHECK(freeGpu && freeGpu->isEnabled());

        // ---- one job at a time ----
        qputenv("TT_FAKE_SLEEP_MS", "1500");
        tabs->setCurrentIndex(0);
        const QString media2 = work.filePath(QStringLiteral("second.wav"));
        QFile::copy(media, media2);
        tp->setInputFile(media2);
        click(tp, QStringLiteral("Transcribe"));
        CHECK(tp->isRunning() && tt::JobGate::instance().busy());
        CHECK(tt::JobGate::instance().owner() == QStringLiteral("Transcribe"));
        // Another tab's job frees the Translate tab's model first.
        CHECK(tt::TranslateEngine::instance().loadedModel().isEmpty());
        CHECK(freeGpu && !freeGpu->isEnabled());
        QPushButton* models = button(&w, QStringLiteral("Model files"));
        CHECK(models && !models->isEnabled());
        // The Speech translation tab is told to wait: a message, no second engine.
        w.speechPage()->setInputFile(media);
        bool asked = false;
        QTimer::singleShot(300, [&] {
            if (QWidget* m = QApplication::activeModalWidget()) {
                asked = true;
                m->close();
            }
        });
        click(w.speechPage(), QStringLiteral("Translate"));
        CHECK(asked);
        CHECK(!w.speechPage()->isRunning());
        CHECK(waitFor([&] { return !tp->isRunning(); }));
        CHECK(models && models->isEnabled());
        CHECK(fakeRuns(log).size() == 3);
        qunsetenv("TT_FAKE_SLEEP_MS");

        // ---- Speech translation: transcribe + translate -> NAME.de.srt (+ NAME.srt) ----
        tabs->setCurrentIndex(2);
        SpeechPage* sp = w.speechPage();
        click(sp, QStringLiteral("Translate"));
        CHECK(waitFor([&] { return !sp->isRunning(); }));
        runs = fakeRuns(log);
        CHECK(runs.size() == 5);
        CHECK(runs.value(3).startsWith(QStringLiteral("transcribe\t")) && runs.value(4).startsWith(QStringLiteral("translate\t")));
        CHECK(runs.value(4).contains(QStringLiteral("\t--draft\tauto")));
        const QString deSrt = work.filePath(QStringLiteral("talk.de.srt"));
        CHECK(readText(deSrt).contains(QStringLiteral("[de] Good morning, everyone.")));
        CHECK(readText(deSrt).contains(QStringLiteral("00:00:01,000 --> 00:00:03,000")));
        CHECK(QFileInfo::exists(work.filePath(QStringLiteral("talk.srt"))));
        CHECK(w.historyPage()->jobs().size() == 4);
        if (w.historyPage()->jobs().size() == 4) {
            const tt::JobRecord& r = w.historyPage()->jobs().first();
            CHECK(r.kind == QStringLiteral("Speech translation") && r.ok);
            CHECK(r.outputs.contains(QDir::toNativeSeparators(deSrt)));
            // The translated text as a PDF too (written here, from the subtitles).
            CHECK(r.outputs.contains(QDir::toNativeSeparators(work.filePath(QStringLiteral("talk.de.pdf")))));
        }
        CHECK(QFileInfo(work.filePath(QStringLiteral("talk.de.pdf"))).size() > 500);

        // ---- a PDF: read here, translated as Markdown, written as a PDF ----
        if (!pdfium) std::fprintf(stderr, "note: %s - PDF translation not tested\n", qPrintable(tt::pdfiumProblem()));
        if (pdfium) {
            const QString srcPdf = work.filePath(QStringLiteral("report.pdf"));
            tt::PdfDocument d;
            d.blocks << tt::PdfBlock{tt::PdfBlock::Heading1, QStringLiteral("Quarterly report")}
                     << tt::PdfBlock{tt::PdfBlock::Paragraph, QStringLiteral("The river carries the boats to the sea.")}
                     << tt::PdfBlock{tt::PdfBlock::PageBreak, QString()}
                     << tt::PdfBlock{tt::PdfBlock::Paragraph, QStringLiteral("A sentence on the second page.")};
            CHECK(tt::writePdf(d, srcPdf, nullptr));
            tabs->setCurrentIndex(1);
            lp->openFile(srcPdf);
            click(lp, QStringLiteral("Translate file"));
            CHECK(waitFor([&] { return !lp->isRunning(); }));
            const QString dePdf = work.filePath(QStringLiteral("report.de.pdf"));
            const tt::PdfReadResult r = tt::readPdf(dePdf);
            CHECK(r.ok && r.pages == 2);
            QString text;
            for (const auto& b : r.doc.blocks) text += b.text + QLatin1Char('\n');
            CHECK(text.contains(QStringLiteral("[de] The river carries the boats to the sea.")));
            CHECK(text.contains(QStringLiteral("[de] A sentence on the second page.")));
            runs = fakeRuns(log);
            CHECK(runs.last().startsWith(QStringLiteral("translate\t")) && runs.last().contains(QStringLiteral("source.md\t")));
            CHECK(!w.historyPage()->jobs().isEmpty());
            if (!w.historyPage()->jobs().isEmpty()) {
                const tt::JobRecord& j = w.historyPage()->jobs().first();
                CHECK(j.kind == QStringLiteral("Translate") && j.ok && QFileInfo(j.input) == QFileInfo(srcPdf));
                CHECK(j.outputs.size() == 1 && QFileInfo(j.outputs.value(0)) == QFileInfo(dePdf));
            }
        }

        // ---- the model kept loaded: two translations, one engine; then Free GPU memory ----
        {
            tabs->setCurrentIndex(1);
            const int serversBefore = (int)fakeRuns(log + QStringLiteral(".serve")).size();
            QLineEdit* outEdit = nullptr;
            for (QLineEdit* e : lp->findChildren<QLineEdit*>())
                if (e->placeholderText().startsWith(QStringLiteral("(default: next to the input"))) outEdit = e;
            CHECK(outEdit);
            for (int i = 0; i < 2 && outEdit; ++i) {
                lp->openFile(txt);
                outEdit->setText(work.filePath(QStringLiteral("again%1.de.txt").arg(i)));
                click(lp, QStringLiteral("Translate file"));
                CHECK(waitFor([&] { return !lp->isRunning(); }));
                CHECK(readText(work.filePath(QStringLiteral("again%1.de.txt").arg(i))).startsWith(QStringLiteral("[de] Good morning")));
            }
            // At most one engine started for the two (none if the PDF's was still running).
            CHECK((int)fakeRuns(log + QStringLiteral(".serve")).size() - serversBefore <= 1);
            CHECK(tt::TranslateEngine::instance().loadedModel() == QStringLiteral("Fake model"));
            CHECK(freeGpu && freeGpu->isEnabled());
            if (freeGpu) freeGpu->click();
            CHECK(tt::TranslateEngine::instance().loadedModel().isEmpty());
            CHECK(freeGpu && !freeGpu->isEnabled());

            // Back on the tab: the model is loaded ahead, in the background, without a job.
            const int preloads = (int)fakeRuns(log + QStringLiteral(".preload")).size();
            const int jobsBefore = (int)fakeRuns(log).size();
            tabs->setCurrentIndex(3);
            QApplication::processEvents();
            tabs->setCurrentIndex(1);
            CHECK(waitFor([&] { return !tt::TranslateEngine::instance().loadedModel().isEmpty(); }, 8000));
            CHECK((int)fakeRuns(log + QStringLiteral(".preload")).size() == preloads + 1);
            CHECK((int)fakeRuns(log).size() == jobsBefore);  // no translation
            CHECK(!lp->isRunning() && !tt::JobGate::instance().busy());
            CHECK(freeGpu && freeGpu->isEnabled());
            // The next translation uses it (the same engine: no second one started).
            const int servers = (int)fakeRuns(log + QStringLiteral(".serve")).size();
            lp->openFile(txt);
            if (outEdit) outEdit->setText(work.filePath(QStringLiteral("again2.de.txt")));
            click(lp, QStringLiteral("Translate file"));
            CHECK(waitFor([&] { return !lp->isRunning(); }));
            CHECK(readText(work.filePath(QStringLiteral("again2.de.txt"))).startsWith(QStringLiteral("[de] Good morning")));
            CHECK((int)fakeRuns(log + QStringLiteral(".serve")).size() == servers);

            // A slow load: a setting changed while it loads is loaded next ...
            auto& engine = tt::TranslateEngine::instance();
            QComboBox* device = nullptr;
            for (QComboBox* c : lp->findChildren<QComboBox*>())
                if (c->findText(QStringLiteral("vulkan")) >= 0) device = c;
            CHECK(device);
            qputenv("TT_FAKE_PRELOAD_MS", "1000");
            if (freeGpu) freeGpu->click();
            const int p0 = (int)fakeRuns(log + QStringLiteral(".preload")).size();
            tabs->setCurrentIndex(3);
            QApplication::processEvents();
            tabs->setCurrentIndex(1);
            CHECK(waitFor([&] { return engine.isPreloading(); }, 5000));
            if (freeGpu) CHECK(freeGpu->toolTip().contains(QStringLiteral("loading its model in the background")));
            if (device) device->setCurrentText(QStringLiteral("cpu"));
            CHECK(waitFor([&] { return (int)fakeRuns(log + QStringLiteral(".preload")).size() == p0 + 2 &&
                                       !engine.isPreloading(); }, 10000));
            CHECK(fakeRuns(log + QStringLiteral(".preload")).last().contains(QStringLiteral("--device\tcpu")));
            // ... and a translation started during a load waits for it, and says so.
            if (freeGpu) freeGpu->click();
            tabs->setCurrentIndex(3);
            QApplication::processEvents();
            tabs->setCurrentIndex(1);
            CHECK(waitFor([&] { return engine.isPreloading(); }, 5000));
            lp->openFile(txt);
            if (outEdit) outEdit->setText(work.filePath(QStringLiteral("again3.de.txt")));
            click(lp, QStringLiteral("Translate file"));
            bool waitShown = false;
            for (QLabel* l : lp->findChildren<QLabel*>())
                waitShown = waitShown || l->text().startsWith(QStringLiteral("Waiting for the model to finish loading"));
            CHECK(waitShown);
            CHECK(waitFor([&] { return !lp->isRunning(); }, 10000));
            CHECK(readText(work.filePath(QStringLiteral("again3.de.txt"))).startsWith(QStringLiteral("[de] Good morning")));
            qunsetenv("TT_FAKE_PRELOAD_MS");
            if (device) device->setCurrentText(QStringLiteral("auto"));
            CHECK(waitFor([&] { return !engine.isPreloading(); }, 10000));
        }

        // ---- Transcribe with several speakers: three runs, and the Speakers window ----
        {
            tabs->setCurrentIndex(0);
            QCheckBox* several = nullptr;
            for (QCheckBox* c : tp->findChildren<QCheckBox*>())
                if (c->text().startsWith(QStringLiteral("Several speakers"))) several = c;
            CHECK(several);
            if (several) several->setChecked(true);
            // What to do in the Speakers window each time it opens.
            std::function<void(QDialog*)> inWindow;
            int windows = 0;
            QTimer watch;
            watch.setInterval(50);
            QObject::connect(&watch, &QTimer::timeout, [&] {
                auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (!d || d->windowTitle() != QStringLiteral("Speakers") || d->property("seen").toBool()) return;
                d->setProperty("seen", true);
                ++windows;
                CHECK(tp->isRunning() && tt::JobGate::instance().busy());  // the run is waiting for the answer
                if (inWindow) inWindow(d);
            });
            watch.start();
            auto meeting = [&](const char* name) {
                const QString m = work.filePath(QLatin1String(name) + QStringLiteral(".wav"));
                QFile::copy(media, m);
                tp->setInputFile(m);
                return work.filePath(QLatin1String(name) + QStringLiteral(".txt"));
            };
            auto statusText = [&] {
                for (QLabel* l : tp->findChildren<QLabel*>())
                    if (l->text().startsWith(QStringLiteral("Done")) || l->text().startsWith(QStringLiteral("Cancelled")))
                        return l->text();
                return QString();
            };

            // Named, and written with the names.
            QString out = meeting("meeting");
            inWindow = [](QDialog* d) {
                if (auto* name = d->findChild<QLineEdit*>(QStringLiteral("name0"))) name->setText(QStringLiteral("Anna"));
                click(d, QStringLiteral("Continue"));
            };
            int before = (int)fakeRuns(log).size();
            click(tp, QStringLiteral("Transcribe"));
            // Finding the speakers shows how far it is (and not the late "49%").
            bool half = false, back = false;
            CHECK(waitFor([&] {
                for (QLabel* l : tp->findChildren<QLabel*>()) {
                    half = half || l->text() == QStringLiteral("Finding who speaks when: 50%");
                    back = back || l->text() == QStringLiteral("Finding who speaks when: 49%");
                }
                return !tp->isRunning();
            }));
            CHECK(half && !back);
            CHECK(windows == 1 && !tt::JobGate::instance().busy());
            CHECK(readText(out) == QStringLiteral("Anna: Good morning, everyone.\n\nSpeaker 2: Today we talk about the budget.\n"));
            runs = fakeRuns(log);
            CHECK(runs.size() == before + 3);
            CHECK(runs.value(before).startsWith(QStringLiteral("transcribe\t")) &&
                  runs.value(before).contains(QStringLiteral("\t-f\tsrt\t")) && runs.value(before).contains(QStringLiteral("--words")));
            CHECK(runs.value(before + 1).startsWith(QStringLiteral("speakers\t")));
            CHECK(runs.value(before + 2).contains(QStringLiteral("--from-srt")) &&
                  runs.value(before + 2).contains(QStringLiteral("--speakers-file")) &&
                  runs.value(before + 2).contains(QStringLiteral("\t-f\ttxt\t")));
            CHECK(!w.historyPage()->jobs().isEmpty() && w.historyPage()->jobs().first().ok &&
                  w.historyPage()->jobs().first().outputs.size() == 1);  // only the transcript, not the work files

            // "Detect again" with a number: another detection, another look.
            out = meeting("again");
            inWindow = [&](QDialog* d) {
                if (windows == 2) {
                    if (auto* count = d->findChild<QComboBox*>()) {
                        for (QComboBox* c : d->findChildren<QComboBox*>())
                            if (c->findData(8) >= 0) count = c;
                        count->setCurrentIndex(count->findData(1));
                    }
                    click(d, QStringLiteral("Detect again"));
                } else {
                    click(d, QStringLiteral("Continue"));
                }
            };
            before = (int)fakeRuns(log).size();
            click(tp, QStringLiteral("Transcribe"));
            CHECK(waitFor([&] { return !tp->isRunning(); }));
            CHECK(windows == 3);
            runs = fakeRuns(log);
            CHECK(runs.size() == before + 4 && runs.value(before + 2).contains(QStringLiteral("--num-speakers\t1")));
            CHECK(readText(out) == QStringLiteral("Good morning, everyone. Today we talk about the budget.\n"));  // one speaker

            // Cancel in the window: nothing is written.
            out = meeting("cancelled");
            inWindow = [](QDialog* d) { click(d, QStringLiteral("Cancel")); };
            click(tp, QStringLiteral("Transcribe"));
            CHECK(waitFor([&] { return !tp->isRunning(); }));
            CHECK(windows == 4 && !QFileInfo::exists(out) && !tt::JobGate::instance().busy());
            CHECK(statusText().startsWith(QStringLiteral("Cancelled")));
            CHECK(!w.historyPage()->jobs().isEmpty() && !w.historyPage()->jobs().first().ok);

            // The speakers can't be found: the transcript is still written, without names.
            out = meeting("nospeakers");
            qputenv("TT_FAKE_SPEAKERS_FAIL", "1");
            click(tp, QStringLiteral("Transcribe"));
            CHECK(waitFor([&] { return !tp->isRunning(); }));
            qunsetenv("TT_FAKE_SPEAKERS_FAIL");
            CHECK(windows == 4);
            CHECK(readText(out) == QStringLiteral("Good morning, everyone. Today we talk about the budget.\n"));
            CHECK(statusText().contains(QStringLiteral("without speaker names")));
            CHECK(!fakeRuns(log).last().contains(QStringLiteral("--speakers-file")));
            watch.stop();
            if (several) several->setChecked(false);
        }

        // Pictures of the tabs, for a look at the layout: TT_SCREENSHOTS=<folder>.
        if (const QByteArray dir = qgetenv("TT_SCREENSHOTS"); !dir.isEmpty())
            for (int i = 0; i < tabs->count(); ++i) {
                tabs->setCurrentIndex(i);
                QApplication::processEvents();
                w.grab().save(QDir(QString::fromLocal8Bit(dir)).filePath(QStringLiteral("tab%1.png").arg(i)));
            }

        // ---- History: "Use again" goes back to the job's tab with its input ----
        tabs->setCurrentIndex(3);
        emit w.historyPage()->reuseInput(QStringLiteral("Transcribe"), media2);
        CHECK(tabs->currentIndex() == 0);

        // ---- a document from the command line goes to Translate ----
        w.openFile(de);
        CHECK(tabs->currentIndex() == 1);
        w.openFile(media);
        CHECK(tabs->currentIndex() == 0);
        w.openFile(media, QStringLiteral("speech"));  // "--speech FILE" (Explorer's menu)
        CHECK(tabs->currentIndex() == 2);

        w.close();
    }

    // ---- the History is kept ----
    {
        HistoryPage h(historyFile);
        CHECK(h.jobs().size() == jobs);
        CHECK(h.jobs().value(jobs - 1).kind == QStringLiteral("Transcribe"));
        CHECK(h.jobs().value(0).finished.isValid());
        for (int i = 0; i < HistoryPage::kMaxJobs + 3; ++i) h.add({QStringLiteral("Translate"), QString(), {}, QStringLiteral("x"), true, QDateTime::currentDateTime()});
        CHECK(h.jobs().size() == HistoryPage::kMaxJobs);
    }
    // ---- the tabs' settings are kept apart ----
    {
        QSettings s(QStringLiteral("DavidV"), QStringLiteral("TranscribeTranslate"));
        CHECK(s.value(QStringLiteral("Translate/to")).toString() == QStringLiteral("de"));
        CHECK(s.contains(QStringLiteral("Transcribe/model")));
        CHECK(s.contains(QStringLiteral("SpeechTranslate/asr")));
        CHECK(s.contains(QStringLiteral("tab")));
    }

    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    else std::printf("window tests passed\n");
    return failures ? 1 : 0;
}
