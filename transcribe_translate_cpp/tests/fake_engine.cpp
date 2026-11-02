// fake_engine.cpp - a stand-in for tt_engine in the window's tests: answers
// "transcribe" and "translate" the way the real engine does (the lines the window
// reads, the files it writes) without any model. Each run appends its command
// line to $TT_FAKE_LOG; $TT_FAKE_SLEEP_MS makes it take that long.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string arg(const std::vector<std::string>& a, const std::string& name, const std::string& def = "") {
    for (size_t i = 0; i + 1 < a.size(); ++i)
        if (a[i] == name) return a[i + 1];
    return def;
}

std::string stem(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    return dot != std::string::npos && (slash == std::string::npos || dot > slash) ? path.substr(0, dot) : path;
}

std::string read(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void say(const std::string& s) {
    std::printf("%s\n", s.c_str());
    std::fflush(stdout);
}

const char* kSrt = "1\n00:00:01,000 --> 00:00:03,000\nGood morning, everyone.\n\n"
                   "2\n00:00:03,500 --> 00:00:06,000\nToday we talk about the budget.\n";

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> a(argv + 1, argv + argc);
    if (const char* log = std::getenv("TT_FAKE_LOG")) {
        std::ofstream f(log, std::ios::app);
        for (const auto& s : a) f << s << '\t';
        f << '\n';
    }
    if (a.empty()) return 2;
    if (const char* ms = std::getenv("TT_FAKE_SLEEP_MS"))
        std::this_thread::sleep_for(std::chrono::milliseconds(std::atoi(ms)));
    if (a[0] == "transcribe" && a.size() > 1) {
        say("Audio : input (6.0 s)");
        say("Engine: CPU (fake)");
        say("progress: transcribe 6/6 s");
        say("Transcribed in 0.1 s");
        const std::string base = stem(arg(a, "-o", a[1]));
        std::string formats = arg(a, "-f", "txt") + ",";
        for (size_t p; (p = formats.find(',')) != std::string::npos; formats.erase(0, p + 1)) {
            const std::string f = formats.substr(0, p);
            if (f != "txt" && f != "srt") continue;
            std::ofstream(base + "." + f, std::ios::binary)
                << (f == "srt" ? std::string(kSrt) : std::string("Good morning, everyone. Today we talk about the budget.\n"));
            say("  wrote " + base + "." + f);
        }
        return 0;
    }
    if (a[0] == "translate" && a.size() > 1) {
        const std::string out = arg(a, "-o");
        const std::string to = arg(a, "-t", "xx");
        if (out.empty()) return 2;
        const std::string in = read(a[1]);
        say("Words : 9");
        say("Engine: CPU (fake)");
        say("Translating ...");
        say("progress: translate 1/1 parts");
        std::string res;
        std::istringstream lines(in);
        for (std::string line; std::getline(lines, line);) {
            const bool plain = !line.empty() && line.find("-->") == std::string::npos &&
                               line.find_first_not_of("0123456789\r") != std::string::npos;
            res += (plain ? "[" + to + "] " : std::string()) + line + "\n";
        }
        std::ofstream(out, std::ios::binary) << res;
        say("Translated in 0.1 s (9 words, 5400 words/min)");
        say("wrote " + out);
        return 0;
    }
    std::fprintf(stderr, "Error: fake engine: unknown command %s\n", a[0].c_str());
    return 2;
}
