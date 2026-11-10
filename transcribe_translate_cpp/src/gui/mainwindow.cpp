#include "mainwindow.h"
#include "translate_engine.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

#include "help_window.h"  // ../qt
#include "history_page.h"
#include "jobgate.h"
#include "model_files.h"
#include "models.h"  // ../translate: Translate's model files
#include "models_dialog.h"
#include "speech_page.h"
#include "transcribe_page.h"
#include "translate_page.h"

namespace {

// A tab's page in a scroll area: the window can be smaller than the page.
QScrollArea* scrolled(QWidget* page) {
    auto* s = new QScrollArea;
    s->setWidget(page);
    s->setWidgetResizable(true);
    s->setFrameShape(QFrame::NoFrame);
    return s;
}

bool isDocument(const QString& path) {
    static const QStringList ext = {QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("markdown"),
                                    QStringLiteral("srt"), QStringLiteral("docx"), QStringLiteral("pdf")};
    return ext.contains(QFileInfo(path).suffix().toLower());
}

}  // namespace

MainWindow::MainWindow(const QString& historyFile, QWidget* parent)
    : QWidget(parent), settings_(QStringLiteral("DavidV"), QStringLiteral("TranscribeTranslate")) {
    setWindowTitle(QApplication::applicationDisplayName());
    resize(1000, 920);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);

    tabs_ = new QTabWidget;
    transcribe_ = new TranscribePage;
    translate_ = new TranslatePage;
    speech_ = new SpeechPage;
    history_ = new HistoryPage(historyFile);
    tabs_->addTab(scrolled(transcribe_), tr("Transcribe"));
    tabs_->addTab(scrolled(translate_), tr("Translate"));
    tabs_->addTab(scrolled(speech_), tr("Speech translation"));
    tabs_->addTab(history_, tr("History"));
    tabs_->setTabToolTip(0, tr("Speech to text: .txt, subtitles, Word, PDF"));
    tabs_->setTabToolTip(1, tr("Translate text, .txt, .md, .srt, Word and PDF documents"));
    tabs_->setTabToolTip(2, tr("Audio or video to translated subtitles, and a dubbed soundtrack"));
    tabs_->setTabToolTip(3, tr("The jobs run so far, and the files they saved"));

    // Help, Free GPU memory, Model files and Quit: for every tab, next to the tabs.
    auto* corner = new QWidget;
    auto* ch = new QHBoxLayout(corner);
    ch->setContentsMargins(0, 0, 0, 2);
    auto* help = new QPushButton(tr("Help"));
    help->setShortcut(QKeySequence::HelpContents);  // F1
    help->setToolTip(tr("How each tab works, and the languages of each model (F1)"));
    connect(help, &QPushButton::clicked, this, [this] {
        pk::show_help_window(this, tr("%1 - Help").arg(QApplication::applicationDisplayName()), helpHtml());
    });
    modelsBtn_ = new QPushButton(tr("Model files"));
    modelsBtn_->setToolTip(tr("Show the downloaded models and where they are (and delete them)"));
    connect(modelsBtn_, &QPushButton::clicked, this, &MainWindow::showModels);
    // The Translate tab keeps its model loaded between jobs (translate_engine.h).
    freeGpuBtn_ = new QPushButton(tr("Free GPU memory"));
    connect(freeGpuBtn_, &QPushButton::clicked, this, [] { tt::TranslateEngine::instance().release(); });
    connect(&tt::TranslateEngine::instance(), &tt::TranslateEngine::loadedChanged, this, &MainWindow::updateFreeGpu);
    auto* quit = new QPushButton(tr("Quit"));
    quit->setShortcut(QKeySequence::Quit);  // Ctrl+Q
    quit->setToolTip(tr("Close the program (asks first if a job is running)"));
    connect(quit, &QPushButton::clicked, this, &QWidget::close);
    ch->addWidget(help);
    ch->addWidget(freeGpuBtn_);
    ch->addWidget(modelsBtn_);
    ch->addWidget(quit);
    tabs_->setCornerWidget(corner, Qt::TopRightCorner);
    root->addWidget(tabs_);

    // No deleting a model while a job may be using it.
    connect(&tt::JobGate::instance(), &tt::JobGate::changed, modelsBtn_, [this](bool busy) {
        modelsBtn_->setEnabled(!busy);
        updateFreeGpu();
    });
    updateFreeGpu();

    connect(transcribe_, &TranscribePage::jobFinished, history_, &HistoryPage::add);
    connect(translate_, &TranslatePage::jobFinished, history_, &HistoryPage::add);
    connect(speech_, &SpeechPage::jobFinished, history_, &HistoryPage::add);
    connect(transcribe_, &TranscribePage::translateRequested, this, [this](const QString& path) {
        tabs_->setCurrentIndex(1);
        translate_->openFile(path);
    });
    connect(history_, &HistoryPage::reuseInput, this, &MainWindow::reuse);

    tabs_->setCurrentIndex(std::clamp(settings_.value(QStringLiteral("tab"), 0).toInt(), 0, tabs_->count() - 1));
    restoreGeometry(settings_.value(QStringLiteral("geometry")).toByteArray());
}

