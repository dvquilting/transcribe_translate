// jobgate.h - what the three job tabs of Transcribe & Translate share: one job
// at a time (each model gets the graphics card to itself), the engine's path,
// and the record of a finished job for the History tab.
#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>

class QWidget;

namespace tt {

struct JobRecord {
    QString kind;         // "Transcribe", "Translate", "Speech translation"
    QString input;        // the input file ("" for text typed into the Translate tab)
    QStringList outputs;  // the files the job saved
    QString status;       // the status line when it ended
    bool ok = false;      // finished without errors
    QDateTime finished;   // when it ended
};

class JobGate : public QObject {
    Q_OBJECT
public:
    static JobGate& instance();

    bool busy() const { return !owner_.isEmpty(); }
    QString owner() const { return owner_; }  // the tab whose job is running
    // True when no job is running; otherwise tells the user which tab is busy.
    bool checkFree(QWidget* parent) const;
    // `keepTranslateEngine`: the job runs in the Translate tab's engine, which
    // keeps its model; any other job first frees it (TranslateEngine::release).
    void acquire(const QString& owner, bool keepTranslateEngine = false);
    void release();

signals:
    void changed(bool busy);

private:
    QString owner_;
};

// tt_engine(.exe) next to this program.
QString enginePath();

}  // namespace tt
