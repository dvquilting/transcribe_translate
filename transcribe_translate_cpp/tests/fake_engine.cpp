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
        if (std::find(args.begin(), args.end(), "--preload") != args.end()) {  // load only (not in the job log)
            if (const char* log = std::getenv("TT_FAKE_LOG")) {
                std::ofstream f(std::string(log) + ".preload", std::ios::app);
                for (size_t i = 0; i < args.size(); ++i) f << (i ? "\t" : "") << args[i];
                f << "\n";
            }
            if (!loaded) say("tt-model Fake model");
            loaded = true;
            if (const char* ms = std::getenv("TT_FAKE_PRELOAD_MS"))  // a slow load
                std::this_thread::sleep_for(std::chrono::milliseconds(std::atoi(ms)));
            say("tt-preloaded Fake model");
            say("tt-job-done 0");
            continue;
        }
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
    // Who speaks when: the first subtitle is Speaker 1's, the second Speaker 2's
    // (one speaker with --num-speakers 1). $TT_FAKE_SPEAKERS_FAIL: it fails.
    if (a[0] == "speakers" && a.size() > 1) {
        if (std::getenv("TT_FAKE_SPEAKERS_FAIL")) {
            std::fprintf(stderr, "Error: fake engine: speaker detection failed\n");
            return 1;
        }
        const bool one = arg(a, "--num-speakers") == "1";
        say("Finding who speaks when ...");
        say("progress: speakers 50/100 percent");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        say("progress: speakers 49/100 percent");  // (late, from another thread: ignored)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        say("progress: speakers 100/100 percent");
        std::ofstream(arg(a, "-o"), std::ios::binary)
            << "# speakers 1\nspeaker\t0\tSpeaker 1\t2.000\t1.000\t3.000\n"
            << (one ? "" : "speaker\t1\tSpeaker 2\t2.500\t3.500\t6.000\n")
            << "cue\t1.000\t3.000\t0\t0\ncue\t3.500\t6.000\t" << (one ? "0" : "1") << "\t1\n";
        if (!arg(a, "--split-srt").empty()) std::ofstream(arg(a, "--split-srt"), std::ios::binary) << read(arg(a, "--srt"));
        say(one ? "Speakers: 1 found" : "Speakers: 2 found");
        say("  wrote " + arg(a, "-o"));
        return 0;
    }
    if (a[0] == "transcribe" && a.size() > 1) {
        // --from-srt: the outputs from an earlier run's subtitles, each line with its
        // speaker's name from --speakers-file (the n-th speaker said the n-th line).
        const std::string from = arg(a, "--from-srt");
        std::vector<std::string> names;
        {
            std::istringstream in(read(arg(a, "--speakers-file")));
            for (std::string line; std::getline(in, line);)
                if (line.rfind("speaker\t", 0) == 0) {
                    const size_t b = line.find('\t', 8);
                    names.push_back(line.substr(b + 1, line.find('\t', b + 1) - b - 1));
                }
        }
        const auto named = [&](size_t k, const char* text) {
            return (names.size() >= 2 ? names[std::min(k, names.size() - 1)] + ": " : std::string()) + text;
        };
        if (from.empty()) {
            say("Audio : input (6.0 s)");
            say("Engine: CPU (fake)");
            say("progress: transcribe 6/6 s");
            say("Transcribed in 0.1 s");
        } else if (read(from).empty()) {
            std::fprintf(stderr, "Error: fake engine: no subtitles in %s\n", from.c_str());
            return 1;
        }
        if (!arg(a, "--words").empty()) std::ofstream(arg(a, "--words"), std::ios::binary) << "1.000\t1.400\tGood\n";
        const std::string base = stem(arg(a, "-o", a[1]));
        std::string formats = arg(a, "-f", "txt") + ",";
        for (size_t p; (p = formats.find(',')) != std::string::npos; formats.erase(0, p + 1)) {
            const std::string f = formats.substr(0, p);
            if (f != "txt" && f != "srt") continue;
            const std::string plain = names.size() >= 2 ? named(0, "Good morning, everyone.") + "\n\n" +
                                                              named(1, "Today we talk about the budget.") + "\n"
                                                        : std::string("Good morning, everyone. Today we talk about the budget.\n");
            std::ofstream(base + "." + f, std::ios::binary)
                << (f == "srt" ? "1\n00:00:01,000 --> 00:00:03,000\n" + named(0, "Good morning, everyone.") +
                                     "\n\n2\n00:00:03,500 --> 00:00:06,000\n" +
                                     named(1, "Today we talk about the budget.") + "\n"
                               : plain);
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
