// monitor.cpp - see monitor.h.
#include "monitor.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#endif

#include "utf8_path.h"

namespace fs = std::filesystem;

namespace enh {

namespace {

std::atomic<Recorder*> g_current{nullptr};

std::string clock_text() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const int ms = (int)(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    return buf;
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

}  // namespace

// NVIDIA's management library (nvml.dll, installed with the driver), loaded if present.
struct Recorder::Nvml {
#ifdef _WIN32
    HMODULE dll = nullptr;
#endif
    void* device = nullptr;
    using Fn0 = int (*)();
    using GetCount = int (*)(unsigned*);
    using GetHandle = int (*)(unsigned, void**);
    using GetName = int (*)(void*, char*, unsigned);
    using GetUInt2 = int (*)(void*, int, unsigned*);
    using GetUInt = int (*)(void*, unsigned*);
    using GetULL = int (*)(void*, unsigned long long*);
    struct Util { unsigned gpu, memory; };
    using GetUtil = int (*)(void*, Util*);
    struct Memory { unsigned long long total, free, used; };
    using GetMem = int (*)(void*, Memory*);
    Fn0 shutdown = nullptr;
    GetUInt2 temperature = nullptr, clock = nullptr;
    GetUInt power = nullptr, limit = nullptr, fan = nullptr;
    GetULL reasons = nullptr;
    GetUtil util = nullptr;
    GetMem memory = nullptr;

    bool open(const std::string& want_name) {
#ifdef _WIN32
        dll = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!dll) return false;
        auto sym = [&](const char* n) { return reinterpret_cast<void*>(GetProcAddress(dll, n)); };
        auto init = reinterpret_cast<Fn0>(sym("nvmlInit_v2"));
        auto count = reinterpret_cast<GetCount>(sym("nvmlDeviceGetCount_v2"));
        auto handle = reinterpret_cast<GetHandle>(sym("nvmlDeviceGetHandleByIndex_v2"));
        auto name = reinterpret_cast<GetName>(sym("nvmlDeviceGetName"));
        shutdown = reinterpret_cast<Fn0>(sym("nvmlShutdown"));
        temperature = reinterpret_cast<GetUInt2>(sym("nvmlDeviceGetTemperature"));
        clock = reinterpret_cast<GetUInt2>(sym("nvmlDeviceGetClockInfo"));
        power = reinterpret_cast<GetUInt>(sym("nvmlDeviceGetPowerUsage"));
        limit = reinterpret_cast<GetUInt>(sym("nvmlDeviceGetEnforcedPowerLimit"));
        fan = reinterpret_cast<GetUInt>(sym("nvmlDeviceGetFanSpeed"));
        reasons = reinterpret_cast<GetULL>(sym("nvmlDeviceGetCurrentClocksEventReasons"));
        if (!reasons) reasons = reinterpret_cast<GetULL>(sym("nvmlDeviceGetCurrentClocksThrottleReasons"));
        util = reinterpret_cast<GetUtil>(sym("nvmlDeviceGetUtilizationRates"));
        memory = reinterpret_cast<GetMem>(sym("nvmlDeviceGetMemoryInfo"));
        if (!init || !count || !handle || !name || init() != 0) {
            FreeLibrary(dll);
            dll = nullptr;
            return false;
        }
        unsigned n = 0;
        count(&n);
        for (unsigned i = 0; i < n; ++i) {
            void* d = nullptr;
            char buf[128] = {};
            if (handle(i, &d) != 0) continue;
            name(d, buf, sizeof(buf));
            if (!device) device = d;  // the first NVIDIA GPU, unless one matches by name
            if (!want_name.empty() && lower(want_name).find(lower(buf)) != std::string::npos) {
                device = d;
                break;
            }
        }
        if (!device) {
            close();
            return false;
        }
        return true;
#else
        (void)want_name;
        return false;
#endif
    }

    void close() {
#ifdef _WIN32
        if (dll) {
            if (shutdown) shutdown();
            FreeLibrary(dll);
            dll = nullptr;
        }
#endif
        device = nullptr;
    }

