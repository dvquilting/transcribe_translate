// tt_engine - the engine of Transcribe & Translate: everything the window
// (transcribe_translate) runs, one job per run, as a child process.
//
//   tt_engine transcribe INPUT [options]           speech to text (CrispASR)
//   tt_engine translate INPUT -t LANG [options]    text, subtitle and Word translation (llama.cpp)
//   tt_engine speakers MEDIA --srt FILE -o FILE    who speaks when
//   tt_engine dub MEDIA --srt FILE -o OUTPUT       speak translated subtitles
//
// Each command prints "progress: <phase> <done>/<total> <unit>" lines for the
// window, and "wrote <path>" for each file it saves.
//
// Speech recognition and the voices use CrispASR, linked into this program.
// Translation uses a newer llama.cpp, with its own copy of ggml, in the library
// tt_translate (.dll / libtt_translate.so) next to this program; it is loaded
// only for "translate", and its symbols are kept apart from CrispASR's.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "dub.h"
#include "pathutil.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#else
#include <dlfcn.h>
#endif

int transcribe_main(const std::vector<std::string>& av);  // cli_main.cpp

namespace {

constexpr const char* kVersion = "1.0";

std::filesystem::path exe_dir() {
#ifdef _WIN32
    std::wstring buf(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
    buf.resize(n);
    return std::filesystem::path(buf).parent_path();
#else
    std::error_code ec;
    const auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::current_path() : p.parent_path();
#endif
}

std::vector<std::string> utf8_argv(int argc, char** argv) {
    std::vector<std::string> out;
#ifdef _WIN32
    (void)argc;
    (void)argv;
    int n = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 0; i < n; ++i) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(len > 0 ? len - 1 : 0, '\0');
        if (len > 1) WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, s.data(), len, nullptr, nullptr);
        out.push_back(std::move(s));
    }
    LocalFree(wargv);
#else
    for (int i = 0; i < argc; ++i) out.emplace_back(argv[i]);
#endif
    return out;
}

void usage() {
    std::printf(
        "tt_engine %s - the engine of Transcribe & Translate\n"
        "\n"
        "usage: tt_engine COMMAND [options]   (tt_engine COMMAND --help for its options)\n"
        "\n"
        "commands:\n"
        "  transcribe INPUT          speech to text: .txt, .srt, .docx (Parakeet, ARK, Qwen3-ASR,\n"
        "                            Granite, Cohere)\n"
        "  translate INPUT -t LANG   translate .txt, .md, .srt or .docx (TranslateGemma, Hy-MT2, Qwen 3.5)\n"
        "  speakers MEDIA --srt FILE -o SPEAKERS.txt\n"
        "                            who speaks when\n"
        "  dub MEDIA --srt FILE -o OUTPUT\n"
        "                            speak translated subtitles in the speaker's voice\n",
        kVersion);
}

// The translation library: loaded when needed, from this program's folder.
int run_translate(const std::vector<std::string>& av) {
    using Entry = int (*)(int, const char* const*);
#ifdef _WIN32
    const std::filesystem::path lib = exe_dir() / "tt_translate.dll";
    HMODULE h = LoadLibraryExW(lib.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) {
        std::fprintf(stderr, "Error: could not load %s (error %lu). It must be next to tt_engine.exe.\n",
                     tmx::path_to_utf8(lib).c_str(), (unsigned long)GetLastError());
        return 1;
    }
    auto entry = reinterpret_cast<Entry>(GetProcAddress(h, "tt_translate_main"));
#else
    const std::filesystem::path lib = exe_dir() / "libtt_translate.so";
    // RTLD_LOCAL: the library's ggml stays its own (CrispASR's is in this program).
    void* h = dlopen(lib.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        std::fprintf(stderr, "Error: could not load %s (%s). It must be next to tt_engine.\n", lib.c_str(), dlerror());
        return 1;
    }
    auto entry = reinterpret_cast<Entry>(dlsym(h, "tt_translate_main"));
#endif
    if (!entry) {
        std::fprintf(stderr, "Error: the translation library is not the right version (no tt_translate_main)\n");
        return 1;
    }
    std::vector<const char*> argv;
    for (const auto& s : av) argv.push_back(s.c_str());
    std::fflush(stdout);
    return entry((int)argv.size(), argv.data());  // the library stays loaded until the process ends
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    const std::vector<std::string> av = utf8_argv(argc, argv);
    if (av.size() < 2 || av[1] == "-h" || av[1] == "--help") {
        usage();
        return av.size() < 2 ? 2 : 0;
    }
    if (av[1] == "--version") {
        std::printf("tt_engine %s\n", kVersion);
        return 0;
    }
    const std::string& cmd = av[1];
    const std::vector<std::string> sub(av.begin() + 1, av.end());  // sub[0] = the command
    if (cmd == "transcribe") return transcribe_main(sub);
    if (cmd == "translate") return run_translate(sub);
    if (cmd == "speakers") return tmx::speakers_main(sub);
    if (cmd == "dub") return tmx::dub_main(sub);
    std::fprintf(stderr, "Error: unknown command '%s' (transcribe, translate, speakers, dub)\n", cmd.c_str());
    return 2;
}
