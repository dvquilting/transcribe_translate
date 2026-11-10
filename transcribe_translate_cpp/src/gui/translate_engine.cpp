#include "translate_engine.h"

#include <QProcess>

#include "jobgate.h"

namespace tt {

QString escapeEngineArg(const QString& arg) {
    QString s;
    s.reserve(arg.size());
    for (const QChar c : arg) {
        if (c == QLatin1Char('\\')) s += QStringLiteral("\\\\");
        else if (c == QLatin1Char('\t')) s += QStringLiteral("\\t");
        else if (c == QLatin1Char('\n')) s += QStringLiteral("\\n");
        else if (c == QLatin1Char('\r')) s += QStringLiteral("\\r");
        else s += c;
    }
    return s;
}

TranslateEngine& TranslateEngine::instance() {
    static TranslateEngine engine;
    return engine;
}

TranslateEngine::TranslateEngine() {
    killTimer_.setSingleShot(true);
    killTimer_.setInterval(4000);
    connect(&killTimer_, &QTimer::timeout, this, [this] {
        if (job_) process_.cancel();  // finished(..., cancelled) ends the job
    });
    connect(&process_, &pk::ChildProcess::lineReceived, this, &TranslateEngine::onLine);
    connect(&process_, &pk::ChildProcess::finished, this, &TranslateEngine::onProcessFinished);
    connect(&process_, &pk::ChildProcess::failedToStart, this, &TranslateEngine::onFailedToStart);
}

EngineJob* TranslateEngine::start(const QStringList& args) {
    if (job_) return nullptr;
    job_ = new EngineJob(this);
    cancelRequested_ = false;
    QStringList escaped;
    for (const QString& a : args) escaped << escapeEngineArg(a);
    const QByteArray line = (QStringLiteral("job\t") + escaped.join(QLatin1Char('\t'))).toUtf8();
    // After the caller has connected to the job's signals.
    QTimer::singleShot(0, this, [this, line] {
        if (!job_) return;
        if (!process_.isRunning() &&
            !process_.start(enginePath(), {QStringLiteral("translate"), QStringLiteral("--serve")})) {
            onFailedToStart();  // (the last engine is still being ended)
            return;
        }
        send(line);
    });
    return job_;
}

void TranslateEngine::send(const QByteArray& line) {
    if (QProcess* p = process_.process()) p->write(line + '\n');
}

bool TranslateEngine::cancel() {
    if (!job_) return false;
    cancelRequested_ = true;
    send("cancel");
    killTimer_.start();
    return true;
}

void TranslateEngine::release() {
    if (job_ || !process_.isActive()) return;
    process_.stop();  // no signals: the model is gone with the process
    setLoaded(QString());
}

void TranslateEngine::shutdown() {
    killTimer_.stop();
    if (job_) {
        job_->disconnect();
        job_->deleteLater();
        job_ = nullptr;
    }
    process_.stop();
    loaded_.clear();
}

void TranslateEngine::onLine(const QString& line) {
    if (line.startsWith(QLatin1String("tt-model "))) {
        setLoaded(line.mid(9).trimmed());
        return;
    }
    if (line.startsWith(QLatin1String("tt-job-done "))) {
        endJob(line.mid(12).trimmed().toInt(), cancelRequested_);
        return;
    }
    if (!job_) return;
    emit job_->outputReceived(line + QLatin1Char('\n'));
    emit job_->lineReceived(line);
}

void TranslateEngine::onProcessFinished(int exitCode, bool cancelled) {
    // The engine ended (killed after a cancel, or crashed): its model is gone.
    setLoaded(QString());
    if (job_) endJob(exitCode == 0 ? 1 : exitCode, cancelled || cancelRequested_);
}

void TranslateEngine::onFailedToStart() {
    setLoaded(QString());
    killTimer_.stop();
    if (!job_) return;
    EngineJob* j = job_;
    job_ = nullptr;
    emit j->failedToStart();
    j->deleteLater();
}

void TranslateEngine::endJob(int exitCode, bool cancelled) {
    killTimer_.stop();
    if (!job_) return;
    EngineJob* j = job_;
    job_ = nullptr;  // idle again before the handler runs (it may start the next job)
    emit j->finished(exitCode, cancelled);
    j->deleteLater();
}

void TranslateEngine::setLoaded(const QString& model) {
    if (model == loaded_) return;
    loaded_ = model;
    emit loadedChanged(loaded_);
}

}  // namespace tt
