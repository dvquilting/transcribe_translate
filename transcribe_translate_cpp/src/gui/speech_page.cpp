#include "speech_page.h"
#include "faster_mode.h"
#include "pdf_files.h"

#include <algorithm>

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QSaveFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "formats.h"
#include "help.h"
#include "languages.h"
#include "model_catalog.h"  // ../common: the engine's speech and voice models
#include "model_files.h"    // ../common/qt
#include "models_dialog.h"
#include "models.h"
#include "subtitles.h"

namespace {

QString absolutePath(QString path) {
    path = path.trimmed();
    if (path.size() >= 2 && path.startsWith(QLatin1Char('"')) && path.endsWith(QLatin1Char('"')))
        path = path.mid(1, path.size() - 2);
    return path.isEmpty() ? QString() : QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
}

// The speech models offered here, in menu order (data in ../common/src/model_catalog.cpp).
// Parakeet Redux is left out: an experimental small model of the command-line program.
const char* const kAsrChoices[] = {"parakeet-ultra", "qwen", "granite", "cohere", "ark"};

bool isVideoFile(const QString& path) {
    static const QStringList ext = {QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("mov"),
                                    QStringLiteral("avi"), QStringLiteral("webm"), QStringLiteral("m4v"),
                                    QStringLiteral("wmv")};
    return ext.contains(QFileInfo(path).suffix().toLower());
}

const char* kMediaFilter =
    "Audio/Video (*.mp3 *.wav *.m4a *.flac *.ogg *.opus *.aac *.wma *.mp4 *.mkv *.mov *.avi *.webm *.m4v "
    "*.wmv);;All files (*)";

QString q(const std::string& s) { return QString::fromUtf8(s.c_str()); }

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

QString formatDuration(double s) {
    if (s < 0) return QStringLiteral("—");
    const qint64 t = qRound64(s);
    if (t >= 3600)
        return QStringLiteral("%1:%2:%3").arg(t / 3600).arg((t / 60) % 60, 2, 10, QLatin1Char('0')).arg(t % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(t / 60).arg(t % 60, 2, 10, QLatin1Char('0'));
}

QLineEdit* readOnlyField(int minChars, const QString& tip) {
    auto* e = new QLineEdit;
    e->setReadOnly(true);
    e->setFocusPolicy(Qt::NoFocus);
    e->setAlignment(Qt::AlignRight);
    e->setMinimumWidth(e->fontMetrics().horizontalAdvance(QLatin1Char('0')) * minChars + 16);
    e->setMaximumWidth(e->fontMetrics().horizontalAdvance(QLatin1Char('0')) * (minChars + 6) + 16);
    e->setText(QStringLiteral("—"));
    e->setToolTip(tip);
    return e;
}

std::string readAll(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray b = f.readAll();
    return std::string(b.constData(), (size_t)b.size());
}

bool writeAll(const QString& path, const std::string& data) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    return f.write(data.data(), (qint64)data.size()) == (qint64)data.size() && f.commit();
}

// The speakers found, for the user to check before the run goes on: listen to
// each, name them, say which two are one person, or detect again with a
// given number of speakers.
class SpeakersDialog : public QDialog {
public:
    SpeakersDialog(const pk::SpeakerMap& map, const QString& samplesDir, bool dub, int count, QWidget* parent)
        : QDialog(parent) {
        setWindowTitle(QObject::tr("Speakers"));
        auto* v = new QVBoxLayout(this);
        const int n = (int)map.speakers.size();
        auto* head = new QLabel(
            (n == 1 ? QObject::tr("One speaker was found.")
                    : QObject::tr("%1 speakers were found.").arg(n)) +
            QStringLiteral(" ") +
            QObject::tr("Listen to each one, name them if you like, and mark two that are really the same person.") +
            (dub ? QStringLiteral(" ") + QObject::tr("Each speaker gets their own voice in the dub.") : QString()) +
            QStringLiteral(" ") +
            QObject::tr("Where the speaker changes within a subtitle, each speaker's part gets its own line, "
                        "starting with a dash."));
        head->setWordWrap(true);
        v->addWidget(head);
        auto* grid = new QGridLayout;
        grid->addWidget(new QLabel(QObject::tr("<b>Name</b>")), 0, 0);
        grid->addWidget(new QLabel(QObject::tr("<b>Speaks</b>")), 0, 1);
        grid->addWidget(new QLabel(QObject::tr("<b>Same person as</b>")), 0, 3);
        for (int k = 0; k < n; ++k) {
            const pk::SpeakerInfo& s = map.speakers[(size_t)k];
            auto* name = new QLineEdit(QString::fromStdString(s.name));
            name->setObjectName(QStringLiteral("name%1").arg(k));
            int lines = 0;
            for (const auto& c : map.cues) lines += c.speaker == s.id;
            const QString time = formatDuration(s.seconds);
            QString speaks = lines == 1 ? QObject::tr("%1, 1 subtitle").arg(time)
                                        : QObject::tr("%1, %2 subtitles").arg(time).arg(lines);
            if (dub && s.seconds < 5)
                speaks += QStringLiteral("\n") + QObject::tr("(little speech: may get a built-in voice)");
            auto* info = new QLabel(speaks);
            auto* listen = new QPushButton(QObject::tr("Listen"));
            const QString wav = QDir(samplesDir).filePath(QStringLiteral("speaker_%1.wav").arg(s.id + 1));
            listen->setEnabled(QFileInfo::exists(wav));
            listen->setToolTip(QObject::tr("Play a few seconds of this speaker"));
            QObject::connect(listen, &QPushButton::clicked, this, [wav] { QDesktopServices::openUrl(QUrl::fromLocalFile(wav)); });
            // Only someone listed above: a merge always goes to an earlier speaker, so it can't go round in circles.
            auto* same = new QComboBox;
            same->addItem(QStringLiteral("—"), -1);
            for (int j = 0; j < k; ++j) same->addItem(QString::fromStdString(map.speakers[(size_t)j].name), map.speakers[(size_t)j].id);
            same->setEnabled(k > 0);
            QObject::connect(name, &QLineEdit::textChanged, this, [this, k](const QString& t) {
                for (QComboBox* c : sameAs)  // keep the names in the menus up to date
                    if (const int i = c->findData(ids[(size_t)k]); i >= 0)
                        c->setItemText(i, t.trimmed().isEmpty() ? defaults[(size_t)k] : t.trimmed());
            });
            grid->addWidget(name, k + 1, 0);
            grid->addWidget(info, k + 1, 1);
            grid->addWidget(listen, k + 1, 2);
            grid->addWidget(same, k + 1, 3);
            names.push_back(name);
            sameAs.push_back(same);
            ids.push_back(s.id);
            defaults.push_back(QString::fromStdString(s.name));
        }
        v->addLayout(grid);
        auto* again = new QHBoxLayout;
        again->addWidget(new QLabel(QObject::tr("Wrong number of speakers? Detect again with:")));
        countCombo = new QComboBox;
        countCombo->addItem(QObject::tr("Detect"), 0);
        for (int c = 1; c <= 8; ++c) countCombo->addItem(QString::number(c), c);
        selectData(countCombo, count);
        again->addWidget(countCombo);
        auto* redo = new QPushButton(QObject::tr("Detect again"));
        QObject::connect(redo, &QPushButton::clicked, this, [this] {
            redetect = true;
            accept();
        });
        again->addWidget(redo);
        again->addStretch();
        v->addLayout(again);
        auto* buttons = new QHBoxLayout;
        buttons->addStretch();
        continueBtn = new QPushButton(QObject::tr("Continue"));
        continueBtn->setDefault(true);
        auto* cancel = new QPushButton(QObject::tr("Cancel"));
        QObject::connect(continueBtn, &QPushButton::clicked, this, &QDialog::accept);
        QObject::connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        buttons->addWidget(continueBtn);
        buttons->addWidget(cancel);
        v->addLayout(buttons);
    }

    // The map with the names and merges from the window.
    pk::SpeakerMap result(pk::SpeakerMap map) const {
        std::vector<std::pair<int, int>> into;
        for (size_t k = 0; k < ids.size() && k < map.speakers.size(); ++k) {
            const QString t = names[k]->text().trimmed();
            if (!t.isEmpty()) map.speakers[k].name = t.toStdString();
            const int target = sameAs[k]->currentData().toInt();
            if (target >= 0) into.push_back({ids[k], target});
        }
        pk::merge_speakers(&map, into);
        return map;
    }

