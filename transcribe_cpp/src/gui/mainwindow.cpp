#include "mainwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QKeySequence>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QLocale>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextDocument>

#include "help_window.h"  // ../common/qt
#include "model_catalog.h"  // ../common/src
#include "model_files.h"  // ../common/qt
#include "models_dialog.h"

namespace {

QString absolutePath(QString path) {
    path = path.trimmed();
    if (path.size() >= 2 && path.startsWith(QLatin1Char('"')) && path.endsWith(QLatin1Char('"')))
        path = path.mid(1, path.size() - 2);
    return path.isEmpty() ? QString() : QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
}


struct ModelChoice {
    const char* key;
    const char* name;
    const char* desc;
};

const ModelChoice kModels[] = {
    {"parakeet-ultra", "Parakeet Ultra", "Fastest. Very good accuracy. Handles long files natively."},
    {"ark", "ARK-ASR-3B",
     "Most accurate. Slower; audio is processed in 30 s chunks. Writes no punctuation, so a small punctuation "
     "model (163 MB) adds it. (CrispASR support is new.)"},
    {"qwen", "Qwen3-ASR-1.7B", "Accurate and multilingual. Audio is processed in 30 s chunks."},
    {"granite", "Granite Speech 4.1 Plus",
     "IBM. Very accurate; English, French, German, Spanish, Portuguese. Audio is processed in 30 s chunks."},
    {"cohere", "Cohere Transcribe",
     "Very accurate; 14 languages. Choose the spoken language (it can't detect it). 30 s chunks."},
    {"parakeet-redux", "Parakeet Redux", "Small ternary Parakeet variant."},
};

// A PDF of one of transcribe_multi's text results (transcript, corrected copy or
// summary): the title, the AI note in italics when the text starts with one
// ("[Summary written by ...]"), then the paragraphs. Qt lays out the text with the
// system's fonts (embedded in the PDF), so every script prints.
bool writePdf(const QString& textPath, const QString& pdfPath, const QString& title, QString* error) {
    QFile in(textPath);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error) *error = in.errorString();
        return false;
    }
    const QString text = QString::fromUtf8(in.readAll()).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    QStringList paragraphs;
    for (const QString& p : text.split(QRegularExpression(QStringLiteral("\n\\s*\n"))))
        if (!p.trimmed().isEmpty()) paragraphs << p.trimmed();
    QString note;
    if (!paragraphs.isEmpty() && paragraphs.first().startsWith(QLatin1Char('[')) &&
        paragraphs.first().endsWith(QLatin1Char(']')))
        note = paragraphs.takeFirst();

    QString html = QStringLiteral("<h1>%1</h1>").arg(title.toHtmlEscaped());
    if (!note.isEmpty())
        html += QStringLiteral("<p style=\"color:#595959\"><i>%1</i></p>").arg(note.toHtmlEscaped());
    for (const QString& p : paragraphs)
        html += QStringLiteral("<p>%1</p>").arg(p.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));

    QSaveFile out(pdfPath);  // never a half-written PDF
    if (!out.open(QIODevice::WriteOnly)) {
        if (error) *error = out.errorString();
        return false;
    }
    {
        QPdfWriter pdf(&out);
        pdf.setTitle(title);
        pdf.setCreator(QStringLiteral("Transcribe"));
        // Letter in the US and Canada, A4 elsewhere. QTextDocument::print adds 2 cm
        // margins (and the page numbers) inside these 5 mm: 2.5 cm in all.
        const QLocale::Country c = QLocale().territory();
        const bool letter = c == QLocale::UnitedStates || c == QLocale::Canada;
        pdf.setPageLayout(QPageLayout(QPageSize(letter ? QPageSize::Letter : QPageSize::A4), QPageLayout::Portrait,
                                      QMarginsF(5, 5, 5, 5), QPageLayout::Millimeter));
        QTextDocument doc;
        QFont font = doc.defaultFont();
        font.setPointSizeF(11);
        doc.setDefaultFont(font);
        doc.setHtml(html);
        doc.print(&pdf);
    }
    if (!out.commit()) {
        if (error) *error = out.errorString();
        return false;
    }
    return true;
}

// "Auto-detect" first, then alphabetical.
const std::pair<const char*, const char*> kLanguages[] = {
    {"auto", "Auto-detect"}, {"ar", "Arabic"},    {"zh", "Chinese"},    {"nl", "Dutch"},   {"en", "English"},
    {"fr", "French"},        {"de", "German"},    {"el", "Greek"},      {"it", "Italian"}, {"ja", "Japanese"},
    {"ko", "Korean"},        {"pl", "Polish"},    {"pt", "Portuguese"}, {"es", "Spanish"}, {"vi", "Vietnamese"}};

// The Help window's page: how a transcription works and what each model handles.
// The language column comes from the shared model table (../common/src/model_catalog.cpp).
QString helpHtml() {
    QString h = QStringLiteral(
        "<h2>Transcribe</h2>"
        "<p>Transcribe writes down the speech in an audio or video file, as text, as subtitles (.srt) or both. "
        "Everything runs on this computer; nothing is uploaded. Each model is downloaded once, the first time it "
        "is needed (<b>Model files</b> shows them and can delete them).</p>"
        "<p>Choose a file (or drop one onto the window), a model and an output format, then press "
        "<b>Transcribe</b>. <b>Cancel</b> stops it; the <b>Log</b> tab shows what is happening and the "
        "<b>Transcript</b> tab the result.</p>"

        "<h3>Options</h3>"
        "<ul>"
        "<li><b>Model:</b> Parakeet works on the whole file at once and detects the language itself. ARK, Qwen, "
        "Granite and Cohere listen in chunks (30 seconds by default) and are given the <b>Language</b> you choose; "
        "Cohere can't detect it, so it needs it.</li>"
        "<li><b>Save as:</b> any of plain text (in paragraphs), subtitles with the time of each line, a Word "
        "document and a PDF. The corrected copy and the summary are saved in the same document formats. The files "
        "go next to the input unless you choose another output file.</li>"
        "<li><b>Ask ARK to write punctuation:</b> ARK normally writes none, and a small punctuation model adds it "
        "afterwards; this asks ARK itself (experimental).</li>"
        "</ul>"

        "<h3>AI post-processing (optional)</h3>"
        "<p>A local language model (Qwen3-4B, 2.5 GB) works on the finished transcript. The transcript itself is "
        "always kept unchanged; AI output can contain mistakes.</p>"
        "<ul>"
        "<li><b>Smart paragraphs:</b> starts new paragraphs where the topic changes or the speaker pauses. Only the "
        "line breaks change, never the words.</li>"
        "<li><b>Fix spelling &amp; grammar:</b> saves a corrected copy as <i>name.clean.txt</i>.</li>"
        "<li><b>Write a summary:</b> saves a short overview with key points as <i>name.summary.txt</i>.</li>"
        "</ul>"
        "<p>Qwen3-4B handles the common languages well and smaller languages less reliably.</p>"

        "<h3>Advanced</h3>"
        "<ul>"
        "<li><b>Device</b> chooses where the models run: <i>auto</i> uses an NVIDIA graphics card with CUDA, other "
        "graphics cards with Vulkan, or the processor. <b>CPU threads</b> limits the processor threads used.</li>"
        "<li><b>Chunk length:</b> how much audio the chunked models hear at a time (5-30 s).</li>"
        "<li><b>Sentences per paragraph:</b> the paragraph size of the text (and the fallback for Smart "
        "paragraphs).</li>"
        "<li><b>Punctuation:</b> <i>Automatic</i> adds punctuation and capitals with a small model (163 MB) for "
        "ARK and for parts that come back without any; <i>Always restore</i> does it for everything; <i>Off</i> "
        "never.</li>"
        "<li><b>Word-level subtitle timing:</b> for the chunked models' subtitles, an extra model (986 MB) times "
        "each word, which gives better-timed subtitles.</li>"
        "<li><b>Model file override</b>, <b>AI model file</b> and <b>Models folder</b> use your own model files or "
        "another download folder.</li>"
        "</ul>"

        "<h2>Languages of each model</h2><table cellpadding=\"4\">");
    for (const auto& m : pk::asr_models())
        h += pk::help_row(QString::fromStdString(m.display).toHtmlEscaped(),
                          QString::fromStdString(m.description).toHtmlEscaped());
    h += QStringLiteral(
        "</table><p>The <b>Language</b> menu lists the languages you can choose for the chunked models. A language "
        "a model doesn't know is usually transcribed badly; Qwen3-ASR covers the most languages.</p>");
    return h;
}

