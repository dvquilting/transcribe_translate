#include "jobgate.h"

#include <QApplication>
#include <QDir>
#include <QMessageBox>

namespace tt {

JobGate& JobGate::instance() {
    static JobGate gate;
    return gate;
}

bool JobGate::checkFree(QWidget* parent) const {
    if (!busy()) return true;
    QMessageBox::information(parent, QApplication::applicationDisplayName(),
                             QObject::tr("A job is running on the %1 tab. Wait for it to finish, or cancel it there "
                                         "first: one job runs at a time, so each model has the computer to itself.")
                                 .arg(owner_));
    return false;
}

void JobGate::acquire(const QString& owner) {
    owner_ = owner;
    emit changed(true);
}

void JobGate::release() {
    if (owner_.isEmpty()) return;
    owner_.clear();
    emit changed(false);
}

QString enginePath() {
#ifdef Q_OS_WIN
    return QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("tt_engine.exe"));
#else
    return QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("tt_engine"));
#endif
}

}  // namespace tt
