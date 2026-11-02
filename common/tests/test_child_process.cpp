// Tests of pk::ChildProcess with real child processes (this program started
// again in a helper mode): UTF-8 split between reads, line splitting, exit
// codes, a crash, cancelling a whole process tree, start failures and stop().
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <functional>

#include "child_process.h"

#ifndef Q_OS_WIN
#include <signal.h>
#endif

static int failures = 0;
#define CHECK(x)                                                              \
    do {                                                                      \
        if (!(x)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

// Helper modes of the test program itself.
static int helper(int argc, char** argv) {
    if (argc < 2) return -1;
    QCoreApplication app(argc, argv);
    const QString mode = app.arguments()[1];
    if (mode == QLatin1String("--utf8")) {  // "café\n" in two writes, split inside the é
        const QByteArray text = QString::fromUtf8("caf\xC3\xA9\n").toUtf8();
        std::fwrite(text.constData(), 1, 4, stdout);
        std::fflush(stdout);
        QTimer::singleShot(150, [&] {
            std::fwrite(text.constData() + 4, 1, text.size() - 4, stdout);
            std::fflush(stdout);
            app.quit();
        });
        return app.exec();
    }
    if (mode == QLatin1String("--lines")) {  // CRLF, LF and an unterminated last line
        // On Windows stdout is in text mode, so this arrives as "a\r\r\nb\r\nc"; elsewhere
        // the doubled carriage return is written directly, so both are tested everywhere.
#ifdef Q_OS_WIN
        std::fputs("a\r\nb\nc", stdout);
#else
        std::fputs("a\r\r\nb\nc", stdout);
#endif
        std::fflush(stdout);
        return 7;
    }
    if (mode == QLatin1String("--crash")) {  // output without a line break, then killed by a signal
        std::fputs("partial", stdout);
        std::fflush(stdout);
#ifdef Q_OS_WIN
        TerminateProcess(GetCurrentProcess(), 0xC0000005);  // what an access violation ends with
#else
        ::raise(SIGTERM);  // a signal number reads like an exit code (and makes no core dump)
#endif
        return 0;
    }
    if (mode == QLatin1String("--child-tree")) {  // a grandchild the cancel must kill too
        QProcess child;
        child.start(app.applicationFilePath(), {QStringLiteral("--sleep")});
        if (!child.waitForStarted()) return 1;
        std::printf("%lld\n", (long long)child.processId());
        std::fflush(stdout);
        QTimer::singleShot(15000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    QTimer::singleShot(15000, &app, &QCoreApplication::quit);  // --sleep
    return app.exec();
}

// Run the event loop until `done` or `ms` passed.
static bool waitFor(const std::function<bool()>& done, int ms = 10000) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(5);
    }
    return done();
}

static bool processGone(qint64 pid) {
#ifdef Q_OS_WIN
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!h) return true;
    const bool gone = WaitForSingleObject(h, 5000) == WAIT_OBJECT_0;
    CloseHandle(h);
    return gone;
#else
    for (int i = 0; i < 100; ++i) {
        if (::kill((pid_t)pid, 0) != 0) return true;
        QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));  // killed but not yet reaped by init
        if (stat.open(QIODevice::ReadOnly) && QString::fromLatin1(stat.readAll()).section(QStringLiteral(") "), 1)
                                                   .startsWith(QLatin1Char('Z')))
            return true;
        QThread::msleep(50);
    }
    return false;
#endif
}

struct Recorder {
    QString text;
    QStringList lines;
    int finishedCount = 0, exitCode = -100, failedCount = 0;
    bool cancelled = false;
    explicit Recorder(pk::ChildProcess& p) {
        QObject::connect(&p, &pk::ChildProcess::outputReceived, [this](const QString& s) { text += s; });
        QObject::connect(&p, &pk::ChildProcess::lineReceived, [this](const QString& l) { lines << l; });
        QObject::connect(&p, &pk::ChildProcess::finished, [this](int code, bool c) {
            ++finishedCount;
            exitCode = code;
            cancelled = c;
        });
        QObject::connect(&p, &pk::ChildProcess::failedToStart, [this] { ++failedCount; });
    }
};

