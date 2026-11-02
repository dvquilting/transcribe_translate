#include "mainwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "languages.h"
#include "model_files.h"  // ../common/qt
#include "help_window.h"  // ../common/qt
#include "models_dialog.h"
#include "models.h"
#include "models_help.h"

namespace {

QString absolutePath(QString path) {
    path = path.trimmed();
    if (path.size() >= 2 && path.startsWith(QLatin1Char('"')) && path.endsWith(QLatin1Char('"')))
        path = path.mid(1, path.size() - 2);
    return path.isEmpty() ? QString() : QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
}


const char* kFileFilter =
    "Documents (*.txt *.md *.markdown *.srt *.docx);;Text (*.txt);;Markdown (*.md *.markdown);;"
    "Subtitles (*.srt);;Word (*.docx);;All files (*)";

QString q(const std::string& s) { return QString::fromUtf8(s.c_str()); }

QWidget* row(QLineEdit* edit, QPushButton* button) {
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(edit, 1);
    h->addWidget(button);
    return w;
}

// The Help window's page: how a translation works and the languages of each model
// (from the model table, so it can't drift from what the program does).
QString helpHtml() {
    QString h = QStringLiteral(
        "<h2>Translate</h2>"
        "<p>Translate translates text and documents with a language model that runs on this computer; nothing "
        "is uploaded. Each model is downloaded once, the first time it is needed (<b>Model files</b> shows them "
        "and can delete them).</p>"
        "<p>Choose a <b>Model</b> and the languages (<b>From</b> can be <i>Detect automatically</i>; the arrow "
        "button swaps them), then use one of the tabs. <b>Cancel</b> stops a translation; the <b>Log</b> tab shows "
        "what is happening.</p>"

        "<h3>Text tab</h3>"
        "<p>Type or paste text (or drop a text file onto the window) and press <b>Translate</b> (Ctrl+Enter). The "
        "translation appears piece by piece; <b>Copy translation</b> puts it on the clipboard.</p>"

        "<h3>File tab</h3>"
        "<p>Translates a whole file and saves it in the same format: plain text (.txt), Markdown (.md), "
        "subtitles (.srt) or Word (.docx). The layout is kept: paragraphs and Markdown formatting, the timing of "
        "each subtitle, and the paragraph styles, tables and page breaks of a Word document (formatting inside "
        "a paragraph, such as a single bold word, takes the look of the paragraph's text). The translation is saved "
        "next to the original with the language added (report.docx becomes report.de.docx) unless you choose "
        "another name.</p>"
        "<p>If a few pieces can't be translated, the file is still saved with those pieces in the original "
        "language, and the log says which.</p>"

        "<h3>Advanced</h3>"
        "<ul>"
        "<li><b>Device</b> chooses where the model runs: <i>auto</i> uses an NVIDIA graphics card with CUDA, other "
        "graphics cards with Vulkan, or the processor. A model larger than the graphics card's memory runs partly "
        "on the processor, which is slower. <b>CPU threads</b> limits the processor threads used.</li>"
        "<li><b>Show the model the preceding text</b> (Hy-MT2 and Qwen) helps with names, pronouns and "
        "consistent terms.</li>"
        "<li><b>Model file override</b> and <b>Models folder</b> use your own model file or another download "
        "folder.</li>"
        "</ul>"

        "<h2>The models</h2><table cellpadding=\"4\">");
    for (const auto& m : tl::model_table())
        h += pk::help_row(q(m.display).toHtmlEscaped() + QStringLiteral("<br><small>") + q(m.size).toHtmlEscaped() +
                              QStringLiteral("</small>"),
                          q(m.description).toHtmlEscaped());
    h += QStringLiteral("</table><h2>Languages of each model</h2><table cellpadding=\"4\">");
    h += q(tl::translation_model_rows());
    h += QStringLiteral(
        "</table><p>A model warns when it wasn't trained on the chosen languages; it may still work, but less "
        "well. A regional variant counts as its language: Spanish (Spain) works with every model that knows "
        "Spanish.</p>");
    return h;
}

void selectData(QComboBox* combo, const QVariant& value) {
    const int i = combo->findData(value);
    if (i >= 0) combo->setCurrentIndex(i);
}

QLineEdit* readOnlyField(int minChars, const QString& tip) {
    auto* e = new QLineEdit;
    e->setReadOnly(true);
    e->setFocusPolicy(Qt::NoFocus);
    e->setAlignment(Qt::AlignRight);
    e->setMinimumWidth(e->fontMetrics().horizontalAdvance(QLatin1Char('0')) * minChars + 16);
    e->setMaximumWidth(e->fontMetrics().horizontalAdvance(QLatin1Char('0')) * (minChars + 6) + 16);
    e->setText(QStringLiteral("\u2014"));
    e->setToolTip(tip);
    return e;
}

QString formatDuration(double s) {
    if (s < 0) return QStringLiteral("—");
    const qint64 t = qRound64(s);
    if (t >= 3600) return QStringLiteral("%1:%2:%3").arg(t / 3600).arg((t / 60) % 60, 2, 10, QLatin1Char('0')).arg(t % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(t / 60).arg(t % 60, 2, 10, QLatin1Char('0'));
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent), settings_(QStringLiteral("DavidV"), QStringLiteral("TranslateCpp")) {
    setWindowTitle(tr("Translate"));
    setAcceptDrops(true);
    resize(980, 720);
    connect(&runner_, &pk::ChildProcess::outputReceived, this, &MainWindow::appendLog);
    connect(&runner_, &pk::ChildProcess::lineReceived, this, &MainWindow::parseLine);
    connect(&runner_, &pk::ChildProcess::finished, this, &MainWindow::processFinished);
    connect(&runner_, &pk::ChildProcess::failedToStart, this, &MainWindow::processFailedToStart);
    buildUi();
    loadSettings();
    updateModelInfo();
}

MainWindow::~MainWindow() {
    runner_.stop();  // before tempDir_ goes: translate_text may still be writing into it
    delete tempDir_;
}

void MainWindow::buildUi() {
    auto* root = new QVBoxLayout(this);

    // --- model + languages ----------------------------------------------
    auto* top = new QGroupBox(tr("Model and languages"));
    auto* tf = new QFormLayout(top);
    modelCombo_ = new QComboBox;
    for (const auto& m : tl::model_table())
        modelCombo_->addItem(q(m.display) + QStringLiteral("  —  ") + q(m.size), q(m.key));
    modelDesc_ = new QLabel;
    modelDesc_->setWordWrap(true);
    modelDesc_->setStyleSheet(QStringLiteral("color: gray;"));
    tf->addRow(tr("Model:"), modelCombo_);
    tf->addRow(QString(), modelDesc_);

    fromCombo_ = new QComboBox;
    toCombo_ = new QComboBox;
    fromCombo_->addItem(tr("Detect automatically"), QStringLiteral("auto"));
    for (const auto& l : tl::languages()) {
        const QString label = l.native == l.name ? q(l.name) : q(l.name) + QStringLiteral(" — ") + q(l.native);
        fromCombo_->addItem(label, q(l.code));
        toCombo_->addItem(label, q(l.code));
    }
    fromCombo_->setMaxVisibleItems(20);
    toCombo_->setMaxVisibleItems(20);
    auto* swap = new QPushButton(QStringLiteral("⇄"));
    swap->setToolTip(tr("Swap languages"));
    swap->setFixedWidth(40);
    auto* langRow = new QHBoxLayout;
    langRow->addWidget(new QLabel(tr("From:")));
    langRow->addWidget(fromCombo_, 1);
    langRow->addWidget(swap);
    langRow->addWidget(new QLabel(tr("To:")));
    langRow->addWidget(toCombo_, 1);
    tf->addRow(langRow);
    langNote_ = new QLabel;
    langNote_->setWordWrap(true);
    langNote_->setStyleSheet(QStringLiteral("color: #b06000;"));
    langNote_->hide();
    tf->addRow(langNote_);
    root->addWidget(top);

    connect(modelCombo_, &QComboBox::currentIndexChanged, this, &MainWindow::updateModelInfo);
    connect(fromCombo_, &QComboBox::currentIndexChanged, this, &MainWindow::updateModelInfo);
    connect(toCombo_, &QComboBox::currentIndexChanged, this, &MainWindow::updateModelInfo);
    connect(swap, &QPushButton::clicked, this, &MainWindow::swapLanguages);

    // --- tabs: text / file / log -------------------------------------------
    tabs_ = new QTabWidget;

    auto* textTab = new QWidget;
    auto* tv = new QVBoxLayout(textTab);
    auto* split = new QSplitter(Qt::Horizontal);
    sourceEdit_ = new QPlainTextEdit;
    sourceEdit_->setPlaceholderText(tr("Type or paste text here, or drop a file onto the window"));
    resultEdit_ = new QPlainTextEdit;
    resultEdit_->setReadOnly(true);
    resultEdit_->setPlaceholderText(tr("The translation appears here"));
    split->addWidget(sourceEdit_);
    split->addWidget(resultEdit_);
    tv->addWidget(split, 1);
    auto* textButtons = new QHBoxLayout;
    translateTextBtn_ = new QPushButton(tr("Translate"));
    translateTextBtn_->setDefault(true);
    translateTextBtn_->setToolTip(tr("Ctrl+Enter"));
    auto* copyBtn = new QPushButton(tr("Copy translation"));
    auto* clearBtn = new QPushButton(tr("Clear"));
    textButtons->addWidget(translateTextBtn_);
    textButtons->addStretch();
    textButtons->addWidget(copyBtn);
    textButtons->addWidget(clearBtn);
    tv->addLayout(textButtons);
    connect(translateTextBtn_, &QPushButton::clicked, this, &MainWindow::translateText);
    connect(copyBtn, &QPushButton::clicked, this,
            [this] { QApplication::clipboard()->setText(resultEdit_->toPlainText()); });
    connect(clearBtn, &QPushButton::clicked, this, [this] {
        sourceEdit_->clear();
        resultEdit_->clear();
    });
    auto* sc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), textTab);
    connect(sc, &QShortcut::activated, this, &MainWindow::translateText);
    tabs_->addTab(textTab, tr("Text"));

