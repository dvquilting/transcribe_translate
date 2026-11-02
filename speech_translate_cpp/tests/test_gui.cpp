#include <QtWidgets>
#include <QProcess>
#include <QSettings>
#include <QStringDecoder>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
#include <functional>
#define private public
#include "../src/mainwindow.h"
#undef private
#include "../src/mainwindow.cpp"
#include "../src/help.h"

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)
[[maybe_unused]] static void writeFile(const QString& path, const QByteArray& data) {
    QFile f(path); CHECK(f.open(QIODevice::WriteOnly)); CHECK(f.write(data) == data.size()); f.close();
}
[[maybe_unused]] static QByteArray readFile(const QString& path) { QFile f(path); CHECK(f.open(QIODevice::ReadOnly)); return f.readAll(); }

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir fixture;
    CHECK(fixture.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, fixture.path());
    QDir::setCurrent(fixture.path());
    MainWindow window;
    
    // pk::ChildProcess (UTF-8 output, process-tree cancellation) is tested in ../common/tests.

    // The model menus come from transcribe_multi's own table (../common/src/model_catalog.cpp).
    CHECK(window.asrCombo_->count() == 5 && window.asrCombo_->itemData(0).toString() == QStringLiteral("parakeet-ultra"));
    CHECK(window.ttsCombo_->itemText(0) == QString::fromUtf8("Qwen3-TTS 1.7B  \xE2\x80\x94  2.0 GB"));
    for (int i = 0; i < window.asrCombo_->count(); ++i)
        CHECK(pk::find_asr_model(window.asrCombo_->itemData(i).toString().toStdString()));
    window.fromCombo_->setCurrentIndex(window.fromCombo_->findData(QStringLiteral("auto")));
    window.asrCombo_->setCurrentIndex(window.asrCombo_->findData(QStringLiteral("cohere")));
    CHECK(window.fromCombo_->currentData().toString() == QStringLiteral("en"));  // Cohere needs the language
    CHECK(!window.asrDesc_->text().isEmpty());

    window.inputEdit_->setText(QStringLiteral("first.wav"));
    window.consentCheck_->setChecked(true);
    window.inputEdit_->setText(QStringLiteral("second.wav"));
    CHECK(!window.consentCheck_->isChecked());
    window.consentCheck_->setChecked(true);
    window.inputEdit_->setText(QDir::toNativeSeparators(QFileInfo(QStringLiteral("second.wav")).absoluteFilePath()));
    CHECK(window.consentCheck_->isChecked());
    const QString output = fixture.filePath(QStringLiteral("existing.srt"));
    writeFile(output, "original");
    CHECK(writeAll(output, "complete"));
    CHECK(readFile(output) == "complete");
