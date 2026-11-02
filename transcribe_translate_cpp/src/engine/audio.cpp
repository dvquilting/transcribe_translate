#include "audio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <list>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace tmx {

namespace {

#ifdef _WIN32
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
#endif

// Windows command-line quoting (CRT argv rules), without cmd.exe expansion.
std::string quote_arg(const std::string& s) {
#ifdef _WIN32
    std::string out = "\"";
    size_t slashes = 0;
    for (char c : s) {
        if (c == '\\') { ++slashes; continue; }
        out.append(slashes * (c == '"' ? 2 : 1), '\\');
        slashes = 0;
        if (c == '"') out += '\\';
        out += c;
    }
    out.append(slashes * 2, '\\');
    return out + "\"";
#else
    std::string out = "'";
    for (char c : s) out += c == '\'' ? "'\\''" : std::string(1, c);
    return out + "'";
#endif
}

class ProcessPipe {
public:
    FILE* stream = nullptr;
    ProcessPipe(const std::vector<std::string>& args, bool capture_errors) {
        std::string cmd;
        for (const auto& arg : args) cmd += (cmd.empty() ? "" : " ") + quote_arg(arg);
#ifdef _WIN32
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE read = nullptr, write = nullptr;
        if (!CreatePipe(&read, &write, &sa, 0)) return;
        SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
        HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &sa, OPEN_EXISTING, 0, nullptr);
        HANDLE error = nullptr;
        if (!capture_errors && !DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_ERROR_HANDLE),
                                                GetCurrentProcess(), &error, 0, TRUE, DUPLICATE_SAME_ACCESS))
            error = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                &sa, OPEN_EXISTING, 0, nullptr);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = input;
        si.hStdOutput = write;
        si.hStdError = capture_errors ? write : error;
        PROCESS_INFORMATION pi{};
        auto command = widen(cmd);
        const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        CloseHandle(write);
        if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
        if (error && error != INVALID_HANDLE_VALUE) CloseHandle(error);
        if (!started) { CloseHandle(read); return; }
        CloseHandle(pi.hThread);
        process_ = pi.hProcess;
        const int fd = _open_osfhandle((intptr_t)read, _O_RDONLY | _O_BINARY);
        if (fd < 0) CloseHandle(read);
        else {
            stream = _fdopen(fd, "rb");
            if (!stream) _close(fd);
        }
#else
        if (capture_errors) cmd += " 2>&1";
        stream = popen(cmd.c_str(), "r");
#endif
    }
    ~ProcessPipe() { close(); }
    ProcessPipe(const ProcessPipe&) = delete;
    ProcessPipe& operator=(const ProcessPipe&) = delete;
    int close() {
#ifdef _WIN32
        if (stream) { fclose(stream); stream = nullptr; }
        if (!process_) return -1;
        WaitForSingleObject(process_, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(process_, &code);
        CloseHandle(process_); process_ = nullptr;
        return (int)code;
#else
        if (!stream) return -1;
        FILE* f = stream; stream = nullptr;
        return pclose(f);
#endif
    }
private:
#ifdef _WIN32
    HANDLE process_ = nullptr;
#endif
};

// "ffmpeg", or the path of an ffmpeg bundled next to this program
// (the Windows installer can install one there).
const std::string& ffmpeg_command() {
    static const std::string cmd = [] {
        std::string bundled;
#ifdef _WIN32
        std::wstring buf(32768, L'\0');
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        buf.resize(n);
        const size_t slash = buf.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            const std::wstring candidate = buf.substr(0, slash + 1) + L"ffmpeg.exe";
            if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
                const int len = WideCharToMultiByte(CP_UTF8, 0, candidate.c_str(), -1, nullptr, 0, nullptr, nullptr);
                bundled.assign(len > 0 ? len : 0, '\0');
                WideCharToMultiByte(CP_UTF8, 0, candidate.c_str(), -1, bundled.data(), len, nullptr, nullptr);
                if (!bundled.empty()) bundled.pop_back();
            }
        }
#else
        char buf[4096];
        const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) {
            std::string exe(buf, (size_t)n);
            const std::string candidate = exe.substr(0, exe.find_last_of('/') + 1) + "ffmpeg";
            if (access(candidate.c_str(), X_OK) == 0) bundled = candidate;
        }
#endif
        return bundled.empty() ? std::string("ffmpeg") : bundled;
    }();
    return cmd;
}

}  // namespace

bool ffmpeg_available() {
    ProcessPipe pipe({ffmpeg_command(), "-version"}, true);
    if (!pipe.stream) return false;
    char buf[4096];
    while (fread(buf, 1, sizeof(buf), pipe.stream) > 0) {}
    return pipe.close() == 0;
}

std::vector<float> load_audio(const std::string& path) { return load_audio_as(path, kSampleRate, 1); }