    auto* fileTab = new QWidget;
    auto* fv = new QVBoxLayout(fileTab);
    auto* ff = new QFormLayout;
    inputEdit_ = new QLineEdit;
    inputEdit_->setPlaceholderText(tr("A .txt, .md, .srt or .docx file"));
    auto* bi = new QPushButton(tr("Browse..."));
    connect(bi, &QPushButton::clicked, this, &MainWindow::browseInput);
    ff->addRow(tr("Input file:"), row(inputEdit_, bi));
    outputEdit_ = new QLineEdit;
    outputEdit_->setPlaceholderText(tr("(default: next to the input, e.g. report.de.docx)"));
    auto* bo = new QPushButton(tr("Browse..."));
    connect(bo, &QPushButton::clicked, this, &MainWindow::browseOutput);
    ff->addRow(tr("Output file:"), row(outputEdit_, bo));
    fv->addLayout(ff);
    auto* note = new QLabel(
        tr("Text and Markdown keep their layout; code blocks are not translated. Subtitles keep their timing. "
           "Word documents keep paragraph styles, tables, headers and footers, but formatting that changes "
           "inside a paragraph (a single bold word, say) is not kept."));
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: gray;"));
    fv->addWidget(note);
    auto* fileButtons = new QHBoxLayout;
    translateFileBtn_ = new QPushButton(tr("Translate file"));
    openFolderBtn_ = new QPushButton(tr("Open output folder"));
    openFolderBtn_->setEnabled(false);
    fileButtons->addWidget(translateFileBtn_);
    fileButtons->addStretch();
    fileButtons->addWidget(openFolderBtn_);
    fv->addLayout(fileButtons);
    fv->addStretch();
    connect(translateFileBtn_, &QPushButton::clicked, this, &MainWindow::translateFile);
    connect(openFolderBtn_, &QPushButton::clicked, this, &MainWindow::openOutputFolder);
    tabs_->addTab(fileTab, tr("File"));

    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    logView_->setMaximumBlockCount(5000);
    tabs_->addTab(logView_, tr("Log"));
    root->addWidget(tabs_, 1);