// Models whose audio we cut into chunks (and that take a language).
bool isChunkedModel(const QString& key) {
    return key == QLatin1String("ark") || key == QLatin1String("qwen") || key == QLatin1String("granite") ||
           key == QLatin1String("cohere");
}

const char* kMediaFilter =
    "Audio/Video (*.mp3 *.wav *.m4a *.flac *.ogg *.opus *.aac *.wma *.mp4 *.mkv *.mov *.avi *.webm *.m4v "
    "*.wmv);;All files (*)";

QWidget* row(QLineEdit* edit, QPushButton* button) {
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(edit, 1);
    h->addWidget(button);
    return w;
}

void selectData(QComboBox* combo, const QVariant& value) {
    const int i = combo->findData(value);
    if (i >= 0) combo->setCurrentIndex(i);
}

// 3725.4 s -> "1:02:05.4", 65.4 s -> "1:05.4"
QString formatDuration(double seconds) {
    if (seconds < 0) return QStringLiteral("\u2014");
    const qint64 tenths = qRound64(seconds * 10.0);
    const qint64 h = tenths / 36000, m = (tenths / 600) % 60, sec = (tenths / 10) % 60, t = tenths % 10;
    if (h > 0)
        return QStringLiteral("%1:%2:%3.%4").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(sec, 2, 10, QLatin1Char('0')).arg(t);
    return QStringLiteral("%1:%2.%3").arg(m).arg(sec, 2, 10, QLatin1Char('0')).arg(t);
}

QLineEdit* readOnlyField(int minChars) {
    auto* e = new QLineEdit;
    e->setReadOnly(true);
    e->setFocusPolicy(Qt::NoFocus);
    e->setAlignment(Qt::AlignRight);
    e->setMinimumWidth(e->fontMetrics().horizontalAdvance(QLatin1Char('0')) * minChars + 16);
    e->setText(QStringLiteral("\u2014"));
    return e;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent), settings_(QStringLiteral("DavidV"), QStringLiteral("TranscribeMultiCpp")) {
    setWindowTitle(tr("Transcribe"));
    setAcceptDrops(true);
    resize(820, 780);
    connect(&runner_, &pk::ChildProcess::outputReceived, this, &MainWindow::appendLog);
    connect(&runner_, &pk::ChildProcess::lineReceived, this, &MainWindow::parseLine);
    connect(&runner_, &pk::ChildProcess::finished, this, &MainWindow::processFinished);
    connect(&runner_, &pk::ChildProcess::failedToStart, this, &MainWindow::processFailedToStart);
    buildUi();
    loadSettings();
    updateEnabledState();
}

MainWindow::~MainWindow() {
    runner_.stop();  // explicit: before the widgets the handlers use go away
}