int run_ffmpeg(const std::vector<std::string>& args, std::string* messages) {
    std::vector<std::string> command = {ffmpeg_command(), "-nostdin", "-hide_banner", "-loglevel", "error"};
    command.insert(command.end(), args.begin(), args.end());
    ProcessPipe pipe(command, true);
    FILE* f = pipe.stream;
    if (!f) return -1;
    std::string out;
    char buf[4096];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, got);
        if (out.size() > 16384) out.erase(0, out.size() - 8192);  // keep the end
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
    if (messages) *messages = out;
    return pipe.close();
}

std::vector<float> load_audio_as(const std::string& path, int rate, int channels, size_t spare) {
    ProcessPipe process({ffmpeg_command(), "-nostdin", "-v", "error", "-i", path, "-vn", "-ac",
                         std::to_string(channels), "-ar", std::to_string(rate), "-f", "f32le", "-"}, false);
    FILE* pipe = process.stream;
    if (!pipe) throw std::runtime_error("could not start ffmpeg (is it on PATH?)");

    // Read into fixed-size blocks and join them once at the end: growing one
    // vector by doubling would hold up to 3x the audio while it reallocates (and
    // keep up to 2x as spare capacity), which matters for an hour of 44.1 kHz stereo.
    const size_t block = (size_t)1 << 20;  // floats (4 MB)
    std::list<std::vector<float>> blocks;
    size_t total = 0;
    for (;;) {
        std::vector<float> b(block);
        size_t filled = 0, got;
        while (filled < block && (got = fread(b.data() + filled, sizeof(float), block - filled, pipe)) > 0)
            filled += got;
        if (filled == 0) break;
        b.resize(filled);
        total += filled;
        blocks.push_back(std::move(b));
        if (filled < block) break;  // end of stream
    }
    std::vector<float> pcm;
    if (blocks.size() == 1 && spare == 0) {
        pcm = std::move(blocks.front());
    } else {
        pcm.reserve(total + spare);
        while (!blocks.empty()) {  // free each block as soon as it is copied
            pcm.insert(pcm.end(), blocks.front().begin(), blocks.front().end());
            blocks.pop_front();
        }
    }

    const int rc = process.close();
    if (rc != 0) throw std::runtime_error("ffmpeg failed to decode '" + path + "' (see message above)");
    if (pcm.empty()) throw std::runtime_error("no audio decoded from '" + path + "' (no audio stream?)");
    return pcm;
}

std::vector<std::pair<size_t, size_t>> split_on_silence(const float* wav, size_t n, int sr, double max_seconds,
                                                        double search_seconds, double window_ms) {
    std::vector<std::pair<size_t, size_t>> ranges;
    const size_t max_len = (size_t)(max_seconds * sr);
    const size_t search = std::min((size_t)(search_seconds * sr), max_len / 2);
    const size_t win = std::max<size_t>(1, (size_t)(window_ms / 1000.0 * sr));

    size_t start = 0;
    std::vector<double> csum;
    while (n - start > max_len) {
        const size_t hi = start + max_len;
        const size_t lo = hi - search;
        size_t cut = hi;
        const size_t len = hi - lo;
        if (len > win) {
            csum.assign(len + 1, 0.0);
            for (size_t i = 0; i < len; ++i) {
                const double v = wav[lo + i];
                csum[i + 1] = csum[i] + v * v;
            }
            double best = std::numeric_limits<double>::max();
            size_t best_i = 0;
            for (size_t i = 0; i + win <= len; ++i) {
                const double e = csum[i + win] - csum[i];
                if (e < best) {
                    best = e;
                    best_i = i;
                }
            }
            cut = lo + best_i + win / 2;
        }
        cut = std::min(std::max(cut, start + 1), hi);
        ranges.emplace_back(start, cut);
        start = cut;
    }
    if (start < n) ranges.emplace_back(start, n);
    return ranges;
}

double rms_dbfs(const float* x, size_t n) {
    if (n == 0) return -std::numeric_limits<double>::infinity();
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) acc += (double)x[i] * x[i];
    const double rms = std::sqrt(acc / (double)n);
    return rms > 0 ? 20.0 * std::log10(rms) : -std::numeric_limits<double>::infinity();
}

std::vector<Chunk> make_chunks(const std::vector<float>& wav, int sr, double max_seconds, int* skipped) {
    std::vector<Chunk> chunks;
    int n_skipped = 0;
    const size_t min_len = (size_t)(kMinChunkSeconds * sr);
    for (const auto& [a, b] : split_on_silence(wav.data(), wav.size(), sr, max_seconds)) {
        if (rms_dbfs(wav.data() + a, b - a) < kSilenceDbfs) {
            ++n_skipped;
            continue;
        }
        Chunk c;
        c.audio.assign(wav.begin() + (std::ptrdiff_t)a, wav.begin() + (std::ptrdiff_t)b);
        c.offset = (double)a / sr;
        c.duration = (double)(b - a) / sr;
        if (c.audio.size() < min_len) c.audio.resize(min_len, 0.0f);
        chunks.push_back(std::move(c));
    }
    if (skipped) *skipped = n_skipped;
    return chunks;
}

}  // namespace tmx
