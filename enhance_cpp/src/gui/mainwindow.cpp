// mainwindow.cpp - see mainwindow.h.
#include "mainwindow.h"

#include <QApplication>
#include <QAudioOutput>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "help_window.h"  // ../common/qt

namespace {

const char* const kModelFiles[] = {"filters.bin", "denoiser.onnx", "encoder.onnx",
                                   "velocity.onnx", "decoder.onnx", "vocoder.onnx"};

const QStringList& videoExtensions() {
    static const QStringList v = {QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("mov"),
                                  QStringLiteral("m4v"), QStringLiteral("webm"), QStringLiteral("avi")};
    return v;
}

QString mediaFilter() {
    return QObject::tr("Audio and video (*.wav *.mp3 *.flac *.m4a *.aac *.ogg *.opus *.wma *.aif *.aiff "
                       "*.mp4 *.mkv *.mov *.m4v *.webm *.avi);;All files (*)");
}

QString formatDuration(double s) {
    if (s < 0) return QStringLiteral("—");
    const int total = (int)(s + 0.5);
    return total >= 3600 ? QStringLiteral("%1:%2:%3").arg(total / 3600).arg(total / 60 % 60, 2, 10, QLatin1Char('0'))
                               .arg(total % 60, 2, 10, QLatin1Char('0'))
                         : QStringLiteral("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QLatin1Char('0'));
}

// Quality presets: CFM function evaluations.
struct Quality { const char* name; int nfe; };
const Quality kQualities[] = {{QT_TRANSLATE_NOOP("MainWindow", "Draft (16 steps, fastest)"), 16},
                              {QT_TRANSLATE_NOOP("MainWindow", "Good (32 steps)"), 32},
                              {QT_TRANSLATE_NOOP("MainWindow", "Best (64 steps, as Resemble's default)"), 64},
                              {QT_TRANSLATE_NOOP("MainWindow", "Maximum (128 steps, slowest)"), 128}};

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent), settings_(QStringLiteral("DavidVickers"), QStringLiteral("EnhanceAudio")) {
    setWindowTitle(tr("Enhance Audio"));
    setAcceptDrops(true);
    buildUi();
    loadSettings();
    connect(&runner_, &pk::ChildProcess::lineReceived, this, &MainWindow::parseLine);
    connect(&runner_, &pk::ChildProcess::finished, this, &MainWindow::processFinished);
    connect(&runner_, &pk::ChildProcess::failedToStart, this, &MainWindow::processFailedToStart);
    updateLabels();
    updateEnabledState();
    if (!modelsPresent(false))
        statusLabel_->setText(tr("<b>The model files were not found</b> in %1. Build the program with "
                                 "build_all.bat, which creates them.")
                                  .arg(QDir::toNativeSeparators(modelsDir()).toHtmlEscaped()));
}

MainWindow::~MainWindow() {
    runner_.stop();
}

void MainWindow::buildUi() {
    auto* root = new QVBoxLayout(this);

    // ---- files ----
    filesBox_ = new QGroupBox(tr("Files"));
    auto* fl = new QVBoxLayout(filesBox_);
    fileList_ = new QListWidget;
    fileList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fileList_->setMinimumHeight(90);
    fileList_->setToolTip(tr("Audio or video files to clean up. Drop files here or use Add files."));
    fl->addWidget(fileList_);
    auto* fileButtons = new QHBoxLayout;
    addBtn_ = new QPushButton(tr("Add files..."));
    removeBtn_ = new QPushButton(tr("Remove"));
    clearBtn_ = new QPushButton(tr("Clear"));
    fileButtons->addWidget(addBtn_);
    fileButtons->addWidget(removeBtn_);
    fileButtons->addWidget(clearBtn_);
    fileButtons->addStretch();
    fl->addLayout(fileButtons);

    auto* outForm = new QFormLayout;
    auto* where = new QWidget;
    auto* wl = new QHBoxLayout(where);
    wl->setContentsMargins(0, 0, 0, 0);
    nextToInput_ = new QRadioButton(tr("Next to each file"));
    toFolder_ = new QRadioButton(tr("In this folder:"));
    outDirEdit_ = new QLineEdit;
    outDirBtn_ = new QPushButton(tr("Browse..."));
    auto* whereGroup = new QButtonGroup(this);
    whereGroup->addButton(nextToInput_);
    whereGroup->addButton(toFolder_);
    nextToInput_->setChecked(true);
    wl->addWidget(nextToInput_);
    wl->addWidget(toFolder_);
    wl->addWidget(outDirEdit_, 1);
    wl->addWidget(outDirBtn_);
    outForm->addRow(tr("Save:"), where);
    formatCombo_ = new QComboBox;
    formatCombo_->addItem(tr("WAV (32-bit float, lossless)"), QStringLiteral("wav"));
    formatCombo_->addItem(tr("FLAC (lossless, smaller)"), QStringLiteral("flac"));
    formatCombo_->addItem(tr("MP3"), QStringLiteral("mp3"));
    formatCombo_->addItem(tr("M4A (AAC)"), QStringLiteral("m4a"));
    formatCombo_->addItem(tr("Videos as video (the picture kept, new sound); audio as WAV"), QStringLiteral("video"));
    outForm->addRow(tr("Format:"), formatCombo_);
    auto* nameNote = new QLabel(tr("The results are named <i>name</i>.enhanced.<i>ext</i> (or .denoised.<i>ext</i>); "
                                   "the originals are never changed."));
    nameNote->setWordWrap(true);
    nameNote->setStyleSheet(QStringLiteral("color: gray;"));
    outForm->addRow(QString(), nameNote);
    fl->addLayout(outForm);
    root->addWidget(filesBox_);

    // ---- what to do ----
    modeBox_ = new QGroupBox(tr("What to do"));
    auto* ml = new QVBoxLayout(modeBox_);
    enhanceRadio_ = new QRadioButton(tr("Remove noise and enhance: restores muffled, band-limited or distorted "
                                        "speech (slower; regenerates the voice, so it can change it slightly)"));
    denoiseRadio_ = new QRadioButton(tr("Remove noise only: keeps the voice exactly as recorded (fast)"));
    auto* modeGroup = new QButtonGroup(this);
    modeGroup->addButton(enhanceRadio_);
    modeGroup->addButton(denoiseRadio_);
    enhanceRadio_->setChecked(true);
    ml->addWidget(enhanceRadio_);
    ml->addWidget(denoiseRadio_);

    enhanceOptions_ = new QWidget;
    auto* ef = new QFormLayout(enhanceOptions_);
    ef->setContentsMargins(20, 0, 0, 0);
    auto slider = [](int value) {
        auto* s = new QSlider(Qt::Horizontal);
        s->setRange(0, 100);
        s->setValue(value);
        s->setPageStep(10);
        return s;
    };
    strengthSlider_ = slider(90);
    strengthLabel_ = new QLabel;
    strengthLabel_->setMinimumWidth(40);
    auto* sRow = new QHBoxLayout;
    sRow->addWidget(strengthSlider_, 1);
    sRow->addWidget(strengthLabel_);
    strengthSlider_->setToolTip(tr("How much of the noise to remove before enhancing (Resemble's \"lambd\").\n"
                                   "High: for noisy recordings. Low: for clean but muffled ones."));
    ef->addRow(tr("Noise removal before enhancing:"), sRow);
    tauSlider_ = slider(50);
    tauLabel_ = new QLabel;
    tauLabel_->setMinimumWidth(40);
    auto* tRow = new QHBoxLayout;
    tRow->addWidget(tauSlider_, 1);
    tRow->addWidget(tauLabel_);
    tauSlider_->setToolTip(tr("How freely the voice is regenerated (Resemble's prior temperature \"tau\").\n"
                              "Lower: closer to the original recording. Higher: cleaner, but can drift from the\n"
                              "speaker's voice and invent detail."));
    ef->addRow(tr("Regeneration:"), tRow);
    qualityCombo_ = new QComboBox;
    for (const auto& q : kQualities) qualityCombo_->addItem(tr(q.name), q.nfe);
    qualityCombo_->setCurrentIndex(2);
    qualityCombo_->setToolTip(tr("Steps of the enhancement model's solver: more is slower and a little cleaner."));
    ef->addRow(tr("Quality:"), qualityCombo_);
    ml->addWidget(enhanceOptions_);
    gentleCheck_ = new QCheckBox(tr("Go easy on the graphics card (about half the speed; less heat and power)"));
    gentleCheck_->setToolTip(tr("The graphics card rests after each piece of work as long as the work took.\n"
                                "Use it if the computer becomes unstable or the card gets hot while enhancing."));
    ml->addWidget(gentleCheck_);
    root->addWidget(modeBox_);

    // ---- advanced ----
    advancedBox_ = new QGroupBox(tr("Advanced"));
    advancedBox_->setCheckable(true);
    advancedBox_->setChecked(false);
    auto* al = new QVBoxLayout(advancedBox_);
    advancedBody_ = new QWidget;
    auto* af = new QFormLayout(advancedBody_);
    af->setContentsMargins(0, 0, 0, 0);
    solverCombo_ = new QComboBox;
    solverCombo_->addItem(tr("Midpoint (default)"), QStringLiteral("midpoint"));
    solverCombo_->addItem(tr("Runge-Kutta 4"), QStringLiteral("rk4"));
    solverCombo_->addItem(tr("Euler"), QStringLiteral("euler"));
    af->addRow(tr("Solver:"), solverCombo_);
    deviceCombo_ = new QComboBox;
    deviceCombo_->addItem(tr("Automatic (graphics card if possible)"), QStringLiteral("auto"));
    deviceCombo_->addItem(tr("Graphics card (DirectML)"), QStringLiteral("gpu"));
    deviceCombo_->addItem(tr("Processor (CPU, slow)"), QStringLiteral("cpu"));
    af->addRow(tr("Run on:"), deviceCombo_);
    chunkSpin_ = new QSpinBox;
    chunkSpin_->setRange(5, 120);
    chunkSpin_->setValue(30);
    chunkSpin_->setSuffix(tr(" s"));
    chunkSpin_->setToolTip(tr("Long audio is processed in overlapping pieces of this length (Resemble's default 30 s).\n"
                              "Shorter pieces need less memory."));
    af->addRow(tr("Piece length:"), chunkSpin_);
    seedSpin_ = new QSpinBox;
    seedSpin_->setRange(0, 1000000);
    seedSpin_->setToolTip(tr("The enhancement uses random numbers; another seed gives a slightly different result."));
    af->addRow(tr("Random seed:"), seedSpin_);
    al->addWidget(advancedBody_);
    advancedBody_->setVisible(false);
    root->addWidget(advancedBox_);

    // ---- run ----
    auto* buttons = new QHBoxLayout;
    startBtn_ = new QPushButton(tr("Start"));
    startBtn_->setDefault(true);
    cancelBtn_ = new QPushButton(tr("Cancel"));
    helpBtn_ = new QPushButton(tr("Help"));
    openBtn_ = new QPushButton(tr("Open output folder"));
    buttons->addWidget(startBtn_);
    buttons->addWidget(cancelBtn_);
    buttons->addStretch();
    buttons->addWidget(openBtn_);
    buttons->addWidget(helpBtn_);
    root->addLayout(buttons);

    auto* statusRow = new QHBoxLayout;
    statusLabel_ = new QLabel(tr("Add audio or video files, then press Start."));
    statusLabel_->setWordWrap(true);
    statusLabel_->setTextFormat(Qt::RichText);
    engineLabel_ = new QLabel;
    engineLabel_->setTextFormat(Qt::RichText);
    statusRow->addWidget(statusLabel_, 1);
    statusRow->addWidget(engineLabel_);
    root->addLayout(statusRow);
    auto* progressRow = new QHBoxLayout;
    progress_ = new QProgressBar;
    progress_->setRange(0, 1000);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    timeLabel_ = new QLabel;
    progressRow->addWidget(progress_, 1);
    progressRow->addWidget(timeLabel_);
    root->addLayout(progressRow);

    tabs_ = new QTabWidget;
    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(5000);
    logView_->setFont(QFont(QStringLiteral("Consolas"), 9));
    auto* results = new QWidget;
    auto* rl = new QVBoxLayout(results);
    resultList_ = new QListWidget;
    rl->addWidget(resultList_);
    auto* playRow = new QHBoxLayout;
    playOrigBtn_ = new QPushButton(tr("Play original"));
    playResultBtn_ = new QPushButton(tr("Play result"));
    stopBtn_ = new QPushButton(tr("Stop"));
    playRow->addWidget(playOrigBtn_);
    playRow->addWidget(playResultBtn_);
    playRow->addWidget(stopBtn_);
    playRow->addStretch();
    rl->addLayout(playRow);
    tabs_->addTab(results, tr("Results"));
    tabs_->addTab(logView_, tr("Log"));
    root->addWidget(tabs_, 1);

    player_ = new QMediaPlayer(this);
    audioOut_ = new QAudioOutput(this);
    player_->setAudioOutput(audioOut_);

    tick_ = new QTimer(this);
    tick_->setInterval(1000);
    connect(tick_, &QTimer::timeout, this, [this] {
        timeLabel_->setText(formatDuration(runClock_.elapsed() / 1000.0));
    });

    connect(addBtn_, &QPushButton::clicked, this, &MainWindow::browseFiles);
    connect(removeBtn_, &QPushButton::clicked, this, &MainWindow::removeSelected);
    connect(clearBtn_, &QPushButton::clicked, fileList_, &QListWidget::clear);
    connect(clearBtn_, &QPushButton::clicked, this, &MainWindow::updateEnabledState);
    connect(fileList_, &QListWidget::itemSelectionChanged, this, &MainWindow::updateEnabledState);
    connect(outDirBtn_, &QPushButton::clicked, this, &MainWindow::browseOutputDir);
    connect(nextToInput_, &QRadioButton::toggled, this, &MainWindow::updateEnabledState);
    connect(outDirEdit_, &QLineEdit::textChanged, this, &MainWindow::updateEnabledState);
    connect(enhanceRadio_, &QRadioButton::toggled, this, &MainWindow::updateEnabledState);
    connect(strengthSlider_, &QSlider::valueChanged, this, &MainWindow::updateLabels);
    connect(tauSlider_, &QSlider::valueChanged, this, &MainWindow::updateLabels);
    connect(advancedBox_, &QGroupBox::toggled, advancedBody_, &QWidget::setVisible);
    connect(startBtn_, &QPushButton::clicked, this, &MainWindow::start);
    connect(cancelBtn_, &QPushButton::clicked, this, &MainWindow::cancel);
    connect(helpBtn_, &QPushButton::clicked, this, &MainWindow::showHelp);
    connect(openBtn_, &QPushButton::clicked, this, &MainWindow::openOutputFolder);
    connect(resultList_, &QListWidget::itemSelectionChanged, this, &MainWindow::updateEnabledState);
    connect(resultList_, &QListWidget::itemDoubleClicked, this, [this] { play(false); });
    connect(playOrigBtn_, &QPushButton::clicked, this, [this] { play(true); });
    connect(playResultBtn_, &QPushButton::clicked, this, [this] { play(false); });
    connect(stopBtn_, &QPushButton::clicked, this, &MainWindow::stopPlayback);
    connect(player_, &QMediaPlayer::playbackStateChanged, this, &MainWindow::updateEnabledState);

    resize(760, 760);
}

void MainWindow::updateLabels() {
    strengthLabel_->setText(QStringLiteral("%1").arg(strengthSlider_->value() / 100.0, 0, 'f', 2));
    tauLabel_->setText(QStringLiteral("%1").arg(tauSlider_->value() / 100.0, 0, 'f', 2));
}

bool MainWindow::denoiseOnly() const { return denoiseRadio_->isChecked(); }

QString MainWindow::cliPath() const {
#ifdef Q_OS_WIN
    return QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("enhance_audio.exe"));
#else
    return QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("enhance_audio"));