void MainWindow::buildUi() {
    auto* root = new QVBoxLayout(this);

    // --- Files -----------------------------------------------------------
    auto* files = filesBox_ = new QGroupBox(tr("Files"));
    auto* ff = new QFormLayout(files);
    inputEdit_ = new QLineEdit;
    inputEdit_->setPlaceholderText(tr("Choose a file or drop one onto this window"));
    auto* browseIn = new QPushButton(tr("Browse..."));
    connect(browseIn, &QPushButton::clicked, this, &MainWindow::browseInput);
    ff->addRow(tr("Input:"), row(inputEdit_, browseIn));

    outputEdit_ = new QLineEdit;
    outputEdit_->setPlaceholderText(tr("(default: next to the input file)"));
    auto* browseOut = new QPushButton(tr("Browse..."));
    connect(browseOut, &QPushButton::clicked, this, &MainWindow::browseOutput);
    ff->addRow(tr("Output:"), row(outputEdit_, browseOut));
    root->addWidget(files);

    // --- Options ---------------------------------------------------------
    auto* opts = optionsBox_ = new QGroupBox(tr("Options"));
    auto* of = new QFormLayout(opts);
    modelCombo_ = new QComboBox;
    for (const auto& m : kModels) modelCombo_->addItem(m.name, QString::fromLatin1(m.key));
    modelDesc_ = new QLabel;
    modelDesc_->setStyleSheet(QStringLiteral("color: gray;"));
    modelDesc_->setWordWrap(true);
    of->addRow(tr("Model:"), modelCombo_);
    of->addRow(QString(), modelDesc_);
    arkPunctCheck_ = new QCheckBox(tr("Ask ARK to write punctuation (experimental)"));
    arkPunctCheck_->setToolTip(
        tr("Changes ARK's instruction to ask for punctuation and capitals, instead of adding them afterwards with "
           "the punctuation model. ARK was trained with a fixed instruction, so results may vary; parts that "
           "still come back without punctuation get the punctuation model."));
    of->addRow(QString(), arkPunctCheck_);

    // Output formats: any combination, but never none.
    txtCheck_ = new QCheckBox(tr("Text (.txt)"));
    srtCheck_ = new QCheckBox(tr("Subtitles (.srt)"));
    docxCheck_ = new QCheckBox(tr("Word (.docx)"));
    pdfCheck_ = new QCheckBox(tr("PDF (.pdf)"));
    srtCheck_->setToolTip(tr("Subtitles with the time of each line"));
    docxCheck_->setToolTip(tr("The transcript as a Word document (the corrected copy and summary too)"));
    pdfCheck_->setToolTip(tr("The transcript as a PDF (the corrected copy and summary too)"));
    auto* formatRow = new QHBoxLayout;
    formatRow->setContentsMargins(0, 0, 0, 0);
    for (QCheckBox* c : {txtCheck_, srtCheck_, docxCheck_, pdfCheck_}) {
        formatRow->addWidget(c);
        connect(c, &QCheckBox::toggled, this, [this, c](bool on) {
            if (!on && !txtCheck_->isChecked() && !srtCheck_->isChecked() && !docxCheck_->isChecked() &&
                !pdfCheck_->isChecked())
                c->setChecked(true);  // the last one stays
            updateEnabledState();
        });
    }
    formatRow->addStretch();
    auto* formatBox = new QWidget;
    formatBox->setLayout(formatRow);
    of->addRow(tr("Save as:"), formatBox);

    languageCombo_ = new QComboBox;
    for (const auto& [k, n] : kLanguages) languageCombo_->addItem(n, QString::fromLatin1(k));
    languageCombo_->setToolTip(tr("Spoken language, used by ARK, Qwen, Granite and Cohere (Cohere needs it set). "
                                  "Parakeet detects it automatically."));
    of->addRow(tr("Language:"), languageCombo_);
    root->addWidget(opts);

    // --- AI post-processing ----------------------------------------------
    auto* ai = aiBox_ = new QGroupBox(tr("AI post-processing (optional)"));
    auto* aiLayout = new QVBoxLayout(ai);
    auto* aiChecks = new QHBoxLayout;
    polishCheck_ = new QCheckBox(tr("Fix spelling && grammar"));
    polishCheck_->setToolTip(tr("Also save a corrected copy of the transcript (name.clean.txt). "
                                "The original transcript is always kept unchanged."));
    summaryCheck_ = new QCheckBox(tr("Write a summary"));
    summaryCheck_->setToolTip(tr("Also save a short overview with key points (name.summary.txt)."));
    smartParaCheck_ = new QCheckBox(tr("Smart paragraphs"));
    smartParaCheck_->setToolTip(tr("Start new paragraphs where the topic changes or the speaker pauses, "
                                   "instead of every N sentences. Only the line breaks change, never the words."));
    aiChecks->addWidget(smartParaCheck_);
    aiChecks->addSpacing(16);
    aiChecks->addWidget(polishCheck_);
    aiChecks->addSpacing(16);
    aiChecks->addWidget(summaryCheck_);
    aiChecks->addStretch();
    aiLayout->addLayout(aiChecks);
    aiNote_ = new QLabel(tr("Uses a local language model (Qwen3-4B, downloaded once, 2.5 GB). It runs on this "
                            "computer; nothing is uploaded. AI output can contain mistakes, so check anything "
                            "important against the original transcript."));
    aiNote_->setStyleSheet(QStringLiteral("color: gray;"));
    aiNote_->setWordWrap(true);
    aiLayout->addWidget(aiNote_);
    root->addWidget(ai);
    connect(polishCheck_, &QCheckBox::toggled, this, &MainWindow::updateEnabledState);
    connect(summaryCheck_, &QCheckBox::toggled, this, &MainWindow::updateEnabledState);
    connect(smartParaCheck_, &QCheckBox::toggled, this, &MainWindow::updateEnabledState);

    connect(modelCombo_, &QComboBox::currentIndexChanged, this, &MainWindow::updateEnabledState);
    connect(languageCombo_, &QComboBox::currentIndexChanged, this, &MainWindow::updateEnabledState);

    // --- Advanced (collapsible) -----------------------------------------
    advancedBox_ = new QGroupBox(tr("Advanced"));
    advancedBox_->setCheckable(true);
    advancedBox_->setChecked(false);
    auto* advLayout = new QVBoxLayout(advancedBox_);
    advancedBody_ = new QWidget;
    advLayout->addWidget(advancedBody_);
    auto* af = new QFormLayout(advancedBody_);
    af->setContentsMargins(0, 0, 0, 0);

    deviceCombo_ = new QComboBox;
    deviceCombo_->addItems({QStringLiteral("auto"), QStringLiteral("cuda"), QStringLiteral("vulkan"),
                            QStringLiteral("cpu")});
    af->addRow(tr("Device:"), deviceCombo_);

    threadsSpin_ = new QSpinBox;
    threadsSpin_->setRange(0, 64);
    threadsSpin_->setSpecialValueText(tr("auto"));
    af->addRow(tr("CPU threads:"), threadsSpin_);

    chunkSpin_ = new QDoubleSpinBox;
    chunkSpin_->setRange(5.0, 30.0);
    chunkSpin_->setDecimals(0);
    chunkSpin_->setSuffix(tr(" s"));
    chunkSpin_->setValue(30.0);
    af->addRow(tr("Chunk length:"), chunkSpin_);

    paragraphSpin_ = new QSpinBox;
    paragraphSpin_->setRange(1, 50);
    paragraphSpin_->setValue(5);
    paragraphSpin_->setToolTip(tr("Paragraph size in the transcript. With Smart paragraphs, used only where the "
                                  "model gives no usable answer."));
    af->addRow(tr("Sentences per paragraph:"), paragraphSpin_);

    punctCombo_ = new QComboBox;
    punctCombo_->addItem(tr("Automatic"), QStringLiteral("auto"));
    punctCombo_->addItem(tr("Always restore"), QStringLiteral("on"));
    punctCombo_->addItem(tr("Off"), QStringLiteral("off"));
    punctCombo_->setToolTip(tr("Add punctuation and capitals with a small model (PCS, 163 MB, downloaded once).\n"
                               "Automatic: always for ARK-ASR, which writes none (unless asked to); otherwise only for "
                               "parts that come back without punctuation."));
    af->addRow(tr("Punctuation:"), punctCombo_);

    alignCheck_ = new QCheckBox(tr("Word-level subtitle timing (forced aligner)"));
    alignCheck_->setChecked(true);
    af->addRow(QString(), alignCheck_);

    modelFileEdit_ = new QLineEdit;
    modelFileEdit_->setPlaceholderText(tr("(download the default .gguf)"));
    auto* browseModel = new QPushButton(tr("Browse..."));
    connect(browseModel, &QPushButton::clicked, this, [this] {
        const QString p = QFileDialog::getOpenFileName(this, tr("Model file"), modelFileEdit_->text(),
                                                       tr("GGUF models (*.gguf);;All files (*)"));
        if (!p.isEmpty()) modelFileEdit_->setText(QDir::toNativeSeparators(p));
    });
    af->addRow(tr("Model file override:"), row(modelFileEdit_, browseModel));

    modelsDirEdit_ = new QLineEdit;
    modelsDirEdit_->setPlaceholderText(tr("(default: %USERPROFILE%\\.cache\\crispasr)"));
    auto* browseDir = new QPushButton(tr("Browse..."));
    connect(browseDir, &QPushButton::clicked, this, &MainWindow::browseModelsDir);
    af->addRow(tr("Models folder:"), row(modelsDirEdit_, browseDir));

    llmFileEdit_ = new QLineEdit;
    llmFileEdit_->setPlaceholderText(tr("(download Qwen3-4B-Instruct)"));
    llmFileEdit_->setToolTip(tr("Any instruction-tuned chat model in GGUF format, used for the AI post-processing"));
    auto* browseLlm = new QPushButton(tr("Browse..."));
    connect(browseLlm, &QPushButton::clicked, this, [this] {
        const QString p = QFileDialog::getOpenFileName(this, tr("Language model file"), llmFileEdit_->text(),
                                                       tr("GGUF models (*.gguf);;All files (*)"));
        if (!p.isEmpty()) llmFileEdit_->setText(QDir::toNativeSeparators(p));
    });
    af->addRow(tr("AI model file:"), row(llmFileEdit_, browseLlm));

    advancedBody_->setVisible(false);
    connect(advancedBox_, &QGroupBox::toggled, this, [this](bool on) {
        advancedBody_->setVisible(on);
        updateEnabledState();
    });
    root->addWidget(advancedBox_);

    // --- Run controls ----------------------------------------------------
    auto* runRow = new QHBoxLayout;
    startBtn_ = new QPushButton(tr("Transcribe"));
    startBtn_->setDefault(true);
    cancelBtn_ = new QPushButton(tr("Cancel"));
    cancelBtn_->setEnabled(false);
    openBtn_ = new QPushButton(tr("Open output folder"));
    openBtn_->setEnabled(false);
    connect(startBtn_, &QPushButton::clicked, this, &MainWindow::start);
    connect(cancelBtn_, &QPushButton::clicked, this, &MainWindow::cancel);
    connect(openBtn_, &QPushButton::clicked, this, &MainWindow::openOutputFolder);
    runRow->addWidget(startBtn_);
    runRow->addWidget(cancelBtn_);
    runRow->addStretch();
    helpBtn_ = new QPushButton(tr("Help"));
    helpBtn_->setShortcut(QKeySequence::HelpContents);  // F1
    helpBtn_->setToolTip(tr("How a transcription works, and the languages of each model (F1)"));
    connect(helpBtn_, &QPushButton::clicked, this,
            [this] { pk::show_help_window(this, tr("Transcribe - Help"), helpHtml()); });
    runRow->addWidget(helpBtn_);
    infoBtn_ = new QPushButton(tr("Model files"));
    infoBtn_->setToolTip(tr("Show the downloaded models and where they are (and delete them)"));
    connect(infoBtn_, &QPushButton::clicked, this, &MainWindow::showModels);
    runRow->addWidget(infoBtn_);
    runRow->addWidget(openBtn_);
    auto* quitBtn = new QPushButton(tr("Quit"));
    quitBtn->setShortcut(QKeySequence::Quit);  // Ctrl+Q
    quitBtn->setToolTip(tr("Close the program (asks first if a transcription is running)"));
    connect(quitBtn, &QPushButton::clicked, this, &QWidget::close);  // goes through closeEvent()
    runRow->addWidget(quitBtn);
    root->insertLayout(0, runRow);  // the action buttons go at the top of the window

    statusLabel_ = new QLabel(tr("Ready."));
    engineLabel_ = new QLabel;
    engineLabel_->setTextFormat(Qt::RichText);
    auto* statusRow = new QHBoxLayout;
    statusRow->addWidget(statusLabel_, 1);
    statusRow->addWidget(engineLabel_);
    progress_ = new QProgressBar;
    progress_->setRange(0, 1);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    root->addLayout(statusRow);
    root->addWidget(progress_);

    // --- Timing: audio length vs. transcription time ---------------------
    audioLengthEdit_ = readOnlyField(10);
    audioLengthEdit_->setToolTip(tr("Length of the audio in the input file"));
    elapsedEdit_ = readOnlyField(10);
    elapsedEdit_->setToolTip(tr("Wall-clock time of the transcription run, including model loading "
                                "(and the one-time model download on first use)"));
    ratioEdit_ = readOnlyField(12);
    ratioEdit_->setToolTip(tr("Audio length \u00F7 transcription time. 20\u00D7 means one hour of audio "
                              "took three minutes."));
    auto* timingRow = new QHBoxLayout;
    timingRow->addWidget(new QLabel(tr("Audio length:")));
    timingRow->addWidget(audioLengthEdit_);
    timingRow->addSpacing(12);
    timingRow->addWidget(new QLabel(tr("Transcription time:")));
    timingRow->addWidget(elapsedEdit_);
    timingRow->addSpacing(12);
    timingRow->addWidget(new QLabel(tr("Speed:")));
    timingRow->addWidget(ratioEdit_);
    postEdit_ = readOnlyField(10);
    postEdit_->setToolTip(tr("Time spent on smart paragraphs, spelling/grammar correction and the summary"));
    postLabel_ = new QLabel(tr("AI post-processing:"));
    timingRow->addSpacing(12);
    timingRow->addWidget(postLabel_);
    timingRow->addWidget(postEdit_);
    timingRow->addStretch();
    root->addLayout(timingRow);

    tick_ = new QTimer(this);
    tick_->setInterval(200);
    connect(tick_, &QTimer::timeout, this, [this] { updateTiming(false); });

    // --- Log / transcript ------------------------------------------------
    tabs_ = new QTabWidget;
    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    logView_->setMaximumBlockCount(5000);
    transcriptView_ = new QPlainTextEdit;
    transcriptView_->setReadOnly(true);
    tabs_->addTab(logView_, tr("Log"));
    tabs_->addTab(transcriptView_, tr("Transcript"));
    cleanView_ = new QPlainTextEdit;
    cleanView_->setReadOnly(true);
    summaryView_ = new QPlainTextEdit;
    summaryView_->setReadOnly(true);
    tabs_->addTab(cleanView_, tr("Corrected"));
    tabs_->addTab(summaryView_, tr("Summary"));
    tabs_->setTabVisible(2, false);
    tabs_->setTabVisible(3, false);
    root->addWidget(tabs_, 1);
}

