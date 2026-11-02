#include "model_files.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace pk {

namespace {

QString env(const char* name) { return qEnvironmentVariable(name).trimmed(); }

QString native_abs(const QString& p) { return QDir::toNativeSeparators(QDir::cleanPath(QDir(p).absolutePath())); }

bool is_model(const QString& name) { return name.endsWith(QLatin1String(".gguf"), Qt::CaseInsensitive); }

// The pid in CrispASR's unfinished download "<model>.part.<pid>.<n>", or 0 if
// `name` is not one.
qint64 crispasr_part_pid(const QString& name) {
    const int part = name.lastIndexOf(QLatin1String(".part."), -1, Qt::CaseInsensitive);
    if (part <= 0) return 0;
    const QStringList nums = name.mid(part + 6).split(QLatin1Char('.'));
    bool pid_ok = false, n_ok = false;
    if (nums.size() != 2) return 0;
    const qint64 pid = nums[0].toLongLong(&pid_ok);
    nums[1].toLongLong(&n_ok);
    return pid_ok && n_ok && pid > 0 ? pid : 0;
}

// The model a download leftover belongs to ("x.gguf.part.123.4" -> "x.gguf"), or
// "" if `name` is not a leftover.
QString leftover_of(const QString& name) {
    for (const QLatin1String suffix : {QLatin1String(".src"), QLatin1String(".lock"), QLatin1String(".part")})
        if (name.endsWith(suffix, Qt::CaseInsensitive)) return name.left(name.size() - suffix.size());
    const int part = name.lastIndexOf(QLatin1String(".part."), -1, Qt::CaseInsensitive);
    if (part > 0) return name.left(part);
    return QString();
}

QFileInfoList files_in(const QString& dir) {
    if (dir.isEmpty()) return {};
    return QDir(dir).entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
}

bool process_alive(qint64 pid) {
#ifdef Q_OS_WIN
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return GetLastError() == ERROR_ACCESS_DENIED;  // exists, but not ours to query
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
#else
    return ::kill((pid_t)pid, 0) == 0 || errno == EPERM;
#endif
}

// Translate's download lock is held: by an exclusive, unshared handle on
// Windows, by flock() elsewhere (see translate_cpp/src/download.cpp).
bool lock_held(const QString& path) {
    if (!QFileInfo::exists(path)) return false;
#ifdef Q_OS_WIN
    HANDLE h = CreateFileW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(path).utf16()), GENERIC_READ, 0,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_SHARING_VIOLATION;
    CloseHandle(h);
    return false;
#else
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    const bool held = ::flock(fd, LOCK_EX | LOCK_NB) != 0 && errno == EWOULDBLOCK;
    ::close(fd);  // also releases our own lock if we got it
    return held;
#endif
}

// A download that is still going: deleting its files would break it.
bool in_use(const QFileInfo& f) {
    const QString name = f.fileName();
    if (const qint64 pid = crispasr_part_pid(name)) return process_alive(pid);
    if (name.endsWith(QLatin1String(".lock"), Qt::CaseInsensitive)) return lock_held(f.absoluteFilePath());
    if (name.endsWith(QLatin1String(".part"), Qt::CaseInsensitive))  // Translate's, guarded by its .lock
        return lock_held(f.dir().filePath(name.left(name.size() - 5) + QStringLiteral(".lock")));
    return false;
}

bool known_name(const ModelOwner& owner, const QString& name) {
    return owner.names.contains(name, Qt::CaseInsensitive);
}

}  // namespace

QString crispasr_models_dir(const QString& override_dir) {
    if (!override_dir.trimmed().isEmpty()) return native_abs(override_dir.trimmed());
    for (const char* v : {"CRISPASR_CACHE_DIR", "CRISPASR_MODELS_DIR"})
        if (!env(v).isEmpty()) return native_abs(env(v));
    // CrispASR's platform_default_dir(): HOME first (on Windows too), then on
    // Windows USERPROFILE and LOCALAPPDATA, else its working directory (the
    // GUIs start the tools in the program's folder).
    QString home = env("HOME");
#ifdef Q_OS_WIN
    if (home.isEmpty()) home = env("USERPROFILE");
    if (home.isEmpty()) home = env("LOCALAPPDATA");
#endif
    if (home.isEmpty()) home = QCoreApplication::applicationDirPath();
    return native_abs(home + QStringLiteral("/.cache/crispasr"));
}