#endif
}

QString MainWindow::modelsDir() const {
    return QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("models"));
}

bool MainWindow::modelsPresent(bool denoiseOnly) const {
    const QDir d(modelsDir());
    for (const char* f : kModelFiles) {
        if (denoiseOnly && QString::fromLatin1(f) != QLatin1String("filters.bin") &&
            QString::fromLatin1(f) != QLatin1String("denoiser.onnx"))
            continue;
        if (!d.exists(QString::fromLatin1(f))) return false;
    }
    return true;
}

void MainWindow::addFiles(const QStringList& paths) {
    QStringList have;
    for (int i = 0; i < fileList_->count(); ++i) have << fileList_->item(i)->data(Qt::UserRole).toString();
    for (QString p : paths) {
        if (p.size() >= 2 && p.startsWith(QLatin1Char('"')) && p.endsWith(QLatin1Char('"'))) p = p.mid(1, p.size() - 2);
        const QFileInfo fi(p);
        if (!fi.isFile()) continue;
        const QString abs = fi.absoluteFilePath();
        if (have.contains(abs, Qt::CaseInsensitive)) continue;
        have << abs;
        auto* item = new QListWidgetItem(QDir::toNativeSeparators(abs));
        item->setData(Qt::UserRole, abs);
        fileList_->addItem(item);
    }
    updateEnabledState();
}