void MainWindow::updateEnabledState() {
    const QString key = modelCombo_->currentData().toString();
    for (const auto& m : kModels)
        if (key == QLatin1String(m.key)) modelDesc_->setText(tr(m.desc));
    const bool chunked = isChunkedModel(key);
    arkPunctCheck_->setVisible(key == QLatin1String("ark"));
    // Cohere can't detect the language: don't leave it on "Auto-detect".
    if (key == QLatin1String("cohere") && languageCombo_->currentData().toString() == QLatin1String("auto"))
        selectData(languageCombo_, QStringLiteral("en"));
    const bool srt = srtCheck_->isChecked();
    chunkSpin_->setEnabled(chunked);
    alignCheck_->setEnabled(chunked && srt);
    languageCombo_->setEnabled(chunked);
    const bool ai = postProcessing();
    aiNote_->setVisible(ai);
    llmFileEdit_->setEnabled(ai);
    if (!runner_.isRunning()) {
        postLabel_->setVisible(ai);
        postEdit_->setVisible(ai);
    }
}

bool MainWindow::postProcessing() const {
    return polishCheck_->isChecked() || summaryCheck_->isChecked() || smartParaCheck_->isChecked();
}

// ---------------------------------------------------------------- files
void MainWindow::browseInput() {
    QString start = inputEdit_->text();
    if (start.isEmpty()) start = settings_.value(QStringLiteral("lastDir"), QDir::homePath()).toString();
    const QString p = QFileDialog::getOpenFileName(this, tr("Choose audio or video file"), start, tr(kMediaFilter));
    if (!p.isEmpty()) setInputFile(p);
}

