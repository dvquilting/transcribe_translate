// main.cpp - transcribe_translate: the window of Transcribe & Translate.
#include <QApplication>

#include "mainwindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("DavidV"));
    QApplication::setApplicationName(QStringLiteral("TranscribeTranslate"));  // the data folder (History)
    QApplication::setApplicationDisplayName(QStringLiteral("Transcribe & Translate"));

    MainWindow w;
    // transcribe_translate [--transcribe | --translate | --speech] [FILE]: a file dropped
    // on the program, or from Explorer's right-click menu (which names the tab).
    QString tab, file;
    for (const QString& a : QApplication::arguments().mid(1)) {
        if (a == QLatin1String("--transcribe") || a == QLatin1String("--translate") || a == QLatin1String("--speech"))
            tab = a.mid(2);
        else if (file.isEmpty())
            file = a;
    }
    if (!file.isEmpty()) w.openFile(file, tab);
    w.show();
    return app.exec();
}