void MainWindow::browseFiles() {
    const QString dir = settings_.value(QStringLiteral("lastInputDir")).toString();
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Audio or video files"), dir, mediaFilter());
    if (files.isEmpty()) return;
    settings_.setValue(QStringLiteral("lastInputDir"), QFileInfo(files.first()).absolutePath());
    addFiles(files);
}

void MainWindow::removeSelected() {
    qDeleteAll(fileList_->selectedItems());
    updateEnabledState();
}

void MainWindow::browseOutputDir() {
    const QString d = QFileDialog::getExistingDirectory(this, tr("Save the results in"), outDirEdit_->text());
    if (d.isEmpty()) return;
    outDirEdit_->setText(QDir::toNativeSeparators(d));
    toFolder_->setChecked(true);
}

void MainWindow::updateEnabledState() {
    const bool running = runner_.isActive();
    filesBox_->setEnabled(!running);
    modeBox_->setEnabled(!running);
    advancedBox_->setEnabled(!running);
    removeBtn_->setEnabled(!fileList_->selectedItems().isEmpty());
    clearBtn_->setEnabled(fileList_->count() > 0);
    outDirEdit_->setEnabled(toFolder_->isChecked());
    outDirBtn_->setEnabled(true);
    enhanceOptions_->setEnabled(!denoiseOnly());
    const bool outOk = nextToInput_->isChecked() || !outDirEdit_->text().trimmed().isEmpty();
    startBtn_->setEnabled(!running && fileList_->count() > 0 && outOk && modelsPresent(denoiseOnly()));
    cancelBtn_->setEnabled(running);
    openBtn_->setEnabled(!written_.isEmpty());
    const bool haveResult = resultList_->currentItem() != nullptr;
    playOrigBtn_->setEnabled(haveResult);
    playResultBtn_->setEnabled(haveResult);
    stopBtn_->setEnabled(player_->playbackState() != QMediaPlayer::StoppedState);
}