    std::vector<QLineEdit*> names;
    std::vector<QComboBox*> sameAs;
    std::vector<int> ids;
    std::vector<QString> defaults;
    QComboBox* countCombo = nullptr;
    QPushButton* continueBtn = nullptr;
    bool redetect = false;
};

}  // namespace

QStringList SpeechPage::Outputs::all() const {
    QStringList l;
    for (const QString& s : {translatedSrt, originalSrt, bilingualSrt, translatedTxt, translatedPdf, dubbed})
        if (!s.isEmpty()) l << s;
    return l;
}

SpeechPage::SpeechPage(QWidget* parent)
    : QWidget(parent), settings_(QStringLiteral("DavidV"), QStringLiteral("TranscribeTranslate")) {
    settings_.beginGroup(QStringLiteral("SpeechTranslate"));
    setAcceptDrops(true);
    connect(&runner_, &pk::ChildProcess::outputReceived, this, &SpeechPage::appendLog);
    connect(&runner_, &pk::ChildProcess::lineReceived, this, &SpeechPage::parseLine);
    connect(&runner_, &pk::ChildProcess::finished, this, &SpeechPage::stepFinished);
    connect(&runner_, &pk::ChildProcess::failedToStart, this, &SpeechPage::stepFailedToStart);
    buildUi();
    loadSettings();
    updateEnabledState();
}

SpeechPage::~SpeechPage() {
    stopProcess();  // before tempDir_ goes: the child may still be writing into it
    delete tempDir_;
    delete dubWorkDir_;
}