    // --- advanced ------------------------------------------------------------
    advancedBox_ = new QGroupBox(tr("Advanced"));
    advancedBox_->setCheckable(true);
    advancedBox_->setChecked(false);
    auto* al = new QVBoxLayout(advancedBox_);
    advancedBody_ = new QWidget;
    al->addWidget(advancedBody_);
    auto* af = new QFormLayout(advancedBody_);
    af->setContentsMargins(0, 0, 0, 0);
    deviceCombo_ = new QComboBox;
    deviceCombo_->addItems({QStringLiteral("auto"), QStringLiteral("cuda"), QStringLiteral("vulkan"), QStringLiteral("cpu")});
    af->addRow(tr("Device:"), deviceCombo_);
    threadsSpin_ = new QSpinBox;
    threadsSpin_->setRange(0, 64);
    threadsSpin_->setSpecialValueText(tr("auto"));
    af->addRow(tr("CPU threads:"), threadsSpin_);
    contextCheck_ = new QCheckBox(tr("Show the model the preceding text (Hy-MT2 and Qwen)"));
    contextCheck_->setChecked(true);
    contextCheck_->setToolTip(tr("Helps with pronouns and terminology across paragraphs. TranslateGemma has no "
                                 "place for it in its prompt."));
    af->addRow(QString(), contextCheck_);
    modelFileEdit_ = new QLineEdit;
    modelFileEdit_->setPlaceholderText(tr("(download the selected model)"));
    auto* bm = new QPushButton(tr("Browse..."));
    connect(bm, &QPushButton::clicked, this, [this] {
        const QString p = QFileDialog::getOpenFileName(this, tr("Model file"), modelFileEdit_->text(),
                                                       tr("GGUF models (*.gguf);;All files (*)"));
        if (!p.isEmpty()) modelFileEdit_->setText(QDir::toNativeSeparators(p));
    });
    modelFileEdit_->setToolTip(tr("A local .gguf of the same family as the selected model (the prompt format "
                                  "depends on the family)"));
    af->addRow(tr("Model file override:"), row(modelFileEdit_, bm));
    modelsDirEdit_ = new QLineEdit;
    modelsDirEdit_->setPlaceholderText(tr("(default: %LOCALAPPDATA%\\Translate\\models)"));
    auto* bd = new QPushButton(tr("Browse..."));
    connect(bd, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Models folder"), modelsDirEdit_->text());
        if (!d.isEmpty()) modelsDirEdit_->setText(QDir::toNativeSeparators(d));
    });
    af->addRow(tr("Models folder:"), row(modelsDirEdit_, bd));
    advancedBody_->setVisible(false);
    connect(advancedBox_, &QGroupBox::toggled, advancedBody_, &QWidget::setVisible);
    root->addWidget(advancedBox_);

    // --- status ----------------------------------------------------------------
    statusLabel_ = new QLabel(tr("Ready."));
    engineLabel_ = new QLabel;
    engineLabel_->setTextFormat(Qt::RichText);
    auto* sr = new QHBoxLayout;
    sr->addWidget(statusLabel_, 1);
    sr->addWidget(engineLabel_);
    root->addLayout(sr);
    progress_ = new QProgressBar;
    progress_->setRange(0, 1);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    root->addWidget(progress_);

    // --- timing: like the transcriber's audio length / time / speed fields ---
    wordsEdit_ = readOnlyField(7, tr("Words in the source text"));
    loadEdit_ = readOnlyField(6, tr("Time to start the model: loading it into memory (and the one-time "
                                    "download on first use)"));
    translateEdit_ = readOnlyField(6, tr("Time spent translating, after the model was loaded"));
    speedEdit_ = readOnlyField(13, tr("Source words translated per minute (\u2248 = estimate while running)"));
    totalEdit_ = readOnlyField(6, tr("Whole run, from pressing Translate to the finished file"));
    auto* tr_ = new QHBoxLayout;
    auto field = [&](const QString& label, QLineEdit* e) {
        tr_->addWidget(new QLabel(label));
        tr_->addWidget(e);
        tr_->addSpacing(10);
    };
    field(tr("Words:"), wordsEdit_);
    field(tr("Model loading:"), loadEdit_);
    field(tr("Translation time:"), translateEdit_);
    field(tr("Speed:"), speedEdit_);
    field(tr("Total:"), totalEdit_);
    tr_->addStretch();
    root->addLayout(tr_);

    // Same order as Transcribe and Speech Translate: Cancel on the left, Info and
    // Quit on the right (the Translate buttons stay on their own tabs).
    auto* br = new QHBoxLayout;
    cancelBtn_ = new QPushButton(tr("Cancel"));
    cancelBtn_->setEnabled(false);
    connect(cancelBtn_, &QPushButton::clicked, this, &MainWindow::cancel);
    br->addWidget(cancelBtn_);
    br->addStretch();
    helpBtn_ = new QPushButton(tr("Help"));
    helpBtn_->setShortcut(QKeySequence::HelpContents);  // F1
    helpBtn_->setToolTip(tr("How translation works, and the languages of each model (F1)"));
    connect(helpBtn_, &QPushButton::clicked, this,
            [this] { pk::show_help_window(this, tr("Translate - Help"), helpHtml()); });
    br->addWidget(helpBtn_);
    infoBtn_ = new QPushButton(tr("Model files"));
    infoBtn_->setToolTip(tr("Show the downloaded models and where they are (and delete them)"));
    connect(infoBtn_, &QPushButton::clicked, this, &MainWindow::showModels);
    br->addWidget(infoBtn_);
    auto* quit = new QPushButton(tr("Quit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QPushButton::clicked, this, &QWidget::close);
    br->addWidget(quit);
    root->insertLayout(0, br);  // the action buttons go at the top of the window

    tick_ = new QTimer(this);
    tick_->setInterval(250);
    connect(tick_, &QTimer::timeout, this, &MainWindow::updateTiming);
}

// ------------------------------------------------------------------ helpers
void MainWindow::updateModelInfo() {
    const tl::ModelInfo* m = tl::find_model(modelCombo_->currentData().toString().toStdString());
    if (!m) return;
    modelDesc_->setText(q(m->description));
    QStringList notes;
    const QString to = toCombo_->currentData().toString(), from = fromCombo_->currentData().toString();
    if (!tl::model_supports(*m, to.toStdString()))
        notes << tr("%1 was not trained on %2; the translation may be poor. Qwen 3.5 handles every language.")
                     .arg(q(m->display), toCombo_->currentText());
    if (from != QLatin1String("auto") && !tl::model_supports(*m, from.toStdString()))
        notes << tr("%1 was not trained on %2.").arg(q(m->display), fromCombo_->currentText());
    if (from == to) notes << tr("Source and target language are the same.");
    langNote_->setText(notes.join(QLatin1Char(' ')));
    langNote_->setVisible(!notes.isEmpty());
    contextCheck_->setEnabled(m->family != tl::Family::TranslateGemma);
}

void MainWindow::swapLanguages() {
    const QString from = fromCombo_->currentData().toString(), to = toCombo_->currentData().toString();
    if (from == QLatin1String("auto")) {
        selectData(fromCombo_, to);
        return;
    }
    selectData(fromCombo_, to);
    selectData(toCombo_, from);
    if (!resultEdit_->toPlainText().isEmpty()) {  // translate back: the result becomes the source
        sourceEdit_->setPlainText(resultEdit_->toPlainText());
        resultEdit_->clear();
    }
}

QString MainWindow::cliPath() const {
#ifdef Q_OS_WIN
    return QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("translate_text.exe"));
#else
    return QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("translate_text"));
