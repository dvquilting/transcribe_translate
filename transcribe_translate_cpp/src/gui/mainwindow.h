// mainwindow.h - Transcribe & Translate: one window with a tab for each job -
// Transcribe (speech to text), Translate (text and documents), Speech
// translation (translated subtitles and dubbing) - and the History of jobs.
// Help, Model files and Quit sit next to the tabs, for all of them.
#pragma once

#include <QSettings>
#include <QWidget>

class QPushButton;
class QTabWidget;
class HistoryPage;
class SpeechPage;
class TranscribePage;
class TranslatePage;

class MainWindow : public QWidget {
    Q_OBJECT
public:
    // `historyFile`: for tests ("" = the program's data folder).
    explicit MainWindow(const QString& historyFile = QString(), QWidget* parent = nullptr);

    // A file from the command line (or dropped on the program's icon): media goes
    // to Transcribe, documents to Translate - or to the tab named by `tab`
    // ("transcribe", "translate", "speech").
    void openFile(const QString& path, const QString& tab = QString());

    static QString helpHtml();  // the whole Help window

    TranscribePage* transcribePage() const { return transcribe_; }
    TranslatePage* translatePage() const { return translate_; }
    SpeechPage* speechPage() const { return speech_; }
    HistoryPage* historyPage() const { return history_; }
    QTabWidget* tabs() const { return tabs_; }

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void showModels();
    void reuse(const QString& kind, const QString& path);

    QTabWidget* tabs_ = nullptr;
    TranscribePage* transcribe_ = nullptr;
    TranslatePage* translate_ = nullptr;
    SpeechPage* speech_ = nullptr;
    HistoryPage* history_ = nullptr;
    QPushButton* modelsBtn_ = nullptr;
    QPushButton* freeGpuBtn_ = nullptr;  // ends the Translate tab's engine and its loaded model
    void updateFreeGpu();
    QSettings settings_;
};
