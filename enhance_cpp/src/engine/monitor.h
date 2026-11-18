// monitor.h - the run log ("crash recorder"): once a second the GPU's state
// (temperature, power, clocks, memory, throttling; NVIDIA cards through NVML,
// others only memory) and what the program is doing, written straight to disk so
// that the last seconds before a system freeze are kept. GPU runs that take long
// are logged as they end; a run that never ends shows in the per-second lines.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace enh {

class Recorder {
public:
    // Opens `path` (UTF-8) for writing; on failure the recorder does nothing.
    explicit Recorder(const std::string& path);
    ~Recorder();  // writes "end" (a clean finish) and stops
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    bool ok() const { return file_ != nullptr; }
    const std::string& path() const { return path_; }

    // The GPU to watch: its name as DXGI reports it (matched against NVML's),
    // and a function giving this process's GPU memory use and budget.
    void watch_gpu(const std::string& name, std::function<bool(double&, double&)> memory);

    void stage(const std::string& what);  // e.g. "file 1/1: solve chunk 3/20"
    void note(const std::string& line);   // an event line, written at once
    void run_begin(const char* net, int frames);
    void run_end();

    // The recorder of this process (nullptr if none).
    static Recorder* current();

    // The default folder of the logs: %LOCALAPPDATA%\EnhanceAudio\logs.
    static std::string default_dir();
    // Older logs beyond the newest `keep` are deleted.
    static void prune(const std::string& dir, int keep);
    // If the newest log in `dir` has no clean end, a description of its last
    // lines (the previous run stopped in the middle: a crash or a kill).
    static std::string unfinished_previous(const std::string& dir);

private:
    void loop();
    void write(const std::string& line);  // with a time stamp, flushed to disk
    std::string gpu_state();

    std::string path_;
    FILE* file_ = nullptr;
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::thread thread_;
    std::string stage_;
    const char* net_ = nullptr;  // the GPU run in progress
    int frames_ = 0;
    std::chrono::steady_clock::time_point run_start_;
    std::function<bool(double&, double&)> memory_;
    struct Nvml;
    Nvml* nvml_ = nullptr;
};

}  // namespace enh