#endif
}

QStringList MainWindow::commonArguments() const {
    QStringList a{QStringLiteral("-m"), modelCombo_->currentData().toString(),
                  QStringLiteral("-s"), fromCombo_->currentData().toString(),
                  QStringLiteral("-t"), toCombo_->currentData().toString()};
    if (deviceCombo_->currentText() != QLatin1String("auto")) a << QStringLiteral("--device") << deviceCombo_->currentText();
    if (threadsSpin_->value() > 0) a << QStringLiteral("--threads") << QString::number(threadsSpin_->value());
    if (!contextCheck_->isChecked()) a << QStringLiteral("--no-context");
    if (!modelFileEdit_->text().trimmed().isEmpty()) a << QStringLiteral("--model-file") << absolutePath(modelFileEdit_->text());
    if (!modelsDirEdit_->text().trimmed().isEmpty()) a << QStringLiteral("--models-dir") << absolutePath(modelsDirEdit_->text());
    return a;
}

// ------------------------------------------------------------------ text mode
void MainWindow::translateText() {
    if (runner_.isActive()) return;
    const QString text = sourceEdit_->toPlainText();
    if (text.trimmed().isEmpty()) {
        sourceEdit_->setFocus();
        return;
    }
    if (fromCombo_->currentData() == toCombo_->currentData()) {
        QMessageBox::information(this, tr("Translate"), tr("Source and target language are the same."));
        return;
    }
    delete tempDir_;
    tempDir_ = new QTemporaryDir;
    if (!tempDir_->isValid()) {
        QMessageBox::critical(this, tr("Translate"), tr("Could not create a temporary folder."));
        return;
    }
    const QString in = tempDir_->filePath(QStringLiteral("input.txt"));
    textOutput_ = tempDir_->filePath(QStringLiteral("output.txt"));
    QFile f(in);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(text.toUtf8());
    f.close();
    resultEdit_->clear();
    QStringList args{in, QStringLiteral("-o"), textOutput_, QStringLiteral("--partial")};
    args << commonArguments();
    startProcess(args, Mode::Text);
}