void SpeechPage::buildUi() {
    auto* root = new QVBoxLayout(this);

    // --- files -----------------------------------------------------------
    auto* files = new QGroupBox(tr("Files"));
    auto* ff = new QFormLayout(files);
    inputEdit_ = new QLineEdit;
    inputEdit_->setPlaceholderText(tr("An audio or video file - or drop one onto this window"));
    inBrowseBtn_ = new QPushButton(tr("Browse..."));
    connect(inBrowseBtn_, &QPushButton::clicked, this, &SpeechPage::browseInput);
    ff->addRow(tr("Input:"), row(inputEdit_, inBrowseBtn_));
    outDirEdit_ = new QLineEdit;
    outDirEdit_->setPlaceholderText(tr("(default: the input file's folder)"));
    outBrowseBtn_ = new QPushButton(tr("Browse..."));
    connect(outBrowseBtn_, &QPushButton::clicked, this, &SpeechPage::browseOutputDir);
    ff->addRow(tr("Save to folder:"), row(outDirEdit_, outBrowseBtn_));
    root->addWidget(files);

    // --- step 1: speech --------------------------------------------------
    auto* speech = new QGroupBox(tr("Step 1 - Speech to text"));
    auto* sf = new QFormLayout(speech);
    asrCombo_ = new QComboBox;
    for (const char* key : kAsrChoices)
        if (const pk::AsrModel* m = pk::find_asr_model(key))
            asrCombo_->addItem(q(m->display) + QStringLiteral("  —  ") + q(m->size), q(m->key));
    asrDesc_ = new QLabel;
    asrDesc_->setStyleSheet(QStringLiteral("color: gray;"));
    asrDesc_->setWordWrap(true);
    fromCombo_ = new QComboBox;
    fromCombo_->addItem(tr("Detect automatically"), QStringLiteral("auto"));
    sf->addRow(tr("Speech model:"), asrCombo_);
    sf->addRow(QString(), asrDesc_);
    sf->addRow(tr("Spoken language:"), fromCombo_);
    speakersCheck_ = new QCheckBox(tr("Several speakers: find who speaks when"));
    speakersCheck_->setToolTip(tr("Finds the speakers (any language) and lets you check them before translating.\n"
                                  "A subtitle in which the speaker changes shows each speaker's part on its own "
                                  "line, starting with a dash; the dub gives each speaker their own voice."));
    speakersCount_ = new QComboBox;
    speakersCount_->addItem(tr("Detect how many"), 0);
    for (int c = 2; c <= 8; ++c) speakersCount_->addItem(tr("%1 speakers").arg(c), c);
    auto* spkRow = new QWidget;
    auto* spkLayout = new QHBoxLayout(spkRow);
    spkLayout->setContentsMargins(0, 0, 0, 0);
    spkLayout->addWidget(speakersCheck_);
    spkLayout->addWidget(speakersCount_);
    spkLayout->addStretch();
    sf->addRow(tr("Speakers:"), spkRow);
    root->addWidget(speech);

    // --- step 2: translation ---------------------------------------------
    auto* trans = new QGroupBox(tr("Step 2 - Translation"));
    auto* tf = new QFormLayout(trans);
    mtCombo_ = new QComboBox;
    for (const auto& m : tl::model_table()) mtCombo_->addItem(q(m.display) + QStringLiteral("  —  ") + q(m.size), q(m.key));
    mtDesc_ = new QLabel;
    mtDesc_->setStyleSheet(QStringLiteral("color: gray;"));
    mtDesc_->setWordWrap(true);
    toCombo_ = new QComboBox;
    for (const auto& l : tl::languages()) {
        const QString label = l.native == l.name ? q(l.name) : q(l.name) + QStringLiteral(" — ") + q(l.native);
        fromCombo_->addItem(label, q(l.code));
        toCombo_->addItem(label, q(l.code));
    }
    fromCombo_->setMaxVisibleItems(20);
    toCombo_->setMaxVisibleItems(20);
    tf->addRow(tr("Translation model:"), mtCombo_);
    tf->addRow(QString(), mtDesc_);
    tf->addRow(tr("Translate into:"), toCombo_);
    langNote_ = new QLabel;
    langNote_->setWordWrap(true);
    langNote_->setStyleSheet(QStringLiteral("color: #b06000;"));
    tf->addRow(langNote_);
    root->addWidget(trans);

    // --- outputs -----------------------------------------------------------
    auto* outs = new QGroupBox(tr("Save"));
    auto* ov = new QGridLayout(outs);
    auto* fixed = new QCheckBox(tr("Translated subtitles (NAME.<language>.srt)"));
    fixed->setChecked(true);
    fixed->setEnabled(false);
    origCheck_ = new QCheckBox(tr("Original subtitles (NAME.srt)"));
    biCheck_ = new QCheckBox(tr("Bilingual subtitles (NAME.<language>.bilingual.srt)"));
    biCheck_->setToolTip(tr("The original line above its translation"));
    txtCheck_ = new QCheckBox(tr("Translated text (NAME.<language>.txt)"));
    ov->addWidget(fixed, 0, 0);
    ov->addWidget(origCheck_, 0, 1);
    ov->addWidget(biCheck_, 1, 0);
    ov->addWidget(txtCheck_, 1, 1);
    pdfCheck_ = new QCheckBox(tr("Translated text as PDF (NAME.<language>.pdf)"));
    pdfCheck_->setToolTip(tr("The translated text in paragraphs, as a PDF document"));
    ov->addWidget(pdfCheck_, 2, 1);
    root->addWidget(outs);

    // --- step 3: dubbing (optional) ----------------------------------------------
    dubBox_ = new QGroupBox(tr("Step 3 - Dubbed audio in the speaker's voice (optional)"));
    dubBox_->setCheckable(true);
    dubBox_->setChecked(false);
    auto* dl = new QVBoxLayout(dubBox_);
    auto* dubBody = new QWidget;
    dl->addWidget(dubBody);
    auto* df = new QFormLayout(dubBody);
    df->setContentsMargins(0, 0, 0, 0);
    ttsCombo_ = new QComboBox;
    for (const auto& m : pk::tts_models()) ttsCombo_->addItem(q(m.display) + QStringLiteral("  —  ") + q(m.size), q(m.key));
    ttsDesc_ = new QLabel;
    ttsDesc_->setStyleSheet(QStringLiteral("color: gray;"));
    ttsDesc_->setWordWrap(true);
    df->addRow(tr("Voice model:"), ttsCombo_);
    df->addRow(QString(), ttsDesc_);
    voiceCombo_ = new QComboBox;
    voiceCombo_->addItem(tr("Clone the speaker's voice"), QStringLiteral("clone"));
    voiceCombo_->addItem(tr("Built-in voice"), QStringLiteral("builtin"));
    df->addRow(tr("Voice:"), voiceCombo_);
    consentCheck_ = new QCheckBox(tr("The speaker agreed to having their voice cloned (or it is my own voice)"));
    df->addRow(QString(), consentCheck_);
    accentCheck_ = new QCheckBox(tr("Keep the speaker's original accent (default: sound like a native speaker)"));
    accentCheck_->setToolTip(tr("Off: only the voice is cloned and the translation is spoken with a native accent "
                                "(Qwen3-TTS, CosyVoice3).\nOn: the way the speaker talks is copied too, "
                                "including their accent."));
    df->addRow(QString(), accentCheck_);
    consentNote_ = new QLabel(tr("Cloned audio gets a short AI notice (spoken, or as the first subtitle of a video) and "
                                 "an inaudible watermark."));
    consentNote_->setStyleSheet(QStringLiteral("color: gray;"));
    consentNote_->setWordWrap(true);
    df->addRow(QString(), consentNote_);
    backgroundCombo_ = new QComboBox;
    backgroundCombo_->addItem(tr("Keep music and sound effects (separates them from the voice)"), QStringLiteral("keep"));
    backgroundCombo_->addItem(tr("Original audio quietly underneath"), QStringLiteral("duck"));
    backgroundCombo_->addItem(tr("Voice only"), QStringLiteral("none"));
    df->addRow(tr("Background:"), backgroundCombo_);
    dubFormatCombo_ = new QComboBox;
    dubFormatCombo_->addItem(tr("Video with the new soundtrack (for video input)"), QStringLiteral("video"));
    dubFormatCombo_->addItem(tr("MP3 audio"), QStringLiteral("mp3"));
    dubFormatCombo_->addItem(tr("M4A audio"), QStringLiteral("m4a"));
    dubFormatCombo_->addItem(tr("WAV audio"), QStringLiteral("wav"));
    df->addRow(tr("Save as:"), dubFormatCombo_);
    dubNote_ = new QLabel;
    dubNote_->setWordWrap(true);
    dubNote_->setStyleSheet(QStringLiteral("color: #b06000;"));
    df->addRow(dubNote_);
    dubBody->setVisible(false);
    connect(dubBox_, &QGroupBox::toggled, dubBody, &QWidget::setVisible);
    connect(dubBox_, &QGroupBox::toggled, this, &SpeechPage::updateEnabledState);
    root->addWidget(dubBox_);

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
    alignCheck_ = new QCheckBox(tr("Word-level subtitle timing (forced aligner, for chunked speech models)"));
    alignCheck_->setChecked(true);
    af->addRow(QString(), alignCheck_);
    contextCheck_ = new QCheckBox(tr("Show the translation model the preceding text (Hy-MT2 and Qwen)"));
    contextCheck_->setChecked(true);
    af->addRow(QString(), contextCheck_);
    draftCombo_ = tt::makeFasterModeCombo();
    af->addRow(tr("Faster mode:"), draftCombo_);
    advancedBody_->setVisible(false);
    connect(advancedBox_, &QGroupBox::toggled, advancedBody_, &QWidget::setVisible);
    root->addWidget(advancedBox_);

    // --- run -----------------------------------------------------------------
    auto* runRow = new QHBoxLayout;
    startBtn_ = new QPushButton(tr("Translate"));
    startBtn_->setDefault(true);
    cancelBtn_ = new QPushButton(tr("Cancel"));
    cancelBtn_->setEnabled(false);
    openBtn_ = new QPushButton(tr("Open output folder"));
    openBtn_->setEnabled(false);
    saveLogBtn_ = new QPushButton(tr("Save log..."));
    saveLogBtn_->setToolTip(tr("Save everything shown in the Log tab to a text file"));
    saveLogBtn_->setEnabled(false);
    connect(startBtn_, &QPushButton::clicked, this, &SpeechPage::start);
    connect(cancelBtn_, &QPushButton::clicked, this, &SpeechPage::cancel);
    connect(openBtn_, &QPushButton::clicked, this, &SpeechPage::openOutputFolder);
    connect(saveLogBtn_, &QPushButton::clicked, this, &SpeechPage::saveLog);
    runRow->addWidget(startBtn_);
    runRow->addWidget(cancelBtn_);
    runRow->addStretch();
    runRow->addWidget(saveLogBtn_);
    runRow->addWidget(openBtn_);
    root->insertLayout(0, runRow);  // the action buttons go at the top of the window

    stepLabel_ = new QLabel;
    stepLabel_->setStyleSheet(QStringLiteral("font-weight: bold;"));
    statusLabel_ = new QLabel(tr("Ready."));
    engineLabel_ = new QLabel;
    engineLabel_->setTextFormat(Qt::RichText);
    auto* sr = new QHBoxLayout;
    sr->addWidget(stepLabel_);
    sr->addWidget(statusLabel_, 1);
    sr->addWidget(engineLabel_);
    root->addLayout(sr);
    progress_ = new QProgressBar;
    progress_->setRange(0, 1);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    root->addWidget(progress_);

    audioEdit_ = readOnlyField(7, tr("Length of the audio"));
    asrTimeEdit_ = readOnlyField(6, tr("Step 1: speech to text, including model loading"));
    mtTimeEdit_ = readOnlyField(6, tr("Step 2: translation, including model loading"));
    dubTimeEdit_ = readOnlyField(6, tr("Step 3: dubbing, including model loading"));
    totalEdit_ = readOnlyField(6, tr("Whole run"));
    speedEdit_ = readOnlyField(10, tr("Audio length ÷ total time"));
    auto* tr_ = new QHBoxLayout;
    auto field = [&](const QString& label, QLineEdit* e) {
        tr_->addWidget(new QLabel(label));
        tr_->addWidget(e);
        tr_->addSpacing(10);
    };
    field(tr("Audio length:"), audioEdit_);
    field(tr("Transcription:"), asrTimeEdit_);
    field(tr("Translation:"), mtTimeEdit_);
    field(tr("Dubbing:"), dubTimeEdit_);
    field(tr("Total:"), totalEdit_);
    field(tr("Speed:"), speedEdit_);
    tr_->addStretch();
    root->addLayout(tr_);

    tabs_ = new QTabWidget;
    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    logView_->setMaximumBlockCount(20000);
    resultView_ = new QPlainTextEdit;
    resultView_->setReadOnly(true);
    tabs_->addTab(logView_, tr("Log"));
    tabs_->addTab(resultView_, tr("Translated subtitles"));
    root->addWidget(tabs_, 1);

    tick_ = new QTimer(this);
    tick_->setInterval(250);
    connect(tick_, &QTimer::timeout, this, &SpeechPage::updateTiming);

    connect(asrCombo_, &QComboBox::currentIndexChanged, this, &SpeechPage::updateEnabledState);
    connect(mtCombo_, &QComboBox::currentIndexChanged, this, &SpeechPage::updateEnabledState);
    connect(fromCombo_, &QComboBox::currentIndexChanged, this, &SpeechPage::updateEnabledState);
    connect(toCombo_, &QComboBox::currentIndexChanged, this, &SpeechPage::updateEnabledState);
    connect(ttsCombo_, &QComboBox::currentIndexChanged, this, &SpeechPage::updateEnabledState);
    connect(voiceCombo_, &QComboBox::currentIndexChanged, this, &SpeechPage::updateEnabledState);
    connect(speakersCheck_, &QCheckBox::toggled, this, &SpeechPage::updateEnabledState);
    connect(dubFormatCombo_, &QComboBox::currentIndexChanged, this, &SpeechPage::updateEnabledState);
    connect(inputEdit_, &QLineEdit::textChanged, this, [this] {
        // Consent belongs to a particular input, including paths typed by hand.
        const QString path = absolutePath(inputEdit_->text());
        if (path != consentInput_) consentCheck_->setChecked(false);
        consentInput_ = path;
        updateEnabledState();
    });
}

