// transcribe_page.h - the Transcribe tab of Transcribe & Translate: speech to
// text (.txt, .srt, .docx, .pdf) with "tt_engine transcribe". With several
// speakers it is three runs: the speech (subtitles and word timings into a
// work folder), "tt_engine speakers" (who speaks when; the user checks them in
// the Speakers window), then "tt_engine transcribe --from-srt", which writes the
// outputs with the speakers' names.
#pragma once

#include <QElapsedTimer>
#include <QSettings>
#include <QStringList>
#include <QWidget>

#include <memory>

#include "child_process.h"  // ../qt
#include "jobgate.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTemporaryDir;
class QTimer;

class TranscribePage : public QWidget {
    Q_OBJECT
public:
    explicit TranscribePage(QWidget* parent = nullptr);
    ~TranscribePage() override;
    void setInputFile(const QString& path);
    static QString helpHtml();  // this tab's part of the Help window
    QString modelsDir() const;  // the models folder chosen under Advanced ("" = the default)
    bool isRunning() const { return step_ != Step::Idle; }  // also while the Speakers window is open
    void shutdown();            // the window is closing: stop the job, save the settings

signals:
    void jobFinished(const tt::JobRecord& job);  // for the History tab
    void translateRequested(const QString& path);  // "Translate...": the transcript, for the Translate tab

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void browseInput();
    void browseOutput();
    void browseModelsDir();
    void updateEnabledState();
    void start();
    void cancel();
    void processFinished(int exitCode, bool cancelled);
    void processFailedToStart();
    void openOutputFolder();

private:
    // One run, or with several speakers: Transcribe, Speakers (again if asked), Finish.
    enum class Step { Idle, Single, Transcribe, Speakers, Finish };
    void runStep(Step step);
    QStringList speechArguments(const QString& input) const;  // what the speech recognition takes
    QStringList speakersArguments() const;
    enum class Review { Continue, Again, Cancelled, Failed };
    Review reviewSpeakers();
    QString workFile(const char* name) const;
    void buildUi();
    void loadSettings();
    void saveSettings();
    void setRunning(bool running);
    void parseLine(const QString& line);
    void appendLog(const QString& text);
    void showTranscript();
    bool loadInto(QPlainTextEdit* view, const QString& path);
    bool postProcessing() const;
    void setEngine(const QString& backend, const QString& detail, bool lastRun);
    void updateTiming(bool finished);
    QStringList buildArguments(const QString& input) const;
    QString cliPath() const;
    QStringList plannedOutputs(const QString& input) const;
    bool confirmOverwrite(const QString& input);
    QString translatableOutput() const;  // the transcript to translate (.txt, .srt or .docx)
    QString cliFormats() const;  // "-f" for tt_engine transcribe: txt,srt,docx (PDFs are made here)
    void makePdfs();             // the PDFs, from tt_engine transcribe's text copies

    // widgets
    QGroupBox* filesBox_ = nullptr;    // these four are disabled while a run is active
    QGroupBox* optionsBox_ = nullptr;
    QGroupBox* aiBox_ = nullptr;
    QLineEdit* inputEdit_ = nullptr;
    QLineEdit* outputEdit_ = nullptr;
    QComboBox* modelCombo_ = nullptr;
    QLabel* modelDesc_ = nullptr;
    QCheckBox* arkPunctCheck_ = nullptr;
    QCheckBox* txtCheck_ = nullptr;   // output formats (at least one is always ticked)
    QCheckBox* srtCheck_ = nullptr;
    QCheckBox* docxCheck_ = nullptr;
    QCheckBox* pdfCheck_ = nullptr;
    QCheckBox* speakersCheck_ = nullptr;   // several speakers: their names in the transcript
    QComboBox* speakersCount_ = nullptr;
    QComboBox* languageCombo_ = nullptr;
    QGroupBox* advancedBox_ = nullptr;
    QWidget* advancedBody_ = nullptr;
    QComboBox* deviceCombo_ = nullptr;
    QSpinBox* threadsSpin_ = nullptr;
    QDoubleSpinBox* chunkSpin_ = nullptr;
    QSpinBox* paragraphSpin_ = nullptr;
    QComboBox* punctCombo_ = nullptr;
    QCheckBox* alignCheck_ = nullptr;
    QLineEdit* modelFileEdit_ = nullptr;
    QLineEdit* modelsDirEdit_ = nullptr;
    QCheckBox* polishCheck_ = nullptr;
    QCheckBox* summaryCheck_ = nullptr;
    QCheckBox* smartParaCheck_ = nullptr;
    QLabel* aiNote_ = nullptr;
    QLineEdit* llmFileEdit_ = nullptr;
    QPushButton* startBtn_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;
    QPushButton* openBtn_ = nullptr;
    QPushButton* translateBtn_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* engineLabel_ = nullptr;
    QString engineBackend_;
    QProgressBar* progress_ = nullptr;
    QLineEdit* audioLengthEdit_ = nullptr;
    QLineEdit* elapsedEdit_ = nullptr;
    QLineEdit* ratioEdit_ = nullptr;
    QLabel* postLabel_ = nullptr;
    QLineEdit* postEdit_ = nullptr;
    double transcribeSeconds_ = -1.0;  // wall-clock time until the transcript was written
    QTimer* tick_ = nullptr;
    QElapsedTimer runClock_;
    double audioSeconds_ = -1.0;
    QTabWidget* tabs_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
    QPlainTextEdit* transcriptView_ = nullptr;
    QPlainTextEdit* cleanView_ = nullptr;
    QPlainTextEdit* summaryView_ = nullptr;

    // state
    Step step_ = Step::Idle;
    bool speakers_ = false;        // this run: several speakers
    int numSpeakers_ = 0;          // ... how many (0: detect)
    bool speakersFailed_ = false;  // ... but they could not be found: written without names
    QStringList speakerLabels_;    // "Anna:", ...: bold in the PDFs (empty: no names were written)
    std::unique_ptr<QTemporaryDir> workDir_;  // the subtitles, word timings and speakers between the runs
    qint64 pausedMs_ = 0;          // time in the Speakers window (not the run's)
    qint64 pauseStartMs_ = -1;     // >= 0 while it is open
    QSettings settings_;
    // tt_engine transcribe; its arguments are built once, when the run starts.
    pk::ChildProcess runner_;
    QStringList written_;
    // PDFs: tt_engine transcribe writes text copies into this folder (--text-dir),
    // which become PDFs in pdfDir_ when it is done.
    std::unique_ptr<QTemporaryDir> textDir_;
    QStringList textCopies_;
    QString pdfDir_;
    QString inputTitle_;  // the title of the Word/PDF documents: the input's name
    QString runInput_;     // the input of the current/last run (History)
    QString lastProblem_;  // the run's last "Warning:" or "Error:" line
};