// ------------------------------------------------------------------ file mode
void MainWindow::browseInput() {
    QString start = inputEdit_->text();
    if (start.isEmpty()) start = settings_.value(QStringLiteral("lastDir"), QDir::homePath()).toString();
    const QString p = QFileDialog::getOpenFileName(this, tr("Choose a file to translate"), start, tr(kFileFilter));
    if (!p.isEmpty()) openFile(p);
}

void MainWindow::browseOutput() {
    const QString start = outputEdit_->text().isEmpty() ? plannedOutput() : outputEdit_->text();
    const QString p = QFileDialog::getSaveFileName(this, tr("Save the translation as"), start, tr(kFileFilter), nullptr,
                                                   QFileDialog::DontConfirmOverwrite);  // asked when starting
    if (!p.isEmpty()) outputEdit_->setText(QDir::toNativeSeparators(p));
}

void MainWindow::openFile(const QString& path) {
    inputEdit_->setText(absolutePath(path));
    outputEdit_->clear();
    settings_.setValue(QStringLiteral("lastDir"), QFileInfo(path).absolutePath());
    tabs_->setCurrentIndex(1);
}

QString MainWindow::plannedOutput() const {
    if (!outputEdit_->text().trimmed().isEmpty()) return absolutePath(outputEdit_->text());
    const QFileInfo in(absolutePath(inputEdit_->text()));
    if (in.fileName().isEmpty()) return QString();
    const QString ext = in.suffix().isEmpty() ? QStringLiteral("txt") : in.suffix();
    return QDir::toNativeSeparators(QDir(in.absolutePath()).filePath(in.completeBaseName() + QLatin1Char('.') +
                                                      toCombo_->currentData().toString() + QLatin1Char('.') + ext));
}

