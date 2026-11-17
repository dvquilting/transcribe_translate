#include <QApplication>

#include "mainwindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Enhance Audio"));

    MainWindow w;
    const QStringList args = QApplication::arguments();
    if (args.size() > 1) w.addFiles(args.mid(1));  // e.g. files dropped onto the .exe, or "Open with"
    w.show();
    return app.exec();
}