#ifdef Q_OS_WIN
    // A locked destination forces commit to fail after staging the new content.
    HANDLE locked = CreateFileW(reinterpret_cast<LPCWSTR>(output.utf16()), GENERIC_READ,
                                 FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(locked != INVALID_HANDLE_VALUE);
    CHECK(!writeAll(output, "must not replace the existing subtitle"));
    CHECK(readFile(output) == "complete");
    if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
#endif
    const QString dubbed = fixture.filePath(QStringLiteral("existing.wav"));
    writeFile(dubbed, "existing dub");
    window.outputs_.dubbed = dubbed;
    CHECK(window.rememberDubFiles());
    const QString work = window.dubWorkDir_->path();
    writeFile(window.dubWorkDir_->filePath(QStringLiteral("rendered.wav")), "unfinished");
    window.removePartialDub();
    CHECK(!QFileInfo::exists(work) && readFile(dubbed) == "existing dub");
    window.tempDir_ = new QTemporaryDir;
    const QByteArray srt = "1\n00:00:00,000 --> 00:00:01,000\nOriginal failed unit.\n\n";
    writeFile(window.tempDir_->filePath(QStringLiteral("source.srt")), srt);
    writeFile(window.tempDir_->filePath(QStringLiteral("translated.srt")), srt);
    window.outputs_.translatedSrt = output;
    window.cfg_.to = QStringLiteral("de"); window.cfg_.dub = true;
    window.step_ = MainWindow::Step::Translate;
    window.clock_.start();
    window.stepFinished(3, false);
    CHECK(window.step_ == MainWindow::Step::Idle && !window.runner_.isActive());
    CHECK(readFile(output) == srt && readFile(dubbed) == "existing dub");
    CHECK(window.statusLabel_->text().contains(QStringLiteral("Dubbing was skipped")));

    // The dub's work folder goes when the run fails or is cancelled, whatever the
    // step; a failed dub still shows the saved subtitles as done in the progress bar.
    window.setRunning(true);
    window.step_ = MainWindow::Step::Dub;
    CHECK(window.rememberDubFiles());
    QString work2 = window.dubWorkDir_->path();
    window.fail(QStringLiteral("test"));
    CHECK(!QFileInfo::exists(work2) && !window.dubWorkDir_ && readFile(dubbed) == "existing dub");
    CHECK(window.openBtn_->isEnabled() && window.progress_->value() == 1);
    window.setRunning(true);
    window.step_ = MainWindow::Step::Transcribe;
    CHECK(window.rememberDubFiles());
    work2 = window.dubWorkDir_->path();
    window.stepFinished(0, true);
    CHECK(!QFileInfo::exists(work2) && !window.dubWorkDir_ && window.step_ == MainWindow::Step::Idle);
    CHECK(!window.openBtn_->isEnabled() && window.progress_->value() == 0);

    // Several speakers: the arguments, the review window (names, merges, detect
    // again, cancel) and the subtitles saved with a dash per speaker.
    {
        delete window.tempDir_;
        window.tempDir_ = new QTemporaryDir;
        QTemporaryDir& tmp = *window.tempDir_;
        window.cfg_ = MainWindow::RunConfig();
        window.cfg_.input = fixture.filePath(QStringLiteral("talk.mp4"));
        window.cfg_.to = QStringLiteral("en");
        window.cfg_.from = QStringLiteral("es");
        window.cfg_.speakers = true;
        window.cfg_.numSpeakers = 2;
        window.cfg_.dub = true;
        window.cfg_.voice = QStringLiteral("clone");
        window.cfg_.consent = true;
        window.outputs_ = MainWindow::Outputs();
        window.outputs_.dubbed = fixture.filePath(QStringLiteral("talk.en.dubbed.mp4"));
        const QStringList sa = window.speakersArgs();
        CHECK(sa.first() == QStringLiteral("speakers") && sa.contains(QStringLiteral("--split-srt")) &&
              sa[sa.indexOf(QStringLiteral("--num-speakers")) + 1] == QStringLiteral("2"));
        CHECK(window.translateArgs().first().endsWith(QStringLiteral("split.srt")));
        const QStringList da = window.dubArgs();
        CHECK(da[da.indexOf(QStringLiteral("--speakers")) + 1].endsWith(QStringLiteral("speakers.txt")) &&
              da[da.indexOf(QStringLiteral("--source-srt")) + 1].endsWith(QStringLiteral("split.srt")));
        window.cfg_.speakers = false;
        CHECK(!window.dubArgs().contains(QStringLiteral("--speakers")) &&
              window.translateArgs().first().endsWith(QStringLiteral("source.srt")));
        window.cfg_.speakers = true;

        const QByteArray found = "# speakers 1\nspeaker\t0\tSpeaker 1\t20\t1\t5\nspeaker\t1\tSpeaker 2\t9\t5\t7\n"
                                 "speaker\t2\tSpeaker 3\t2\t9\t10\n"
                                 "cue\t1\t3\t0\t0\ncue\t3\t5\t1\t0\ncue\t6\t8\t2\t1\n";
        const QString spk = tmp.filePath(QStringLiteral("speakers.txt"));
        writeFile(spk, found);
        // The window opens modally: answer it from a timer.
        auto answer = [&](std::function<void(SpeakersDialog*)> act) {
            QTimer::singleShot(0, [act] {
                for (QWidget* w : QApplication::topLevelWidgets())
                    if (auto* d = dynamic_cast<SpeakersDialog*>(w); d && d->isVisible()) {
                        act(d);
                        return;
                    }
            });
        };
        answer([](SpeakersDialog* d) {
            CHECK(d->names.size() == 3 && !d->sameAs[0]->isEnabled() && d->sameAs[2]->count() == 3);
            d->names[0]->setText(QStringLiteral("Anna"));
            CHECK(d->sameAs[2]->itemText(1) == QStringLiteral("Anna"));
            d->sameAs[2]->setCurrentIndex(1);  // Speaker 3 is Anna
            d->continueBtn->click();
        });
        window.step_ = MainWindow::Step::Speakers;
        CHECK(window.reviewSpeakers());
        pk::SpeakerMap m;
        CHECK(pk::parse_speakers(readFile(spk).toStdString(), &m));
        CHECK(m.speakers.size() == 2 && m.speakers[0].name == "Anna" && m.speakers[0].seconds == 22 &&
              m.cues.size() == 3 && m.cues[2].speaker == 0);
        CHECK(window.speakerMap_.speakers.size() == 2);

        // The subtitles: saved as the original ones, a dash before each speaker.
        writeFile(tmp.filePath(QStringLiteral("split.srt")),
                  "1\n00:00:01,000 --> 00:00:03,000\n¿Vienes?\n\n2\n00:00:03,000 --> 00:00:05,000\nSí, ya voy.\n\n"
                  "3\n00:00:06,000 --> 00:00:08,000\nBien.\n\n");
        writeFile(tmp.filePath(QStringLiteral("translated.srt")),
                  "1\n00:00:01,000 --> 00:00:03,000\nAre you coming?\n\n2\n00:00:03,000 --> 00:00:05,000\nYes, I am.\n\n"
                  "3\n00:00:06,000 --> 00:00:08,000\nGood.\n\n");
        window.outputs_.translatedSrt = fixture.filePath(QStringLiteral("talk.en.srt"));
        window.outputs_.originalSrt = fixture.filePath(QStringLiteral("talk.srt"));
        window.outputs_.bilingualSrt = fixture.filePath(QStringLiteral("talk.en.bilingual.srt"));
        window.outputs_.translatedTxt = fixture.filePath(QStringLiteral("talk.en.txt"));
        window.writeSubtitles();
        CHECK(window.writeFailures_.isEmpty());
        const QString en = QString::fromUtf8(readFile(window.outputs_.translatedSrt));
        CHECK(en.contains(QStringLiteral("00:00:01,000 --> 00:00:05,000\n- Are you coming?\n- Yes, I am.\n")) &&
              en.contains(QStringLiteral("2\n00:00:06,000 --> 00:00:08,000\nGood.")));
        CHECK(QString::fromUtf8(readFile(window.outputs_.originalSrt)).contains(QString::fromUtf8("- ¿Vienes?\n- Sí, ya voy.")));
        CHECK(QString::fromUtf8(readFile(window.outputs_.bilingualSrt))
                  .contains(QString::fromUtf8("- ¿Vienes?\n- Sí, ya voy.\n- Are you coming?\n- Yes, I am.")));
        CHECK(!readFile(window.outputs_.translatedTxt).contains("- "));
        CHECK(window.resultView_->toPlainText().contains(QStringLiteral("- Are you coming?")));

        // Detect again with another number of speakers; cancelling stops the run.
        writeFile(spk, found);
        answer([](SpeakersDialog* d) {
            d->countCombo->setCurrentIndex(d->countCombo->findData(3));
            for (QPushButton* b : d->findChildren<QPushButton*>())
                if (b->text() == QStringLiteral("Detect again")) b->click();
        });
        window.setRunning(true);
        window.step_ = MainWindow::Step::Speakers;
        CHECK(!window.reviewSpeakers());
        CHECK(window.cfg_.numSpeakers == 3);
        window.stopProcess();
        window.setRunning(true);
        window.step_ = MainWindow::Step::Speakers;
        answer([](SpeakersDialog* d) { d->reject(); });
        CHECK(!window.reviewSpeakers());
        CHECK(window.step_ == MainWindow::Step::Idle && window.statusLabel_->text() == QStringLiteral("Cancelled."));
        writeFile(spk, "not a speakers file");
        window.setRunning(true);
        window.step_ = MainWindow::Step::Speakers;
        CHECK(!window.reviewSpeakers() && window.step_ == MainWindow::Step::Idle);
        delete window.tempDir_;
        window.tempDir_ = nullptr;

        // The wording follows the choice.
        window.speakersCheck_->setChecked(true);
        CHECK(window.voiceCombo_->itemText(0).contains(QStringLiteral("each speaker")) &&
              window.consentCheck_->text().startsWith(QStringLiteral("Every speaker")) && window.speakersCount_->isEnabled());
        window.speakersCheck_->setChecked(false);
        CHECK(!window.speakersCount_->isEnabled());
    }

    // Help: every model of the three menus is described, and the button works while running.
    {
        const QString help = st::help_html();
        for (const auto& m : pk::asr_models()) CHECK(help.contains(QString::fromStdString(m.display)));
        for (const auto& m : tl::model_table()) CHECK(help.contains(QString::fromStdString(m.display)));
        for (const auto& m : pk::tts_models()) CHECK(help.contains(QString::fromStdString(m.display)));
        CHECK(help.contains(QStringLiteral("Haitian Creole")) && help.contains(QStringLiteral("Spanish (Spain)")));
        window.setRunning(true);
        CHECK(window.helpBtn_->isEnabled());
        window.helpBtn_->click();
        QDialog* dlg = nullptr;
        for (QWidget* w : QApplication::topLevelWidgets())
            if (auto* d = qobject_cast<QDialog*>(w); d && d->isVisible()) dlg = d;
        CHECK(dlg && dlg->findChild<QTextBrowser*>());
        if (dlg) dlg->close();
        window.setRunning(false);
    }

    return failures ? 1 : 0;
}