bool MainWindow::confirmOverwrite(QString& out) {
    for (;;) {
        if (!QFileInfo::exists(out)) return true;
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Translate"));
        box.setText(tr("The output file already exists:"));
        box.setInformativeText(QDir::toNativeSeparators(out) + QStringLiteral("\n\n") + tr("Do you want to replace it?"));
        QPushButton* ow = box.addButton(tr("Overwrite"), QMessageBox::DestructiveRole);
        QPushButton* other = box.addButton(tr("Choose another name..."), QMessageBox::ActionRole);
        QPushButton* cancelBtn = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(cancelBtn);
        box.exec();
        if (box.clickedButton() == ow) return true;
        if (box.clickedButton() != other) return false;
        const QString p = QFileDialog::getSaveFileName(this, tr("Save the translation as"), out, tr(kFileFilter),
                                                       nullptr, QFileDialog::DontConfirmOverwrite);
        if (p.isEmpty()) return false;
        out = QDir::toNativeSeparators(p);
        outputEdit_->setText(out);
    }
}

void MainWindow::translateFile() {
    if (runner_.isActive()) return;
    QString in = inputEdit_->text().trimmed();
    if (in.size() >= 2 && in.startsWith(QLatin1Char('"')) && in.endsWith(QLatin1Char('"'))) in = in.mid(1, in.size() - 2);
    in = absolutePath(in);
    if (!QFileInfo(in).isFile()) {
        QMessageBox::warning(this, tr("Translate"), tr("Please choose an existing input file."));
        return;
    }
    if (fromCombo_->currentData() == toCombo_->currentData()) {
        QMessageBox::information(this, tr("Translate"), tr("Source and target language are the same."));
        return;
    }
    QString out = plannedOutput();
    if (QFileInfo(out).absoluteFilePath() == QFileInfo(in).absoluteFilePath()) {
        QMessageBox::warning(this, tr("Translate"), tr("The output file must be different from the input file."));
        return;
    }
    if (!confirmOverwrite(out)) return;
    out = absolutePath(out);
    if (out == in) {
        QMessageBox::warning(this, tr("Translate"), tr("The output file must be different from the input file."));
        return;
    }
    QStringList args{in, QStringLiteral("-o"), out};
    args << commonArguments();
    startProcess(args, Mode::File);
}

// ------------------------------------------------------------------ running
bool MainWindow::startProcess(const QStringList& args, Mode mode) {
    if (!QFileInfo::exists(cliPath())) {
        QMessageBox::critical(this, tr("Translate"),
                              tr("Can't find %1 next to this program.").arg(QFileInfo(cliPath()).fileName()));
        return false;
    }
    saveSettings();
    mode_ = mode;
    written_.clear();
    lastError_.clear();
    downloading_ = false;
    resetTiming();
    logView_->clear();
    QStringList shown;
    for (const QString& s : args) shown << (s.contains(QLatin1Char(' ')) ? QLatin1Char('"') + s + QLatin1Char('"') : s);
    appendLog(QStringLiteral("> translate_text ") + shown.join(QLatin1Char(' ')) + QLatin1Char('\n'));

    setRunning(true);
    statusLabel_->setText(tr("Starting..."));
    clock_.start();
    tick_->start();
    updateTiming();
    runner_.start(cliPath(), args);
    return true;
}

void MainWindow::setRunning(bool running) {
    translateTextBtn_->setEnabled(!running);
    translateFileBtn_->setEnabled(!running);
    cancelBtn_->setEnabled(running);
    infoBtn_->setEnabled(!running);  // no deleting a model while it may be in use
    modelCombo_->setEnabled(!running);
    if (running) {
        openFolderBtn_->setEnabled(false);
        progress_->setRange(0, 0);
    } else {
        progress_->setRange(0, 1);
        progress_->setValue(written_.isEmpty() ? 0 : 1);
    }
}

void MainWindow::showModels() {
    if (runner_.isActive()) return;
    // Only Translate's own model files count as ours (the folder may be shared).
    pk::ModelOwner ours;
    for (const auto& m : tl::model_table()) ours.names << q(m.file);
    pk::ModelsDialog dlg({{tr("Translation models"), pk::translate_models_dir(absolutePath(modelsDirEdit_->text())), ours}},
                         this);
    dlg.exec();
}

void MainWindow::cancel() {
    if (runner_.cancel()) statusLabel_->setText(tr("Cancelling..."));
}

void MainWindow::reloadPartial() {
    if (mode_ != Mode::Text || textOutput_.isEmpty()) return;
    QFile f(textOutput_);
    if (!f.open(QIODevice::ReadOnly)) return;
    resultEdit_->setPlainText(QString::fromUtf8(f.readAll()));
}