void SpeechPage::updateEnabledState() {
    const QString asr = asrCombo_->currentData().toString();
    bool needsLang = false;
    if (const pk::AsrModel* m = pk::find_asr_model(asr.toStdString())) {
        asrDesc_->setText(q(m->description));
        needsLang = m->needs_language;
    }
    if (needsLang && fromCombo_->currentData().toString() == QLatin1String("auto"))
        selectData(fromCombo_, QStringLiteral("en"));
    const tl::ModelInfo* mt = tl::find_model(mtCombo_->currentData().toString().toStdString());
    QStringList notes;
    if (mt) {
        mtDesc_->setText(q(mt->description));
        const QString to = toCombo_->currentData().toString(), from = fromCombo_->currentData().toString();
        if (!tl::model_supports(*mt, to.toStdString()))
            notes << tr("%1 was not trained on %2; Qwen 3.5 handles every language.").arg(q(mt->display), toCombo_->currentText());
        if (from != QLatin1String("auto") && !tl::model_supports(*mt, from.toStdString()))
            notes << tr("%1 was not trained on %2.").arg(q(mt->display), fromCombo_->currentText());
        if (from == to) notes << tr("The spoken language and the target language are the same.");
        contextCheck_->setEnabled(mt->family != tl::Family::TranslateGemma);
        draftCombo_->setEnabled(tt::fasterModeApplies(mt));
    }
    langNote_->setText(notes.join(QLatin1Char(' ')));
    langNote_->setVisible(!notes.isEmpty());
    const bool chunked = asr != QLatin1String("parakeet-ultra");
    alignCheck_->setEnabled(chunked);

    // Step 3
    const QString tts = ttsCombo_->currentData().toString();
    const QString to = toCombo_->currentData().toString().section(QLatin1Char('-'), 0, 0);
    QStringList dnotes;
    if (const pk::TtsModel* m = pk::find_tts_model(tts.toStdString())) {
        ttsDesc_->setText(q(m->description));
        if (!m->langs.empty() && std::find(m->langs.begin(), m->langs.end(), to.toStdString()) == m->langs.end())
            dnotes << tr("%1 doesn't speak %2; choose OmniVoice.").arg(q(m->display), toCombo_->currentText());
    }
    const bool clone = voiceCombo_->currentData().toString() == QLatin1String("clone");
    const bool several = speakersCheck_->isChecked();
    speakersCount_->setEnabled(several);
    voiceCombo_->setItemText(0, several ? tr("Clone each speaker's voice") : tr("Clone the speaker's voice"));
    consentCheck_->setText(several ? tr("Every speaker agreed to having their voice cloned")
                                   : tr("The speaker agreed to having their voice cloned (or it is my own voice)"));
    accentCheck_->setText(several ? tr("Keep the speakers' original accents (default: sound like native speakers)")
                                  : tr("Keep the speaker's original accent (default: sound like a native speaker)"));
    dubBox_->setTitle(several ? tr("Step 3 - Dubbed audio in the speakers' voices (optional)")
                              : tr("Step 3 - Dubbed audio in the speaker's voice (optional)"));
    consentCheck_->setVisible(clone);
    accentCheck_->setVisible(clone);
    consentNote_->setVisible(clone);
    const QString in = inputEdit_->text().trimmed();
    if (dubFormatCombo_->currentData().toString() == QLatin1String("video") && !in.isEmpty() && !isVideoFile(in))
        dnotes << tr("The input is an audio file, so the dub will be saved as MP3.");
    dubNote_->setText(dnotes.join(QLatin1Char(' ')));
    dubNote_->setVisible(!dnotes.isEmpty());
}

// ------------------------------------------------------------------ files
void SpeechPage::browseInput() {
    QString start = inputEdit_->text();
    if (start.isEmpty()) start = settings_.value(QStringLiteral("lastDir"), QDir::homePath()).toString();
    const QString p = QFileDialog::getOpenFileName(this, tr("Choose audio or video file"), start, tr(kMediaFilter));
    if (!p.isEmpty()) setInputFile(p);
}

void SpeechPage::browseOutputDir() {
    const QString d = QFileDialog::getExistingDirectory(this, tr("Save to folder"), outDirEdit_->text());
    if (!d.isEmpty()) outDirEdit_->setText(QDir::toNativeSeparators(d));
}

void SpeechPage::setInputFile(const QString& path) {
    if (running()) return;
    // Absolute: the tools run with the program's folder as working directory.
    const QString abs = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    inputEdit_->setText(abs);
    settings_.setValue(QStringLiteral("lastDir"), QFileInfo(path).absolutePath());
}

void SpeechPage::dragEnterEvent(QDragEnterEvent* e) {
    if (!running() && e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void SpeechPage::dropEvent(QDropEvent* e) {
    if (running()) return;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile()) {
            setInputFile(u.toLocalFile());
            break;
        }
}

void SpeechPage::openOutputFolder() {
    if (!outputs_.translatedSrt.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(outputs_.translatedSrt).absolutePath()));
}

void SpeechPage::saveLog() {
    const QFileInfo in(inputEdit_->text().trimmed());
    QString dir = outDirEdit_->text().trimmed();
    if (dir.isEmpty()) dir = in.absolutePath();
    const QString suggested = QDir(dir).filePath(in.completeBaseName() + QStringLiteral(".log.txt"));
    const QString p = QFileDialog::getSaveFileName(this, tr("Save log"), suggested, tr("Text files (*.txt);;All files (*)"));
    if (p.isEmpty()) return;
    QString text = logView_->toPlainText();
#ifdef Q_OS_WIN
    text.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
#endif
    if (!writeAll(p, text.toStdString()))
        QMessageBox::warning(this, tr("Speech translation"), tr("Could not write %1").arg(QDir::toNativeSeparators(p)));
}

QString SpeechPage::toolPath(const QString& name) const {
#ifdef Q_OS_WIN
    return QDir(QApplication::applicationDirPath()).filePath(name + QStringLiteral(".exe"));
#else
    return QDir(QApplication::applicationDirPath()).filePath(name);
#endif
}

SpeechPage::RunConfig SpeechPage::readConfig() const {
    RunConfig c;
    const QFileInfo in(inputEdit_->text().trimmed());
    // Absolute: the tools run with the program's folder as working directory.
    c.input = QDir::toNativeSeparators(in.absoluteFilePath());
    const QString dir = outDirEdit_->text().trimmed();
    c.outDir = QDir::toNativeSeparators(dir.isEmpty() ? in.absolutePath() : QFileInfo(dir).absoluteFilePath());
    c.asr = asrCombo_->currentData().toString();
    c.from = fromCombo_->currentData().toString();
    c.mt = mtCombo_->currentData().toString();
    c.to = toCombo_->currentData().toString();
    c.device = deviceCombo_->currentText();
    c.threads = threadsSpin_->value();
    c.align = alignCheck_->isChecked();
    c.context = contextCheck_->isChecked();
    c.draft = tt::fasterModeApplies(tl::find_model(c.mt.toStdString())) ? draftCombo_->currentData().toString()
                                                                         : QStringLiteral("off");
    c.speakers = speakersCheck_->isChecked();
    c.numSpeakers = speakersCount_->currentData().toInt();
    c.saveOriginal = origCheck_->isChecked();
    c.saveBilingual = biCheck_->isChecked();
    c.saveText = txtCheck_->isChecked();
    c.savePdf = pdfCheck_->isChecked();
    c.dub = dubBox_->isChecked();
    c.tts = ttsCombo_->currentData().toString();
    c.voice = voiceCombo_->currentData().toString();
    c.background = backgroundCombo_->currentData().toString();
    c.dubFormat = dubFormatCombo_->currentData().toString();
    c.consent = consentCheck_->isChecked();
    c.keepAccent = accentCheck_->isChecked();
    return c;
}

SpeechPage::Outputs SpeechPage::plannedOutputs(const RunConfig& c) const {
    Outputs o;
    const QFileInfo in(c.input);
    const QString base = QDir(c.outDir).filePath(in.completeBaseName());
    const QString& lang = c.to;
    o.translatedSrt = QDir::toNativeSeparators(base + QLatin1Char('.') + lang + QStringLiteral(".srt"));
    if (c.saveOriginal) o.originalSrt = QDir::toNativeSeparators(base + QStringLiteral(".srt"));
    if (c.saveBilingual) o.bilingualSrt = QDir::toNativeSeparators(base + QLatin1Char('.') + lang + QStringLiteral(".bilingual.srt"));
    if (c.saveText) o.translatedTxt = QDir::toNativeSeparators(base + QLatin1Char('.') + lang + QStringLiteral(".txt"));
    if (c.savePdf) o.translatedPdf = QDir::toNativeSeparators(base + QLatin1Char('.') + lang + QStringLiteral(".pdf"));
    if (c.dub) {
        QString ext = c.dubFormat;
        if (ext == QLatin1String("video")) {
            // MP4 keeps the video stream of MP4-family files; anything else (AVI, WMV,
            // WebM, MKV) may hold codecs MP4 can't, so it goes into MKV.
            const QString inExt = in.suffix().toLower();
            ext = !isVideoFile(in.filePath()) ? QStringLiteral("mp3")
                  : (inExt == QLatin1String("mp4") || inExt == QLatin1String("m4v") || inExt == QLatin1String("mov"))
                      ? QStringLiteral("mp4")
                      : QStringLiteral("mkv");
        }
        o.dubbed = QDir::toNativeSeparators(base + QLatin1Char('.') + lang + QStringLiteral(".dubbed.") + ext);
    }
    return o;
}

