// speech_page.h - the Speech translation tab of Transcribe & Translate:
// audio/video -> translated subtitles and, optionally, a dubbed soundtrack in
// the speaker's (cloned) voice. Runs "tt_engine transcribe", (optionally
// "tt_engine speakers": who speaks when, checked by the user), then
// "tt_engine translate", then (optionally) "tt_engine dub" as child processes,
// one after the other, so each model has the GPU to itself.
#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QSettings>
#include <QStringList>
#include <QWidget>

#include "child_process.h"  // ../qt
#include "jobgate.h"
#include "speakers.h"       // ../common

class QCheckBox;
class QComboBox;
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

class SpeechPage : public QWidget {
    Q_OBJECT
    enum class Step { Idle, Transcribe, Speakers, Translate, Dub };

public:
    explicit SpeechPage(QWidget* parent = nullptr);
    ~SpeechPage() override;
    void setInputFile(const QString& path);
    static QString helpHtml();  // this tab's part of the Help window
    QString modelsDir() const;  // the models folder chosen under Advanced ("" = the default)
    bool isRunning() const { return step_ != Step::Idle || runner_.isActive(); }
    void shutdown();            // the window is closing: stop the job, save the settings

signals:
    void jobFinished(const tt::JobRecord& job);  // for the History tab

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void start();
    void cancel();
    void stepFinished(int exitCode, bool cancelled);
    void stepFailedToStart();
    void updateEnabledState();
    void browseInput();
    void browseOutputDir();
    void openOutputFolder();
    void saveLog();

private:
    // Everything a run needs, read from the window when it starts: the
    // widgets may change (or be changed by a drop) while the steps run.
    struct RunConfig {
        QString input, outDir;  // absolute, native separators
        QString asr, from, mt, to, device;
        int threads = 0;
        bool align = true, context = true;
        QString draft = QStringLiteral("auto");  // --draft: auto, a helper model, or off
        bool speakers = false;  // several speakers: find who speaks when
        int numSpeakers = 0;    // how many (0 = detect)
        bool saveOriginal = false, saveBilingual = false, saveText = false, savePdf = false;
        bool dub = false;
        QString tts, voice, background, dubFormat;
        bool consent = false, keepAccent = false;
    };
    struct Outputs {
        QString translatedSrt, originalSrt, bilingualSrt, translatedTxt, translatedPdf, dubbed;
        QStringList all() const;
    };

    void buildUi();
    void loadSettings();
    void saveSettings();
    QString toolPath(const QString& name) const;
    RunConfig readConfig() const;
    Outputs plannedOutputs(const RunConfig& c) const;
    bool confirmOverwrite(const Outputs& o);
    void runStep(Step step);
    void writeSubtitles();
    bool reviewSpeakers();
    void finishCancelled(bool dubbing);
    QString sourceSrtPath() const;  // the original subtitles (split by speaker when found)
    void finishRun();
    int totalSteps() const;
    void fail(const QString& message);
    QString savedSubtitlesNote();
    bool rememberDubFiles();
    void removePartialDub();
    void stopProcess();
    void reportJob(bool ok);  // the run ended: free the job gate, tell the History tab
    bool running() const { return step_ != Step::Idle; }
    void setRunning(bool running);
    void parseLine(const QString& line);
    void appendLog(const QString& text);
    void setEngine(const QString& backend, const QString& detail);
    void updateTiming();
    void resetTiming();
    QStringList transcribeArgs() const;
    QStringList speakersArgs() const;
    QStringList translateArgs() const;
    QStringList dubArgs() const;
    QString asrLanguage() const;

    // widgets
    QLineEdit* inputEdit_ = nullptr;
    QLineEdit* outDirEdit_ = nullptr;
    QPushButton* inBrowseBtn_ = nullptr;
    QPushButton* outBrowseBtn_ = nullptr;
    QComboBox* asrCombo_ = nullptr;
    QLabel* asrDesc_ = nullptr;
    QComboBox* fromCombo_ = nullptr;
    QCheckBox* speakersCheck_ = nullptr;
    QComboBox* speakersCount_ = nullptr;
    QComboBox* mtCombo_ = nullptr;
    QLabel* mtDesc_ = nullptr;
    QComboBox* toCombo_ = nullptr;
    QLabel* langNote_ = nullptr;
    QCheckBox* origCheck_ = nullptr;
    QCheckBox* biCheck_ = nullptr;
    QCheckBox* txtCheck_ = nullptr;
    QCheckBox* pdfCheck_ = nullptr;  // the translated text as a PDF
    QGroupBox* advancedBox_ = nullptr;
    QWidget* advancedBody_ = nullptr;
    QComboBox* deviceCombo_ = nullptr;
    QSpinBox* threadsSpin_ = nullptr;
    QCheckBox* alignCheck_ = nullptr;
    QCheckBox* contextCheck_ = nullptr;
    QComboBox* draftCombo_ = nullptr;  // the faster mode: --draft auto|<helper>|off (faster_mode.h)
    QGroupBox* dubBox_ = nullptr;
    QComboBox* ttsCombo_ = nullptr;
    QLabel* ttsDesc_ = nullptr;
    QComboBox* voiceCombo_ = nullptr;
    QCheckBox* consentCheck_ = nullptr;
    QCheckBox* accentCheck_ = nullptr;
    QLabel* consentNote_ = nullptr;
    QComboBox* backgroundCombo_ = nullptr;
    QComboBox* dubFormatCombo_ = nullptr;
    QLabel* dubNote_ = nullptr;
    QPushButton* startBtn_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;
    QPushButton* openBtn_ = nullptr;
    QPushButton* saveLogBtn_ = nullptr;
    QLabel* stepLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* engineLabel_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLineEdit* audioEdit_ = nullptr;
    QLineEdit* asrTimeEdit_ = nullptr;
    QLineEdit* mtTimeEdit_ = nullptr;
    QLineEdit* dubTimeEdit_ = nullptr;
    QLineEdit* totalEdit_ = nullptr;
    QLineEdit* speedEdit_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
    QPlainTextEdit* resultView_ = nullptr;
    QTimer* tick_ = nullptr;

    // state
    QSettings settings_;
    pk::ChildProcess runner_;  // the current step's tool (one at a time: each frees its GPU memory on exit)
    Step step_ = Step::Idle;
    RunConfig cfg_;
    QString consentInput_;
    QTemporaryDir* dubWorkDir_ = nullptr;
    QTemporaryDir* tempDir_ = nullptr;
    Outputs outputs_;
    QString lastError_;
    bool translatingSeen_ = false;  // this step's engine reports "progress: translating"
    QElapsedTimer clock_;
    qint64 stepStartMs_ = 0;
    double asrSeconds_ = -1, mtSeconds_ = -1, dubSeconds_ = -1, audioSeconds_ = -1;
    QStringList writeFailures_;
    pk::SpeakerMap speakerMap_;  // the speakers of this run, as the user confirmed them
};