QString MainWindow::formatArg() const { return formatCombo_->currentData().toString(); }

QString MainWindow::plannedOutput(const QString& input) const {
    const QFileInfo fi(input);
    QString ext = formatArg();
    if (ext == QLatin1String("video"))
        ext = videoExtensions().contains(fi.suffix().toLower()) ? fi.suffix().toLower() : QStringLiteral("wav");
    const QString dir = nextToInput_->isChecked() ? fi.absolutePath() : QDir::fromNativeSeparators(outDirEdit_->text().trimmed());
    return QDir(dir).filePath(fi.completeBaseName() + (denoiseOnly() ? QStringLiteral(".denoised.")
                                                                     : QStringLiteral(".enhanced.")) + ext);
}

QStringList MainWindow::arguments() const {
    QStringList a;
    if (denoiseOnly()) {
        a << QStringLiteral("--denoise-only");
    } else {
        a << QStringLiteral("--lambd") << QString::number(strengthSlider_->value() / 100.0, 'f', 2)
          << QStringLiteral("--tau") << QString::number(tauSlider_->value() / 100.0, 'f', 2)
          << QStringLiteral("--nfe") << QString::number(qualityCombo_->currentData().toInt())
          << QStringLiteral("--solver") << solverCombo_->currentData().toString()
          << QStringLiteral("--seed") << QString::number(seedSpin_->value());
    }
    a << QStringLiteral("--chunk-seconds") << QString::number(chunkSpin_->value())
      << QStringLiteral("--device") << deviceCombo_->currentData().toString()
      << QStringLiteral("-f") << formatArg() << QStringLiteral("-y");
    if (gentleCheck_->isChecked()) a << QStringLiteral("--gentle");
    if (toFolder_->isChecked())
        a << QStringLiteral("--out-dir") << QDir::fromNativeSeparators(outDirEdit_->text().trimmed());
    a << runInputs_;
    return a;
}

