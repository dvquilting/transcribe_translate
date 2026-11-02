#include "child_process.h"

#include <QCoreApplication>

namespace pk {

ChildProcess::ChildProcess(QObject* parent) : QObject(parent) {}

ChildProcess::~ChildProcess() { stop(); }

bool ChildProcess::start(const QString& program, const QStringList& args) {
    if (process_) return false;
    decoder_.resetState();
    pending_.clear();
    cancelled_ = false;
    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    process_->setWorkingDirectory(QCoreApplication::applicationDirPath());
    if (tree_.ready()) tree_.configure(process_);
    connect(process_, &QProcess::readyReadStandardOutput, this, &ChildProcess::readOutput);
    connect(process_, &QProcess::finished, this, &ChildProcess::onFinished);
    connect(process_, &QProcess::errorOccurred, this, &ChildProcess::onError);
    // Nothing may touch process_ after this: a start failure can be reported
    // (and the process released) before start() returns.
    process_->start(program, args);
    return true;
}

bool ChildProcess::cancel() {
    if (!isRunning()) return false;
    cancelled_ = true;
    tree_.kill(process_);
    return true;
}

void ChildProcess::stop() {
    if (!process_) return;
    process_->disconnect(this);  // no finished() for a window that is going away
    tree_.kill(process_);
    process_->waitForFinished(5000);
    delete process_;
    process_ = nullptr;
}

namespace {
// A line without its line-end carriage returns: "\r\n" (Windows), and also
// "\r\r\n", which a program writing "\r\n" to a text-mode stdout produces.
QString withoutCr(QString line) {
    while (line.endsWith(QLatin1Char('\r'))) line.chop(1);
    return line;
}
}  // namespace

void ChildProcess::readOutput() {
    if (!process_) return;
    const QString data = decoder_.decode(process_->readAllStandardOutput());  // keeps a split UTF-8 sequence
    if (data.isEmpty()) return;
    emit outputReceived(data);
    pending_ += data;
    QStringList lines = pending_.split(QLatin1Char('\n'));
    pending_ = lines.takeLast();  // a partial line waits for the next read
    for (const QString& l : lines) emit lineReceived(withoutCr(l));
}

void ChildProcess::onFinished(int exitCode, QProcess::ExitStatus status) {
    readOutput();  // whatever arrived after the last readyRead
    const bool unterminated = !pending_.isEmpty();
    if (unterminated) {
        const QString last = withoutCr(pending_);
        pending_.clear();
        emit lineReceived(last);
    }
    const bool cancelled = cancelled_;
    if (status == QProcess::CrashExit && !cancelled) {
        // exitCode is then meaningless (on Linux the signal number: SIGQUIT = 3
        // would read as translate_text's "partial" code), so it is replaced by
        // kCrashExitCode, and the log says what happened.
        const QString msg = QStringLiteral("The program crashed.");
        emit outputReceived((unterminated ? QStringLiteral("\n") : QString()) + msg + QStringLiteral("\n"));
        emit lineReceived(msg);
        exitCode = kCrashExitCode;
    }
    release();
    emit finished(exitCode, cancelled);
}

void ChildProcess::onError(QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart || !process_) return;  // other errors end with finished()
    release();
    emit failedToStart();
}

void ChildProcess::release() {
    process_->disconnect(this);
    process_->deleteLater();  // we may be inside one of its signals
    process_ = nullptr;
}

}  // namespace pk
