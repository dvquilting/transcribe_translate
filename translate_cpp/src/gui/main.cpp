#include <QApplication>

#include "mainwindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Translate"));

    MainWindow w;
    const QStringList args = QApplication::arguments();
    if (args.size() > 1) w.openFile(args.at(1));  // e.g. a file dropped onto the .exe, or "Translate" in Explorer
    w.show();
    return app.exec();
}
