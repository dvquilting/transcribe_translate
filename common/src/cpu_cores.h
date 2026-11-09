// cpu_cores.h - the fast cores of a phone's processor (big.LITTLE).
//
// A phone has a few fast cores and some slow ("little") ones; a Galaxy S23 has
// 1 + 2 + 2 fast cores and 3 little ones. ggml divides each piece of work evenly
// among its threads and waits for the slowest, so a thread that Android puts on
// a little core holds all the others up. The engine therefore runs as many
// threads as there are fast cores and keeps its threads on those cores
// (threads ggml starts inherit the affinity of the thread that starts them).
//
// The cores are told apart by their highest clock speed (cpufreq in sysfs).
#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(__linux__)
#include <sched.h>
#include <unistd.h>
#endif

namespace pk {

// The cores faster than the slowest kind, fastest first. Empty when all cores
// are alike or the speeds can't be read (then nothing is restricted).
inline std::vector<int> fast_cores(const std::string& sysfs = "/sys/devices/system/cpu") {
    std::vector<std::pair<long, int>> cores;  // (max kHz, cpu)
    std::error_code ec;
    for (int cpu = 0; cpu < 1024; ++cpu) {
        const std::string dir = sysfs + "/cpu" + std::to_string(cpu);
        if (!std::filesystem::exists(dir, ec)) break;
        std::ifstream f(dir + "/cpufreq/cpuinfo_max_freq");
        long khz = 0;
        if (f >> khz && khz > 0) cores.push_back({khz, cpu});  // (an offline core has no cpufreq)
    }
    if (cores.size() < 2) return {};
    long slowest = cores[0].first;
    for (const auto& c : cores) slowest = std::min(slowest, c.first);
    std::vector<std::pair<long, int>> fast;
    for (const auto& c : cores)
        if (c.first > slowest) fast.push_back(c);
    if (fast.empty()) return {};  // all alike
    std::stable_sort(fast.begin(), fast.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<int> out;
    for (const auto& c : fast) out.push_back(c.second);
    return out;
}

// For the time it exists, the calling thread (and every thread it starts) runs
// only on `cores`; the previous setting comes back afterwards. Does nothing
// when `cores` is empty or on systems without CPU affinity.
class CoreAffinity {
public:
    explicit CoreAffinity(const std::vector<int>& cores) {
#if defined(__linux__)
        if (cores.empty()) return;
        if (sched_getaffinity(0, sizeof(old_), &old_) != 0) return;
        cpu_set_t set;
        CPU_ZERO(&set);
        for (int c : cores)
            if (c >= 0 && c < CPU_SETSIZE) CPU_SET(c, &set);
        active_ = sched_setaffinity(0, sizeof(set), &set) == 0;
#else
        (void)cores;
#endif
    }
    ~CoreAffinity() {
#if defined(__linux__)
        if (active_) sched_setaffinity(0, sizeof(old_), &old_);
#endif
    }
    CoreAffinity(const CoreAffinity&) = delete;
    CoreAffinity& operator=(const CoreAffinity&) = delete;
    bool active() const { return active_; }

private:
    bool active_ = false;
#if defined(__linux__)
    cpu_set_t old_{};
#endif
};

}  // namespace pk
