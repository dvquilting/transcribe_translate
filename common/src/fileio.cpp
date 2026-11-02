#include "fileio.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include "utf8_path.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace pk {

std::string read_file(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path_to_utf8(path));
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void replace_file(const fs::path& from, const fs::path& to) {
#ifdef _WIN32
    // A GUI reloading the file or a virus scanner checking it may hold it open for
    // a moment: try again for about half a second before giving up.
    for (int attempt = 1;; ++attempt) {
        if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return;
        const DWORD e = GetLastError();
        const bool busy = e == ERROR_SHARING_VIOLATION || e == ERROR_ACCESS_DENIED || e == ERROR_LOCK_VIOLATION;
        if (busy && attempt < 10) {
            Sleep(50);
            continue;
        }
        throw std::runtime_error("cannot replace " + path_to_utf8(to) + " (Windows error " + std::to_string(e) +
                                 "; is it open in another program?)");
    }
#else
    std::error_code ec;
    fs::rename(from, to, ec);
    if (ec) throw std::runtime_error("cannot replace " + path_to_utf8(to) + ": " + ec.message());
#endif
}

void write_file_atomic(const fs::path& target, const std::string& bytes) {
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);  // reported by the open below
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("cannot write " + path_to_utf8(target));
        f.write(bytes.data(), (std::streamsize)bytes.size());
        f.close();  // flushes: a full disk shows up here
        if (!f) {
            fs::remove(tmp, ec);
            throw std::runtime_error("cannot write " + path_to_utf8(target) + " (disk full?)");
        }
    }
    try {
        replace_file(tmp, target);
    } catch (...) {
        fs::remove(tmp, ec);
        throw;
    }
}

}  // namespace pk
