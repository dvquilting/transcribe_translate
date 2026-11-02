// models_dialog.h - the GUIs' "Model files" window: every model in the model
// folders with its full path and size, a "Delete" button for each, and "Clean All
// models". Models that these programs did not download (other programs' models
// in a shared folder, see ModelOwner) are marked as such and are left alone by
// Clean All. Deleted models are downloaded again the next time they are needed.
#pragma once

#include <QDialog>
#include <QList>
#include <QString>

#include "model_files.h"

class QVBoxLayout;
class QLabel;
class QPushButton;

namespace pk {

class ModelsDialog : public QDialog {
    Q_OBJECT
public:
    struct Folder {
        QString title;  // "Speech recognition and voice models"
        QString dir;    // from crispasr_models_dir() / translate_models_dir()
        ModelOwner owner;  // which of its models are ours: {true} for CrispASR's cache,
                           // {false, Translate's model file names} for Translate's
    };
    explicit ModelsDialog(const QList<Folder>& folders, QWidget* parent = nullptr);

    // For tests: skip the "are you sure" questions (answer yes).
    void setConfirm(bool ask) { confirm_ = ask; }
    // For tests: the text of the last question (asked or not).
    QString lastQuestion() const { return lastQuestion_; }
    QPushButton* cleanAllButton() const { return cleanAll_; }

public slots:
    void refresh();
    void cleanAll();

private:
    bool ask(const QString& title, const QString& text);
    void deleteOne(const QString& path, qint64 bytes, bool ours);

    QList<Folder> folders_;
    QWidget* body_ = nullptr;
    QVBoxLayout* bodyLayout_ = nullptr;
    QLabel* totalLabel_ = nullptr;
    QPushButton* cleanAll_ = nullptr;
    bool confirm_ = true;
    QString lastQuestion_;
};

}  // namespace pk
