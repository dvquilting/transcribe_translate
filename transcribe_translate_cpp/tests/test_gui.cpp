// test_gui.cpp - the window of Transcribe & Translate, offscreen, with the
// stand-in engine (fake_engine.cpp, built as tt_engine next to this test):
// the tabs, the Help page, a transcription handed on to Translate, a file
// translation, a speech translation, one job at a time, and the History.
#include <cstdio>
#include <functional>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>

#include "history_page.h"
#include "jobgate.h"
#include "mainwindow.h"
#include "speech_page.h"
#include "transcribe_page.h"
#include "translate_page.h"

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
        CHECK(w.historyPage()->jobs().size() == 2 && w.historyPage()->jobs().value(0).kind == QStringLiteral("Translate"));

        // ---- one job at a time ----
        qputenv("TT_FAKE_SLEEP_MS", "1500");
        tabs->setCurrentIndex(0);
        const QString media2 = work.filePath(QStringLiteral("second.wav"));
        QFile::copy(media, media2);
        tp->setInputFile(media2);
        click(tp, QStringLiteral("Transcribe"));
        CHECK(tp->isRunning() && tt::JobGate::instance().busy());
        CHECK(tt::JobGate::instance().owner() == QStringLiteral("Transcribe"));
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
        const QString deSrt = work.filePath(QStringLiteral("talk.de.srt"));
        CHECK(readText(deSrt).contains(QStringLiteral("[de] Good morning, everyone.")));
        CHECK(readText(deSrt).contains(QStringLiteral("00:00:01,000 --> 00:00:03,000")));
        CHECK(QFileInfo::exists(work.filePath(QStringLiteral("talk.srt"))));
        CHECK(w.historyPage()->jobs().size() == 4);
        if (w.historyPage()->jobs().size() == 4) {
            const tt::JobRecord& r = w.historyPage()->jobs().first();
            CHECK(r.kind == QStringLiteral("Speech translation") && r.ok);
            CHECK(r.outputs.contains(QDir::toNativeSeparators(deSrt)));
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
        CHECK(h.jobs().size() == 4);
        CHECK(h.jobs().value(3).kind == QStringLiteral("Transcribe"));
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