    std::string state() {
        if (!device) return {};
        std::string s;
        char buf[96];
        unsigned v = 0;
        if (temperature && temperature(device, 0, &v) == 0) {
            std::snprintf(buf, sizeof(buf), " temp %uC", v);
            s += buf;
        }
        unsigned p = 0, l = 0;
        if (power && power(device, &p) == 0) {
            if (limit && limit(device, &l) == 0) std::snprintf(buf, sizeof(buf), " power %.0f/%.0fW", p / 1000.0, l / 1000.0);
            else std::snprintf(buf, sizeof(buf), " power %.0fW", p / 1000.0);
            s += buf;
        }
        if (clock && clock(device, 0, &v) == 0) {
            std::snprintf(buf, sizeof(buf), " clock %uMHz", v);
            s += buf;
        }
        Util u{};
        if (util && util(device, &u) == 0) {
            std::snprintf(buf, sizeof(buf), " load %u%%", u.gpu);
            s += buf;
        }
        Memory m{};
        if (memory && memory(device, &m) == 0) {
            std::snprintf(buf, sizeof(buf), " vram %.1f/%.1fGB", m.used / 1073741824.0, m.total / 1073741824.0);
            s += buf;
        }
        if (fan && fan(device, &v) == 0) {
            std::snprintf(buf, sizeof(buf), " fan %u%%", v);
            s += buf;
        }
        unsigned long long r = 0;
        if (reasons && reasons(device, &r) == 0 && r) {
            // nvml.h: clocks event reasons (idle and "applications clocks" left out)
            static const std::pair<unsigned long long, const char*> names[] = {
                {0x4ULL, "sw-power-cap"}, {0x8ULL, "hw-slowdown"}, {0x20ULL, "sw-thermal"},
                {0x40ULL, "hw-thermal"}, {0x80ULL, "hw-power-brake"}, {0x10ULL, "sync-boost"}};
            std::string list;
            for (const auto& [bit, n] : names)
                if (r & bit) list += (list.empty() ? "" : ",") + std::string(n);
            if (!list.empty()) s += " limited:" + list;
        }
        return s;
    }
};

Recorder::Recorder(const std::string& path) : path_(path) {
    std::error_code ec;
    const fs::path p = pk::path_from_utf8(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
#ifdef _WIN32
    file_ = _wfopen(p.wstring().c_str(), L"wb");
#else
    file_ = std::fopen(p.string().c_str(), "wb");
#endif
    if (!file_) return;
    g_current = this;
    write("start");
    thread_ = std::thread([this] { loop(); });
}

Recorder::~Recorder() {
    if (!file_) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    g_current = nullptr;
    write("end");
    std::fclose(file_);
    if (nvml_) {
        nvml_->close();
        delete nvml_;
    }
}

Recorder* Recorder::current() { return g_current.load(); }

void Recorder::write(const std::string& line) {
    if (!file_) return;
    std::lock_guard<std::mutex> lock(mu_);
    std::fprintf(file_, "%s %s\n", clock_text().c_str(), line.c_str());
    std::fflush(file_);
#ifdef _WIN32
    _commit(_fileno(file_));  // to the disk itself, so that a system freeze cannot lose it
#endif
}

void Recorder::watch_gpu(const std::string& name, std::function<bool(double&, double&)> memory) {
    memory_ = std::move(memory);
    auto* n = new Nvml;
    if (n->open(name)) {
        nvml_ = n;
    } else {
        delete n;
    }
    write("gpu " + name + (nvml_ ? " (NVML)" : " (no NVML: memory only)"));
}

void Recorder::stage(const std::string& what) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        stage_ = what;
    }
    write("stage " + what);
}

void Recorder::note(const std::string& line) { write(line); }

void Recorder::run_begin(const char* net, int frames) {
    std::lock_guard<std::mutex> lock(mu_);
    net_ = net;
    frames_ = frames;
    run_start_ = std::chrono::steady_clock::now();
}

void Recorder::run_end() {
    const char* net;
    int frames;
    double s;
    {
        std::lock_guard<std::mutex> lock(mu_);
        net = net_;
        frames = frames_;
        s = std::chrono::duration<double>(std::chrono::steady_clock::now() - run_start_).count();
        net_ = nullptr;
    }
    static const bool all = std::getenv("ENHANCE_LOG_RUNS") != nullptr;  // every run (development)
    if (net && (s >= 1.0 || all)) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "long GPU run: %s, %d frames, %.2f s", net, frames, s);
        write(buf);
    }
}

std::string Recorder::gpu_state() {
    std::string s = nvml_ ? nvml_->state() : std::string();
    double used = 0, budget = 0;
    if (memory_ && memory_(used, budget)) {
        char buf[80];
        std::snprintf(buf, sizeof(buf), " ours %.2f/%.2fGB", used, budget);
        s += buf;
    }
    return s;
}

void Recorder::loop() {
    for (;;) {
        std::string line;
        {
            std::unique_lock<std::mutex> lock(mu_);
            if (cv_.wait_for(lock, std::chrono::seconds(1), [this] { return stop_; })) return;
            line = "tick";
            if (!stage_.empty()) line += " [" + stage_ + "]";
            if (net_) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), " running %s %d frames for %.1f s", net_, frames_,
                              std::chrono::duration<double>(std::chrono::steady_clock::now() - run_start_).count());
                line += buf;
            }
        }
        write(line + gpu_state());
    }
}

std::string Recorder::default_dir() {
#ifdef _WIN32
    const wchar_t* base = _wgetenv(L"LOCALAPPDATA");
    if (base) return pk::path_to_utf8(fs::path(base) / L"EnhanceAudio" / L"logs");
#endif
    return pk::path_to_utf8(fs::temp_directory_path() / "EnhanceAudio" / "logs");
}

namespace {
std::vector<fs::path> logs_newest_first(const std::string& dir) {
    std::vector<fs::path> logs;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(pk::path_from_utf8(dir), ec))
        if (e.is_regular_file() && e.path().extension() == ".log") logs.push_back(e.path());
    std::sort(logs.begin(), logs.end(), [](const fs::path& a, const fs::path& b) { return a.filename() > b.filename(); });
    return logs;  // names hold the start time, so they sort by time
}
}  // namespace

void Recorder::prune(const std::string& dir, int keep) {
    auto logs = logs_newest_first(dir);
    std::error_code ec;
    for (size_t i = (size_t)keep; i < logs.size(); ++i) fs::remove(logs[i], ec);
}

std::string Recorder::unfinished_previous(const std::string& dir) {
    const auto logs = logs_newest_first(dir);
    if (logs.empty()) return {};
    std::ifstream in(logs.front(), std::ios::binary);
    std::vector<std::string> lines;
    for (std::string l; std::getline(in, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (!l.empty()) lines.push_back(l);
    }
    const std::string& last = lines.empty() ? std::string() : lines.back();
    if (lines.empty() || (last.size() >= 4 && last.compare(last.size() - 4, 4, " end") == 0)) return {};
    std::string out = pk::path_to_utf8(logs.front());
    const size_t from = lines.size() > 3 ? lines.size() - 3 : 0;
    for (size_t i = from; i < lines.size(); ++i) out += "\n    " + lines[i];
    return out;
}

}  // namespace enh