void MainWindow::start() {
    if (runner_.isActive()) return;
    runInputs_.clear();
    for (int i = 0; i < fileList_->count(); ++i) runInputs_ << fileList_->item(i)->data(Qt::UserRole).toString();
    if (runInputs_.isEmpty()) return;
    if (!QFileInfo::exists(cliPath())) {
        QMessageBox::warning(this, windowTitle(), tr("enhance_audio.exe was not found next to this program."));
        return;
    }
    if (toFolder_->isChecked() && !QDir().mkpath(QDir::fromNativeSeparators(outDirEdit_->text().trimmed()))) {
        QMessageBox::warning(this, windowTitle(), tr("The output folder cannot be created."));
        return;
    }
    // Existing results are replaced only when the user agrees.
    QStringList existing;
    for (const QString& in : runInputs_) {
        const QString out = plannedOutput(in);
        if (QFileInfo(out).absoluteFilePath().compare(QFileInfo(in).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
            QMessageBox::warning(this, windowTitle(), tr("The result for %1 would replace the file itself; choose "
                                                         "another format or folder.").arg(QFileInfo(in).fileName()));
            return;
        }
        if (QFileInfo::exists(out)) existing << QDir::toNativeSeparators(out);
    }
    if (!existing.isEmpty()) {
        const QString list = existing.size() > 8 ? existing.mid(0, 8).join(QLatin1Char('\n')) + QStringLiteral("\n...")
                                                 : existing.join(QLatin1Char('\n'));
        if (QMessageBox::question(this, windowTitle(), tr("These results exist already and will be replaced:\n\n%1\n\n"
                                                          "Continue?").arg(list)) != QMessageBox::Yes)
            return;
    }
    saveSettings();
    stopPlayback();
    logView_->clear();
    written_.clear();
    lastProblem_.clear();
    logPath_.clear();
    failures_ = 0;
    fileIndex_ = 0;
    engineBackend_.clear();
    setEngine(QString(), QString(), false);
    const QStringList args = arguments();
    appendLog(QStringLiteral("> enhance_audio %1\n").arg(args.join(QLatin1Char(' '))));
    if (!runner_.start(cliPath(), args)) return;
    runClock_.start();
    tick_->start();
    timeLabel_->setText(formatDuration(0));
    setRunning(true);
    statusLabel_->setText(tr("Loading the models..."));
    progress_->setValue(0);
}

void MainWindow::cancel() {
    if (runner_.cancel()) statusLabel_->setText(tr("Cancelling..."));
}

void MainWindow::setRunning(bool running) {
    Q_UNUSED(running);
    updateEnabledState();
}

void MainWindow::appendLog(const QString& text) {
    logView_->moveCursor(QTextCursor::End);
    logView_->insertPlainText(text);
    logView_->verticalScrollBar()->setValue(logView_->verticalScrollBar()->maximum());
}

void MainWindow::parseLine(const QString& line) {
    const int n = std::max<int>(1, (int)runInputs_.size());
    static const QRegularExpression progressRe(QStringLiteral("^Progress: ([0-9.]+)% (.*)$"));
    const auto m = progressRe.match(line);
    if (!m.hasMatch()) appendLog(line + QLatin1Char('\n'));  // (the bar shows the progress lines)
    if (m.hasMatch()) {
        const double f = m.captured(1).toDouble() / 100.0;
        progress_->setValue((int)(1000.0 * (fileIndex_ + f) / n));
        const QString name = fileIndex_ < runInputs_.size() ? QFileInfo(runInputs_[fileIndex_]).fileName() : QString();
        statusLabel_->setText(n > 1 ? tr("%1 file %2 of %3 (%4): %5%")
                                          .arg(m.captured(2).trimmed()).arg(fileIndex_ + 1).arg(n)
                                          .arg(name.toHtmlEscaped()).arg(m.captured(1))
                                    : tr("%1 %2: %3%").arg(m.captured(2).trimmed(), name.toHtmlEscaped(), m.captured(1)));
        return;
    }
    static const QRegularExpression fileRe(QStringLiteral("^File (\\d+) of (\\d+): "));
    if (const auto m = fileRe.match(line); m.hasMatch()) {
        fileIndex_ = m.captured(1).toInt() - 1;
        progress_->setValue((int)(1000.0 * fileIndex_ / n));
        return;
    }
    static const QRegularExpression engineRe(QStringLiteral("^Engine: (\\S+)(?: \\((.*)\\))?\\s*$"));
    if (const auto m = engineRe.match(line); m.hasMatch()) {
        setEngine(m.captured(1), m.captured(2), false);
        return;
    }
    static const QRegularExpression logRe(QStringLiteral("^Log: (.+)$"));
    if (const auto lm = logRe.match(line); lm.hasMatch()) {
        logPath_ = lm.captured(1).trimmed();
        return;
    }
    static const QRegularExpression wroteRe(QStringLiteral("^Wrote: (.+)$"));
    if (const auto m = wroteRe.match(line); m.hasMatch()) {
        const QString out = QFileInfo(m.captured(1).trimmed()).absoluteFilePath();
        written_ << out;
        auto* item = new QListWidgetItem(QDir::toNativeSeparators(out));
        if (fileIndex_ < runInputs_.size()) item->setData(Qt::UserRole, runInputs_[fileIndex_]);
        item->setData(Qt::UserRole + 1, out);
        resultList_->addItem(item);
        if (!resultList_->currentItem()) resultList_->setCurrentItem(item);
        return;
    }
    const QString s = line.trimmed();
    if (s.startsWith(QLatin1String("Error:"))) {
        lastProblem_ = s;
        ++failures_;
    } else if (s.startsWith(QLatin1String("Warning:"))) {
        lastProblem_ = s;
    }
    if (s.startsWith(QLatin1String("Loading")) || s.startsWith(QLatin1String("Decoding")) ||
        s.startsWith(QLatin1String("Audio :")) || s.startsWith(QLatin1String("Warning:")))
        statusLabel_->setText(s.toHtmlEscaped());
}

void MainWindow::processFinished(int exitCode, bool cancelled) {
    tick_->stop();
    timeLabel_->setText(formatDuration(runClock_.elapsed() / 1000.0));
    updateEnabledState();
    if (cancelled) {
        statusLabel_->setText(tr("Cancelled."));
        appendLog(QStringLiteral("\n[cancelled]\n"));
        progress_->setValue(0);
    } else if (exitCode == 0) {
        progress_->setValue(1000);
        const QString files = written_.size() == 1 ? QFileInfo(written_.first()).fileName().toHtmlEscaped()
                                                   : tr("%1 files").arg(written_.size());
        statusLabel_->setText(engineBackend_.isEmpty() ? tr("Done: wrote %1.").arg(files)
                                                       : tr("Done on %1: wrote %2.").arg(engineBackend_, files));
        tabs_->setCurrentIndex(0);
    } else if (!written_.isEmpty()) {
        statusLabel_->setText(tr("Wrote %1 of %2 files; the others failed: %3")
                                  .arg(written_.size()).arg(runInputs_.size()).arg(lastProblem_.toHtmlEscaped()));
        tabs_->setCurrentWidget(logView_);
    } else if (!lastProblem_.isEmpty()) {
        statusLabel_->setText(tr("Failed: %1").arg(lastProblem_.toHtmlEscaped()) +
                              (logPath_.isEmpty() ? QString()
                                                  : QStringLiteral("<br>") + tr("Run log: %1")
                                                        .arg(QDir::toNativeSeparators(logPath_).toHtmlEscaped())));
        tabs_->setCurrentWidget(logView_);
    } else {
        statusLabel_->setText(tr("Failed (exit code %1). See the log.").arg(exitCode));
        tabs_->setCurrentWidget(logView_);
    }
    if (engineBackend_.isEmpty()) {
        const QString last = settings_.value(QStringLiteral("lastEngine")).toString();
        if (!last.isEmpty()) setEngine(last, settings_.value(QStringLiteral("lastEngineDetail")).toString(), true);
    }
    updateEnabledState();
}

void MainWindow::processFailedToStart() {
    tick_->stop();
    statusLabel_->setText(tr("Could not start enhance_audio."));
    appendLog(tr("\nFailed to start %1\n").arg(cliPath()));
    updateEnabledState();
}

void MainWindow::openOutputFolder() {
    QString target = written_.isEmpty() ? QString() : written_.last();
    if (auto* item = resultList_->currentItem()) target = item->data(Qt::UserRole + 1).toString();
    if (!target.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(target).absolutePath()));
}

