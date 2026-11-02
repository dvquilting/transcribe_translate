// Tests of the downloaded-model helpers (model_files.h) and the "Model files" window
// (models_dialog.h) on a temporary folder: what counts as a model, what goes
// with it when it is deleted, which models are ours (and the only ones Clean All
// deletes), that a download in progress is left alone, and that nothing else is
// ever touched.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#include "model_files.h"
#include "models_dialog.h"

static int failures = 0;
#define CHECK(x)                                                              \
    do {                                                                      \
        if (!(x)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

static void touch(const QString& path, int bytes) {
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(QByteArray(bytes, 'x'));
}

static bool exists(const QDir& d, const char* name) { return QFile::exists(d.filePath(QString::fromUtf8(name))); }

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    QDir d(tmp.path());

    // Folder locations.
    CHECK(pk::crispasr_models_dir(tmp.path()) == QDir::toNativeSeparators(tmp.path()));
    qputenv("CRISPASR_CACHE_DIR", tmp.path().toUtf8());
    CHECK(pk::crispasr_models_dir() == QDir::toNativeSeparators(tmp.path()));
    qunsetenv("CRISPASR_CACHE_DIR");
    CHECK(pk::crispasr_models_dir().endsWith(QDir::toNativeSeparators(QStringLiteral(".cache/crispasr"))));
    CHECK(pk::translate_models_dir(tmp.path()) == QDir::toNativeSeparators(tmp.path()));
    CHECK(!pk::translate_models_dir().isEmpty());

    // CrispASR's order: HOME first (on Windows too, before USERPROFILE / LOCALAPPDATA).
    const QByteArray oldHome = qgetenv("HOME");
    qputenv("HOME", tmp.path().toUtf8());
    CHECK(pk::crispasr_models_dir() == QDir::toNativeSeparators(tmp.path() + QStringLiteral("/.cache/crispasr")));
    qputenv("HOME", oldHome);

    // CrispASR's cache: a model with a .src sidecar is ours.
    const pk::ModelOwner crispasr{true, {}};
    const QString livePart = QStringLiteral("asr.gguf.part.%1.1").arg(QCoreApplication::applicationPid());
    touch(d.filePath(QStringLiteral("asr.gguf")), 3000);
    touch(d.filePath(QStringLiteral("asr.gguf.src")), 10);       // CrispASR's origin note
    touch(d.filePath(QStringLiteral("asr.gguf.part.999999999.0")), 5);  // CrispASR, unfinished, its process gone
    touch(d.filePath(livePart), 5);                               // CrispASR, still downloading (our pid)
    touch(d.filePath(QStringLiteral("llm.GGUF")), 2000);         // no .src: another program's
    touch(d.filePath(QStringLiteral("llm.GGUF.lock")), 0);
    touch(d.filePath(QStringLiteral("other.gguf")), 100);        // another program's
    touch(d.filePath(QStringLiteral("big.gguf.part")), 700);     // Translate-style: not CrispASR's
    touch(d.filePath(QStringLiteral("gone.gguf.src")), 10);      // its model was deleted by hand
    touch(d.filePath(QStringLiteral("notes.txt")), 10);          // not a model
    touch(d.filePath(QStringLiteral("readme.src")), 10);         // not a model's
    d.mkpath(QStringLiteral("sub"));
    touch(d.filePath(QStringLiteral("sub/deep.gguf")), 10);      // folders are not searched

    const auto models = pk::list_models(tmp.path(), crispasr);
    CHECK(models.size() == 3);
    CHECK(models.size() == 3 && models[0].path.endsWith(QStringLiteral("asr.gguf")) && models[0].bytes == 3000 &&
          models[0].ours);
    CHECK(models.size() == 3 && models[1].path.endsWith(QStringLiteral("llm.GGUF")) && !models[1].ours);
    CHECK(models.size() == 3 && models[2].path.endsWith(QStringLiteral("other.gguf")) && !models[2].ours);
    bool anyOurs = false;
    for (const auto& m : pk::list_models(tmp.path())) anyOurs = anyOurs || m.ours;
    CHECK(!anyOurs);  // without an owner nothing is ours
    const auto left = pk::list_leftovers(tmp.path(), crispasr);
    CHECK(left.size() == 2);  // asr.gguf.part.999999999.0, gone.gguf.src (not the live one, not big.gguf.part)
    CHECK(left.size() == 2 && left[0].path.endsWith(QStringLiteral(".part.999999999.0")) &&
          left[1].path.endsWith(QStringLiteral("gone.gguf.src")));
    CHECK(pk::list_models(d.filePath(QStringLiteral("missing"))).isEmpty());

    // One model goes with its sidecars only, but not with a download still in progress.
    QString error;
    CHECK(pk::delete_model(d.filePath(QStringLiteral("asr.gguf")), &error));
    CHECK(!exists(d, "asr.gguf") && !exists(d, "asr.gguf.src") && !exists(d, "asr.gguf.part.999999999.0"));
    CHECK(QFile::exists(d.filePath(livePart)));
    QFile::remove(d.filePath(livePart));
    CHECK(exists(d, "llm.GGUF") && exists(d, "llm.GGUF.lock") && exists(d, "notes.txt"));
    CHECK(!pk::delete_model(d.filePath(QStringLiteral("notes.txt")), &error));  // never anything but a model
    CHECK(exists(d, "notes.txt"));

    // Translate's folder: its own model names are ours. A held download lock and
    // its .part are a download in progress.
    QTemporaryDir tmp2;
    QDir d2(tmp2.path());
    const pk::ModelOwner translate{false, {QStringLiteral("mt.gguf"), QStringLiteral("big.gguf"),
                                           QStringLiteral("busy.gguf")}};
    touch(d2.filePath(QStringLiteral("MT.gguf")), 1500);         // names compare case-insensitively
    touch(d2.filePath(QStringLiteral("foreign.gguf")), 1200);    // another program's
    touch(d2.filePath(QStringLiteral("big.gguf.part")), 700);    // ours, unfinished
    touch(d2.filePath(QStringLiteral("busy.gguf.part")), 70);    // ours, being downloaded right now
    const QString lockPath = d2.filePath(QStringLiteral("busy.gguf.lock"));
#ifdef Q_OS_WIN
    HANDLE lock = CreateFileW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(lockPath).utf16()),
                              GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(lock != INVALID_HANDLE_VALUE);
#else
    const int lock = ::open(QFile::encodeName(lockPath).constData(), O_RDWR | O_CREAT, 0644);
    CHECK(lock >= 0 && ::flock(lock, LOCK_EX | LOCK_NB) == 0);
#endif
    const auto left2 = pk::list_leftovers(tmp2.path(), translate);
    CHECK(left2.size() == 1 && left2[0].path.endsWith(QStringLiteral("big.gguf.part")));

    // The window: one Delete button per model, ours or not; other programs' models
    // are marked; Clean All names what it deletes and deletes only that.
    pk::ModelsDialog dlg({{QStringLiteral("Speech"), pk::crispasr_models_dir(tmp.path()), crispasr},
                          {QStringLiteral("Translation"), pk::translate_models_dir(tmp2.path()), translate}});
    dlg.setConfirm(false);
    int deletes = 0;
    for (QPushButton* b : dlg.findChildren<QPushButton*>())
        if (b->text() == QLatin1String("Delete")) ++deletes;
    CHECK(deletes == 4);  // llm.GGUF, other.gguf, MT.gguf, foreign.gguf
    int foreign = 0;
    for (QLabel* l : dlg.findChildren<QLabel*>())
        if (l->objectName().startsWith(QLatin1String("foreign:"))) ++foreign;
    CHECK(foreign == 3);  // llm.GGUF, other.gguf, foreign.gguf
    CHECK(dlg.cleanAllButton()->isEnabled());
    dlg.cleanAll();
    const QString q = dlg.lastQuestion();
    CHECK(q.contains(QLatin1String("MT.gguf")) && q.contains(QLatin1String("big.gguf.part")) &&
          q.contains(QLatin1String("gone.gguf.src")));
    CHECK(!q.contains(QLatin1String("foreign.gguf")) && !q.contains(QLatin1String("other.gguf")) &&
          !q.contains(QLatin1String("llm.GGUF")) && !q.contains(QLatin1String("busy.gguf")));
    CHECK(!exists(d2, "MT.gguf") && !exists(d2, "big.gguf.part") && !exists(d, "gone.gguf.src"));
    CHECK(exists(d2, "foreign.gguf") && exists(d, "other.gguf") && exists(d, "llm.GGUF") &&
          exists(d, "llm.GGUF.lock") && exists(d, "big.gguf.part"));
    CHECK(exists(d2, "busy.gguf.part") && exists(d2, "busy.gguf.lock"));  // still downloading
    CHECK(exists(d, "notes.txt") && exists(d, "readme.src") && exists(d, "sub/deep.gguf"));
    QApplication::processEvents();
    CHECK(!dlg.cleanAllButton()->isEnabled());  // nothing of ours left
#ifdef Q_OS_WIN
    if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
#else
    if (lock >= 0) ::close(lock);
#endif
    // Released: now a leftover of ours.
    CHECK(pk::list_leftovers(tmp2.path(), translate).size() == 2);  // busy.gguf.part, busy.gguf.lock

    CHECK(pk::human_size(512) == QLatin1String("512 bytes"));
    CHECK(pk::human_size(2500000000LL) == QLatin1String("2.5 GB"));

    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    else std::printf("model files: all checks passed\n");
    return failures ? 1 : 0;
}
