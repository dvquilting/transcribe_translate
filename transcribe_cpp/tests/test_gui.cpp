#include <QtWidgets>
#include <QProcess>
#include <QSettings>
#include <QStringDecoder>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
#define private public
#include "../src/gui/mainwindow.h"
#undef private
#include "../src/gui/mainwindow.cpp"

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)
[[maybe_unused]] static void writeFile(const QString& path, const QByteArray& data) {
    QFile f(path); CHECK(f.open(QIODevice::WriteOnly)); CHECK(f.write(data) == data.size()); f.close();
}
[[maybe_unused]] static QByteArray readFile(const QString& path) { QFile f(path); CHECK(f.open(QIODevice::ReadOnly)); return f.readAll(); }

// A real child process: the window shows its output (split inside a UTF-8 character) intact.
// pk::ChildProcess itself, including process-tree cancellation, is tested in ../common/tests.
static int helper(int argc, char** argv) {
    if (argc < 2) return -1;
    QCoreApplication app(argc, argv);
    if (app.arguments()[1] == QLatin1String("--utf8")) {
        const QByteArray text = QString::fromUtf8("caf\xC3\xA9\n").toUtf8();
        std::fwrite(text.constData(), 1, 4, stdout); std::fflush(stdout);
        QTimer::singleShot(150, [&] { std::fwrite(text.constData() + 4, 1, text.size() - 4, stdout); std::fflush(stdout); app.quit(); });
        return app.exec();
    }
    QTimer::singleShot(15000, &app, &QCoreApplication::quit);
    return app.exec();
}

