// mainwindow.h - Speech Translate: audio/video -> translated subtitles and,
// optionally, a dubbed soundtrack in the speaker's (cloned) voice.
// Runs transcribe_multi, (optionally "transcribe_multi speakers": who speaks
// when, checked by the user), then translate_text, then (optionally)
// "transcribe_multi dub" as child processes, one after the other, so each
// model has the GPU to itself.
#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QSettings>
#include <QStringList>
#include <QWidget>

#include "child_process.h"  // ../common/qt
#include "speakers.h"       // ../common/src

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

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void setInputFile(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void start();
    void cancel();
    void stepFinished(int exitCode, bool cancelled);
    void stepFailedToStart();
    void updateEnabledState();
    void browseInput();
    void browseOutputDir();
    void openOutputFolder();
    void showModels();  // "Model files": the downloaded models, with Delete / Clean All models
    void saveLog();

private:
    enum class Step { Idle, Transcribe, Speakers, Translate, Dub };
    // Everything a run needs, read from the window when it starts: the
    // widgets may change (or be changed by a drop) while the steps run.
    struct RunConfig {
        QString input, outDir;  // absolute, native separators
        QString asr, from, mt, to, device;
        int threads = 0;
        bool align = true, context = true;
        bool speakers = false;  // several speakers: find who speaks when
        int numSpeakers = 0;    // how many (0 = detect)
        bool saveOriginal = false, saveBilingual = false, saveText = false;
        bool dub = false;
        QString tts, voice, background, dubFormat;
        bool consent = false, keepAccent = false;
    };
    struct Outputs {
        QString translatedSrt, originalSrt, bilingualSrt, translatedTxt, dubbed;
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
    QGroupBox* advancedBox_ = nullptr;
    QWidget* advancedBody_ = nullptr;
    QComboBox* deviceCombo_ = nullptr;
    QSpinBox* threadsSpin_ = nullptr;
    QCheckBox* alignCheck_ = nullptr;
    QCheckBox* contextCheck_ = nullptr;
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
    QPushButton* infoBtn_ = nullptr;
    QPushButton* helpBtn_ = nullptr;  // always enabled, also while running
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
    QElapsedTimer clock_;
    qint64 stepStartMs_ = 0;
    double asrSeconds_ = -1, mtSeconds_ = -1, dubSeconds_ = -1, audioSeconds_ = -1;
    QStringList writeFailures_;
    pk::SpeakerMap speakerMap_;  // the speakers of this run, as the user confirmed them
};
