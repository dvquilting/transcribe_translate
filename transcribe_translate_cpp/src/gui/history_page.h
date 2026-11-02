// history_page.h - the History tab: the jobs of the three other tabs, newest
// first, with the files each one saved (open the file or its folder). Kept in
// history.json in the program's data folder, so it survives a restart.
#pragma once

#include <QList>
#include <QWidget>

#include "jobgate.h"

class QLabel;
class QListWidget;
class QPushButton;
class QTreeWidget;

class HistoryPage : public QWidget {
    Q_OBJECT
public:
    // `file`: where the history is kept ("" = history.json in the data folder).
    explicit HistoryPage(const QString& file = QString(), QWidget* parent = nullptr);

    void add(const tt::JobRecord& job);  // newest first; saved at once
    const QList<tt::JobRecord>& jobs() const { return jobs_; }
    QString file() const { return file_; }

    static constexpr int kMaxJobs = 500;  // the oldest go when there are more

signals:
    void reuseInput(const QString& kind, const QString& path);  // "Use again": back to its tab with this input

private:
    void load();
    void save() const;
    void rebuild();
    void showSelected();
    int selectedIndex() const;
    void openPath(const QString& path, bool folder);

    QString file_;
    QList<tt::JobRecord> jobs_;
    QTreeWidget* list_ = nullptr;
    QListWidget* files_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* openFileBtn_ = nullptr;
    QPushButton* openFolderBtn_ = nullptr;
    QPushButton* reuseBtn_ = nullptr;
    QPushButton* removeBtn_ = nullptr;
    QPushButton* clearBtn_ = nullptr;
};
