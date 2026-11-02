#include "history_page.h"

#include <algorithm>

#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QString defaultFile() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(dir).filePath(QStringLiteral("history.json"));
}

}  // namespace

HistoryPage::HistoryPage(const QString& file, QWidget* parent)
    : QWidget(parent), file_(file.isEmpty() ? defaultFile() : file) {
    auto* root = new QVBoxLayout(this);

    auto* buttons = new QHBoxLayout;
    openFileBtn_ = new QPushButton(tr("Open file"));
    openFileBtn_->setToolTip(tr("Open the selected saved file (double-click does the same)"));
    openFolderBtn_ = new QPushButton(tr("Open folder"));
    reuseBtn_ = new QPushButton(tr("Use again"));
    reuseBtn_->setToolTip(tr("Go back to the job's tab with the same input file"));
    removeBtn_ = new QPushButton(tr("Remove"));
    removeBtn_->setToolTip(tr("Remove the job from the history (its files are kept)"));
    clearBtn_ = new QPushButton(tr("Clear history"));
    clearBtn_->setToolTip(tr("Remove every job from the history (the files are kept)"));
    buttons->addWidget(openFileBtn_);
    buttons->addWidget(openFolderBtn_);
    buttons->addWidget(reuseBtn_);
    buttons->addStretch();
    buttons->addWidget(removeBtn_);
    buttons->addWidget(clearBtn_);
    root->addLayout(buttons);

    auto* split = new QSplitter(Qt::Vertical);
    list_ = new QTreeWidget;
    list_->setRootIsDecorated(false);
    list_->setAlternatingRowColors(true);
    list_->setUniformRowHeights(true);
    list_->setHeaderLabels({tr("When"), tr("Job"), tr("Input"), tr("Result")});
    list_->header()->setStretchLastSection(true);
    split->addWidget(list_);

    auto* lower = new QWidget;
    auto* lv = new QVBoxLayout(lower);
    lv->setContentsMargins(0, 0, 0, 0);
    status_ = new QLabel;
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lv->addWidget(status_);
    files_ = new QListWidget;
    lv->addWidget(files_, 1);
    split->addWidget(lower);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    root->addWidget(split, 1);

    connect(list_, &QTreeWidget::currentItemChanged, this, &HistoryPage::showSelected);
    connect(list_, &QTreeWidget::itemDoubleClicked, this, [this] {
        const int i = selectedIndex();
        if (i >= 0 && !jobs_[i].outputs.isEmpty()) openPath(jobs_[i].outputs.first(), false);
    });
    connect(files_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* it) { openPath(it->data(Qt::UserRole).toString(), false); });
    connect(openFileBtn_, &QPushButton::clicked, this, [this] {
        if (QListWidgetItem* it = files_->currentItem()) openPath(it->data(Qt::UserRole).toString(), false);
    });
    connect(openFolderBtn_, &QPushButton::clicked, this, [this] {
        if (QListWidgetItem* it = files_->currentItem()) openPath(it->data(Qt::UserRole).toString(), true);
        else if (const int i = selectedIndex(); i >= 0 && !jobs_[i].input.isEmpty()) openPath(jobs_[i].input, true);
    });
    connect(reuseBtn_, &QPushButton::clicked, this, [this] {
        const int i = selectedIndex();
        if (i >= 0) emit reuseInput(jobs_[i].kind, jobs_[i].input);
    });
    connect(removeBtn_, &QPushButton::clicked, this, [this] {
        const int i = selectedIndex();
        if (i < 0) return;
        jobs_.removeAt(i);
        save();
        rebuild();
        if (list_->topLevelItemCount() > 0) list_->setCurrentItem(list_->topLevelItem(std::min(i, list_->topLevelItemCount() - 1)));
    });
    connect(clearBtn_, &QPushButton::clicked, this, [this] {
        if (jobs_.isEmpty()) return;
        if (QMessageBox::question(this, tr("History"),
                                  tr("Remove all %1 jobs from the history? The files they saved are kept.").arg(jobs_.size())) != QMessageBox::Yes)
            return;
        jobs_.clear();
        save();
        rebuild();
    });

    load();
    rebuild();
}

void HistoryPage::add(const tt::JobRecord& job) {
    jobs_.prepend(job);
    while (jobs_.size() > kMaxJobs) jobs_.removeLast();
    save();
    rebuild();
    list_->setCurrentItem(list_->topLevelItem(0));
}

