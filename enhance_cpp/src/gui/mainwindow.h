// mainwindow.h - Qt6 front end for enhance_audio: choose audio/video files and
// how to clean them up, run enhance_audio as a child process (all files in one run,
// so the models load once), follow its progress, and listen to the results.
#pragma once

#include <QElapsedTimer>
#include <QSettings>
#include <QStringList>
#include <QWidget>

#include "child_process.h"  // ../common/qt

class QAudioOutput;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMediaPlayer;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTimer;

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void addFiles(const QStringList& paths);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void browseFiles();
    void removeSelected();
    void browseOutputDir();
    void updateEnabledState();
    void start();
    void cancel();
    void processFinished(int exitCode, bool cancelled);
    void processFailedToStart();
    void openOutputFolder();
    void showHelp();
    void play(bool original);
    void stopPlayback();

private:
    void buildUi();
    void loadSettings();
    void saveSettings();
    void setRunning(bool running);
    void parseLine(const QString& line);
    void appendLog(const QString& text);
    void setEngine(const QString& backend, const QString& detail, bool lastRun);
    QString cliPath() const;
    QString modelsDir() const;
    bool modelsPresent(bool denoiseOnly) const;
    QStringList arguments() const;
    QString plannedOutput(const QString& input) const;  // the file enhance_audio will write
    QString formatArg() const;                          // -f value
    bool denoiseOnly() const;
    void updateLabels();

    // widgets
    QGroupBox* filesBox_ = nullptr;
    QGroupBox* modeBox_ = nullptr;
    QGroupBox* advancedBox_ = nullptr;
    QWidget* advancedBody_ = nullptr;
    QListWidget* fileList_ = nullptr;
    QPushButton* addBtn_ = nullptr;
    QPushButton* removeBtn_ = nullptr;
    QPushButton* clearBtn_ = nullptr;
    QRadioButton* nextToInput_ = nullptr;
    QRadioButton* toFolder_ = nullptr;
    QLineEdit* outDirEdit_ = nullptr;
    QPushButton* outDirBtn_ = nullptr;
    QComboBox* formatCombo_ = nullptr;
    QRadioButton* enhanceRadio_ = nullptr;
    QRadioButton* denoiseRadio_ = nullptr;
    QWidget* enhanceOptions_ = nullptr;
    QSlider* strengthSlider_ = nullptr;
    QLabel* strengthLabel_ = nullptr;
    QSlider* tauSlider_ = nullptr;
    QLabel* tauLabel_ = nullptr;
    QComboBox* qualityCombo_ = nullptr;
    QComboBox* solverCombo_ = nullptr;
    QComboBox* deviceCombo_ = nullptr;
    QSpinBox* chunkSpin_ = nullptr;
    QSpinBox* seedSpin_ = nullptr;
    QCheckBox* gentleCheck_ = nullptr;
    QPushButton* startBtn_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;
    QPushButton* helpBtn_ = nullptr;
    QPushButton* openBtn_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* engineLabel_ = nullptr;
    QLabel* timeLabel_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
    QListWidget* resultList_ = nullptr;  // each item: the result, with its input in Qt::UserRole
    QPushButton* playOrigBtn_ = nullptr;
    QPushButton* playResultBtn_ = nullptr;
    QPushButton* stopBtn_ = nullptr;
    QTimer* tick_ = nullptr;

    // state
    pk::ChildProcess runner_;
    QSettings settings_;
    QStringList runInputs_;   // the files of the current run
    int fileIndex_ = 0;       // 0-based, the file being processed
    QString lastProblem_;
    QString logPath_;  // this run's log (enhance_audio's "Log:" line)
    int failures_ = 0;
    QStringList written_;
    QElapsedTimer runClock_;
    QString engineBackend_;
    QMediaPlayer* player_ = nullptr;
    QAudioOutput* audioOut_ = nullptr;
};
