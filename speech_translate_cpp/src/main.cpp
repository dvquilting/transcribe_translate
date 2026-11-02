#include <QApplication>

#include "mainwindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Speech Translate"));

    MainWindow w;
    const QStringList args = QApplication::arguments();
    if (args.size() > 1) w.setInputFile(args.at(1));  // file dropped on the .exe or from Explorer's menu
    w.show();
    return app.exec();
}