void HistoryPage::load() {
    QFile f(file_);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("jobs")).toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        tt::JobRecord r;
        r.kind = o.value(QStringLiteral("kind")).toString();
        r.input = o.value(QStringLiteral("input")).toString();
        for (const QJsonValue& p : o.value(QStringLiteral("outputs")).toArray()) r.outputs << p.toString();
        r.status = o.value(QStringLiteral("status")).toString();
        r.ok = o.value(QStringLiteral("ok")).toBool();
        r.finished = QDateTime::fromString(o.value(QStringLiteral("finished")).toString(), Qt::ISODate);
        if (!r.kind.isEmpty()) jobs_ << r;
        if (jobs_.size() >= kMaxJobs) break;
    }
}

void HistoryPage::save() const {
    QJsonArray arr;
    for (const tt::JobRecord& r : jobs_) {
        QJsonObject o;
        o[QStringLiteral("kind")] = r.kind;
        o[QStringLiteral("input")] = r.input;
        o[QStringLiteral("outputs")] = QJsonArray::fromStringList(r.outputs);
        o[QStringLiteral("status")] = r.status;
        o[QStringLiteral("ok")] = r.ok;
        o[QStringLiteral("finished")] = r.finished.toString(Qt::ISODate);
        arr << o;
    }
    QDir().mkpath(QFileInfo(file_).absolutePath());
    QSaveFile f(file_);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("jobs"), arr}}).toJson());
    f.commit();
}

void HistoryPage::rebuild() {
    list_->clear();
    for (const tt::JobRecord& r : jobs_) {
        auto* it = new QTreeWidgetItem(list_);
        it->setText(0, QLocale().toString(r.finished, QLocale::ShortFormat));
        it->setText(1, r.kind);
        it->setText(2, r.input.isEmpty() ? QString() : QFileInfo(r.input).fileName());
        it->setToolTip(2, QDir::toNativeSeparators(r.input));
        const int n = (int)r.outputs.size();
        it->setText(3, !r.ok ? r.status : n == 0 ? tr("Done") : n == 1 ? tr("Done (1 file)") : tr("Done (%1 files)").arg(n));
        it->setToolTip(3, r.status);
        if (!r.ok) it->setForeground(3, QColor(0xb0, 0x30, 0x30));
    }
    for (int c = 0; c < 3; ++c) list_->resizeColumnToContents(c);
    showSelected();
}

int HistoryPage::selectedIndex() const {
    QTreeWidgetItem* it = list_->currentItem();
    return it ? list_->indexOfTopLevelItem(it) : -1;
}

void HistoryPage::showSelected() {
    files_->clear();
    const int i = selectedIndex();
    const bool any = i >= 0;
    removeBtn_->setEnabled(any);
    clearBtn_->setEnabled(!jobs_.isEmpty());
    reuseBtn_->setEnabled(any && !jobs_[i].input.isEmpty() && QFileInfo::exists(jobs_[i].input));
    if (!any) {
        status_->setText(jobs_.isEmpty() ? tr("No jobs yet. Finished transcriptions and translations are listed here.")
                                         : QString());
        openFileBtn_->setEnabled(false);
        openFolderBtn_->setEnabled(false);
        return;
    }
    const tt::JobRecord& r = jobs_[i];
    status_->setText(QStringLiteral("<b>%1</b> — %2<br>%3")
                         .arg(r.kind.toHtmlEscaped(), QDir::toNativeSeparators(r.input).toHtmlEscaped(),
                              r.status.toHtmlEscaped()));
    for (const QString& p : r.outputs) {
        const bool exists = QFileInfo::exists(p);
        auto* it = new QListWidgetItem(QDir::toNativeSeparators(p) + (exists ? QString() : tr("  (no longer there)")));
        it->setData(Qt::UserRole, p);
        if (!exists) it->setForeground(Qt::gray);
        files_->addItem(it);
    }
    if (files_->count() > 0) files_->setCurrentRow(0);
    openFileBtn_->setEnabled(files_->count() > 0);
    openFolderBtn_->setEnabled(files_->count() > 0 || !r.input.isEmpty());
}

void HistoryPage::openPath(const QString& path, bool folder) {
    const QFileInfo fi(path);
    const QString target = folder ? fi.absolutePath() : fi.absoluteFilePath();
    if (!QFileInfo::exists(target)) {
        QMessageBox::information(this, tr("History"), tr("%1 is no longer there.").arg(QDir::toNativeSeparators(target)));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(target));
}