QString translate_models_dir(const QString& override_dir) {
    if (!override_dir.trimmed().isEmpty()) return native_abs(override_dir.trimmed());
#ifdef Q_OS_WIN
    if (!env("LOCALAPPDATA").isEmpty()) return native_abs(env("LOCALAPPDATA") + QStringLiteral("/Translate/models"));
    if (!env("USERPROFILE").isEmpty())
        return native_abs(env("USERPROFILE") + QStringLiteral("/AppData/Local/Translate/models"));
    return native_abs(QStringLiteral("models"));
#else
    if (!env("XDG_CACHE_HOME").isEmpty()) return native_abs(env("XDG_CACHE_HOME") + QStringLiteral("/translate"));
    if (!env("HOME").isEmpty()) return native_abs(env("HOME") + QStringLiteral("/.cache/translate"));
    return native_abs(QStringLiteral("models"));
#endif
}

QList<ModelFile> list_models(const QString& dir, const ModelOwner& owner) {
    QList<ModelFile> out;
    for (const QFileInfo& f : files_in(dir)) {
        if (!is_model(f.fileName())) continue;
        const bool ours = known_name(owner, f.fileName()) ||
                          (owner.src_sidecar && QFileInfo::exists(f.absoluteFilePath() + QStringLiteral(".src")));
        out.append({QDir::toNativeSeparators(f.absoluteFilePath()), f.size(), ours});
    }
    return out;
}

QList<ModelFile> list_leftovers(const QString& dir, const ModelOwner& owner) {
    QList<ModelFile> out;
    const QFileInfoList all = files_in(dir);
    for (const QFileInfo& f : all) {
        const QString name = f.fileName();
        const QString model = leftover_of(name);
        if (model.isEmpty() || !is_model(model)) continue;
        // Ours: a model of ours, or CrispASR's own sidecar / unfinished download
        // in CrispASR's cache (the .src is the mark of a CrispASR download).
        const bool crispasr_file = name.endsWith(QLatin1String(".src"), Qt::CaseInsensitive) || crispasr_part_pid(name);
        if (!known_name(owner, model) && !(owner.src_sidecar && crispasr_file)) continue;
        const bool unfinished = name.contains(QLatin1String(".part"), Qt::CaseInsensitive);
        // A .src / .lock next to its model belongs to it; on its own it is a leftover.
        if (!unfinished && QFileInfo::exists(f.dir().filePath(model))) continue;
        if (in_use(f)) continue;  // a download in progress
        out.append({QDir::toNativeSeparators(f.absoluteFilePath()), f.size(), true});
    }
    return out;
}

bool delete_model(const QString& model_path, QString* error) {
    const QFileInfo model(model_path);
    if (!is_model(model.fileName())) {
        if (error) *error = QStringLiteral("not a model file: ") + model_path;
        return false;
    }
    QFile f(model.absoluteFilePath());
    if (f.exists() && !f.remove()) {
        if (error) *error = f.errorString();
        return false;
    }
    // Its sidecars and unfinished downloads (best effort; not one still going).
    for (const QFileInfo& x : files_in(model.absolutePath()))
        if (leftover_of(x.fileName()).compare(model.fileName(), Qt::CaseInsensitive) == 0 && !in_use(x))
            QFile::remove(x.absoluteFilePath());
    return true;
}

int delete_all_models(const QString& dir, const ModelOwner& owner, QStringList* failed) {
    int deleted = 0;
    for (const ModelFile& m : list_models(dir, owner)) {
        if (!m.ours) continue;  // another program's
        if (delete_model(m.path)) ++deleted;
        else if (failed) failed->append(m.path);
    }
    for (const ModelFile& x : list_leftovers(dir, owner))
        if (!QFile::remove(x.path) && failed) failed->append(x.path);
    return deleted;
}

QString human_size(qint64 bytes) {
    const double b = (double)bytes;
    if (b >= 1e9) return QString::number(b / 1e9, 'f', 1) + QStringLiteral(" GB");
    if (b >= 1e6) return QString::number(b / 1e6, 'f', 0) + QStringLiteral(" MB");
    if (b >= 1e3) return QString::number(b / 1e3, 'f', 0) + QStringLiteral(" KB");
    return QString::number(bytes) + QStringLiteral(" bytes");
}

}  // namespace pk