bool SpeechPage::confirmOverwrite(const Outputs& o) {
    QStringList existing;
    for (const QString& f : o.all())
        if (QFileInfo::exists(f)) existing << f;
    if (existing.isEmpty()) return true;
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Speech translation"));
    box.setText(existing.size() == 1 ? tr("This file already exists:") : tr("These files already exist:"));
    box.setInformativeText(existing.join(QLatin1Char('\n')) + QStringLiteral("\n\n") + tr("Replace them?"));
    QPushButton* ow = box.addButton(tr("Overwrite"), QMessageBox::DestructiveRole);
    QPushButton* c = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(c);
    box.exec();
    return box.clickedButton() == ow;
}

// ------------------------------------------------------------------ pipeline
QString SpeechPage::asrLanguage() const {
    // The speech models take plain ISO codes: "pt-BR" -> "pt", "zh-Hant" -> "zh".
    const QString& code = cfg_.from;
    if (code == QLatin1String("auto")) return code;
    return code.section(QLatin1Char('-'), 0, 0);
}

QStringList SpeechPage::transcribeArgs() const {
    QStringList a{cfg_.input,
                  QStringLiteral("-m"), cfg_.asr,
                  QStringLiteral("-f"), QStringLiteral("srt"),
                  QStringLiteral("-o"), tempDir_->filePath(QStringLiteral("source.srt")),
                  QStringLiteral("--language"), asrLanguage()};
    if (cfg_.device != QLatin1String("auto")) a << QStringLiteral("--device") << cfg_.device;
    if (cfg_.threads > 0) a << QStringLiteral("--threads") << QString::number(cfg_.threads);
    if (!cfg_.align) a << QStringLiteral("--no-align");
    // Several speakers: the word timings let the speaker step split subtitles
    // exactly where the speaker changes.
    if (cfg_.speakers) a << QStringLiteral("--words") << tempDir_->filePath(QStringLiteral("words.tsv"));
    return a;
}

QString SpeechPage::sourceSrtPath() const {
    return tempDir_->filePath(cfg_.speakers ? QStringLiteral("split.srt") : QStringLiteral("source.srt"));
}

QStringList SpeechPage::speakersArgs() const {
    QStringList a{QStringLiteral("speakers"), cfg_.input,
                  QStringLiteral("--srt"), tempDir_->filePath(QStringLiteral("source.srt")),
                  QStringLiteral("-o"), tempDir_->filePath(QStringLiteral("speakers.txt")),
                  QStringLiteral("--split-srt"), tempDir_->filePath(QStringLiteral("split.srt")),
                  QStringLiteral("--samples-dir"), tempDir_->filePath(QStringLiteral("samples"))};
    if (QFileInfo::exists(tempDir_->filePath(QStringLiteral("words.tsv"))))
        a << QStringLiteral("--words") << tempDir_->filePath(QStringLiteral("words.tsv"));
    if (cfg_.numSpeakers > 0) a << QStringLiteral("--num-speakers") << QString::number(cfg_.numSpeakers);
    if (cfg_.threads > 0) a << QStringLiteral("--threads") << QString::number(cfg_.threads);
    return a;
}

QStringList SpeechPage::translateArgs() const {
    QStringList a{sourceSrtPath(),
                  QStringLiteral("-m"), cfg_.mt,
                  QStringLiteral("-t"), cfg_.to,
                  QStringLiteral("-s"), cfg_.from,
                  QStringLiteral("-o"), tempDir_->filePath(QStringLiteral("translated.srt"))};
    if (cfg_.device != QLatin1String("auto")) a << QStringLiteral("--device") << cfg_.device;
    if (cfg_.threads > 0) a << QStringLiteral("--threads") << QString::number(cfg_.threads);
    if (!cfg_.context) a << QStringLiteral("--no-context");
    if (cfg_.draft != QLatin1String("off")) a << QStringLiteral("--draft") << cfg_.draft;
    // Never one sentence across two speakers: each keeps their own words.
    if (cfg_.speakers) a << QStringLiteral("--speakers") << tempDir_->filePath(QStringLiteral("speakers.txt"));
    return a;
}

int SpeechPage::totalSteps() const { return cfg_.dub ? 3 : 2; }

QStringList SpeechPage::dubArgs() const {
    QStringList a{QStringLiteral("dub"), cfg_.input,
                  QStringLiteral("--srt"), tempDir_->filePath(QStringLiteral("translated.srt")),
                  QStringLiteral("--source-srt"), sourceSrtPath(),
                  QStringLiteral("-t"), cfg_.to,
                  QStringLiteral("--tts"), cfg_.tts,
                  QStringLiteral("--background"), cfg_.background,
                  QStringLiteral("-o"), outputs_.dubbed,
                  QStringLiteral("--work-dir"), dubWorkDir_ ? dubWorkDir_->path() : QString()};
    if (asrLanguage() != QLatin1String("auto")) a << QStringLiteral("-s") << asrLanguage();
    if (cfg_.speakers) a << QStringLiteral("--speakers") << tempDir_->filePath(QStringLiteral("speakers.txt"));
    if (cfg_.voice == QLatin1String("clone")) {
        if (cfg_.consent) a << QStringLiteral("--i-have-rights");
        if (cfg_.keepAccent) a << QStringLiteral("--keep-accent");
    } else {
        a << QStringLiteral("--no-clone");
    }
    if (cfg_.device != QLatin1String("auto")) a << QStringLiteral("--device") << cfg_.device;
    if (cfg_.threads > 0) a << QStringLiteral("--threads") << QString::number(cfg_.threads);
    return a;
}

void SpeechPage::start() {
    if (running() || runner_.isActive()) return;
    QString in = inputEdit_->text().trimmed();
    if (in.size() >= 2 && in.startsWith(QLatin1Char('"')) && in.endsWith(QLatin1Char('"'))) {
        in = in.mid(1, in.size() - 2);
        inputEdit_->setText(in);
    }
    if (!QFileInfo(in).isFile()) {
        QMessageBox::warning(this, tr("Speech translation"), tr("Please choose an existing audio or video file."));
        return;
    }
    if (!tt::JobGate::instance().checkFree(this)) return;
    for (const QString& tool : {QStringLiteral("tt_engine")})
        if (!QFileInfo::exists(toolPath(tool))) {
            QMessageBox::critical(this, tr("Speech translation"),
                                  tr("Can't find %1 next to this program.").arg(QFileInfo(toolPath(tool)).fileName()));
            return;
        }
    const RunConfig c = readConfig();
    if (c.input != inputEdit_->text()) inputEdit_->setText(c.input);
    if (c.from == c.to) {
        QMessageBox::information(this, tr("Speech translation"), tr("The spoken language and the target language are the same."));
        return;
    }
    if (c.dub && c.voice == QLatin1String("clone") && !c.consent) {
        QMessageBox::information(this, tr("Speech translation"),
                                 c.speakers ? tr("Cloning the speakers' voices needs their permission.\n\nTick "
                                                 "\"Every speaker agreed to having their voice cloned\" if they did, "
                                                 "or choose \"Built-in voice\".")
                                            : tr("Cloning a voice needs the speaker's permission.\n\nTick \"The speaker "
                                                 "agreed to having their voice cloned\" if they did (or if it is your "
                                                 "own voice), or choose \"Built-in voice\"."));
        consentCheck_->setFocus();
        return;
    }
    if (!QDir().mkpath(c.outDir)) {
        QMessageBox::critical(this, tr("Speech translation"), tr("Could not create the folder %1").arg(c.outDir));
        return;
    }
    const Outputs planned = plannedOutputs(c);
    if (!confirmOverwrite(planned)) return;

    saveSettings();
    delete tempDir_;
    tempDir_ = new QTemporaryDir;
    if (!tempDir_->isValid()) {
        QMessageBox::critical(this, tr("Speech translation"), tr("Could not create a temporary folder."));
        return;
    }
    cfg_ = c;
    outputs_ = planned;
    speakerMap_ = pk::SpeakerMap();
    removePartialDub();  // the dub's work folder is made when step 3 starts
    if (!runner_.treeReady()) {
        QMessageBox::critical(this, tr("Speech translation"), tr("Could not initialize process cancellation."));
        return;
    }
    logView_->clear();
    resultView_->clear();
    tabs_->setCurrentWidget(logView_);
    resetTiming();
    tt::JobGate::instance().acquire(tr("Speech translation"));
    step_ = Step::Transcribe;  // running() from here on
    setRunning(true);
    clock_.start();
    tick_->start();
    runStep(Step::Transcribe);
}

