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

    window.inputEdit_->setText(QStringLiteral("input.txt"));
    window.outputEdit_->setText(QStringLiteral("output.txt"));
    window.modelFileEdit_->setText(QStringLiteral("model.gguf"));
    window.modelsDirEdit_->setText(QStringLiteral("models"));
    CHECK(QDir::isAbsolutePath(window.plannedOutput()));
    const auto args = window.commonArguments();
    CHECK(args[args.indexOf(QStringLiteral("--model-file")) + 1] == absolutePath(QStringLiteral("model.gguf")));
    CHECK(args[args.indexOf(QStringLiteral("--models-dir")) + 1] == absolutePath(QStringLiteral("models")));
    window.mode_ = MainWindow::Mode::Text;
    window.textOutput_ = fixture.filePath(QStringLiteral("partial.txt"));
    writeFile(window.textOutput_, "Translation\nOriginal failed unit");
    window.processFinished(3, false);
    CHECK(window.resultEdit_->toPlainText().contains(QStringLiteral("Original failed unit")));
    CHECK(window.statusLabel_->text().startsWith(QStringLiteral("Partial translation saved")));

    // Help: the page describes every model, and the button opens it, also while running.
    {
        const QString help = helpHtml();
        for (const auto& m : tl::model_table()) CHECK(help.contains(QString::fromStdString(m.display).toHtmlEscaped()));
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
