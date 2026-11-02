#include "download.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <vector>

#include "pathutil.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#endif

#include <chrono>
#include <thread>

namespace fs = std::filesystem;

namespace tl {

std::string default_models_dir() {
#ifdef _WIN32
    if (const wchar_t* la = _wgetenv(L"LOCALAPPDATA")) return path_to_utf8(fs::path(la) / "Translate" / "models");
    if (const wchar_t* up = _wgetenv(L"USERPROFILE"))
        return path_to_utf8(fs::path(up) / "AppData" / "Local" / "Translate" / "models");
    return "models";
#else
    if (const char* x = std::getenv("XDG_CACHE_HOME"); x && *x) return path_to_utf8(fs::path(x) / "translate");
    if (const char* h = std::getenv("HOME")) return path_to_utf8(fs::path(h) / ".cache" / "translate");
    return "models";
#endif
}

namespace {

uint64_t file_size_or_zero(const fs::path& p) {
    std::error_code ec;
    const auto n = fs::file_size(p, ec);
    return ec ? 0 : (uint64_t)n;
}

void discard(const fs::path& part) {
    std::error_code ec;
    fs::remove(part, ec);
}

// Move the downloaded .part file into place. `complete`: the server sent all
// of its file, so a size other than the expected one is not a broken
// connection (resuming would only ask for bytes that don't exist) but a
// different file: it is removed, and the next run starts afresh.
void finish(const fs::path& part, const fs::path& dest, uint64_t expected, bool complete) {
    const uint64_t got = file_size_or_zero(part);
    if (expected && got != expected) {
        if (complete) {
            discard(part);
            throw std::runtime_error("the downloaded file has " + std::to_string(got) + " bytes, but " +
                                     std::to_string(expected) +
                                     " were expected (the model may have been updated on the server); the "
                                     "download was removed. Download the file yourself and use --model-file, "
                                     "or update this program");
        }
        throw std::runtime_error("download incomplete (" + std::to_string(got) + " of " + std::to_string(expected) +
                                 " bytes); run again to resume");
    }
    std::error_code ec;
    fs::rename(part, dest, ec);
    if (ec) throw std::runtime_error("could not rename " + path_to_utf8(part) + ": " + ec.message());
}

// An exclusive lock on <dest>.lock for the whole download, so two copies of the
// program (a second run while the first is still going, or the GUI restarting
// after Cancel) never append to the same .part file at once.
class DownloadLock {
public:
    explicit DownloadLock(const fs::path& lock_path) : path_(lock_path) {
        // Wait a little: after a Cancel the old process may take a moment to go.
        const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        for (;;) {
            if (try_lock()) return;
            if (std::chrono::steady_clock::now() > give_up)
                throw std::runtime_error("another Translate program is already downloading " +
                                         path_to_utf8(lock_path.stem()) +
                                         "; wait for it to finish (or close it) and try again");
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
    ~DownloadLock() { release(false); }
    // Unlock; `done` also removes the lock file (only once the model is in place,
    // so a waiting process then finds the finished file).
    void release(bool done) {
#ifdef _WIN32
        // FILE_FLAG_DELETE_ON_CLOSE removes the file; `done` changes nothing here.
        (void)done;
        if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_), h_ = INVALID_HANDLE_VALUE;
#else
        if (fd_ < 0) return;
        if (done) unlink(path_.c_str());
        close(fd_);  // releases the flock (and the kernel does it if the process is killed)
        fd_ = -1;
#endif
    }
    DownloadLock(const DownloadLock&) = delete;
    DownloadLock& operator=(const DownloadLock&) = delete;

private:
    bool try_lock() {
#ifdef _WIN32
        // No sharing: a second open fails until this handle is closed, which
        // Windows also does when the process ends.
        h_ = CreateFileW(path_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (h_ != INVALID_HANDLE_VALUE) return true;
        const DWORD e = GetLastError();
        if (e == ERROR_SHARING_VIOLATION || e == ERROR_ACCESS_DENIED) return false;
        throw std::runtime_error("cannot create " + path_to_utf8(path_) + " (Windows error " + std::to_string(e) + ")");
#else
        // Not O_CLOEXEC on purpose: curl inherits the descriptor, so the lock is
        // held for as long as anything may still write the .part file.
        fd_ = open(path_.c_str(), O_RDWR | O_CREAT, 0644);
        if (fd_ < 0) throw std::runtime_error("cannot create " + path_to_utf8(path_));
        if (flock(fd_, LOCK_EX | LOCK_NB) == 0) return true;
        close(fd_);
        fd_ = -1;
        return false;
#endif
    }

    fs::path path_;
#ifdef _WIN32
    HANDLE h_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
};

#ifdef _WIN32

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string win_error(const std::string& what) {
    const DWORD e = GetLastError();
    return what + " failed (Windows error " + std::to_string(e) + ")";
}

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};

#endif

}  // namespace

void download_file(const std::string& url, const std::string& dest_utf8, uint64_t expected_bytes,
                   const DownloadProgress& progress) {
    const fs::path dest = path_from_utf8(dest_utf8);
    if (dest.has_parent_path()) fs::create_directories(dest.parent_path());
    fs::path lock_path = dest;
    lock_path += ".lock";
    DownloadLock lock(lock_path);
    if (fs::exists(dest)) {  // finished by another process while we waited for the lock
        lock.release(true);
        return;
    }
    fs::path part = dest;
    part += ".part";
    uint64_t have = file_size_or_zero(part);
    if (expected_bytes && have > expected_bytes) {  // stale partial file
        fs::remove(part);
        have = 0;
    }
    if (expected_bytes && have == expected_bytes) {
        finish(part, dest, expected_bytes, true);
        lock.release(true);
        return;
    }

#ifdef _WIN32
    const std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256], path[4096];
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 4096;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) throw std::runtime_error(win_error("WinHttpCrackUrl"));