void MainWindow::play(bool original) {
    auto* item = resultList_->currentItem();
    if (!item) return;
    const QString path = item->data(original ? Qt::UserRole : Qt::UserRole + 1).toString();
    if (path.isEmpty()) return;
    player_->stop();
    player_->setSource(QUrl::fromLocalFile(path));
    player_->play();
    statusLabel_->setText((original ? tr("Playing the original: %1") : tr("Playing the result: %1"))
                              .arg(QFileInfo(path).fileName().toHtmlEscaped()));
}

void MainWindow::stopPlayback() {
    if (player_) player_->stop();
}

void MainWindow::setEngine(const QString& backend, const QString& detail, bool lastRun) {
    engineBackend_ = lastRun ? QString() : backend;
    if (backend.isEmpty()) {
        engineLabel_->setText(tr("Compute: <i>detecting...</i>"));
        return;
    }
    const QString colour = backend.startsWith(QLatin1String("DirectML"), Qt::CaseInsensitive)
                               ? QStringLiteral("#4e8a00") : QStringLiteral("#6b6b6b");
    QString html = tr("Compute:") +
                   QStringLiteral(" <span style=\"background:%1; color:white;\">&nbsp;<b>%2</b>&nbsp;</span>")
                       .arg(colour, backend.toHtmlEscaped());
    if (!detail.isEmpty()) html += QStringLiteral(" ") + detail.toHtmlEscaped();
    if (lastRun) html += QStringLiteral(" <i>") + tr("(last run)") + QStringLiteral("</i>");
    engineLabel_->setText(html);
    if (!lastRun) {
        settings_.setValue(QStringLiteral("lastEngine"), backend);
        settings_.setValue(QStringLiteral("lastEngineDetail"), detail);
    }
}