int main(int argc, char** argv) {
    const int mode = helper(argc, argv);
    if (mode >= 0) return mode;
    QApplication app(argc, argv);
    QTemporaryDir fixture;
    CHECK(fixture.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, fixture.path());
    QDir::setCurrent(fixture.path());
    MainWindow window;
    
    CHECK(window.runner_.start(app.applicationFilePath(), {QStringLiteral("--utf8")}));
    {
        QEventLoop loop;  // until the window got finished() (or 10 s)
        QObject::connect(&window.runner_, &pk::ChildProcess::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    CHECK(!window.runner_.isActive());
    CHECK(window.logView_->toPlainText().contains(QString::fromUtf8("caf\xC3\xA9")));
    CHECK(!window.logView_->toPlainText().contains(QChar(0xfffd)));

    window.outputEdit_->setText(QStringLiteral("result.txt"));
    window.modelFileEdit_->setText(QStringLiteral("model.bin"));
    window.modelsDirEdit_->setText(QStringLiteral("models"));
    window.polishCheck_->setChecked(true);
    window.llmFileEdit_->setText(QStringLiteral("llm.gguf"));
    const auto args = window.buildArguments(absolutePath(QStringLiteral("input.wav")));
    for (const auto& option : {QStringLiteral("-o"), QStringLiteral("--model-file"), QStringLiteral("--models-dir"), QStringLiteral("--llm-file")})
        CHECK(QDir::isAbsolutePath(args[args.indexOf(option) + 1]));
    CHECK(window.plannedOutputs(QStringLiteral("input.wav")).contains(QDir::fromNativeSeparators(absolutePath(QStringLiteral("result.txt")))));

    // The options can't be changed while a run is active, and come back afterwards.
    window.setRunning(true);
    CHECK(!window.inputEdit_->isEnabled() && !window.modelCombo_->isEnabled() && !window.polishCheck_->isEnabled() &&
          !window.advancedBox_->isEnabled() && window.cancelBtn_->isEnabled());
    window.setRunning(false);
    CHECK(window.inputEdit_->isEnabled() && window.modelCombo_->isEnabled() && window.polishCheck_->isEnabled() &&
          window.advancedBox_->isEnabled() && !window.cancelBtn_->isEnabled());

    // A run that found no speech says so instead of "Failed (exit code 1)".
    window.lastProblem_.clear();
    window.parseLine(QStringLiteral("Warning: no speech was transcribed."));
    window.processFinished(1, false);
    CHECK(window.statusLabel_->text().contains(QStringLiteral("No speech")));
    window.lastProblem_.clear();
    window.parseLine(QStringLiteral("Error: could not decode 'x.wav'"));
    window.processFinished(1, false);
    CHECK(window.statusLabel_->text().contains(QStringLiteral("could not decode")));

    // Output formats: any combination, never none; the corrected copy and summary
    // follow the document formats (.txt when only subtitles are chosen).
    {
        const auto planned = [&] {
            QStringList names;
            for (const QString& p : window.plannedOutputs(QStringLiteral("input.wav"))) names << QFileInfo(p).fileName();
            return names;
        };
        window.summaryCheck_->setChecked(true);
        window.txtCheck_->setChecked(true);
        window.srtCheck_->setChecked(false);
        window.docxCheck_->setChecked(true);
        window.pdfCheck_->setChecked(true);
        CHECK(window.cliFormats() == QStringLiteral("txt,docx"));
        CHECK(planned() == (QStringList{"result.txt", "result.docx", "result.pdf", "result.clean.txt", "result.clean.docx",
                                        "result.clean.pdf", "result.summary.txt", "result.summary.docx", "result.summary.pdf"}));
        window.txtCheck_->setChecked(false);
        window.docxCheck_->setChecked(false);
        CHECK(window.cliFormats() == QStringLiteral("none"));  // PDF only: made from the text copies
        CHECK(planned() == (QStringList{"result.pdf", "result.clean.pdf", "result.summary.pdf"}));
        window.pdfCheck_->setChecked(false);  // the last one stays
        CHECK(window.pdfCheck_->isChecked());
        window.srtCheck_->setChecked(true);
        window.pdfCheck_->setChecked(false);
        CHECK(window.cliFormats() == QStringLiteral("srt"));
        CHECK(planned() == (QStringList{"result.srt", "result.clean.txt", "result.summary.txt"}));
        window.summaryCheck_->setChecked(false);
        window.polishCheck_->setChecked(false);

        // A PDF from the CLI's text copies: "text" lines are collected, the PDFs
        // go to the output folder and count as written.
        QTemporaryDir copies;
        writeFile(copies.filePath(QStringLiteral("talk.txt")),
                  QString::fromUtf8("First paragraph,\nsecond line.\n\nZweiter Absatz \xC3\xBC \xE6\x97\xA5\xE6\x9C\xAC.\n").toUtf8());
        writeFile(copies.filePath(QStringLiteral("talk.summary.txt")), "[Summary written by AI]\n\nShort.\n");
        window.written_.clear();
        window.textCopies_.clear();
        window.parseLine(QStringLiteral("  text ") + copies.filePath(QStringLiteral("talk.txt")));
        window.parseLine(QStringLiteral("  text ") + copies.filePath(QStringLiteral("talk.summary.txt")));
        CHECK(window.textCopies_.size() == 2);
        window.pdfDir_ = fixture.path();
        window.inputTitle_ = QStringLiteral("talk");
        window.makePdfs();
        CHECK(window.written_.size() == 2);
        CHECK(readFile(QDir(fixture.path()).filePath(QStringLiteral("talk.pdf"))).startsWith("%PDF"));
        CHECK(readFile(QDir(fixture.path()).filePath(QStringLiteral("talk.summary.pdf"))).startsWith("%PDF"));
        QString error;
        CHECK(!writePdf(copies.filePath(QStringLiteral("missing.txt")), QDir(fixture.path()).filePath(QStringLiteral("x.pdf")),
                        QStringLiteral("x"), &error) && !error.isEmpty());
        CHECK(!QFile::exists(QDir(fixture.path()).filePath(QStringLiteral("x.pdf"))));
        // The transcript tab shows the text copy when no .txt was saved.
        window.showTranscript();
        CHECK(window.transcriptView_->toPlainText().startsWith(QStringLiteral("First paragraph")));
        window.written_.clear();
        window.textCopies_.clear();

        // Settings: the old single "format" is read as a list.
        window.settings_.remove(QStringLiteral("formats"));
        window.settings_.setValue(QStringLiteral("format"), QStringLiteral("both"));
        window.loadSettings();
        CHECK(window.txtCheck_->isChecked() && window.srtCheck_->isChecked() && !window.docxCheck_->isChecked() &&
              !window.pdfCheck_->isChecked());
        window.docxCheck_->setChecked(true);
        window.saveSettings();
        CHECK(window.settings_.value(QStringLiteral("formats")).toStringList() == (QStringList{"txt", "srt", "docx"}));
    }

    // Help: the page describes every model, and the button opens it, also while running.
    {
        const QString help = helpHtml();
        for (const auto& m : pk::asr_models()) CHECK(help.contains(QString::fromStdString(m.display).toHtmlEscaped()));
        window.setRunning(true);
        CHECK(window.helpBtn_->isEnabled());
        window.helpBtn_->click();
        QDialog* dlg = nullptr;
        for (QWidget* w : QApplication::topLevelWidgets())
            if (auto* d = qobject_cast<QDialog*>(w); d && d->isVisible() && d->windowTitle().endsWith(QStringLiteral("Help")))
                dlg = d;
        CHECK(dlg && dlg->findChild<QTextBrowser*>());
        if (dlg) dlg->close();
        window.setRunning(false);
    }

    return failures ? 1 : 0;
}
