#include <QApplication>

#include "mainwindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Transcribe"));

    MainWindow w;
    const QStringList args = QApplication::arguments();
    if (args.size() > 1) w.setInputFile(args.at(1));  // e.g. drag a file onto the .exe
    w.show();
    return app.exec();
}