void MainWindow::loadSettings() {
    auto& s = settings_;
    denoiseRadio_->setChecked(s.value(QStringLiteral("denoiseOnly"), false).toBool());
    enhanceRadio_->setChecked(!denoiseRadio_->isChecked());
    strengthSlider_->setValue(s.value(QStringLiteral("strength"), 90).toInt());
    tauSlider_->setValue(s.value(QStringLiteral("tau"), 50).toInt());
    const int nfe = s.value(QStringLiteral("nfe"), 64).toInt();
    for (int i = 0; i < qualityCombo_->count(); ++i)
        if (qualityCombo_->itemData(i).toInt() == nfe) qualityCombo_->setCurrentIndex(i);
    auto pick = [](QComboBox* c, const QString& v) {
        const int i = c->findData(v);
        if (i >= 0) c->setCurrentIndex(i);
    };
    pick(solverCombo_, s.value(QStringLiteral("solver"), QStringLiteral("midpoint")).toString());
    pick(deviceCombo_, s.value(QStringLiteral("device"), QStringLiteral("auto")).toString());
    pick(formatCombo_, s.value(QStringLiteral("format"), QStringLiteral("wav")).toString());
    chunkSpin_->setValue(s.value(QStringLiteral("chunkSeconds"), 30).toInt());
    seedSpin_->setValue(s.value(QStringLiteral("seed"), 0).toInt());
    gentleCheck_->setChecked(s.value(QStringLiteral("gentle"), false).toBool());
    outDirEdit_->setText(s.value(QStringLiteral("outDir")).toString());
    (s.value(QStringLiteral("toFolder"), false).toBool() ? toFolder_ : nextToInput_)->setChecked(true);
    const QString last = s.value(QStringLiteral("lastEngine")).toString();
    if (last.isEmpty()) engineLabel_->setText(tr("Compute: <i>shown when a run starts</i>"));
    else setEngine(last, s.value(QStringLiteral("lastEngineDetail")).toString(), true);
    if (s.contains(QStringLiteral("geometry"))) restoreGeometry(s.value(QStringLiteral("geometry")).toByteArray());
}