void MainWindow::browseOutput() {
    QString start = outputEdit_->text().isEmpty() ? inputEdit_->text() : outputEdit_->text();
    const QString p = QFileDialog::getSaveFileName(this, tr("Output file (extension is set by the format)"), start,
                                                   tr("Transcript (*.txt *.srt *.docx *.pdf);;All files (*)"), nullptr,
                                                   QFileDialog::DontConfirmOverwrite);  // checked when starting
    if (!p.isEmpty()) outputEdit_->setText(QDir::toNativeSeparators(p));
}

void MainWindow::browseModelsDir() {
    const QString d = QFileDialog::getExistingDirectory(this, tr("Models folder"), modelsDirEdit_->text());
    if (!d.isEmpty()) modelsDirEdit_->setText(QDir::toNativeSeparators(d));
}

void MainWindow::setInputFile(const QString& path) {
    inputEdit_->setText(absolutePath(path));
    settings_.setValue(QStringLiteral("lastDir"), QFileInfo(path).absolutePath());
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() && !runner_.isActive()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    if (runner_.isActive()) return;
    for (const QUrl& u : event->mimeData()->urls()) {
        if (u.isLocalFile()) {
            setInputFile(u.toLocalFile());
            break;
        }
    }
}

// ------------------------------------------------------------------ run
QString MainWindow::cliPath() const {
#ifdef Q_OS_WIN
    return QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("transcribe_multi.exe"));
#else
    return QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("transcribe_multi"));
#endif
}

QStringList MainWindow::buildArguments(const QString& input) const {
    const QString model = modelCombo_->currentData().toString();
    QStringList a{input,
                  QStringLiteral("-m"), model,
                  QStringLiteral("-f"), cliFormats(),
                  QStringLiteral("--language"), languageCombo_->currentData().toString(),
                  QStringLiteral("--sentences-per-paragraph"), QString::number(paragraphSpin_->value())};
    if (!outputEdit_->text().trimmed().isEmpty()) a << QStringLiteral("-o") << absolutePath(outputEdit_->text());
    if (pdfCheck_->isChecked() && textDir_) a << QStringLiteral("--text-dir") << QDir::toNativeSeparators(textDir_->path());
    if (deviceCombo_->currentText() != QLatin1String("auto"))
        a << QStringLiteral("--device") << deviceCombo_->currentText();
    if (threadsSpin_->value() > 0) a << QStringLiteral("--threads") << QString::number(threadsSpin_->value());
    if (isChunkedModel(model)) {
        a << QStringLiteral("--chunk-seconds") << QString::number(chunkSpin_->value(), 'g', 4);
        if (!alignCheck_->isChecked()) a << QStringLiteral("--no-align");
    }
    if (!modelFileEdit_->text().trimmed().isEmpty())
        a << QStringLiteral("--model-file") << absolutePath(modelFileEdit_->text());
    if (!modelsDirEdit_->text().trimmed().isEmpty())
        a << QStringLiteral("--models-dir") << absolutePath(modelsDirEdit_->text());
    if (polishCheck_->isChecked()) a << QStringLiteral("--polish");
    if (summaryCheck_->isChecked()) a << QStringLiteral("--summary");
    if (smartParaCheck_->isChecked()) a << QStringLiteral("--smart-paragraphs");
    if (model == QLatin1String("ark") && arkPunctCheck_->isChecked()) a << QStringLiteral("--ark-punctuation");
    if (punctCombo_->currentData().toString() != QLatin1String("auto"))
        a << QStringLiteral("--punctuation") << punctCombo_->currentData().toString();
    if (postProcessing() && !llmFileEdit_->text().trimmed().isEmpty())
        a << QStringLiteral("--llm-file") << absolutePath(llmFileEdit_->text());
    return a;
}

void MainWindow::start() {
    if (runner_.isActive()) return;
    QString input = inputEdit_->text().trimmed();
    if (input.size() >= 2 && input.startsWith(QLatin1Char('"')) && input.endsWith(QLatin1Char('"')))
        input = input.mid(1, input.size() - 2);
    input = absolutePath(input);
    if (!QFileInfo(input).isFile()) {
        QMessageBox::warning(this, tr("Transcribe"), tr("Please choose an existing input file."));
        return;
    }
    if (!QFileInfo::exists(cliPath())) {
        QMessageBox::critical(this, tr("Transcribe"),
                              tr("Can't find %1 next to this program.").arg(QFileInfo(cliPath()).fileName()));
        return;
    }

    if (!runner_.treeReady()) {
        QMessageBox::critical(this, tr("Transcribe"), tr("Could not initialize process cancellation."));
        return;
    }
    if (!confirmOverwrite(input)) return;

    saveSettings();
    written_.clear();
    textCopies_.clear();
    textDir_.reset();
    if (pdfCheck_->isChecked()) {
        textDir_ = std::make_unique<QTemporaryDir>();
        if (!textDir_->isValid()) {
            textDir_.reset();
            QMessageBox::critical(this, tr("Transcribe"), tr("Could not create a temporary folder for the PDF."));
            return;
        }
    }
    {
        const QString outText = absolutePath(outputEdit_->text());
        const QFileInfo base(outText.isEmpty() ? input : outText);
        pdfDir_ = base.dir().absolutePath();
        inputTitle_ = QFileInfo(input).completeBaseName();
    }
    lastProblem_.clear();
    logView_->clear();
    transcriptView_->clear();
    cleanView_->clear();
    summaryView_->clear();
    tabs_->setTabVisible(2, false);
    tabs_->setTabVisible(3, false);
    tabs_->setCurrentWidget(logView_);

    const QStringList args = buildArguments(input);
    QStringList shown;
    for (const QString& s : args) shown << (s.contains(QLatin1Char(' ')) ? QLatin1Char('"') + s + QLatin1Char('"') : s);
    appendLog(QStringLiteral("> transcribe_multi ") + shown.join(QLatin1Char(' ')) + QLatin1Char('\n'));

    setRunning(true);
    statusLabel_->setText(tr("Starting..."));
    setEngine(QString(), QString(), false);
    audioSeconds_ = -1.0;
    transcribeSeconds_ = -1.0;
    audioLengthEdit_->setText(QStringLiteral("\u2014"));
    ratioEdit_->setText(QStringLiteral("\u2014"));
    postEdit_->setText(QStringLiteral("\u2014"));
    postLabel_->setVisible(postProcessing());
    postEdit_->setVisible(postProcessing());
    runClock_.start();
    tick_->start();
    updateTiming(false);
    runner_.start(cliPath(), args);
}

QString MainWindow::cliFormats() const {
    QStringList f;
    if (txtCheck_->isChecked()) f << QStringLiteral("txt");
    if (srtCheck_->isChecked()) f << QStringLiteral("srt");
    if (docxCheck_->isChecked()) f << QStringLiteral("docx");
    return f.isEmpty() ? QStringLiteral("none") : f.join(QLatin1Char(','));  // none: PDF only
}

