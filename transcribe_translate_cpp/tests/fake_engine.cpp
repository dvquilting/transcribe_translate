// fake_engine.cpp - a stand-in for tt_engine in the window's tests: answers
// "transcribe" and "translate" the way the real engine does (the lines the window
// reads, the files it writes) without any model. Each run appends its command
// line to $TT_FAKE_LOG; $TT_FAKE_SLEEP_MS makes it take that long.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
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

int run(const std::vector<std::string>& a);

// "translate --serve": jobs from stdin, as the real engine (translate/cli_main.cpp).
int serve() {
    if (const char* log = std::getenv("TT_FAKE_LOG")) std::ofstream(std::string(log) + ".serve", std::ios::app) << "serve\n";
    bool loaded = false;
    for (std::string line; std::getline(std::cin, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "quit") break;
        if (line == "unload") {
            if (loaded) say("tt-model ");
            loaded = false;
            continue;
        }
        if (line.rfind("job\t", 0) != 0) continue;  // (cancel: the jobs here are instant)
        std::vector<std::string> args{"translate"};
        std::string cur;
        for (size_t i = 4; i < line.size(); ++i) {
            if (line[i] == '\t') args.push_back(cur), cur.clear();
            else if (line[i] == '\\' && i + 1 < line.size()) {
                const char n = line[++i];
                cur += n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n;
            } else cur += line[i];
        }
        args.push_back(cur);
        if (!loaded) say("tt-model Fake model");
        else say("Loading Fake model ... (kept loaded from the last translation)");
        loaded = true;
        const int code = run(args);
        std::fflush(stderr);
        say("tt-job-done " + std::to_string(code));
    }
    return 0;
}

int main(int argc, char** argv) {
    std::vector<std::string> a(argv + 1, argv + argc);
    if (a.size() == 2 && a[0] == "translate" && a[1] == "--serve") return serve();
    return run(a);
}

int run(const std::vector<std::string>& a) {
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
        const bool draft = std::find(a.begin(), a.end(), "--draft") != a.end();
        if (draft) say("Draft : translategemma-4b-it.Q4_K_M.gguf (speculative decoding: the same translation, sooner)");
        say("Translating ...");
        say("progress: translating 0/1000 permille");
        say("progress: translating 500/1000 permille");
        say("progress: translate 1/1 parts");
        say("progress: translating 1000/1000 permille");
        std::string res;
        std::istringstream lines(in);
        for (std::string line; std::getline(lines, line);) {
            const bool plain = !line.empty() && line.find("-->") == std::string::npos &&
                               line.find_first_not_of("0123456789\r") != std::string::npos;
            res += (plain ? "[" + to + "] " : std::string()) + line + "\n";
        }
        std::ofstream(out, std::ios::binary) << res;
        say("Translated in 0.1 s (9 words, 5400 words/min)");
        if (draft) say("Draft : drafted in 90% of the steps; writing took 1.0 s, about 1.5 s without the draft model (1.50x)");
        say("wrote " + out);
        return 0;
    }
    std::fprintf(stderr, "Error: fake engine: unknown command %s\n", a[0].c_str());
    return 2;
}