int main(int argc, char** argv) {
    const int mode = helper(argc, argv);
    if (mode >= 0) return mode;
    QCoreApplication app(argc, argv);
    const QString self = app.applicationFilePath();

    {  // a UTF-8 character split between two reads is not garbled
        pk::ChildProcess p;
        CHECK(p.treeReady());
        Recorder r(p);
        CHECK(p.start(self, {QStringLiteral("--utf8")}));
        CHECK(p.isActive());
        CHECK(waitFor([&] { return r.finishedCount > 0; }));
        CHECK(r.text.contains(QString::fromUtf8("caf\xC3\xA9")) && !r.text.contains(QChar(0xFFFD)));
        CHECK(r.lines == QStringList{QString::fromUtf8("caf\xC3\xA9")});
        CHECK(r.exitCode == 0 && !r.cancelled && !p.isActive());
    }
    {  // lines (CRLF/LF, unterminated last one), exit code; the next step can start from finished()
        pk::ChildProcess p;
        Recorder r(p);
        int runs = 0;
        QObject::connect(&p, &pk::ChildProcess::finished, [&] {
            if (++runs == 1) CHECK(p.start(self, {QStringLiteral("--lines")}));
        });
        CHECK(p.start(self, {QStringLiteral("--lines")}));
        CHECK(!p.start(self, {QStringLiteral("--lines")}));  // one run at a time
        CHECK(waitFor([&] { return r.finishedCount == 2; }));
        CHECK(r.lines == (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"),
                                      QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
        CHECK(r.exitCode == 7 && !r.cancelled);
    }
    {  // a crash: not the signal number as exit code, and the log says so
        pk::ChildProcess p;
        Recorder r(p);
        CHECK(p.start(self, {QStringLiteral("--crash")}));
        CHECK(waitFor([&] { return r.finishedCount > 0; }));
        CHECK(r.exitCode == pk::ChildProcess::kCrashExitCode && !r.cancelled);
        CHECK(r.lines == (QStringList{QStringLiteral("partial"), QStringLiteral("The program crashed.")}));
        CHECK(r.text == QLatin1String("partial\nThe program crashed.\n"));
    }
    {  // cancel kills the tool and what it started; finished() reports it
        pk::ChildProcess p;
        Recorder r(p);
        CHECK(!p.cancel());  // nothing running
        CHECK(p.start(self, {QStringLiteral("--child-tree")}));
        CHECK(waitFor([&] { return !r.lines.isEmpty(); }));
        const qint64 grandchild = r.lines.value(0).trimmed().toLongLong();
        CHECK(grandchild > 0);
        CHECK(p.cancel());
        CHECK(waitFor([&] { return r.finishedCount > 0; }));
        CHECK(r.cancelled && r.finishedCount == 1 && !p.isActive());
        CHECK(!r.text.contains(QLatin1String("crashed")));  // killed on purpose
        CHECK(grandchild > 0 && processGone(grandchild));
    }
    {  // a program that can't start: failedToStart(), no finished(), idle again
        pk::ChildProcess p;
        Recorder r(p);
        CHECK(p.start(QStringLiteral("/nonexistent/program-that-does-not-exist"), {}));
        CHECK(waitFor([&] { return r.failedCount > 0; }));
        waitFor([] { return false; }, 200);
        CHECK(r.failedCount == 1 && r.finishedCount == 0 && !p.isActive());
    }
    {  // stop(): for a closing window - no signals, the tree is gone
        pk::ChildProcess p;
        Recorder r(p);
        CHECK(p.start(self, {QStringLiteral("--child-tree")}));
        CHECK(waitFor([&] { return !r.lines.isEmpty(); }));
        const qint64 grandchild = r.lines.value(0).trimmed().toLongLong();
        p.stop();
        CHECK(!p.isActive() && !p.isRunning());
        waitFor([] { return false; }, 200);
        CHECK(r.finishedCount == 0 && r.failedCount == 0);
        CHECK(grandchild > 0 && processGone(grandchild));
    }
    {  // destroyed while running: same as stop()
        auto* p = new pk::ChildProcess;
        Recorder r(*p);
        CHECK(p->start(self, {QStringLiteral("--child-tree")}));
        CHECK(waitFor([&] { return !r.lines.isEmpty(); }));
        const qint64 grandchild = r.lines.value(0).trimmed().toLongLong();
        delete p;
        CHECK(grandchild > 0 && processGone(grandchild));
    }
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