void MainWindow::openFile(const QString& path, const QString& tab) {
    if (tab == QLatin1String("speech")) {
        tabs_->setCurrentIndex(2);
        speech_->setInputFile(path);
    } else if (tab == QLatin1String("translate") || (tab.isEmpty() && isDocument(path))) {
        tabs_->setCurrentIndex(1);
        translate_->openFile(path);
    } else {
        tabs_->setCurrentIndex(0);
        transcribe_->setInputFile(path);
    }
}

void MainWindow::reuse(const QString& kind, const QString& path) {
    if (kind == tr("Speech translation")) {
        tabs_->setCurrentIndex(2);
        speech_->setInputFile(path);
    } else if (kind == tr("Translate")) {
        tabs_->setCurrentIndex(1);
        translate_->openFile(path);
    } else {
        tabs_->setCurrentIndex(0);
        transcribe_->setInputFile(path);
    }
}

QString MainWindow::helpHtml() {
    QString h = QStringLiteral(
        "<h1>Transcribe &amp; Translate</h1>"
        "<p>Speech to text, translation and speech translation in one program. Everything runs on this computer; "
        "nothing is uploaded. Each model is downloaded once, the first time it is needed; <b>Model files</b> "
        "shows them and can delete them. The models are shared with the separate Transcribe, Translate and "
        "Speech Translate programs, so a model one of them downloaded is not downloaded again.</p>"
        "<ul>"
        "<li><a href=\"#transcribe\"><b>Transcribe</b></a>: the speech in an audio or video file as text, "
        "subtitles, a Word document or a PDF. <b>Translate...</b> takes the transcript to the Translate tab.</li>"
        "<li><a href=\"#translate\"><b>Translate</b></a>: text you type, or .txt, .md, .srt, Word and PDF files.</li>"
        "<li><a href=\"#speech\"><b>Speech translation</b></a>: an audio or video file to subtitles in another "
        "language, and optionally a dubbed soundtrack in the speaker's voice.</li>"
        "<li><b>History</b>: the jobs run so far, with the files they saved (open them, open their folder, or run "
        "the same input again).</li>"
        "</ul>"
        "<p>One job runs at a time, so each model has the graphics card to itself; the other tabs can be set up "
        "meanwhile. Dropping a file onto a tab chooses it as that tab's input.</p>"
        "<p>The Translate tab keeps its model loaded on the graphics card after a translation, so the next one "
        "starts at once instead of loading the model again. <b>Free GPU memory</b> unloads it, for a game or "
        "another program that needs the graphics card; it is also unloaded when another tab starts a job, before "
        "<b>Model files</b> opens, and when the program closes.</p><hr>");
    h += TranscribePage::helpHtml() + QStringLiteral("<hr>");
    h += TranslatePage::helpHtml() + QStringLiteral("<hr>");
    h += SpeechPage::helpHtml();
    return h;
}

void MainWindow::updateFreeGpu() {
    const QString model = tt::TranslateEngine::instance().loadedModel();
    freeGpuBtn_->setEnabled(!model.isEmpty() && !tt::JobGate::instance().busy());
    freeGpuBtn_->setToolTip(
        model.isEmpty()
            ? tr("Frees the graphics card's memory when Translate keeps a model loaded for the next translation "
                 "(nothing is loaded now)")
            : tr("%1 stays loaded on the graphics card after a translation, so the next one starts at once. "
                 "This frees its memory for other programs (it is freed anyway when another tab starts a job, "
                 "or when this program closes).").arg(model));
}

void MainWindow::showModels() {
    if (tt::JobGate::instance().busy()) return;
    tt::TranslateEngine::instance().release();  // a loaded model's file can't be deleted (Windows)
    // Only these programs' own downloads count as ours: CrispASR marks each with
    // a .src sidecar, Translate's are its model files.
    pk::ModelOwner translate;
    for (const auto& m : tl::model_table()) translate.names << QString::fromStdString(m.file);
    // The helper models of the faster mode (speculative decoding).
    for (const auto& m : tl::draft_model_table()) translate.names << QString::fromStdString(m.file);
    pk::ModelsDialog dlg({{tr("Speech recognition, spelling and voice models"),
                           pk::crispasr_models_dir(transcribe_->modelsDir()), pk::ModelOwner{true, {}}},
                          {tr("Translation models"), pk::translate_models_dir(translate_->modelsDir()), translate}},
                         this);
    dlg.exec();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    tt::JobGate& gate = tt::JobGate::instance();
    if (gate.busy() &&
        QMessageBox::question(this, QApplication::applicationDisplayName(),
                              tr("A job is running on the %1 tab. Stop it and quit?").arg(gate.owner())) !=
            QMessageBox::Yes) {
        event->ignore();
        return;
    }
    transcribe_->shutdown();
    translate_->shutdown();
    speech_->shutdown();
    settings_.setValue(QStringLiteral("tab"), tabs_->currentIndex());
    settings_.setValue(QStringLiteral("geometry"), saveGeometry());
    event->accept();
}
