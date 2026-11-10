// translate_engine.h - "tt_engine translate --serve", kept running between the
// Translate tab's jobs so the model stays loaded on the graphics card: a second
// translation starts at once instead of loading 10 GB again.
//
// One job at a time (the JobGate already ensures that). The model is freed -
// the engine ends - when another tab starts a job (each model gets the graphics
// card to itself), from the "Free GPU memory" button, before Model files, and
// when the window closes. A job that is cancelled stops at once; if it doesn't
// (while downloading or loading), the engine is ended.
#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>

#include "child_process.h"  // ../qt

namespace tt {

// One translation; its signals are those of pk::ChildProcess for one run.
class EngineJob : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void outputReceived(const QString& text);
    void lineReceived(const QString& line);
    void finished(int exitCode, bool cancelled);
    void failedToStart();
};

class TranslateEngine : public QObject {
    Q_OBJECT
public:
    static TranslateEngine& instance();

    // Run "tt_engine translate ARGS" (ARGS without "translate") in the engine,
    // starting it if needed. nullptr while a job runs. The job reports its end
    // (finished or failedToStart) exactly once and then deletes itself.
    EngineJob* start(const QStringList& args);
    bool isActive() const { return job_ != nullptr; }
    // Load the model for jobs with these ARGS ahead of time, in the background
    // (nothing is translated, nothing downloaded). Ignored while a job or a
    // preload runs. A job started meanwhile waits for it in the engine.
    void preload(const QStringList& args);
    bool isPreloading() const { return preloading_; }
    // Stop the running job (finished(..., true) follows). False if none.
    bool cancel();
    // Free the graphics memory: end the engine and its model. Not while a job runs.
    void release();
    // The window closes: end everything at once, without further signals.
    void shutdown();

    // The model kept loaded ("" = none), e.g. "TranslateGemma 12B".
    QString loadedModel() const { return loaded_; }

    // How a preload ended.
    enum class PreloadEnd { Loaded, Skipped, Stopped };  // Skipped: not downloaded yet; Stopped: ended

signals:
    void loadedChanged(const QString& model);
    void preloadingChanged(bool preloading);
    void preloadEnded(tt::TranslateEngine::PreloadEnd how);

private:
    TranslateEngine();
    void onLine(const QString& line);
    void onProcessFinished(int exitCode, bool cancelled);
    void onFailedToStart();
    void endJob(int exitCode, bool cancelled);
    void setLoaded(const QString& model);
    void send(const QByteArray& line);

    pk::ChildProcess process_;
    QPointer<EngineJob> job_;
    bool preloading_ = false;  // the engine's current (first) command is a preload
    bool preloadSkipped_ = false;  // ... and it said it skipped (tt-preload-skipped)
    void setPreloading(bool on, PreloadEnd how = PreloadEnd::Stopped);
    bool cancelRequested_ = false;
    QTimer killTimer_;  // a cancelled job that doesn't stop in time
    QString loaded_;
};

// One argument of a "job" line: backslash, tab, CR and LF escaped.
QString escapeEngineArg(const QString& arg);

}  // namespace tt