void SpeechPage::runStep(Step step) {
    step_ = step;
    lastError_.clear();
    translatingSeen_ = false;
    stepStartMs_ = clock_.elapsed();
    // The dub's work folder goes next to its output only now, so a run that
    // stops before step 3 leaves nothing there.
    if (step == Step::Dub && !rememberDubFiles()) {
        fail(tr("Could not create a work folder next to %1").arg(QDir::toNativeSeparators(outputs_.dubbed)));
        return;
    }
    const bool first = step == Step::Transcribe;
    const QString tool = QStringLiteral("tt_engine");
    const QStringList args = step == Step::Transcribe  ? QStringList{QStringLiteral("transcribe")} + transcribeArgs()
                             : step == Step::Speakers  ? speakersArgs()
                             : step == Step::Translate ? QStringList{QStringLiteral("translate")} + translateArgs()
                                                       : dubArgs();
    // Finding the speakers belongs to step 1 (the speech).
    const int n = step == Step::Transcribe || step == Step::Speakers ? 1 : step == Step::Translate ? 2 : 3;
    stepLabel_->setText(tr("Step %1 of %2:").arg(n).arg(totalSteps()));
    statusLabel_->setText(step == Step::Transcribe  ? tr("Starting speech recognition...")
                          : step == Step::Speakers  ? tr("Finding who speaks when...")
                          : step == Step::Translate ? tr("Starting translation...")
                                                    : tr("Starting dubbing..."));
    progress_->setRange(0, 0);

    QStringList shown;
    for (const QString& s : args) shown << (s.contains(QLatin1Char(' ')) ? QLatin1Char('"') + s + QLatin1Char('"') : s);
    appendLog(QStringLiteral("%1> %2 %3\n").arg(first ? QString() : QStringLiteral("\n"), tool, shown.join(QLatin1Char(' '))));

    runner_.start(toolPath(tool), args);
}

// The step's output has been read to the end (the last line too) when this
// runs, and runner_ is idle again, so the next step can start from here.
void SpeechPage::stepFinished(int exitCode, bool cancelled) {
    const double secs = (clock_.elapsed() - stepStartMs_) / 1000.0;
    if (step_ == Step::Transcribe) asrSeconds_ = secs;
    else if (step_ == Step::Speakers) asrSeconds_ = std::max(0.0, asrSeconds_) + secs;  // part of step 1
    else if (step_ == Step::Translate) mtSeconds_ = secs;
    else dubSeconds_ = secs;
    if (cancelled) {
        finishCancelled(step_ == Step::Dub);
        return;
    }
    if (step_ == Step::Translate && exitCode == 3) {
        writeSubtitles();
        if (step_ == Step::Idle) return;
        finishRun();
        if (writeFailures_.isEmpty())
            statusLabel_->setText(tr("Partial translation saved. Failed units retain their original text.") +
                                  (cfg_.dub ? tr(" Dubbing was skipped.") : QString()));
        return;
    }
    if (exitCode != 0) {
        fail(lastError_.isEmpty() ? tr("%1 failed (exit code %2). See the log.")
                                        .arg(step_ == Step::Transcribe  ? tr("Speech recognition")
                                             : step_ == Step::Speakers  ? tr("Speaker detection")
                                             : step_ == Step::Translate ? tr("Translation")
                                                                        : tr("Dubbing"))
                                        .arg(exitCode)
                                  : lastError_);
        return;
    }
    if (step_ == Step::Transcribe) {
        if (!QFileInfo::exists(tempDir_->filePath(QStringLiteral("source.srt")))) {
            fail(tr("Speech recognition produced no subtitles (was there any speech?)."));
            return;
        }
        runStep(cfg_.speakers ? Step::Speakers : Step::Translate);
    } else if (step_ == Step::Speakers) {
        if (reviewSpeakers()) runStep(Step::Translate);
    } else if (step_ == Step::Translate) {
        writeSubtitles();
        if (step_ == Step::Idle) return;  // failed
        if (cfg_.dub) runStep(Step::Dub);
        else finishRun();
    } else {
        if (!QFileInfo::exists(outputs_.dubbed)) {
            fail(tr("Dubbing wrote no file."));
            return;
        }
        removePartialDub();
        finishRun();
    }
}

void SpeechPage::writeSubtitles() {
    const QString srcPath = sourceSrtPath();
    const QString trPath = tempDir_->filePath(QStringLiteral("translated.srt"));
    std::string src = readAll(srcPath), trn = readAll(trPath);
    if (trn.empty()) {
        fail(tr("The translation step wrote no subtitles."));
        return;
    }
    auto srcCues = tl::parse_srt(src);
    const auto trCues = tl::parse_srt(trn);  // the text file needs no dashes: from these
    auto shownCues = trCues;
    // Several speakers: the subtitles were split where the speaker changes; they
    // are saved as the original subtitles again, each speaker's part on its own
    // line with a dash. (The dub speaks the split ones, each part in its voice.)
    if (cfg_.speakers && !speakerMap_.cues.empty()) {
        srcCues = pk::join_speaker_parts(srcCues, speakerMap_);
        shownCues = pk::join_speaker_parts(trCues, speakerMap_);
        src = pk::format_srt(srcCues);
        trn = pk::format_srt(shownCues);
    }
    QStringList failed;
    if (!writeAll(outputs_.translatedSrt, trn)) failed << outputs_.translatedSrt;
    if (!outputs_.originalSrt.isEmpty() && !writeAll(outputs_.originalSrt, src)) failed << outputs_.originalSrt;
    if (!outputs_.bilingualSrt.isEmpty() && !writeAll(outputs_.bilingualSrt, st::bilingual_srt(srcCues, shownCues)))
        failed << outputs_.bilingualSrt;
    if (!outputs_.translatedTxt.isEmpty() && !writeAll(outputs_.translatedTxt, st::subtitles_to_text(trCues, 5)))
        failed << outputs_.translatedTxt;
    if (!outputs_.translatedPdf.isEmpty()) {
        tt::PdfDocument doc = tt::pdfFromText(QString::fromUtf8(st::subtitles_to_text(trCues, 5).c_str()));
        doc.title = QFileInfo(cfg_.input).completeBaseName();
        doc.titleHeading = true;
        doc.language = cfg_.to;
        QString error;
        if (!tt::writePdf(doc, outputs_.translatedPdf, &error)) {
            failed << outputs_.translatedPdf;
            appendLog(tr("\nCould not write %1: %2\n").arg(outputs_.translatedPdf, error));
        }
    }
    writeFailures_ = failed;
    resultView_->setPlainText(QString::fromUtf8(trn.c_str()));
}

void SpeechPage::finishCancelled(bool dubbing) {
    removePartialDub();  // whatever step: nothing of the dub's may stay behind
    step_ = Step::Idle;
    tick_->stop();
    updateTiming();
    stepLabel_->clear();
    statusLabel_->setText(dubbing ? tr("Dubbing cancelled. %1").arg(savedSubtitlesNote()) : tr("Cancelled."));
    speedEdit_->setText(tr("cancelled"));
    appendLog(QStringLiteral("\n[cancelled]\n"));
    if (dubbing) openBtn_->setEnabled(true);  // before setRunning(), which shows it in the progress bar
    setRunning(false);
    reportJob(false);
}