void MainWindow::parseLine(const QString& line) {
    static const QRegularExpression progressRe(QStringLiteral("^progress: (\\w+) (\\d+)/(\\d+) (\\w+)"));
    if (const auto m = progressRe.match(line); m.hasMatch()) {
        const qint64 done = m.captured(2).toLongLong(), total = m.captured(3).toLongLong();
        progress_->setRange(0, (int)std::max<qint64>(total, 1));
        progress_->setValue((int)done);
        if (m.captured(1) == QLatin1String("download")) {
            downloading_ = true;
            statusLabel_->setText(tr("Downloading the model (first use only): %1 of %2 GB")
                                      .arg(done / 1000.0, 0, 'f', 1)
                                      .arg(total / 1000.0, 0, 'f', 1));
        } else {
            piecesDone_ = (int)done;
            piecesTotal_ = (int)total;
            statusLabel_->setText(tr("Translating: part %1 of %2").arg(done).arg(total));
            reloadPartial();
        }
        return;
    }
    static const QRegularExpression wordsRe(QStringLiteral("^Words : (\\d+)"));
    if (const auto m = wordsRe.match(line); m.hasMatch()) {
        words_ = m.captured(1).toInt();
        wordsEdit_->setText(QLocale().toString(words_));
        return;
    }
    if (line.startsWith(QLatin1String("Translating ...")) && clock_.isValid()) {
        translateStartMs_ = clock_.elapsed();  // model loaded: translation starts now
        updateTiming();
    }
    static const QRegularExpression engineRe(QStringLiteral("^Engine: (\\S+)(?: \\((.*)\\))?\\s*$"));
    if (const auto m = engineRe.match(line); m.hasMatch()) {
        setEngine(m.captured(1), m.captured(2));
        return;
    }
    static const QRegularExpression detectedRe(QStringLiteral("^Detected: (\\S+)"));
    if (const auto m = detectedRe.match(line); m.hasMatch()) {
        const int i = fromCombo_->findData(m.captured(1));
        if (i >= 0) fromCombo_->setItemText(0, tr("Detect automatically (%1)").arg(fromCombo_->itemText(i)));
        return;
    }
    static const QRegularExpression doneRe(QStringLiteral("^Translated in ([0-9.]+) s \\((\\d+) words, (\\d+) words/min\\)"));
    if (const auto m = doneRe.match(line); m.hasMatch()) {
        translateSeconds_ = m.captured(1).toDouble();
        speedEdit_->setText(tr("%1 words/min").arg(QLocale().toString(m.captured(3).toInt())));
        updateTiming();
        return;
    }
    static const QRegularExpression wroteRe(QStringLiteral("^\\s*wrote (.+)$"));
    if (const auto m = wroteRe.match(line); m.hasMatch()) {
        written_ = m.captured(1).trimmed();
        return;
    }
    const QString s = line.trimmed();
    if (s.startsWith(QLatin1String("Error:"))) {
        lastError_ = s.mid(6).trimmed();
        return;
    }
    if (s.startsWith(QLatin1String("Loading")) || s.startsWith(QLatin1String("Downloading")) ||
        s.startsWith(QLatin1String("Translating")) || s.startsWith(QLatin1String("Note:")) ||
        s.startsWith(QLatin1String("Placement:"))) {
        statusLabel_->setText(s);
        if (s.startsWith(QLatin1String("Loading"))) progress_->setRange(0, 0);
    }
}

// The output has been read to the end (the last line too) when this runs.
void MainWindow::processFinished(int exitCode, bool cancelled) {
    tick_->stop();
    finished_ = true;
    updateTiming();
    if (cancelled || exitCode != 0) {
        if (translateSeconds_ < 0) speedEdit_->setText(cancelled ? tr("cancelled") : tr("failed"));
    }
    fromCombo_->setItemText(0, tr("Detect automatically"));
    if (cancelled) {
        statusLabel_->setText(tr("Cancelled."));
        appendLog(QStringLiteral("\n[cancelled]\n"));
    } else if (exitCode == 3) {
        if (mode_ == Mode::Text) reloadPartial();
        openFolderBtn_->setEnabled(!written_.isEmpty());
        statusLabel_->setText(tr("Partial translation saved. Failed units retain their original text; see the log."));
    } else if (exitCode == 0) {
        if (mode_ == Mode::Text) {
            reloadPartial();
            statusLabel_->setText(tr("Done."));
        } else {
            statusLabel_->setText(tr("Done: %1").arg(QFileInfo(written_).fileName()));
            openFolderBtn_->setEnabled(!written_.isEmpty());
        }
    } else {
        statusLabel_->setText(lastError_.isEmpty() ? tr("Failed (exit code %1). See the Log tab.").arg(exitCode)
                                                   : tr("Failed: %1").arg(lastError_));
        if (!lastError_.isEmpty() && mode_ == Mode::Text) resultEdit_->clear();
    }
    setRunning(false);
    if (mode_ == Mode::Text && exitCode != 0 && exitCode != 3) written_.clear();
}

void MainWindow::processFailedToStart() {
    tick_->stop();
    statusLabel_->setText(tr("Could not start translate_text."));
    setRunning(false);
}

void MainWindow::openOutputFolder() {
    if (!written_.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(written_).absolutePath()));
}