    Handle session, conn;
    session.h = WinHttpOpen(L"Translate/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h)  // WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY needs Windows 8.1+
        session.h = WinHttpOpen(L"Translate/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) throw std::runtime_error(win_error("WinHttpOpen"));
    conn.h = WinHttpConnect(session.h, host, uc.nPort, 0);
    if (!conn.h) throw std::runtime_error(win_error("WinHttpConnect"));
    Handle req;
    DWORD status = 0;
    for (bool restarted = false;; restarted = true) {
        if (req.h) WinHttpCloseHandle(req.h), req.h = nullptr;
        req.h = WinHttpOpenRequest(conn.h, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
        if (!req.h) throw std::runtime_error(win_error("WinHttpOpenRequest"));
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        WinHttpSetOption(req.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));

        std::wstring headers;
        if (have > 0) headers = L"Range: bytes=" + std::to_wstring(have) + L"-\r\n";
        if (!WinHttpSendRequest(req.h, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                headers.empty() ? 0 : (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(req.h, nullptr))
            throw std::runtime_error(win_error("downloading " + url + ": request"));

        DWORD len = sizeof(status);
        status = 0;
        WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &len, WINHTTP_NO_HEADER_INDEX);
        if (status == 416 && expected_bytes && have == expected_bytes) {
            finish(part, dest, expected_bytes, true);
            lock.release(true);
            return;
        }
        if (status == 416 && have > 0 && !restarted) {
            // "Range not satisfiable": the file on the server is no longer than
            // the partial one, so it isn't the file that was being downloaded.
            // The partial file is useless: start over from the beginning, once.
            discard(part);
            have = 0;
            continue;
        }
        break;
    }
    if (status != 200 && status != 206)
        throw std::runtime_error("download of " + url + " failed (HTTP " + std::to_string(status) + ")");
    if (status == 200) have = 0;  // server ignored the range: start over

    wchar_t clen[32] = {};
    DWORD clen_size = sizeof(clen);
    uint64_t total = expected_bytes;
    const bool clen_known = WinHttpQueryHeaders(req.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                                                clen, &clen_size, WINHTTP_NO_HEADER_INDEX) != FALSE;
    if (clen_known) total = have + _wcstoui64(clen, nullptr, 10);

    FILE* f = _wfopen(part.c_str(), have > 0 ? L"ab" : L"wb");
    if (!f) throw std::runtime_error("cannot write " + path_to_utf8(part));
    std::vector<char> buf(1 << 20);
    uint64_t done = have;
    for (;;) {
        DWORD got = 0;
        if (!WinHttpReadData(req.h, buf.data(), (DWORD)buf.size(), &got)) {
            fclose(f);
            throw std::runtime_error(win_error("reading the download") + "; run again to resume");
        }
        if (got == 0) break;
        if (fwrite(buf.data(), 1, got, f) != got) {
            fclose(f);
            throw std::runtime_error("cannot write " + path_to_utf8(part) + " (disk full?)");
        }
        done += got;
        if (progress) progress(done, total);
    }
    fclose(f);
    // Once all the server announced has arrived, a wrong size is a different file.
    finish(part, dest, expected_bytes ? expected_bytes : total, !clen_known || done == total);
    lock.release(true);
#else
    // Linux / macOS: let curl do the HTTPS work, and watch the file grow.
    const std::string part_s = path_to_utf8(part);
    std::vector<std::string> args = {"curl", "-L", "--fail", "-sS", "-C", "-", "-o", part_s, url};
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    // fork + exec rather than posix_spawn, to set PR_SET_PDEATHSIG in the child:
    // when this process dies (the GUI's Cancel kills it), the kernel kills curl
    // too instead of leaving it downloading in the background. (The signal is
    // tied to the thread that forked, which waits for curl below.)
    const pid_t parent = getpid();
    for (bool restarted = false;; restarted = true) {
        const bool resumed = file_size_or_zero(part) > 0;
        const pid_t pid = fork();
        if (pid < 0) throw std::runtime_error("cannot start curl to download the model");
        if (pid == 0) {
#ifdef __linux__
            prctl(PR_SET_PDEATHSIG, SIGKILL);
            if (getppid() != parent) _exit(126);  // the parent died before prctl took effect
#else
            (void)parent;
#endif
            execvp("curl", argv.data());
            _exit(127);
        }
        int status = 0;
        for (;;) {
            const pid_t r = waitpid(pid, &status, WNOHANG);
            if (progress) progress(file_size_or_zero(part), expected_bytes);
            if (r == pid) break;
            if (r < 0) throw std::runtime_error("lost track of the curl download process");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        if (code == 127) throw std::runtime_error("curl is needed to download models (sudo apt install curl)");
        // The partial file can't be resumed when the file on the server changed:
        // curl then fails with a range error (33), or takes the server's "range
        // not satisfiable" for an already complete file and leaves the partial
        // file as it was. Start over from the beginning, once (as on Windows).
        const bool wrong_size = code == 0 && expected_bytes && file_size_or_zero(part) != expected_bytes;
        if (resumed && !restarted && (code == 33 || wrong_size)) {
            discard(part);
            continue;
        }
        if (code != 0)
            throw std::runtime_error("download of " + url + " failed (curl exit code " + std::to_string(code) +
                                     "); run again to resume");
        break;
    }
    // curl ends with 0 only once it has received the whole file (--fail; a
    // connection cut short is exit code 18): a wrong size is a different file.
    finish(part, dest, expected_bytes, true);
    lock.release(true);
#endif
}

}  // namespace tl