// After speaker detection: the user checks the speakers (names, two who are one
// person) or has them detected again. True = go on with the translation; false
// = the run was cancelled, failed, or detection runs again.
bool SpeechPage::reviewSpeakers() {
    const QString path = tempDir_->filePath(QStringLiteral("speakers.txt"));
    pk::SpeakerMap map;
    std::string why;
    if (!pk::parse_speakers(readAll(path), &map, &why) || map.speakers.empty()) {
        fail(tr("Could not read the speakers that were found (%1).").arg(why.empty() ? tr("none listed") : q(why)));
        return false;
    }
    statusLabel_->setText(map.speakers.size() == 1
                              ? tr("Found one speaker: check in the Speakers window.")
                              : tr("Found %1 speakers: check them in the Speakers window.").arg(map.speakers.size()));
    progress_->setRange(0, 1);
    SpeakersDialog dlg(map, tempDir_->filePath(QStringLiteral("samples")), cfg_.dub, cfg_.numSpeakers, this);
    if (dlg.exec() != QDialog::Accepted) {
        finishCancelled(false);
        return false;
    }
    if (dlg.redetect) {
        cfg_.numSpeakers = dlg.countCombo->currentData().toInt();
        appendLog(QStringLiteral("\n") +
                  (cfg_.numSpeakers > 0 ? tr("Detecting again with %1 speaker(s).").arg(cfg_.numSpeakers)
                                        : tr("Detecting again.")) +
                  QStringLiteral("\n"));
        runStep(Step::Speakers);
        return false;
    }
    speakerMap_ = dlg.result(map);
    if (!writeAll(path, pk::format_speakers(speakerMap_))) {
        fail(tr("Could not write %1").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    QStringList names;
    for (const auto& s : speakerMap_.speakers) names << q(s.name);
    appendLog(QStringLiteral("\n") + tr("Speakers: %1").arg(names.join(QStringLiteral(", "))) + QStringLiteral("\n"));
    return true;
}

void SpeechPage::finishRun() {
    removePartialDub();
    const QStringList& failed = writeFailures_;
    step_ = Step::Idle;
    tick_->stop();
    updateTiming();
    stepLabel_->clear();
    tabs_->setCurrentWidget(resultView_);
    if (!failed.isEmpty()) {
        statusLabel_->setText(tr("Could not write: %1").arg(failed.join(QStringLiteral(", "))));
    } else {
        QStringList names;
        for (const QString& f : outputs_.all()) names << QFileInfo(f).fileName();
        statusLabel_->setText(tr("Done: %1").arg(names.join(QStringLiteral(", "))));
        appendLog(QStringLiteral("\n") + tr("Saved:") + QStringLiteral("\n  ") + outputs_.all().join(QStringLiteral("\n  ")) + QStringLiteral("\n"));
    }
    openBtn_->setEnabled(true);
    setRunning(false);
    reportJob(failed.isEmpty());
}

void SpeechPage::fail(const QString& message) {
    const bool subtitlesSaved = step_ == Step::Dub;  // step 3 runs after the subtitles were written
    removePartialDub();
    step_ = Step::Idle;
    tick_->stop();
    updateTiming();
    stepLabel_->clear();
    statusLabel_->setText(subtitlesSaved ? tr("Failed: %1 — %2").arg(message, savedSubtitlesNote()) : tr("Failed: %1").arg(message));
    speedEdit_->setText(tr("failed"));
    if (subtitlesSaved) openBtn_->setEnabled(true);  // before setRunning(), which shows it in the progress bar
    setRunning(false);
    reportJob(false);
}

// For a run that stopped in step 3: the subtitle files are already saved.
QString SpeechPage::savedSubtitlesNote() {
    QStringList saved, names;
    for (const QString& f : {outputs_.translatedSrt, outputs_.originalSrt, outputs_.bilingualSrt, outputs_.translatedTxt,
                             outputs_.translatedPdf})
        if (!f.isEmpty() && !writeFailures_.contains(f)) saved << f, names << QFileInfo(f).fileName();
    QString note;
    if (!saved.isEmpty()) {
        note = tr("The subtitles were saved: %1").arg(names.join(QStringLiteral(", ")));
        appendLog(QStringLiteral("\n") + tr("Saved:") + QStringLiteral("\n  ") + saved.join(QStringLiteral("\n  ")) + QStringLiteral("\n"));
    }
    if (!writeFailures_.isEmpty())
        note += (note.isEmpty() ? QString() : QStringLiteral("; ")) + tr("could not write: %1").arg(writeFailures_.join(QStringLiteral(", ")));
    return note;
}

// A unique folder beside the output gives the GUI ownership of all job temporaries.
// Made when the Dub step starts; removePartialDub() (on success, failure, cancel
// or closing) deletes it again, and nothing but it.
bool SpeechPage::rememberDubFiles() {
    delete dubWorkDir_;
    dubWorkDir_ = nullptr;
    if (outputs_.dubbed.isEmpty()) return true;
    dubWorkDir_ = new QTemporaryDir(QDir(QFileInfo(outputs_.dubbed).absolutePath()).filePath(QStringLiteral(".speech-dub-XXXXXX")));
    return dubWorkDir_->isValid();
}

void SpeechPage::removePartialDub() {
    delete dubWorkDir_;
    dubWorkDir_ = nullptr;
}

void SpeechPage::stepFailedToStart() {
    fail(tr("could not start %1").arg(QFileInfo(toolPath(QStringLiteral("tt_engine"))).fileName()));
}

// Ends the run without reporting back (closing the window).
void SpeechPage::stopProcess() {
    if (!runner_.isActive()) return;
    runner_.stop();  // kills the tool and everything it started (ffmpeg), waits for it
    removePartialDub();
    step_ = Step::Idle;
    tt::JobGate::instance().release();
}

QString SpeechPage::modelsDir() const { return QString(); }  // the engine's default folders

QString SpeechPage::helpHtml() { return QStringLiteral("<a name=\"speech\"></a>") + st::help_html(false); }

void SpeechPage::reportJob(bool ok) {
    tt::JobGate::instance().release();
    QStringList saved;
    for (const QString& f : outputs_.all())
        if (QFileInfo::exists(f)) saved << f;
    emit jobFinished({tr("Speech translation"), cfg_.input, saved, statusLabel_->text(), ok, QDateTime::currentDateTime()});
}

void SpeechPage::cancel() {
    if (runner_.cancel()) statusLabel_->setText(tr("Cancelling..."));  // stepFinished() follows
}

void SpeechPage::setRunning(bool running) {
    startBtn_->setEnabled(!running);
    cancelBtn_->setEnabled(running);
    for (QWidget* w : std::initializer_list<QWidget*>{asrCombo_, mtCombo_, fromCombo_, toCombo_, origCheck_, biCheck_,
                                                      txtCheck_, pdfCheck_, inputEdit_, outDirEdit_, inBrowseBtn_, outBrowseBtn_,
                                                      dubBox_, advancedBox_, speakersCheck_, speakersCount_})
        w->setEnabled(!running);
    if (running) {
        openBtn_->setEnabled(false);
    } else {
        progress_->setRange(0, 1);
        progress_->setValue(openBtn_->isEnabled() ? 1 : 0);
    }
}

void SpeechPage::parseLine(const QString& line) {
    static const QRegularExpression progressRe(QStringLiteral("^progress: (\\w+) (\\d+)/(\\d+) (\\w+)"));
    if (const auto m = progressRe.match(line); m.hasMatch()) {
        const qint64 done = m.captured(2).toLongLong(), total = m.captured(3).toLongLong();
        const QString phase = m.captured(1), unit = m.captured(4);
        // While translating, the bar follows the share translated ("translating",
        // as the model writes), not the finished pieces ("translate").
        if (phase == QLatin1String("translate") && translatingSeen_) return;
        progress_->setRange(0, (int)std::max<qint64>(total, 1));
        progress_->setValue((int)done);
        if (phase == QLatin1String("translating")) {
            translatingSeen_ = true;
            statusLabel_->setText(tr("Translating: %1%").arg(total > 0 ? done * 100 / total : 0));
        } else if (phase == QLatin1String("download"))
            statusLabel_->setText(tr("Downloading the translation model (first use only): %1 of %2 GB")
                                      .arg(done / 1000.0, 0, 'f', 1).arg(total / 1000.0, 0, 'f', 1));
        else if (phase == QLatin1String("speak"))
            statusLabel_->setText(tr("Speaking: line %1 of %2").arg(done).arg(total));
        else if (phase == QLatin1String("separate"))
            statusLabel_->setText(tr("Separating the voice from music and effects..."));
        else if (phase == QLatin1String("translate"))
            statusLabel_->setText(tr("Translating: part %1 of %2").arg(done).arg(total));
        else if (unit == QLatin1String("s"))
            statusLabel_->setText(tr("%1: %2 / %3 min of audio")
                                      .arg(phase == QLatin1String("align") ? tr("Aligning") : tr("Transcribing"))
                                      .arg(done / 60.0, 0, 'f', 1).arg(total / 60.0, 0, 'f', 1));
        else
            statusLabel_->setText(tr("%1: %2 / %3 chunks")
                                      .arg(phase == QLatin1String("align") ? tr("Word timing") : tr("Transcribing"))
                                      .arg(done).arg(total));
        return;
    }
    static const QRegularExpression audioRe(QStringLiteral("^Audio : .*\\(([0-9.]+) s\\)"));
    if (const auto m = audioRe.match(line); m.hasMatch()) {
        audioSeconds_ = m.captured(1).toDouble();
        audioEdit_->setText(formatDuration(audioSeconds_));
        return;
    }
    static const QRegularExpression engineRe(QStringLiteral("^Engine: (\\S+)(?: \\((.*)\\))?\\s*$"));
    if (const auto m = engineRe.match(line); m.hasMatch()) {
        setEngine(m.captured(1), m.captured(2));
        return;
    }
    const QString s = line.trimmed();
    if (s.startsWith(QLatin1String("Warning: no speech was transcribed"))) {
        lastError_ = tr("Speech recognition produced no subtitles (was there any speech?).");
        statusLabel_->setText(lastError_);
        return;
    }
    if (s.startsWith(QLatin1String("Error:"))) {
        lastError_ = s.mid(6).trimmed();
        return;
    }
    if (s.startsWith(QLatin1String("Loading")) || s.startsWith(QLatin1String("Downloading")) ||
        s.startsWith(QLatin1String("Translating")) || s.startsWith(QLatin1String("Transcribing")) ||
        s.startsWith(QLatin1String("Note:")) || s.startsWith(QLatin1String("Punctuation:")) ||
        s.startsWith(QLatin1String("Detected:")) || s.startsWith(QLatin1String("Separating")) ||
        s.startsWith(QLatin1String("Speaking")) || s.startsWith(QLatin1String("Voice :")) || s.startsWith(QLatin1String("Accent:")) ||
        s.startsWith(QLatin1String("Decoding")) || s.startsWith(QLatin1String("Timing:")) ||
        s.startsWith(QLatin1String("Finding")) || s.startsWith(QLatin1String("Speakers:")) ||
        s.startsWith(QLatin1String("Warning:"))) {
        statusLabel_->setText(s);
        if (s.startsWith(QLatin1String("Loading"))) progress_->setRange(0, 0);
    }
}

void SpeechPage::appendLog(const QString& text) {
    saveLogBtn_->setEnabled(true);
    // Follow the output only while the view is at the end: someone who scrolled
    // up to read an earlier line keeps their place (and their selection).
    QScrollBar* bar = logView_->verticalScrollBar();
    const bool atEnd = bar->value() >= bar->maximum();
    const int keep = bar->value();
    QTextCursor end(logView_->document());
    end.movePosition(QTextCursor::End);
    end.insertText(text);
    bar->setValue(atEnd ? bar->maximum() : keep);
}

void SpeechPage::setEngine(const QString& backend, const QString& detail) {
    QString colour = QStringLiteral("#6b6b6b");
    const QString b = backend.toLower();
    if (b.startsWith(QLatin1String("cuda"))) colour = QStringLiteral("#4e8a00");
    else if (b.startsWith(QLatin1String("vulkan"))) colour = QStringLiteral("#a41e22");
    QString html = tr("Compute:") +
                   QStringLiteral(" <span style=\"background:%1; color:white;\">&nbsp;<b>%2</b>&nbsp;</span>").arg(colour, backend.toHtmlEscaped());
    if (!detail.isEmpty()) html += QStringLiteral(" ") + detail.toHtmlEscaped();
    engineLabel_->setText(html);
}

void SpeechPage::resetTiming() {
    asrSeconds_ = mtSeconds_ = dubSeconds_ = audioSeconds_ = -1;
    writeFailures_.clear();
    for (QLineEdit* e : {audioEdit_, asrTimeEdit_, mtTimeEdit_, dubTimeEdit_, totalEdit_, speedEdit_}) e->setText(QStringLiteral("—"));
}

void SpeechPage::updateTiming() {
    if (!clock_.isValid()) return;
    const double now = clock_.elapsed() / 1000.0, inStep = (clock_.elapsed() - stepStartMs_) / 1000.0;
    totalEdit_->setText(formatDuration(now));
    asrTimeEdit_->setText(formatDuration(asrSeconds_ >= 0 ? asrSeconds_ : (step_ == Step::Transcribe ? inStep : -1)));
    mtTimeEdit_->setText(formatDuration(mtSeconds_ >= 0 ? mtSeconds_ : (step_ == Step::Translate ? inStep : -1)));
    dubTimeEdit_->setText(formatDuration(dubSeconds_ >= 0 ? dubSeconds_ : (step_ == Step::Dub ? inStep : -1)));
    if (step_ == Step::Idle && asrSeconds_ >= 0 && mtSeconds_ >= 0 && (!cfg_.dub || dubSeconds_ >= 0) &&
        audioSeconds_ > 0 && now > 0) {
        const double r = audioSeconds_ / now;
        speedEdit_->setText(tr("%1× real time").arg(r, 0, 'f', r >= 10 ? 1 : 2));
    }
}

// ------------------------------------------------------------------ settings
void SpeechPage::loadSettings() {
    auto& s = settings_;
    selectData(asrCombo_, s.value(QStringLiteral("asr"), QStringLiteral("parakeet-ultra")));
    selectData(mtCombo_, s.value(QStringLiteral("mt"), QStringLiteral("translategemma-12b")));
    selectData(fromCombo_, s.value(QStringLiteral("from"), QStringLiteral("auto")));
    selectData(toCombo_, s.value(QStringLiteral("to"), QStringLiteral("en")));
    origCheck_->setChecked(s.value(QStringLiteral("saveOriginal"), true).toBool());
    biCheck_->setChecked(s.value(QStringLiteral("saveBilingual"), false).toBool());
    txtCheck_->setChecked(s.value(QStringLiteral("saveText"), false).toBool());
    pdfCheck_->setChecked(s.value(QStringLiteral("savePdf"), false).toBool());
    outDirEdit_->setText(s.value(QStringLiteral("outDir")).toString());
    deviceCombo_->setCurrentText(s.value(QStringLiteral("device"), QStringLiteral("auto")).toString());
    threadsSpin_->setValue(s.value(QStringLiteral("threads"), 0).toInt());
    alignCheck_->setChecked(s.value(QStringLiteral("align"), true).toBool());
    contextCheck_->setChecked(s.value(QStringLiteral("context"), true).toBool());
    tt::selectFasterMode(draftCombo_, tt::loadFasterMode(s));
    speakersCheck_->setChecked(s.value(QStringLiteral("speakers"), false).toBool());
    selectData(speakersCount_, s.value(QStringLiteral("numSpeakers"), 0));
    dubBox_->setChecked(s.value(QStringLiteral("dub"), false).toBool());
    selectData(ttsCombo_, s.value(QStringLiteral("tts"), QStringLiteral("qwen3-tts")));
    selectData(voiceCombo_, s.value(QStringLiteral("voice"), QStringLiteral("clone")));
    accentCheck_->setChecked(s.value(QStringLiteral("keepAccent"), false).toBool());
    selectData(backgroundCombo_, s.value(QStringLiteral("background"), QStringLiteral("keep")));
    selectData(dubFormatCombo_, s.value(QStringLiteral("dubFormat"), QStringLiteral("video")));
    // Consent is about one particular recording, so it is never remembered.
    engineLabel_->setText(tr("Compute: <i>shown when a run starts</i>"));
}

void SpeechPage::saveSettings() {
    auto& s = settings_;
    s.setValue(QStringLiteral("asr"), asrCombo_->currentData());
    s.setValue(QStringLiteral("mt"), mtCombo_->currentData());
    s.setValue(QStringLiteral("from"), fromCombo_->currentData());
    s.setValue(QStringLiteral("to"), toCombo_->currentData());
    s.setValue(QStringLiteral("saveOriginal"), origCheck_->isChecked());
    s.setValue(QStringLiteral("saveBilingual"), biCheck_->isChecked());
    s.setValue(QStringLiteral("saveText"), txtCheck_->isChecked());
    s.setValue(QStringLiteral("savePdf"), pdfCheck_->isChecked());
    s.setValue(QStringLiteral("outDir"), outDirEdit_->text());
    s.setValue(QStringLiteral("device"), deviceCombo_->currentText());
    s.setValue(QStringLiteral("threads"), threadsSpin_->value());
    s.setValue(QStringLiteral("align"), alignCheck_->isChecked());
    s.setValue(QStringLiteral("context"), contextCheck_->isChecked());
    s.setValue(QStringLiteral("draftModel"), draftCombo_->currentData());
    s.setValue(QStringLiteral("speakers"), speakersCheck_->isChecked());
    s.setValue(QStringLiteral("numSpeakers"), speakersCount_->currentData());
    s.setValue(QStringLiteral("dub"), dubBox_->isChecked());
    s.setValue(QStringLiteral("tts"), ttsCombo_->currentData());
    s.setValue(QStringLiteral("voice"), voiceCombo_->currentData());
    s.setValue(QStringLiteral("background"), backgroundCombo_->currentData());
    s.setValue(QStringLiteral("keepAccent"), accentCheck_->isChecked());
    s.setValue(QStringLiteral("dubFormat"), dubFormatCombo_->currentData());
}


void SpeechPage::shutdown() {
    stopProcess();  // disconnected first: no finished() reaches a closing window, even if it hangs
    saveSettings();
}
