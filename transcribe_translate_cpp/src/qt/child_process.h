// child_process.h - runs one of the command-line tools for a GUI.
//
// The GUIs never load a model themselves: each step is a separate process
// (transcribe_multi, translate_text, "transcribe_multi dub"), so its GPU memory
// is freed when it exits. ChildProcess is what the three GUIs had each written
// for that: start the tool in the program's folder, decode its output as UTF-8
// and split it into lines, cancel it together with everything it started, and
// report the end exactly once. The GUI keeps its own reading of the lines.
#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringDecoder>
#include <QStringList>

#include "process_tree.h"

namespace pk {

class ChildProcess : public QObject {
    Q_OBJECT
public:
    // finished()'s exit code for a tool that crashed (QProcess::CrashExit): its
    // own exit code is then meaningless (on Linux the signal number, which could
    // pass for one of the tools' own codes). Like any non-zero code, a failure.
    static constexpr int kCrashExitCode = -1;

    explicit ChildProcess(QObject* parent = nullptr);
    ~ChildProcess() override;  // stop()

    // Whether whole-tree cancellation could be set up (Windows: the job object).
    // Without it start() still works, but cancel() only kills the tool itself.
    bool treeReady() const { return tree_.ready(); }

    // Start `program` with `args`, working directory = the application's folder
    // (relative paths in `args` must therefore already be absolute), stdout and
    // stderr merged. Returns false without doing anything while a run is active.
    bool start(const QString& program, const QStringList& args);

    // A run was started and its end (finished or failedToStart) not yet reported.
    bool isActive() const { return process_ != nullptr; }
    // The tool's process is still alive.
    bool isRunning() const { return process_ && process_->state() != QProcess::NotRunning; }

    // Kill the tool and everything it started (ffmpeg); finished() follows with
    // cancelled = true. Returns false if nothing was running.
    bool cancel();

    // For a window that closes or is destroyed while a tool runs: no further
    // signals, the process tree is killed and waited for (at most 5 s).
    void stop();

    // The running QProcess (nullptr when idle); for tests.
    QProcess* process() const { return process_; }

signals:
    // Decoded output as it arrives (for a log view); a UTF-8 character split
    // between two reads is kept until it is complete.
    void outputReceived(const QString& text);
    // Each complete output line without its line break (CRLF or LF). At exit an
    // unterminated last line is delivered too, before finished().
    void lineReceived(const QString& line);
    // The run ended. The ChildProcess is idle again when this is emitted, so a
    // handler may start() the next step. A tool that crashed (and was not
    // cancelled) reports kCrashExitCode, after the output line "The program
    // crashed.".
    void finished(int exitCode, bool cancelled);
    // The program could not be started (finished() is not emitted then).
    void failedToStart();

private:
    void readOutput();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onError(QProcess::ProcessError error);
    void release();

    ProcessTree tree_;
    QProcess* process_ = nullptr;
    QStringDecoder decoder_{QStringDecoder::Utf8};
    QString pending_;  // output after the last line break
    bool cancelled_ = false;
};

}  // namespace pk