void MainWindow::setEngine(const QString& backend, const QString& detail) {
    QString colour = QStringLiteral("#6b6b6b");
    const QString b = backend.toLower();
    if (b.startsWith(QLatin1String("cuda"))) colour = QStringLiteral("#4e8a00");
    else if (b.startsWith(QLatin1String("vulkan"))) colour = QStringLiteral("#a41e22");
    QString html = tr("Compute:") +
                   QStringLiteral(" <span style=\"background:%1; color:white;\">&nbsp;<b>%2</b>&nbsp;</span>")
                       .arg(colour, backend.toHtmlEscaped());
    if (!detail.isEmpty()) html += QStringLiteral(" ") + detail.toHtmlEscaped();
    engineLabel_->setText(html);
    settings_.setValue(QStringLiteral("lastEngine"), backend);
    settings_.setValue(QStringLiteral("lastEngineDetail"), detail);
}

void MainWindow::resetTiming() {
    translateStartMs_ = -1;
    translateSeconds_ = -1;
    words_ = -1;
    piecesDone_ = piecesTotal_ = 0;
    finished_ = false;
    for (QLineEdit* e : {wordsEdit_, loadEdit_, translateEdit_, speedEdit_, totalEdit_})
        e->setText(QStringLiteral("\u2014"));
}

void MainWindow::updateTiming() {
    if (!clock_.isValid()) return;
    const double now = clock_.elapsed() / 1000.0;
    totalEdit_->setText(formatDuration(now));
    if (translateStartMs_ < 0) {
        if (!finished_) loadEdit_->setText(formatDuration(now));  // still loading / downloading
        return;
    }
    loadEdit_->setText(formatDuration(translateStartMs_ / 1000.0));
    const double translating = translateSeconds_ >= 0 ? translateSeconds_ : now - translateStartMs_ / 1000.0;
    translateEdit_->setText(formatDuration(translating));
    // While running: estimate from the share of pieces done (pieces are of similar size).
    if (translateSeconds_ < 0 && !finished_ && words_ > 0 && piecesTotal_ > 0 && piecesDone_ > 0 && translating > 0.5) {
        const double wpm = words_ * (double)piecesDone_ / piecesTotal_ * 60.0 / translating;
        speedEdit_->setText(tr("\u2248 %1 words/min").arg(QLocale().toString(qRound(wpm))));
    }
}

void MainWindow::appendLog(const QString& text) {
    logView_->moveCursor(QTextCursor::End);
    logView_->insertPlainText(text);
    logView_->moveCursor(QTextCursor::End);
}

// ------------------------------------------------------------------ drag & drop, settings
void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile()) {
            openFile(u.toLocalFile());
            break;
        }
}

void MainWindow::loadSettings() {
    auto& s = settings_;
    selectData(modelCombo_, s.value(QStringLiteral("model"), QStringLiteral("translategemma-12b")));
    selectData(fromCombo_, s.value(QStringLiteral("from"), QStringLiteral("auto")));
    selectData(toCombo_, s.value(QStringLiteral("to"), QStringLiteral("en")));
    deviceCombo_->setCurrentText(s.value(QStringLiteral("device"), QStringLiteral("auto")).toString());
    threadsSpin_->setValue(s.value(QStringLiteral("threads"), 0).toInt());
    contextCheck_->setChecked(s.value(QStringLiteral("context"), true).toBool());
    modelsDirEdit_->setText(s.value(QStringLiteral("modelsDir")).toString());
    const QString last = s.value(QStringLiteral("lastEngine")).toString();
    if (last.isEmpty()) engineLabel_->setText(tr("Compute: <i>shown when a translation starts</i>"));
    else {
        setEngine(last, s.value(QStringLiteral("lastEngineDetail")).toString());
        engineLabel_->setText(engineLabel_->text() + QStringLiteral(" <i>") + tr("(last run)") + QStringLiteral("</i>"));
    }
}

void MainWindow::saveSettings() {
    auto& s = settings_;
    s.setValue(QStringLiteral("model"), modelCombo_->currentData());
    s.setValue(QStringLiteral("from"), fromCombo_->currentData());
    s.setValue(QStringLiteral("to"), toCombo_->currentData());
    s.setValue(QStringLiteral("device"), deviceCombo_->currentText());
    s.setValue(QStringLiteral("threads"), threadsSpin_->value());
    s.setValue(QStringLiteral("context"), contextCheck_->isChecked());
    s.setValue(QStringLiteral("modelsDir"), modelsDirEdit_->text());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (runner_.isRunning() &&
        QMessageBox::question(this, tr("Translate"), tr("A translation is running. Stop it and quit?")) !=
            QMessageBox::Yes) {
        event->ignore();
        return;
    }
    runner_.stop();  // disconnected first: nothing reaches the closing window, even if the tool hangs
    saveSettings();
    event->accept();
}
