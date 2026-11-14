// speakers_dialog.h - the Speakers window of the Speech translation and
// Transcribe tabs: the speakers that "tt_engine speakers" found, for the user
// to check before the run goes on - listen to each, name them, say which two
// are one person, or detect again with a given number of speakers.
#pragma once

#include <algorithm>
#include <utility>
#include <vector>

#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "speakers.h"  // ../common

namespace tt {

// The speakers found, for the user to check before the run goes on: listen to
// each, name them, say which two are one person, or detect again with a
// given number of speakers.
class SpeakersDialog : public QDialog {
public:
    // What the speakers are for: translated subtitles (a dash before each speaker's
    // part), a dub as well (a voice each), or a transcript (their names in it).
    enum Use { Subtitles, Dub, Transcript };

    SpeakersDialog(const pk::SpeakerMap& map, const QString& samplesDir, Use use, int count, QWidget* parent)
        : QDialog(parent) {
        setWindowTitle(QObject::tr("Speakers"));
        const bool dub = use == Dub;
        auto* v = new QVBoxLayout(this);
        const int n = (int)map.speakers.size();
        auto* head = new QLabel(
            (n == 1 ? QObject::tr("One speaker was found.")
                    : QObject::tr("%1 speakers were found.").arg(n)) +
            QStringLiteral(" ") +
            QObject::tr("Listen to each one, name them if you like, and mark two that are really the same person.") +
            (dub ? QStringLiteral(" ") + QObject::tr("Each speaker gets their own voice in the dub.") : QString()) +
            QStringLiteral(" ") +
            (use == Transcript
                 ? QObject::tr("The names are written into the transcript: each speaker's turn starts with their "
                               "name, and so do the subtitles where the speaker changes.")
                 : QObject::tr("Where the speaker changes within a subtitle, each speaker's part gets its own line, "
                               "starting with a dash.")));
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
            const QString time = duration(s.seconds);
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
        if (const int i = countCombo->findData(count); i >= 0) countCombo->setCurrentIndex(i);
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

    // 65.4 s -> "1:05.4"
    static QString duration(double seconds) {
        const qint64 tenths = qRound64(std::max(0.0, seconds) * 10.0);
        const qint64 h = tenths / 36000, m = (tenths / 600) % 60, sec = (tenths / 10) % 60, t = tenths % 10;
        if (h > 0)
            return QStringLiteral("%1:%2:%3.%4").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(sec, 2, 10, QLatin1Char('0')).arg(t);
        return QStringLiteral("%1:%2.%3").arg(m).arg(sec, 2, 10, QLatin1Char('0')).arg(t);
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

}  // namespace tt