// The files this run will write: the output path (or the input file) with its
// extension replaced - the same rules transcribe_multi uses. The corrected copy
// and the summary come in the same document formats as the transcript, and as
// .txt when only subtitles were chosen.
QStringList MainWindow::plannedOutputs(const QString& input) const {
    const QString outText = absolutePath(outputEdit_->text());
    const QFileInfo base(outText.isEmpty() ? input : outText);
    const QString stem = base.suffix().isEmpty() ? base.fileName() : base.completeBaseName();
    const QDir dir(base.dir().absolutePath());
    const bool txt = txtCheck_->isChecked(), docx = docxCheck_->isChecked(), pdf = pdfCheck_->isChecked();
    QStringList files;
    auto add = [&](const QString& suffix, bool ai) {
        if (txt || (ai && !docx && !pdf)) files << dir.filePath(stem + suffix + QStringLiteral(".txt"));
        if (docx) files << dir.filePath(stem + suffix + QStringLiteral(".docx"));
        if (pdf) files << dir.filePath(stem + suffix + QStringLiteral(".pdf"));
    };
    add(QString(), false);
    if (srtCheck_->isChecked()) files << dir.filePath(stem + QStringLiteral(".srt"));
    if (polishCheck_->isChecked()) add(QStringLiteral(".clean"), true);
    if (summaryCheck_->isChecked()) add(QStringLiteral(".summary"), true);
    return files;
}

// Turn transcribe_multi's text copies into PDFs next to its other outputs.
void MainWindow::makePdfs() {
    for (const QString& copy : textCopies_) {
        QString name = QFileInfo(copy).fileName();
        if (name.endsWith(QLatin1String(".txt"), Qt::CaseInsensitive)) name.chop(4);
        const QString pdf = QDir(pdfDir_).filePath(name + QStringLiteral(".pdf"));
        QString error;
        if (writePdf(copy, pdf, inputTitle_, &error)) {
            written_ << QDir::toNativeSeparators(pdf);
            appendLog(QStringLiteral("  wrote %1\n").arg(QDir::toNativeSeparators(pdf)));
        } else {
            appendLog(tr("\nCould not write %1: %2\n").arg(QDir::toNativeSeparators(pdf), error));
            lastProblem_ = tr("Error: could not write %1").arg(QFileInfo(pdf).fileName());
        }
    }
}

// Ask before replacing existing output files. Returns false to abort the run.
bool MainWindow::confirmOverwrite(const QString& input) {
    for (;;) {
        QStringList existing;
        for (const QString& f : plannedOutputs(input))
            if (QFileInfo::exists(f)) existing << QDir::toNativeSeparators(f);
        if (existing.isEmpty()) return true;

        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Transcribe"));
        box.setText(existing.size() == 1 ? tr("The output file already exists:")
                                         : tr("The output files already exist:"));
        box.setInformativeText(existing.join(QLatin1Char('\n')) + QStringLiteral("\n\n") +
                               tr("Do you want to replace %1?").arg(existing.size() == 1 ? tr("it") : tr("them")));
        QPushButton* overwrite = box.addButton(tr("Overwrite"), QMessageBox::DestructiveRole);
        QPushButton* rename = box.addButton(tr("Choose another name..."), QMessageBox::ActionRole);
        QPushButton* cancelBtn = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(cancelBtn);
        box.exec();

        if (box.clickedButton() == overwrite) return true;
        if (box.clickedButton() != rename) return false;

        const QString before = outputEdit_->text();
        browseOutput();
        if (outputEdit_->text() == before) return false;  // dialog cancelled
        // loop: check the newly chosen name too
    }
}

void MainWindow::showModels() {
    if (runner_.isActive()) return;
    // CrispASR's cache may hold other programs' models too: ours have the .src
    // sidecar CrispASR writes for every download.
    pk::ModelsDialog dlg({{tr("Speech recognition, spelling and voice models"),
                           pk::crispasr_models_dir(absolutePath(modelsDirEdit_->text())), pk::ModelOwner{true, {}}}},
                         this);
    dlg.exec();
}

void MainWindow::cancel() {
    if (runner_.cancel()) statusLabel_->setText(tr("Cancelling..."));  // kills ffmpeg too
}

void MainWindow::setRunning(bool running) {
    startBtn_->setEnabled(!running);
    cancelBtn_->setEnabled(running);
    infoBtn_->setEnabled(!running);  // no deleting a model while it may be in use
    // The options were read when the run started: don't let them look changeable.
    for (QWidget* w : {static_cast<QWidget*>(filesBox_), static_cast<QWidget*>(optionsBox_),
                       static_cast<QWidget*>(aiBox_), static_cast<QWidget*>(advancedBox_)})
        w->setEnabled(!running);
    if (running) {
        openBtn_->setEnabled(false);
        progress_->setRange(0, 0);  // busy indicator
    } else {
        progress_->setRange(0, 1);
        progress_->setValue(written_.isEmpty() ? 0 : 1);
    }
}

