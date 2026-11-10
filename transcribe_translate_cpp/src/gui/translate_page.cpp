#include "translate_page.h"
#include "translate_engine.h"
#include "faster_mode.h"
#include "pdf_files.h"

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
#include <QtMath>
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
    "Documents (*.txt *.md *.markdown *.srt *.docx *.pdf);;Text (*.txt);;Markdown (*.md *.markdown);;"
    "Subtitles (*.srt);;Word (*.docx);;PDF (*.pdf);;All files (*)";

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
}  // namespace

QString TranslatePage::helpHtml() {
    QString h = QStringLiteral(
        "<a name=\"translate\"></a><h2>Translate</h2>"
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
        "<p><b>PDF</b> files are translated into a new PDF (report.pdf becomes report.de.pdf). A PDF stores "
        "positioned text rather than paragraphs, so the text is read page by page and rebuilt into headings and "
        "paragraphs in reading order; the translation is then laid out afresh on pages of the same size, with "
        "the page breaks between paragraphs kept. Columns, images, tables and the exact positions are not kept. "
        "Page numbers and running headers are left out. A scanned PDF (pages that are only pictures) has no "
        "text to read and can't be translated. Text (.txt) and Markdown (.md) files can also be saved as PDF: "
        "choose an output file ending in .pdf.</p>"

        "<h3>Advanced</h3>"
        "<ul>"
        "<li><b>Device</b> chooses where the model runs: <i>auto</i> uses an NVIDIA graphics card with CUDA, other "
        "graphics cards with Vulkan, or the processor. A model larger than the graphics card's memory runs partly "
        "on the processor, which is slower. <b>CPU threads</b> limits the processor threads used.</li>"
        "<li><b>Show the model the preceding text</b> (Hy-MT2 and Qwen) helps with names, pronouns and "
        "consistent terms.</li>"
        "<li><b>Faster mode</b> (TranslateGemma 12B and 27B; speculative decoding): a small helper model guesses "
        "a few words ahead and the large model checks all of them in one step, keeping only the words it would "
        "have written itself. The translation is the same, sooner. It times the steps as it goes and drafts only "
        "as far ahead as pays off (not at all where the guesses are mostly wrong), so it is not slower than "
        "without it. Both models must fit on the graphics card. <i>TranslateGemma 4B</i> (2.5 GB) guesses best; "
        "<i>Gemma 3 1B</i> (1.1 GB) guesses less well but is small enough to fit beside 12B on a 16 GB card. "
        "<i>The best helper that fits</i> takes 4B when there is room, else 1B. Another way on a 16 GB card: "
        "<b>TranslateGemma 12B (smaller)</b>, a 7.3 GB file of the same model (slightly lower quality), with room "
        "for the 4B helper. 27B with a helper needs about 24 GB. The helper is downloaded the first time. Next "
        "to <b>Compute</b> the tab shows whether the faster mode is on (or why not) and, at the end, the speed it "
        "gave; the log has the details.</li>"
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

namespace {

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

TranslatePage::TranslatePage(QWidget* parent)
    : QWidget(parent), settings_(QStringLiteral("DavidV"), QStringLiteral("TranscribeTranslate")) {
    settings_.beginGroup(QStringLiteral("Translate"));
    setAcceptDrops(true);
    buildUi();
    loadSettings();
    updateModelInfo();
}

TranslatePage::~TranslatePage() {
    if (job_) tt::TranslateEngine::instance().shutdown();  // before tempDir_ goes: the engine may still be writing into it
    delete tempDir_;
}

void TranslatePage::buildUi() {
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

    connect(modelCombo_, &QComboBox::currentIndexChanged, this, &TranslatePage::updateModelInfo);
    connect(fromCombo_, &QComboBox::currentIndexChanged, this, &TranslatePage::updateModelInfo);
    connect(toCombo_, &QComboBox::currentIndexChanged, this, &TranslatePage::updateModelInfo);
    connect(swap, &QPushButton::clicked, this, &TranslatePage::swapLanguages);

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
    connect(translateTextBtn_, &QPushButton::clicked, this, &TranslatePage::translateText);
    connect(copyBtn, &QPushButton::clicked, this,
            [this] { QApplication::clipboard()->setText(resultEdit_->toPlainText()); });
    connect(clearBtn, &QPushButton::clicked, this, [this] {
        sourceEdit_->clear();
        resultEdit_->clear();
    });
    auto* sc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), textTab);
    connect(sc, &QShortcut::activated, this, &TranslatePage::translateText);
    tabs_->addTab(textTab, tr("Text"));

    auto* fileTab = new QWidget;
    auto* fv = new QVBoxLayout(fileTab);
    auto* ff = new QFormLayout;
    inputEdit_ = new QLineEdit;
    inputEdit_->setPlaceholderText(tr("A .txt, .md, .srt, .docx or .pdf file"));
    auto* bi = new QPushButton(tr("Browse..."));
    connect(bi, &QPushButton::clicked, this, &TranslatePage::browseInput);
    ff->addRow(tr("Input file:"), row(inputEdit_, bi));
    outputEdit_ = new QLineEdit;
    outputEdit_->setPlaceholderText(tr("(default: next to the input, e.g. report.de.docx)"));
    auto* bo = new QPushButton(tr("Browse..."));
    connect(bo, &QPushButton::clicked, this, &TranslatePage::browseOutput);
    ff->addRow(tr("Output file:"), row(outputEdit_, bo));
    fv->addLayout(ff);
    auto* note = new QLabel(
        tr("Text and Markdown keep their layout; code blocks are not translated. Subtitles keep their timing. "
           "Word documents keep paragraph styles, tables, headers and footers, but formatting that changes "
           "inside a paragraph (a single bold word, say) is not kept. PDFs are rebuilt as headings and "
           "paragraphs on new pages (columns, images and exact positions are not kept)."));
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
    connect(translateFileBtn_, &QPushButton::clicked, this, &TranslatePage::translateFile);
    connect(openFolderBtn_, &QPushButton::clicked, this, &TranslatePage::openOutputFolder);
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
    draftCombo_ = tt::makeFasterModeCombo();
    af->addRow(tr("Faster mode:"), draftCombo_);
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

    // Cancel at the top, as on the other tabs (the Translate buttons are on the Text and File tabs).
    auto* br = new QHBoxLayout;
    cancelBtn_ = new QPushButton(tr("Cancel"));
    cancelBtn_->setEnabled(false);
    connect(cancelBtn_, &QPushButton::clicked, this, &TranslatePage::cancel);
    br->addWidget(cancelBtn_);
    br->addStretch();
    root->insertLayout(0, br);  // the action buttons go at the top of the window

    tick_ = new QTimer(this);
    tick_->setInterval(250);
    connect(tick_, &QTimer::timeout, this, &TranslatePage::updateTiming);
}

// ------------------------------------------------------------------ helpers
void TranslatePage::updateModelInfo() {
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
    draftCombo_->setEnabled(tt::fasterModeApplies(m));
}

void TranslatePage::swapLanguages() {
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

QString TranslatePage::cliPath() const { return tt::enginePath(); }

QStringList TranslatePage::commonArguments() const {
    QStringList a{QStringLiteral("-m"), modelCombo_->currentData().toString(),
                  QStringLiteral("-s"), fromCombo_->currentData().toString(),
                  QStringLiteral("-t"), toCombo_->currentData().toString()};
    if (deviceCombo_->currentText() != QLatin1String("auto")) a << QStringLiteral("--device") << deviceCombo_->currentText();
    if (threadsSpin_->value() > 0) a << QStringLiteral("--threads") << QString::number(threadsSpin_->value());
    if (!contextCheck_->isChecked()) a << QStringLiteral("--no-context");
    // (isEnabled() would also be false while a job runs and the form is locked.)
    const tl::ModelInfo* m = tl::find_model(modelCombo_->currentData().toString().toStdString());
    const QString draft = draftCombo_->currentData().toString();
    if (tt::fasterModeApplies(m) && draft != QLatin1String("off")) a << QStringLiteral("--draft") << draft;
    if (!modelFileEdit_->text().trimmed().isEmpty()) a << QStringLiteral("--model-file") << absolutePath(modelFileEdit_->text());
    if (!modelsDirEdit_->text().trimmed().isEmpty()) a << QStringLiteral("--models-dir") << absolutePath(modelsDirEdit_->text());
    return a;
}

// ------------------------------------------------------------------ text mode
void TranslatePage::translateText() {
    if (job_) return;
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
void TranslatePage::browseInput() {
    QString start = inputEdit_->text();
    if (start.isEmpty()) start = settings_.value(QStringLiteral("lastDir"), QDir::homePath()).toString();
    const QString p = QFileDialog::getOpenFileName(this, tr("Choose a file to translate"), start, tr(kFileFilter));
    if (!p.isEmpty()) openFile(p);
}

void TranslatePage::browseOutput() {
    const QString start = outputEdit_->text().isEmpty() ? plannedOutput() : outputEdit_->text();
    const QString p = QFileDialog::getSaveFileName(this, tr("Save the translation as"), start, tr(kFileFilter), nullptr,
                                                   QFileDialog::DontConfirmOverwrite);  // asked when starting
    if (!p.isEmpty()) outputEdit_->setText(QDir::toNativeSeparators(p));
}

void TranslatePage::openFile(const QString& path) {
    inputEdit_->setText(absolutePath(path));
    outputEdit_->clear();
    settings_.setValue(QStringLiteral("lastDir"), QFileInfo(path).absolutePath());
    tabs_->setCurrentIndex(1);
}

QString TranslatePage::plannedOutput() const {
    if (!outputEdit_->text().trimmed().isEmpty()) return absolutePath(outputEdit_->text());
    const QFileInfo in(absolutePath(inputEdit_->text()));
    if (in.fileName().isEmpty()) return QString();
    const QString ext = in.suffix().isEmpty() ? QStringLiteral("txt") : in.suffix();
    return QDir::toNativeSeparators(QDir(in.absolutePath()).filePath(in.completeBaseName() + QLatin1Char('.') +
                                                      toCombo_->currentData().toString() + QLatin1Char('.') + ext));
}

bool TranslatePage::confirmOverwrite(QString& out) {
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

void TranslatePage::translateFile() {
    if (job_) return;
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
    // PDFs: the engine translates Markdown (from a PDF) or the text file, into the
    // temporary folder; finishPdf() makes the PDF from that.
    const QString inExt = QFileInfo(in).suffix().toLower(), outExt = QFileInfo(out).suffix().toLower();
    const bool pdfIn = inExt == QLatin1String("pdf"), pdfOut = outExt == QLatin1String("pdf");
    const bool markdown = inExt == QLatin1String("md") || inExt == QLatin1String("markdown");
    if (pdfIn && !pdfOut) {
        QMessageBox::warning(this, tr("Translate"), tr("A PDF is translated into a PDF: choose an output file ending in .pdf."));
        return;
    }
    if (pdfOut && !pdfIn && !markdown && inExt != QLatin1String("txt")) {
        QMessageBox::warning(this, tr("Translate"),
                             tr("Only PDF, text (.txt) and Markdown (.md) files can be saved as PDF. "
                                "Save a Word document as .docx, or subtitles as .srt."));
        return;
    }
    pdfFrom_ = PdfFrom::None;
    QString engineIn = in, engineOut = out;
    QStringList notes;
    if (pdfOut) {
        delete tempDir_;
        tempDir_ = new QTemporaryDir;
        if (!tempDir_->isValid()) {
            QMessageBox::critical(this, tr("Translate"), tr("Could not create a temporary folder."));
            return;
        }
        pdfOutput_ = out;
        pdfTitle_ = QFileInfo(out).completeBaseName();
        if (pdfIn) {
            statusLabel_->setText(tr("Reading the PDF..."));
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const tt::PdfReadResult r = tt::readPdf(in);
            QApplication::restoreOverrideCursor();
            if (!r.ok) {
                statusLabel_->setText(tr("Could not read the PDF."));
                QMessageBox::warning(this, tr("Translate"),
                                     tr("Could not read %1:\n\n%2").arg(QFileInfo(in).fileName(), r.error));
                return;
            }
            if (!r.doc.title.isEmpty()) pdfTitle_ = r.doc.title;
            if (!r.emptyPages.isEmpty()) {
                QStringList pages;
                for (int p : r.emptyPages) pages << QString::number(p);
                notes << tr("Note: page(s) %1 of %2 have no text (pictures only?) and are left out.")
                             .arg(pages.join(QStringLiteral(", ")))
                             .arg(r.pages);
            }
            notes << tr("PDF: %1 pages, %2 headings and paragraphs").arg(r.pages).arg([&] {
                int n = 0;
                for (const auto& b : r.doc.blocks) n += b.kind != tt::PdfBlock::PageBreak;
                return n;
            }());
            engineIn = tempDir_->filePath(QStringLiteral("source.md"));
            QFile f(engineIn);
            if (!f.open(QIODevice::WriteOnly) || f.write(tt::pdfToMarkdown(r.doc).toUtf8()) < 0) {
                QMessageBox::critical(this, tr("Translate"), tr("Could not write a temporary file."));
                return;
            }
            f.close();
            engineOut = tempDir_->filePath(QStringLiteral("translated.md"));
            pdfFrom_ = PdfFrom::Pdf;
        } else {
            engineOut = tempDir_->filePath(QStringLiteral("translated.") + (markdown ? QStringLiteral("md") : inExt));
            pdfFrom_ = markdown ? PdfFrom::Markdown : PdfFrom::Text;
        }
        pdfEngineOutput_ = engineOut;
    }
    QStringList args{engineIn, QStringLiteral("-o"), engineOut};
    args << commonArguments();
    if (startProcess(args, Mode::File)) {
        runInput_ = in;  // the user's file, not the temporary Markdown (History)
        for (const QString& n : notes) appendLog(n + QLatin1Char('\n'));
    } else {
        pdfFrom_ = PdfFrom::None;
    }
}

bool TranslatePage::finishPdf() {
    QFile f(pdfEngineOutput_);
    if (!f.open(QIODevice::ReadOnly)) {
        lastError_ = tr("the translation was not written (%1)").arg(f.errorString());
        return false;
    }
    const QString text = QString::fromUtf8(f.readAll());
    tt::PdfDocument doc;
    if (pdfFrom_ == PdfFrom::Pdf) {
        doc = tt::pdfFromMarkdown(text);
    } else if (pdfFrom_ == PdfFrom::Markdown) {
        doc.markdown = text;
    } else {
        doc = tt::pdfFromText(text);
    }
    doc.title = pdfTitle_;
    doc.language = toCombo_->currentData().toString();
    QString error;
    if (!tt::writePdf(doc, pdfOutput_, &error)) {
        lastError_ = tr("could not write %1: %2").arg(QFileInfo(pdfOutput_).fileName(), error);
        appendLog(QStringLiteral("\nError: ") + lastError_ + QLatin1Char('\n'));
        return false;
    }
    written_ = QDir::toNativeSeparators(pdfOutput_);
    appendLog(QStringLiteral("  wrote %1\n").arg(written_));
    return true;
}

// ------------------------------------------------------------------ running
bool TranslatePage::startProcess(const QStringList& args, Mode mode) {
    if (!tt::JobGate::instance().checkFree(this)) return false;
    if (!QFileInfo::exists(cliPath())) {
        QMessageBox::critical(this, tr("Translate"),
                              tr("Can't find %1 next to this program.").arg(QFileInfo(cliPath()).fileName()));
        return false;
    }
    saveSettings();
    mode_ = mode;
    runInput_ = mode == Mode::File ? args.value(0) : QString();
    written_.clear();
    lastError_.clear();
    draftNote_.clear();
    engineHtml_.clear();
    downloading_ = false;
    resetTiming();
    logView_->clear();
    QStringList shown;
    for (const QString& s : args) shown << (s.contains(QLatin1Char(' ')) ? QLatin1Char('"') + s + QLatin1Char('"') : s);
    appendLog(QStringLiteral("> tt_engine translate ") + shown.join(QLatin1Char(' ')) + QLatin1Char('\n'));

    setRunning(true);
    statusLabel_->setText(tr("Starting..."));
    clock_.start();
    tick_->start();
    updateTiming();
    // In the engine that stays running, so the model stays loaded for the next job.
    tt::JobGate::instance().acquire(tr("Translate"), true);
    job_ = tt::TranslateEngine::instance().start(args);
    if (!job_) {  // (can't happen: one job at a time)
        processFailedToStart();
        return false;
    }
    connect(job_, &tt::EngineJob::outputReceived, this, &TranslatePage::appendLog);
    connect(job_, &tt::EngineJob::lineReceived, this, &TranslatePage::parseLine);
    connect(job_, &tt::EngineJob::finished, this, &TranslatePage::processFinished);
    connect(job_, &tt::EngineJob::failedToStart, this, &TranslatePage::processFailedToStart);
    return true;
}

void TranslatePage::setRunning(bool running) {
    translateTextBtn_->setEnabled(!running);
    translateFileBtn_->setEnabled(!running);
    cancelBtn_->setEnabled(running);
    modelCombo_->setEnabled(!running);
    progress_->setTextVisible(false);  // the percentage shows while translating
    if (running) {
        openFolderBtn_->setEnabled(false);
        progress_->setRange(0, 0);
    } else {
        progress_->setRange(0, 1);
        progress_->setValue(written_.isEmpty() ? 0 : 1);
    }
}

QString TranslatePage::modelsDir() const { return absolutePath(modelsDirEdit_->text()); }

void TranslatePage::cancel() {
    if (job_ && tt::TranslateEngine::instance().cancel()) statusLabel_->setText(tr("Cancelling..."));
}

void TranslatePage::reloadPartial() {
    if (mode_ != Mode::Text || textOutput_.isEmpty()) return;
    QFile f(textOutput_);
    if (!f.open(QIODevice::ReadOnly)) return;
    resultEdit_->setPlainText(QString::fromUtf8(f.readAll()));
}

void TranslatePage::parseLine(const QString& line) {
    static const QRegularExpression progressRe(QStringLiteral("^progress: (\\w+) (\\d+)/(\\d+) (\\w+)"));
    if (const auto m = progressRe.match(line); m.hasMatch()) {
        const qint64 done = m.captured(2).toLongLong(), total = m.captured(3).toLongLong();
        const QString phase = m.captured(1);
        if (phase == QLatin1String("translating")) {  // the share done, as the model writes
            fraction_ = total > 0 ? (double)done / (double)total : 0.0;
            progress_->setRange(0, 1000);
            progress_->setValue(qRound(fraction_ * 1000));
            progress_->setFormat(QStringLiteral("%1%").arg(qFloor(fraction_ * 100)));
            progress_->setTextVisible(true);
            showTranslating();
            return;
        }
        if (phase == QLatin1String("download")) {
            progress_->setRange(0, (int)std::max<qint64>(total, 1));
            progress_->setValue((int)done);
            progress_->setTextVisible(false);
            downloading_ = true;
            statusLabel_->setText(tr("Downloading the model (first use only): %1 of %2 GB")
                                      .arg(done / 1000.0, 0, 'f', 1)
                                      .arg(total / 1000.0, 0, 'f', 1));
        } else {
            // A piece finished (the bar follows "translating").
            piecesDone_ = (int)done;
            piecesTotal_ = (int)total;
            if (fraction_ < 0) {  // an engine without "translating" lines
                progress_->setRange(0, (int)std::max<qint64>(total, 1));
                progress_->setValue((int)done);
            }
            showTranslating();
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
    // The faster mode (speculative decoding): on, off and why, and what it gave.
    static const QRegularExpression draftOnRe(QStringLiteral("^Draft : \\S+ \\(speculative"));
    static const QRegularExpression draftOffRe(QStringLiteral("^Note: (?:draft model not used: |draft model |no draft model - )(.*)$"));
    static const QRegularExpression draftGainRe(QStringLiteral("^Draft : drafted in .*\\(([0-9.]+)x\\)"));
    if (draftOnRe.match(line).hasMatch()) setDraftNote(tr("faster mode on"));
    else if (const auto m = draftOffRe.match(line); m.hasMatch()) setDraftNote(tr("faster mode off: %1").arg(m.captured(1)));
    else if (const auto m2 = draftGainRe.match(line); m2.hasMatch())
        setDraftNote(tr("faster mode: %1x the speed").arg(QLocale().toString(m2.captured(1).toDouble(), 'f', 2)));
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
        if (s.startsWith(QLatin1String("Loading"))) {
            progress_->setTextVisible(false);
            progress_->setRange(0, 0);
        }
    }
}

void TranslatePage::showTranslating() {
    QString s = fraction_ >= 0 ? tr("Translating: %1%").arg(qFloor(fraction_ * 100)) : tr("Translating");
    if (piecesTotal_ > 1)
        s += QStringLiteral(" ") + tr("(part %1 of %2)").arg(std::min(piecesDone_ + 1, piecesTotal_)).arg(piecesTotal_);
    statusLabel_->setText(s);
}

// The output has been read to the end (the last line too) when this runs.
void TranslatePage::processFinished(int exitCode, bool cancelled) {
    job_ = nullptr;  // (it deletes itself)
    tt::JobGate::instance().release();
    tick_->stop();
    // The PDF, also from a partial translation (exit code 3).
    if (mode_ == Mode::File && pdfFrom_ != PdfFrom::None) {
        written_.clear();  // the engine wrote the temporary file
        if (!cancelled && (exitCode == 0 || exitCode == 3) && !finishPdf()) exitCode = 1;
        pdfFrom_ = PdfFrom::None;
    }
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
    // Text-box runs write to a temporary file: nothing to list for them.
    emit jobFinished({tr("Translate"), mode_ == Mode::File ? runInput_ : tr("(text box)"),
                      mode_ == Mode::File && !written_.isEmpty() ? QStringList{written_} : QStringList{},
                      statusLabel_->text(), !cancelled && exitCode == 0, QDateTime::currentDateTime()});
}

void TranslatePage::processFailedToStart() {
    job_ = nullptr;
    tt::JobGate::instance().release();
    tick_->stop();
    statusLabel_->setText(tr("Could not start tt_engine."));
    setRunning(false);
    emit jobFinished({tr("Translate"), mode_ == Mode::File ? runInput_ : tr("(text box)"), {}, statusLabel_->text(),
                      false, QDateTime::currentDateTime()});
}

void TranslatePage::openOutputFolder() {
    if (!written_.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(written_).absolutePath()));
}

void TranslatePage::setEngine(const QString& backend, const QString& detail) {
    QString colour = QStringLiteral("#6b6b6b");
    const QString b = backend.toLower();
    if (b.startsWith(QLatin1String("cuda"))) colour = QStringLiteral("#4e8a00");
    else if (b.startsWith(QLatin1String("vulkan"))) colour = QStringLiteral("#a41e22");
    QString html = tr("Compute:") +
                   QStringLiteral(" <span style=\"background:%1; color:white;\">&nbsp;<b>%2</b>&nbsp;</span>")
                       .arg(colour, backend.toHtmlEscaped());
    if (!detail.isEmpty()) html += QStringLiteral(" ") + detail.toHtmlEscaped();
    engineHtml_ = html;
    engineLabel_->setText(html + engineNotes());
    settings_.setValue(QStringLiteral("lastEngine"), backend);
    settings_.setValue(QStringLiteral("lastEngineDetail"), detail);
}

void TranslatePage::setDraftNote(const QString& note) {
    draftNote_ = note;  // also when it comes before the Engine line (a note from before loading)
    if (!engineHtml_.isEmpty()) engineLabel_->setText(engineHtml_ + engineNotes());
}

QString TranslatePage::engineNotes() const {
    QString s;
    if (!draftNote_.isEmpty()) s += QStringLiteral(" &nbsp;\u00B7&nbsp; ") + draftNote_.toHtmlEscaped();
    return s;
}

void TranslatePage::resetTiming() {
    translateStartMs_ = -1;
    translateSeconds_ = -1;
    words_ = -1;
    piecesDone_ = piecesTotal_ = 0;
    fraction_ = -1;
    finished_ = false;
    for (QLineEdit* e : {wordsEdit_, loadEdit_, translateEdit_, speedEdit_, totalEdit_})
        e->setText(QStringLiteral("\u2014"));
}

void TranslatePage::updateTiming() {
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
    // While running: estimate from the share translated so far.
    const double share = fraction_ >= 0 ? fraction_ : piecesTotal_ > 0 ? (double)piecesDone_ / piecesTotal_ : 0.0;
    if (translateSeconds_ < 0 && !finished_ && words_ > 0 && share > 0.02 && translating > 0.5) {
        const double wpm = words_ * share * 60.0 / translating;
        speedEdit_->setText(tr("\u2248 %1 words/min").arg(QLocale().toString(qRound(wpm))));
    }
}

void TranslatePage::appendLog(const QString& text) {
    logView_->moveCursor(QTextCursor::End);
    logView_->insertPlainText(text);
    logView_->moveCursor(QTextCursor::End);
}

// ------------------------------------------------------------------ drag & drop, settings
void TranslatePage::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void TranslatePage::dropEvent(QDropEvent* e) {
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile()) {
            openFile(u.toLocalFile());
            break;
        }
}

void TranslatePage::loadSettings() {
    auto& s = settings_;
    selectData(modelCombo_, s.value(QStringLiteral("model"), QStringLiteral("translategemma-12b")));
    selectData(fromCombo_, s.value(QStringLiteral("from"), QStringLiteral("auto")));
    selectData(toCombo_, s.value(QStringLiteral("to"), QStringLiteral("en")));
    deviceCombo_->setCurrentText(s.value(QStringLiteral("device"), QStringLiteral("auto")).toString());
    threadsSpin_->setValue(s.value(QStringLiteral("threads"), 0).toInt());
    contextCheck_->setChecked(s.value(QStringLiteral("context"), true).toBool());
    tt::selectFasterMode(draftCombo_, tt::loadFasterMode(s));
    modelsDirEdit_->setText(s.value(QStringLiteral("modelsDir")).toString());
    const QString last = s.value(QStringLiteral("lastEngine")).toString();
    if (last.isEmpty()) engineLabel_->setText(tr("Compute: <i>shown when a translation starts</i>"));
    else {
        setEngine(last, s.value(QStringLiteral("lastEngineDetail")).toString());
        engineLabel_->setText(engineLabel_->text() + QStringLiteral(" <i>") + tr("(last run)") + QStringLiteral("</i>"));
    }
}

void TranslatePage::saveSettings() {
    auto& s = settings_;
    s.setValue(QStringLiteral("model"), modelCombo_->currentData());
    s.setValue(QStringLiteral("from"), fromCombo_->currentData());
    s.setValue(QStringLiteral("to"), toCombo_->currentData());
    s.setValue(QStringLiteral("device"), deviceCombo_->currentText());
    s.setValue(QStringLiteral("threads"), threadsSpin_->value());
    s.setValue(QStringLiteral("context"), contextCheck_->isChecked());
    s.setValue(QStringLiteral("draftModel"), draftCombo_->currentData());
    s.setValue(QStringLiteral("modelsDir"), modelsDirEdit_->text());
}


void TranslatePage::shutdown() {
    if (job_) tt::JobGate::instance().release();
    // The window closes: end the engine (and free its model), running or not.
    tt::TranslateEngine::instance().shutdown();
    saveSettings();
}
