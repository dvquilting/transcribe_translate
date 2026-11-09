// translate_page.h - the Translate tab of Transcribe & Translate: text and
// documents (.txt, .md, .srt, .docx) with "tt_engine translate".
#pragma once

#include <QElapsedTimer>
#include <QSettings>
#include <QStringList>
#include <QWidget>

#include "child_process.h"  // ../qt
#include "jobgate.h"

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

class TranslatePage : public QWidget {
    Q_OBJECT
public:
    explicit TranslatePage(QWidget* parent = nullptr);
    ~TranslatePage() override;
    void openFile(const QString& path);  // switch to the File tab with this input
    static QString helpHtml();  // this tab's part of the Help window
    QString modelsDir() const;  // the models folder chosen under Advanced ("" = the default)
    bool isRunning() const { return runner_.isActive(); }
    void shutdown();            // the window is closing: stop the job, save the settings

signals:
    void jobFinished(const tt::JobRecord& job);  // for the History tab

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void translateText();
    void translateFile();
    void cancel();
    void processFinished(int exitCode, bool cancelled);
    void processFailedToStart();
    void updateModelInfo();
    void swapLanguages();
    void browseInput();
    void browseOutput();
    void openOutputFolder();

private:
    enum class Mode { None, Text, File };

    void buildUi();
    void loadSettings();
    void saveSettings();
    QString cliPath() const;
    QStringList commonArguments() const;
    bool startProcess(const QStringList& args, Mode mode);
    void setRunning(bool running);
    void parseLine(const QString& line);
    void appendLog(const QString& text);
    void setEngine(const QString& backend, const QString& detail);
    void setDraftNote(const QString& note);  // the faster mode's state, after the engine
    void updateTiming();
    void resetTiming();
    QString plannedOutput() const;
    bool confirmOverwrite(QString& out);
    void reloadPartial();
    bool finishPdf();  // the translated PDF, from the engine's output (false: failed, see the status)

    // widgets
    QComboBox* modelCombo_ = nullptr;
    QLabel* modelDesc_ = nullptr;
    QComboBox* fromCombo_ = nullptr;
    QComboBox* toCombo_ = nullptr;
    QLabel* langNote_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QPlainTextEdit* sourceEdit_ = nullptr;
    QPlainTextEdit* resultEdit_ = nullptr;
    QPushButton* translateTextBtn_ = nullptr;
    QLineEdit* inputEdit_ = nullptr;
    QLineEdit* outputEdit_ = nullptr;
    QPushButton* translateFileBtn_ = nullptr;
    QPushButton* openFolderBtn_ = nullptr;
    QGroupBox* advancedBox_ = nullptr;
    QWidget* advancedBody_ = nullptr;
    QComboBox* deviceCombo_ = nullptr;
    QSpinBox* threadsSpin_ = nullptr;
    QCheckBox* contextCheck_ = nullptr;
    QComboBox* draftCombo_ = nullptr;  // the faster mode: --draft auto|<helper>|off (faster_mode.h)
    QLineEdit* modelFileEdit_ = nullptr;
    QLineEdit* modelsDirEdit_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* engineLabel_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLineEdit* wordsEdit_ = nullptr;
    QLineEdit* loadEdit_ = nullptr;
    QLineEdit* translateEdit_ = nullptr;
    QLineEdit* speedEdit_ = nullptr;
    QLineEdit* totalEdit_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
    QTimer* tick_ = nullptr;

    // state
    QSettings settings_;
    // tt_engine translate; its arguments are built once, when the run starts.
    pk::ChildProcess runner_;
    Mode mode_ = Mode::None;
    QTemporaryDir* tempDir_ = nullptr;
    QString textOutput_;   // output file of a text-box run
    QString written_;      // file the last run wrote
    QString runInput_;     // the input file of the current/last run (History)
    // PDFs: a PDF is translated as Markdown (pdf_files.h), and a .pdf output is
    // made here from the engine's output when it is done.
    enum class PdfFrom { None, Pdf, Markdown, Text };
    PdfFrom pdfFrom_ = PdfFrom::None;  // what the engine's output is, when a PDF is to be made
    QString pdfOutput_;                // the PDF to write
    QString pdfEngineOutput_;          // the engine's output (in tempDir_)
    QString pdfTitle_;
    QString lastError_;
    QString engineHtml_;  // the Compute line without the faster mode's note
    QString draftNote_;   // the faster mode's note for this run
    bool downloading_ = false;
    QElapsedTimer clock_;           // whole run
    qint64 translateStartMs_ = -1;  // clock_ time when translation began (after loading)
    double translateSeconds_ = -1;  // final, from tt_engine translate
    int words_ = -1;                // source words
    int piecesDone_ = 0, piecesTotal_ = 0;
    double fraction_ = -1;          // share translated ("progress: translating"), -1 = none yet
    void showTranslating();         // the status line while translating
    bool finished_ = false;
};