void MainWindow::saveSettings() {
    auto& s = settings_;
    s.setValue(QStringLiteral("denoiseOnly"), denoiseOnly());
    s.setValue(QStringLiteral("strength"), strengthSlider_->value());
    s.setValue(QStringLiteral("tau"), tauSlider_->value());
    s.setValue(QStringLiteral("nfe"), qualityCombo_->currentData().toInt());
    s.setValue(QStringLiteral("solver"), solverCombo_->currentData().toString());
    s.setValue(QStringLiteral("device"), deviceCombo_->currentData().toString());
    s.setValue(QStringLiteral("format"), formatArg());
    s.setValue(QStringLiteral("chunkSeconds"), chunkSpin_->value());
    s.setValue(QStringLiteral("seed"), seedSpin_->value());
    s.setValue(QStringLiteral("gentle"), gentleCheck_->isChecked());
    s.setValue(QStringLiteral("outDir"), outDirEdit_->text());
    s.setValue(QStringLiteral("toFolder"), toFolder_->isChecked());
    s.setValue(QStringLiteral("geometry"), saveGeometry());
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (!runner_.isActive() && event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    QStringList paths;
    for (const QUrl& u : event->mimeData()->urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    addFiles(paths);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (runner_.isActive() &&
        QMessageBox::question(this, windowTitle(), tr("Files are still being processed. Stop and close?")) !=
            QMessageBox::Yes) {
        event->ignore();
        return;
    }
    player_->stop();
    runner_.stop();
    saveSettings();
    event->accept();
}

void MainWindow::showHelp() {
    using pk::help_row;
    const QString html =
        tr("<h2>Enhance Audio</h2>"
           "<p>Cleans up recorded speech on this computer with "
           "<a href=\"https://github.com/resemble-ai/resemble-enhance\">Resemble Enhance</a> "
           "(Resemble AI, MIT licence). Nothing is uploaded.</p>"
           "<h3>The two modes</h3><table cellspacing=6>") +
        help_row(tr("Remove noise only"),
                 tr("A neural noise remover separates the voice from background noise, music and hum. The voice "
                    "itself is kept as recorded. Fast: about a minute of audio in a few seconds on a graphics "
                    "card.")) +
        help_row(tr("Remove noise and enhance"),
                 tr("After the noise removal, a generative model rebuilds the voice at studio quality: it restores "
                    "high frequencies that phone or low-bitrate recordings lost, and repairs muffled or distorted "
                    "sound. Because the voice is regenerated, it can change slightly, mumbled words can come out "
                    "differently, and odd sounds can be invented. For recordings that must stay faithful "
                    "(interviews, evidence), use <i>Remove noise only</i>.")) +
        tr("</table><h3>Settings</h3><table cellspacing=6>") +
        help_row(tr("Noise removal before enhancing"),
                 tr("0 to 1 (default 0.9): how much of the noise remover's result the enhancer starts from. Use a "
                    "high value for noisy recordings and a low one (0.1) for clean but dull ones.")) +
        help_row(tr("Regeneration"),
                 tr("0 to 1 (default 0.5): the model's prior temperature. Lower stays closer to the recording, "
                    "higher is cleaner but freer.")) +
        help_row(tr("Quality"),
                 tr("The number of solver steps: Draft (16) is four times faster than Best (64), with a small loss "
                    "of clarity.")) +
        help_row(tr("Format"),
                 tr("WAV keeps the full quality (32-bit float, 44.1 kHz mono). For videos, <i>Videos as video</i> "
                    "writes a copy of the video with the cleaned sound (the picture is copied, not re-encoded).")) +
        help_row(tr("Advanced"),
                 tr("<b>Run on</b>: the graphics card is used through DirectML (any DirectX 12 card: NVIDIA, AMD, "
                    "Intel); the processor works too but is many times slower for enhancing. <b>Piece length</b>: "
                    "long audio is processed in overlapping pieces (30 s, as Resemble does); shorter pieces need less "
                    "memory. <b>Random seed</b>: the enhancer uses random numbers; the same seed gives the same "
                    "result.")) +
        tr("</table><h3>Notes</h3><ul>"
           "<li>The output is mono at 44.1 kHz (the model's own format); stereo input is mixed to mono.</li>"
           "<li>The model was trained on speech. Music and singing are not improved and can be damaged.</li>"
           "<li>A graphics card that other programs are using heavily at the same time makes the enhancement "
           "much slower. Close programs that keep AI models loaded on the card (such as Transcribe &amp; "
           "Translate) first.</li>"
           "<li><b>Go easy on the graphics card</b> halves the load: try it if the computer freezes or the "
           "graphics driver resets during long files.</li>"
           "<li>Every run writes a log with the graphics card's temperature, power and memory once a second, "
           "in %LOCALAPPDATA%\\EnhanceAudio\\logs (the last 20 are kept). After a crash, the next run "
           "shows how the previous log ended.</li>"
           "<li>Files that ffmpeg reads are accepted: WAV, MP3, FLAC, M4A, OGG, video files, ...</li></ul>");
    pk::show_help_window(this, tr("Enhance Audio - Help"), html);
}