void MainWindow::parseLine(const QString& line) {
    static const QRegularExpression progressRe(QStringLiteral("^progress: (\\w+) (\\d+)/(\\d+) (\\w+)"));
    static const QRegularExpression wroteRe(QStringLiteral("^\\s*wrote (.+)$"));

    if (const auto m = progressRe.match(line); m.hasMatch()) {
        const int done = m.captured(2).toInt();
        const int total = m.captured(3).toInt();
        const QString key = m.captured(1);
        const QString phase = key == QLatin1String("align")     ? tr("Aligning")
                              : key == QLatin1String("polish")  ? tr("Correcting spelling and grammar")
                              : key == QLatin1String("summary") ? tr("Summarizing")
                              : key == QLatin1String("paragraphs") ? tr("Finding paragraph breaks")
                              : key == QLatin1String("download") ? tr("Downloading")
                                                                  : tr("Transcribing");
        progress_->setRange(0, std::max(total, 1));
        progress_->setValue(done);
        if (m.captured(4) == QLatin1String("s"))
            statusLabel_->setText(tr("%1: %2 / %3 min of audio")
                                      .arg(phase)
                                      .arg(done / 60.0, 0, 'f', 1)
                                      .arg(total / 60.0, 0, 'f', 1));
        else if (m.captured(4) == QLatin1String("parts"))
            statusLabel_->setText(tr("%1: part %2 of %3").arg(phase).arg(done).arg(total));
        else
            statusLabel_->setText(tr("%1: %2 / %3 chunks").arg(phase).arg(done).arg(total));
        return;
    }
    static const QRegularExpression audioRe(QStringLiteral("^Audio : .*\\(([0-9.]+) s\\)"));
    if (const auto m = audioRe.match(line); m.hasMatch()) {
        audioSeconds_ = m.captured(1).toDouble();
        audioLengthEdit_->setText(formatDuration(audioSeconds_));
    }
    static const QRegularExpression doneRe(QStringLiteral("^Transcribed in [0-9.]+ s"));
    if (doneRe.match(line).hasMatch() && runClock_.isValid()) {
        // The transcript is written; anything after this is AI post-processing,
        // which must not count towards the transcription speed.
        transcribeSeconds_ = runClock_.elapsed() / 1000.0;
        updateTiming(false);
        return;
    }
    static const QRegularExpression engineRe(QStringLiteral("^Engine: (\\S+)(?: \\((.*)\\))?\\s*$"));
    if (const auto m = engineRe.match(line); m.hasMatch()) {
        setEngine(m.captured(1), m.captured(2), false);
        return;
    }
    if (const auto m = wroteRe.match(line); m.hasMatch()) {
        written_ << m.captured(1).trimmed();
        return;
    }
    static const QRegularExpression textRe(QStringLiteral("^\\s*text (.+)$"));
    if (const auto m = textRe.match(line); m.hasMatch()) {  // a text copy for a PDF
        textCopies_ << m.captured(1).trimmed();
        return;
    }
    const QString s = line.trimmed();
    if (s.startsWith(QLatin1String("Warning:")) || s.startsWith(QLatin1String("Error:"))) lastProblem_ = s;
    if (s.startsWith(QLatin1String("Loading")) || s.startsWith(QLatin1String("Decoding")) ||
        s.startsWith(QLatin1String("Transcribing")) || s.startsWith(QLatin1String("Audio :")) ||
        s.startsWith(QLatin1String("Chunks:")) || s.startsWith(QLatin1String("Correcting")) ||
        s.startsWith(QLatin1String("Writing summary")) || s.startsWith(QLatin1String("Note:")) ||
        s.startsWith(QLatin1String("Finding paragraph")) || s.startsWith(QLatin1String("Paragraphs:")) ||
        s.startsWith(QLatin1String("Punctuation:")) || s.startsWith(QLatin1String("Warning:")) ||
        s.startsWith(QLatin1String("ARK instruction:"))) {
        statusLabel_->setText(s);
        if (s.startsWith(QLatin1String("Loading")) || s.startsWith(QLatin1String("Transcribing")) ||
            s.startsWith(QLatin1String("Correcting")) || s.startsWith(QLatin1String("Writing summary")) ||
            s.startsWith(QLatin1String("Finding paragraph")))
            progress_->setRange(0, 0);
    }
}

// The output has been read to the end (the last line too) when this runs.
void MainWindow::processFinished(int exitCode, bool cancelled) {
    tick_->stop();
    updateTiming(true);
    if ((cancelled || exitCode != 0) && transcribeSeconds_ < 0)
        ratioEdit_->setText(cancelled ? tr("cancelled") : tr("failed"));
    // The PDFs, also when only the AI post-processing failed (exit code 3).
    if (!cancelled && (exitCode == 0 || exitCode == 3)) makePdfs();
    setRunning(false);
    if (engineBackend_.isEmpty()) {  // the run ended before reporting its engine
        const QString last = settings_.value(QStringLiteral("lastEngine")).toString();
        if (last.isEmpty())
            engineLabel_->setText(tr("Compute: <i>shown when a transcription starts</i>"));
        else
            setEngine(last, settings_.value(QStringLiteral("lastEngineDetail")).toString(), true);
    }
    if (cancelled) {
        statusLabel_->setText(tr("Cancelled."));
        appendLog(QStringLiteral("\n[cancelled]\n"));
    } else if (exitCode == 0) {
        QStringList names;
        for (const QString& p : written_) names << QFileInfo(p).fileName();
        statusLabel_->setText(engineBackend_.isEmpty()
                                  ? tr("Done: %1").arg(names.join(QStringLiteral(", ")))
                                  : tr("Done on %1: %2").arg(engineBackend_, names.join(QStringLiteral(", "))));
        openBtn_->setEnabled(!written_.isEmpty());
        showTranscript();
    } else if (transcribeSeconds_ >= 0 && !written_.isEmpty()) {
        // The transcript was saved; only the AI post-processing failed.
        statusLabel_->setText(tr("Transcript saved, but the AI post-processing failed. See the log."));
        openBtn_->setEnabled(true);
        showTranscript();
        tabs_->setCurrentWidget(logView_);
    } else if (lastProblem_.contains(QLatin1String("no speech was transcribed"))) {
        statusLabel_->setText(tr("No speech was found in the audio."));
        ratioEdit_->setText(QStringLiteral("\u2014"));
    } else if (!lastProblem_.isEmpty()) {
        statusLabel_->setText(tr("Failed (exit code %1): %2").arg(exitCode).arg(lastProblem_));
    } else {
        statusLabel_->setText(tr("Failed (exit code %1). See the log.").arg(exitCode));
    }
    updateEnabledState();
}

void MainWindow::processFailedToStart() {
    setRunning(false);
    tick_->stop();
    statusLabel_->setText(tr("Could not start transcribe_multi."));
    appendLog(tr("\nFailed to start %1\n").arg(cliPath()));
    updateEnabledState();
}

bool MainWindow::loadInto(QPlainTextEdit* view, const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        appendLog(tr("\nCould not read %1\n").arg(path));
        return false;
    }
    view->setPlainText(QString::fromUtf8(f.readAll()));
    return true;
}

void MainWindow::showTranscript() {
    if (written_.isEmpty()) return;
    QString pick, clean, summary;
    // The text files: what was written, or the copies the PDFs were made from.
    QStringList texts;
    for (const QString& p : written_ + textCopies_)
        if (p.endsWith(QLatin1String(".txt"), Qt::CaseInsensitive) || p.endsWith(QLatin1String(".srt"), Qt::CaseInsensitive))
            texts << p;
    for (const QString& p : texts) {
        if (p.endsWith(QLatin1String(".clean.txt"), Qt::CaseInsensitive)) clean = p;
        else if (p.endsWith(QLatin1String(".summary.txt"), Qt::CaseInsensitive)) summary = p;
        else if (pick.isEmpty() || p.endsWith(QLatin1String(".txt"), Qt::CaseInsensitive)) pick = p;
    }
    if (!pick.isEmpty() && loadInto(transcriptView_, pick)) tabs_->setCurrentWidget(transcriptView_);
    if (!clean.isEmpty() && loadInto(cleanView_, clean)) {
        tabs_->setTabVisible(2, true);
        tabs_->setCurrentWidget(cleanView_);
    }
    if (!summary.isEmpty() && loadInto(summaryView_, summary)) {
        tabs_->setTabVisible(3, true);
        tabs_->setCurrentWidget(summaryView_);
    }
}

void MainWindow::openOutputFolder() {
    if (!written_.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(written_.first()).absolutePath()));
}

