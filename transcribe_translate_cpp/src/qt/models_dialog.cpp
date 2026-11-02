#include "models_dialog.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

#include "model_files.h"

namespace pk {

ModelsDialog::ModelsDialog(const QList<Folder>& folders, QWidget* parent) : QDialog(parent), folders_(folders) {
    setWindowTitle(tr("Model files"));
    resize(820, 480);
    auto* root = new QVBoxLayout(this);

    auto* intro = new QLabel(tr("The models in the program's model folders. A deleted model is downloaded "
                                "again the next time it is needed. Models that other programs keep in the same "
                                "folder are marked and are not deleted by Clean All models."));
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // long paths scroll in their own box
    body_ = new QWidget;
    bodyLayout_ = new QVBoxLayout(body_);
    scroll->setWidget(body_);
    root->addWidget(scroll, 1);

    auto* bottom = new QHBoxLayout;
    totalLabel_ = new QLabel;
    bottom->addWidget(totalLabel_);
    bottom->addStretch();
    cleanAll_ = new QPushButton(tr("Clean All models"));
    cleanAll_->setToolTip(tr("Delete every model listed here that these programs downloaded"));
    connect(cleanAll_, &QPushButton::clicked, this, &ModelsDialog::cleanAll);
    bottom->addWidget(cleanAll_);
    auto* close = new QPushButton(tr("Close"));
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    bottom->addWidget(close);
    root->addLayout(bottom);

    refresh();
}

void ModelsDialog::refresh() {
    // Rebuild the list from the folders (cheap: one directory listing each).
    while (QLayoutItem* item = bodyLayout_->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
    qint64 total = 0;
    int count = 0;
    bool anyOurs = false;  // something for Clean All to delete
    for (const Folder& folder : folders_) {
        auto* box = new QGroupBox(folder.title);
        auto* grid = new QGridLayout(box);  // path | note | size | Delete
        grid->setColumnStretch(0, 1);
        int row = 0;

        auto* where = new QLabel(tr("Folder: %1").arg(folder.dir));
        where->setTextInteractionFlags(Qt::TextSelectableByMouse);
        where->setWordWrap(true);
        where->setMinimumWidth(200);
        where->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        grid->addWidget(where, row, 0, 1, 3);
        auto* open = new QPushButton(tr("Open folder"));
        open->setEnabled(QFileInfo(folder.dir).isDir());
        const QString dir = folder.dir;
        connect(open, &QPushButton::clicked, this, [dir] { QDesktopServices::openUrl(QUrl::fromLocalFile(dir)); });
        grid->addWidget(open, row++, 3);

        const QList<ModelFile> models = list_models(folder.dir, folder.owner);
        if (models.isEmpty()) grid->addWidget(new QLabel(tr("No models downloaded.")), row++, 0, 1, 4);
        for (const ModelFile& m : models) {
            // Read-only text box: the full path can be selected and copied, and a path
            // too long for the window keeps its file name in view.
            auto* path = new QLineEdit(m.path);
            path->setReadOnly(true);
            path->setFrame(false);
            path->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; }"));
            path->setMinimumWidth(200);
            path->setCursorPosition(m.path.size());
            path->setToolTip(m.path);
            grid->addWidget(path, row, 0);
            if (!m.ours) {
                auto* note = new QLabel(tr("(not downloaded by these programs)"));
                note->setObjectName(QStringLiteral("foreign:") + m.path);
                note->setToolTip(tr("Another program keeps this model here. Clean All models leaves it alone."));
                note->setEnabled(false);  // greyed: a remark, not a control
                grid->addWidget(note, row, 1);
            }
            anyOurs = anyOurs || m.ours;
            auto* size = new QLabel(human_size(m.bytes));
            size->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            grid->addWidget(size, row, 2);
            auto* del = new QPushButton(tr("Delete"));
            del->setObjectName(QStringLiteral("delete:") + m.path);
            del->setToolTip(tr("Delete %1").arg(QFileInfo(m.path).fileName()));
            const QString p = m.path;
            const qint64 bytes = m.bytes;
            const bool ours = m.ours;
            connect(del, &QPushButton::clicked, this, [this, p, bytes, ours] { deleteOne(p, bytes, ours); });
            grid->addWidget(del, row++, 3);
            total += m.bytes;
            ++count;
        }
        const QList<ModelFile> left = list_leftovers(folder.dir, folder.owner);
        if (!left.isEmpty()) {
            qint64 lb = 0;
            for (const ModelFile& x : left) lb += x.bytes;
            grid->addWidget(new QLabel(tr("Unfinished downloads and leftovers: %n file(s), %1 (removed by Clean All "
                                          "models)",
                                          nullptr, (int)left.size())
                                           .arg(human_size(lb))),
                            row++, 0, 1, 4);
            total += lb;
            anyOurs = true;
        }
        bodyLayout_->addWidget(box);
    }
    bodyLayout_->addStretch();
    totalLabel_->setText(tr("%n model(s), %1 in total", nullptr, count).arg(human_size(total)));
    cleanAll_->setEnabled(anyOurs);
}

bool ModelsDialog::ask(const QString& title, const QString& text) {
    lastQuestion_ = text;
    if (!confirm_) return true;
    return QMessageBox::question(this, title, text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) ==
           QMessageBox::Yes;
}

void ModelsDialog::deleteOne(const QString& path, qint64 bytes, bool ours) {
    const QString again = ours ? tr("It will be downloaded again the next time it is needed.")
                               : tr("These programs did not download it: the program it belongs to may need it.");
    if (!ask(tr("Delete model"),
             tr("Delete %1 (%2)?\n\n%3\n\n%4").arg(QFileInfo(path).fileName(), human_size(bytes), path, again)))
        return;
    QString error;
    if (!delete_model(path, &error))
        QMessageBox::warning(this, tr("Delete model"),
                             tr("Could not delete %1:\n%2\n\nIs another program (or a running transcription, "
                                "translation or dub) using it?")
                                 .arg(path, error));
    refresh();
}

void ModelsDialog::cleanAll() {
    // Only what these programs downloaded, named one by one in the question.
    qint64 total = 0;
    int count = 0, kept = 0;
    QStringList models, leftovers;
    for (const Folder& f : folders_) {
        for (const ModelFile& m : list_models(f.dir, f.owner)) {
            if (!m.ours) {
                ++kept;
                continue;
            }
            total += m.bytes, ++count;
            models << QFileInfo(m.path).fileName();
        }
        for (const ModelFile& x : list_leftovers(f.dir, f.owner)) {
            total += x.bytes;
            leftovers << QFileInfo(x.path).fileName();
        }
    }
    if (models.isEmpty() && leftovers.isEmpty()) return;
    QString text = tr("Delete these %n downloaded model(s) (%1 in all)?", nullptr, count).arg(human_size(total));
    if (!models.isEmpty()) text += QStringLiteral("\n\n") + models.join(QLatin1Char('\n'));
    if (!leftovers.isEmpty())
        text += QStringLiteral("\n\n") + tr("Unfinished downloads and leftovers:") + QLatin1Char('\n') +
                leftovers.join(QLatin1Char('\n'));
    text += QStringLiteral("\n\n") + tr("They will be downloaded again the next time they are needed.");
    if (kept > 0)
        text += QLatin1Char(' ') +
                tr("%n model(s) that these programs did not download will be kept.", nullptr, kept);
    if (!ask(tr("Clean All models"), text)) return;
    QStringList failed;
    for (const Folder& f : folders_) delete_all_models(f.dir, f.owner, &failed);
    if (!failed.isEmpty())
        QMessageBox::warning(this, tr("Clean All models"),
                             tr("Could not delete:\n%1\n\nIs another program (or a running transcription, "
                                "translation or dub) using them?")
                                 .arg(failed.join(QLatin1Char('\n'))));
    refresh();
}

}  // namespace pk