void MainWindow::setEngine(const QString& backend, const QString& detail, bool lastRun) {
    engineBackend_ = lastRun ? QString() : backend;
    if (backend.isEmpty()) {
        engineLabel_->setText(tr("Compute: <i>detecting...</i>"));
        engineLabel_->setToolTip(QString());
        return;
    }
    // Colour-code the engine: CUDA green, Vulkan red, Metal blue, CPU grey.
    QString colour = QStringLiteral("#6b6b6b");
    const QString b = backend.toLower();
    if (b.startsWith(QLatin1String("cuda"))) colour = QStringLiteral("#4e8a00");
    else if (b.startsWith(QLatin1String("vulkan"))) colour = QStringLiteral("#a41e22");
    else if (b.startsWith(QLatin1String("mtl")) || b.startsWith(QLatin1String("metal"))) colour = QStringLiteral("#1f6feb");
    QString html = tr("Compute:") +
                   QStringLiteral(" <span style=\"background:%1; color:white; padding:1px 6px; border-radius:3px;\">"
                                  "&nbsp;<b>%2</b>&nbsp;</span>")
                       .arg(colour, backend.toHtmlEscaped());
    if (!detail.isEmpty()) html += QStringLiteral(" ") + detail.toHtmlEscaped();
    if (lastRun) html += QStringLiteral(" <i>") + tr("(last run)") + QStringLiteral("</i>");
    engineLabel_->setText(html);
    engineLabel_->setToolTip(lastRun ? tr("Compute engine used by the previous transcription")
                                     : tr("Compute engine used by this transcription"));
    if (!lastRun) {
        settings_.setValue(QStringLiteral("lastEngine"), backend);
        settings_.setValue(QStringLiteral("lastEngineDetail"), detail);
    }
}

void MainWindow::updateTiming(bool finished) {
    if (!runClock_.isValid()) return;
    const double elapsed = runClock_.elapsed() / 1000.0;
    // Transcription time stops when the transcript is written; AI post-processing
    // after that is timed separately.
    const double transcribe = transcribeSeconds_ >= 0 ? transcribeSeconds_ : elapsed;
    elapsedEdit_->setText(formatDuration(transcribe));
    if (transcribeSeconds_ >= 0 && postEdit_->isVisible()) postEdit_->setText(formatDuration(elapsed - transcribeSeconds_));
    if ((finished || transcribeSeconds_ >= 0) && audioSeconds_ > 0 && transcribe > 0) {
        const double ratio = audioSeconds_ / transcribe;
        ratioEdit_->setText(ratio >= 10 ? tr("%1\u00D7 real time").arg(ratio, 0, 'f', 1)
                                        : tr("%1\u00D7 real time").arg(ratio, 0, 'f', 2));
    }
}

void MainWindow::appendLog(const QString& text) {
    logView_->moveCursor(QTextCursor::End);
    logView_->insertPlainText(text);
    logView_->moveCursor(QTextCursor::End);
}

// ------------------------------------------------------------- settings
void MainWindow::loadSettings() {
    auto& s = settings_;
    selectData(modelCombo_, s.value(QStringLiteral("model"), QStringLiteral("parakeet-ultra")));
    {
        // "formats" (a list); older versions saved one "format": txt, srt or both.
        QStringList f = s.value(QStringLiteral("formats")).toStringList();
        if (f.isEmpty()) {
            const QString old = s.value(QStringLiteral("format"), QStringLiteral("txt")).toString();
            f = old == QLatin1String("both") ? QStringList{QStringLiteral("txt"), QStringLiteral("srt")} : QStringList{old};
        }
        if (!f.contains(QStringLiteral("srt")) && !f.contains(QStringLiteral("docx")) && !f.contains(QStringLiteral("pdf")))
            f << QStringLiteral("txt");  // never none
        txtCheck_->setChecked(f.contains(QStringLiteral("txt")));
        srtCheck_->setChecked(f.contains(QStringLiteral("srt")));
        docxCheck_->setChecked(f.contains(QStringLiteral("docx")));
        pdfCheck_->setChecked(f.contains(QStringLiteral("pdf")));
    }
    selectData(languageCombo_, s.value(QStringLiteral("language"), QStringLiteral("en")));
    deviceCombo_->setCurrentText(s.value(QStringLiteral("device"), QStringLiteral("auto")).toString());
    threadsSpin_->setValue(s.value(QStringLiteral("threads"), 0).toInt());
    chunkSpin_->setValue(s.value(QStringLiteral("chunk"), 30.0).toDouble());
    paragraphSpin_->setValue(s.value(QStringLiteral("paragraph"), 5).toInt());
    alignCheck_->setChecked(s.value(QStringLiteral("align"), true).toBool());
    modelsDirEdit_->setText(s.value(QStringLiteral("modelsDir")).toString());
    polishCheck_->setChecked(s.value(QStringLiteral("polish"), false).toBool());
    summaryCheck_->setChecked(s.value(QStringLiteral("summary"), false).toBool());
    smartParaCheck_->setChecked(s.value(QStringLiteral("smartParagraphs"), false).toBool());
    selectData(punctCombo_, s.value(QStringLiteral("punctuation"), QStringLiteral("auto")));
    arkPunctCheck_->setChecked(s.value(QStringLiteral("arkPunctuation"), false).toBool());
    llmFileEdit_->setText(s.value(QStringLiteral("llmFile")).toString());
    const QString lastEngine = s.value(QStringLiteral("lastEngine")).toString();
    if (lastEngine.isEmpty())
        engineLabel_->setText(tr("Compute: <i>shown when a transcription starts</i>"));
    else
        setEngine(lastEngine, s.value(QStringLiteral("lastEngineDetail")).toString(), true);
}

void MainWindow::saveSettings() {
    auto& s = settings_;
    s.setValue(QStringLiteral("model"), modelCombo_->currentData());
    {
        QStringList f;
        if (txtCheck_->isChecked()) f << QStringLiteral("txt");
        if (srtCheck_->isChecked()) f << QStringLiteral("srt");
        if (docxCheck_->isChecked()) f << QStringLiteral("docx");
        if (pdfCheck_->isChecked()) f << QStringLiteral("pdf");
        s.setValue(QStringLiteral("formats"), f);
    }
    s.setValue(QStringLiteral("language"), languageCombo_->currentData());
    s.setValue(QStringLiteral("device"), deviceCombo_->currentText());
    s.setValue(QStringLiteral("threads"), threadsSpin_->value());
    s.setValue(QStringLiteral("chunk"), chunkSpin_->value());
    s.setValue(QStringLiteral("paragraph"), paragraphSpin_->value());
    s.setValue(QStringLiteral("align"), alignCheck_->isChecked());
    s.setValue(QStringLiteral("modelsDir"), modelsDirEdit_->text());
    s.setValue(QStringLiteral("polish"), polishCheck_->isChecked());
    s.setValue(QStringLiteral("summary"), summaryCheck_->isChecked());
    s.setValue(QStringLiteral("smartParagraphs"), smartParaCheck_->isChecked());
    s.setValue(QStringLiteral("punctuation"), punctCombo_->currentData());
    s.setValue(QStringLiteral("arkPunctuation"), arkPunctCheck_->isChecked());
    s.setValue(QStringLiteral("llmFile"), llmFileEdit_->text());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (runner_.isRunning() &&
        QMessageBox::question(this, tr("Transcribe"), tr("A transcription is running. Stop it and quit?")) !=
            QMessageBox::Yes) {
        event->ignore();
        return;
    }
    runner_.stop();  // disconnected first: nothing reaches the closing window, even if the tool hangs
    saveSettings();
    event->accept();
}
