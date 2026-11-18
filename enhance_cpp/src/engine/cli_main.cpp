// cli_main.cpp - enhance_audio.exe: Resemble Enhance (speech denoising and enhancement)
// on audio and video files, in C++ with ONNX Runtime. The GUI runs this program and
// reads its output lines ("Engine:", "Progress:", "Wrote:", "Warning:", "Error:").
#include <algorithm>
#include <chrono>
#include <ctime>
#include <memory>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <timeapi.h>
#endif

#include "audio.h"  // ../transcribe_cpp/src: ffmpeg decoding
#include "enhancer.h"
#include "fileio.h"  // ../common/src
#include "monitor.h"
#include "utf8_path.h"

namespace fs = std::filesystem;

namespace {

constexpr const char* kVersion = "1.0.0";

void say(const std::string& s) {
    std::printf("%s\n", s.c_str());
    std::fflush(stdout);
}

[[noreturn]] void die(const std::string& msg) {
    std::fprintf(stdout, "Error: %s\n", msg.c_str());
    std::fflush(stdout);
    std::exit(1);
}

void usage() {
    std::printf(
        "enhance_audio %s - speech denoising and enhancement (Resemble Enhance), on this computer\n"
        "\n"
        "usage: enhance_audio [options] INPUT [INPUT ...]\n"
        "\n"
        "  INPUT                     audio or video files (anything ffmpeg reads)\n"
        "  -o, --output PATH         output file (one input only). The extension chooses the\n"
        "                            format: .wav (32-bit float), .flac, .mp3, .m4a, .ogg, .opus,\n"
        "                            or .mp4 / .mkv / .mov: the input's video with the new sound\n"
        "      --out-dir DIR         write the outputs into DIR (default: next to each input)\n"
        "  -f, --format EXT          output format when no -o is given (default wav;\n"
        "                            \"video\" = the input's own video container)\n"
        "      --denoise-only        only remove noise (fast); default: denoise and enhance\n"
        "      --lambd X             denoise strength before enhancing, 0..1 (default 0.9)\n"
        "      --tau X               prior temperature, 0..1 (default 0.5): higher = more\n"
        "                            freely regenerated, lower = closer to the original\n"
        "      --nfe N               solver evaluations, 1..128 (default 64): fewer = faster\n"
        "      --solver NAME         midpoint (default), rk4 or euler\n"
        "      --seed N              random seed (default 0)\n"
        "      --chunk-seconds S     audio is processed in pieces of S seconds (default 30;\n"
        "                            smaller uses less memory)\n"
        "      --device auto|gpu|cpu DirectML GPU if available (default auto), or force one\n"
        "      --gpu N               which GPU (DirectX adapter number, default 0)\n"
        "      --threads N           CPU threads (default: all)\n"
        "      --models-dir DIR      the model files (default: models\\ next to this program)\n"
        "  -y, --overwrite           replace existing outputs (default: refuse)\n"
        "      --gentle              go easy on the graphics card: rest after each GPU run as long\n"
        "                            as it took (about half the speed, less heat and power)\n"
        "      --log FILE            the run log (default: a new file in\n"
        "                            %%LOCALAPPDATA%%\\EnhanceAudio\\logs; the last 20 are kept);\n"
        "                            once a second the GPU's temperature, power, clocks and memory\n"
        "                            and what is running, written straight to disk\n"
        "      --no-log              no run log\n"
        "      --benchmark           time the enhancement network on this computer (no INPUT)\n"
        "  -h, --help                show this help\n",
        kVersion);
}

std::vector<std::string> utf8_argv(int argc, char** argv) {
    std::vector<std::string> out;
#ifdef _WIN32
    (void)argc;
    (void)argv;
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 0; i < n; ++i) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(len > 0 ? len - 1 : 0, '\0');
        if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, s.data(), len, nullptr, nullptr);
        out.push_back(s);
    }
    LocalFree(w);
#else
    for (int i = 0; i < argc; ++i) out.emplace_back(argv[i]);
#endif
    return out;
}

std::string program_dir() {
#ifdef _WIN32
    std::wstring buf(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
    buf.resize(n);
    return pk::path_to_utf8(fs::path(buf).parent_path());
#else
    return pk::path_to_utf8(fs::canonical("/proc/self/exe").parent_path());
#endif
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool is_video_ext(const std::string& ext) {
    static const char* v[] = {".mp4", ".mkv", ".mov", ".m4v", ".webm", ".avi"};
    for (const char* e : v)
        if (ext == e) return true;
    return false;
}

bool is_audio_ext(const std::string& ext) {
    static const char* a[] = {".wav", ".flac", ".mp3", ".m4a", ".ogg", ".opus"};
    for (const char* e : a)
        if (ext == e) return true;
    return false;
}

// The input's first audio stream (sample rate, channels) and whether it has video,
// from ffmpeg's stream listing.
struct Probe {
    int rate = 0;
    int channels = 0;  // 0: unknown layout (ffmpeg then downmixes)
    bool video = false;
};

Probe probe(const std::string& path) {
    std::string text;
    tmx::run_ffmpeg({"-loglevel", "info", "-i", path}, &text);  // fails (no output named) after listing the streams
    Probe p;
    static const std::regex audio_re(R"(Stream #\d+:\d+.*?: Audio: [^\n]*?, (\d+) Hz, ([^,\n]+))");
    static const std::regex video_re(R"(Stream #\d+:\d+.*?: Video: )");
    std::smatch m;
    if (std::regex_search(text, m, audio_re)) {
        p.rate = std::atoi(m[1].str().c_str());
        std::string layout = m[2].str();
        const size_t paren = layout.find('(');
        const std::string base = paren == std::string::npos ? layout : layout.substr(0, paren);
        static const std::pair<const char*, int> layouts[] = {
            {"mono", 1}, {"stereo", 2}, {"2.1", 3}, {"3.0", 3}, {"4.0", 4}, {"quad", 4}, {"4.1", 5},
            {"5.0", 5}, {"5.1", 6}, {"6.0", 6}, {"6.1", 7}, {"7.0", 7}, {"7.1", 8}, {"downmix", 2}};
        for (const auto& [name, n] : layouts)
            if (base == name) p.channels = n;
        std::smatch c;
        static const std::regex count_re(R"(^(\d+) channels)");
        if (std::regex_search(layout, c, count_re)) p.channels = std::atoi(c[1].str().c_str());
    }
    if (std::regex_search(text, m, video_re)) {
        // Cover art in audio files is listed as a video stream too.
        p.video = text.find("(attached pic)") == std::string::npos;
    }
    if (p.rate <= 0) {
        // Report ffmpeg's own error (no such file, no audio stream, ...).
        std::string msg;
        for (size_t at = 0; at < text.size();) {
            size_t nl = text.find('\n', at);
            if (nl == std::string::npos) nl = text.size();
            std::string line = text.substr(at, nl - at);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.find("No such file") != std::string::npos || line.find("Invalid data") != std::string::npos ||
                line.find("does not contain any stream") != std::string::npos)
                msg = line;
            at = nl + 1;
        }
        throw std::runtime_error(msg.empty() ? "no audio stream found in " + path : msg);
    }
    return p;
}

// torchaudio.load + mean over channels: decode at the file's own rate and channels.
std::vector<float> load_mono(const std::string& path, const Probe& p) {
    if (p.channels <= 1) return tmx::load_audio_as(path, p.rate, 1);
    std::vector<float> inter = tmx::load_audio_as(path, p.rate, p.channels);
    const size_t frames = inter.size() / (size_t)p.channels;
    std::vector<float> mono(frames);
    for (size_t i = 0; i < frames; ++i) {
        float s = 0.0f;
        for (int c = 0; c < p.channels; ++c) s += inter[i * p.channels + c];
        mono[i] = s / (float)p.channels;
    }
    return mono;
}

// 32-bit float WAV (as torchaudio.save writes a float tensor), mono.
void write_wav(const fs::path& path, const std::vector<float>& x, int rate) {
    std::string data;
    const uint32_t bytes = (uint32_t)(x.size() * sizeof(float));
    auto u32 = [&](uint32_t v) { data.append(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { data.append(reinterpret_cast<const char*>(&v), 2); };
    data += "RIFF";
    u32(4 + 26 + 12 + 8 + bytes);
    data += "WAVE";
    data += "fmt ";
    u32(18);
    u16(3);  // IEEE float
    u16(1);
    u32((uint32_t)rate);
    u32((uint32_t)rate * 4);
    u16(4);
    u16(32);
    u16(0);
    data += "fact";
    u32(4);
    u32((uint32_t)x.size());
    data += "data";
    u32(bytes);
    data.append(reinterpret_cast<const char*>(x.data()), bytes);
    pk::write_file_atomic(path, data);
}

std::string format_seconds(double s) {
    char buf[64];
    if (s < 60) std::snprintf(buf, sizeof(buf), "%.1f s", s);
    else std::snprintf(buf, sizeof(buf), "%d:%04.1f", (int)(s / 60), std::fmod(s, 60.0));
    return buf;
}

struct Settings {
    std::vector<std::string> inputs;
    std::string output, out_dir, format = "wav", models_dir;
    enh::Options opt;
    enh::Device device = enh::Device::Auto;
    int gpu = 0, threads = 0;
    bool overwrite = false;
    bool benchmark = false;
    bool gentle = false;
    bool no_log = false;
    std::string log;
};

fs::path output_for(const Settings& s, const std::string& input, const Probe& p) {
    if (!s.output.empty()) return pk::path_from_utf8(s.output);
    const fs::path in = pk::path_from_utf8(input);
    std::string ext = "." + lower(s.format);
    if (ext == ".video") ext = p.video ? lower(pk::path_to_utf8(in.extension())) : ".wav";
    const std::string tag = s.opt.denoise_only ? ".denoised" : ".enhanced";
    const fs::path dir = s.out_dir.empty() ? in.parent_path() : pk::path_from_utf8(s.out_dir);
    return dir / pk::path_from_utf8(pk::path_to_utf8(in.stem()) + tag + ext);
}

// The enhanced audio into `out`: a WAV directly, other formats through ffmpeg
// (a video: the input's video stream copied, with the new sound).
void save(const fs::path& out, const std::string& input, const std::vector<float>& wav, const Probe& p) {
    const std::string ext = lower(pk::path_to_utf8(out.extension()));
    if (!out.parent_path().empty()) fs::create_directories(out.parent_path());
    if (ext == ".wav") {
        write_wav(out, wav, enh::kRate);
        return;
    }
    fs::path tmp = out;
    tmp += ".tmp.wav";
    write_wav(tmp, wav, enh::kRate);
    fs::path part = out.parent_path() / pk::path_from_utf8(pk::path_to_utf8(out.stem()) + ".part" + ext);
    std::vector<std::string> base = {"-y"};
    const bool video = is_video_ext(ext);
    if (video) {
        if (!p.video) {
            fs::remove(tmp);
            throw std::runtime_error("the input has no video for " + pk::path_to_utf8(out.filename()));
        }
        base.insert(base.end(), {"-i", input, "-i", pk::path_to_utf8(tmp), "-map", "0:v", "-map", "1:a", "-map",
                                 "0:s?", "-c:v", "copy", "-c:s", "copy", "-shortest"});
    } else {
        base.insert(base.end(), {"-i", pk::path_to_utf8(tmp)});
    }
    // The audio encoder, best first: ffmpeg builds differ in which external encoders
    // they include (libmp3lame, libvorbis, libopus); the built-in ones are the fallback.
    using Args = std::vector<std::string>;
    std::vector<Args> encoders;
    if (ext == ".mp3") encoders = {{"-c:a", "libmp3lame", "-q:a", "0"}, {"-c:a", "libshine", "-b:a", "256k"}};
    else if (ext == ".ogg") encoders = {{"-c:a", "libvorbis", "-q:a", "8"}, {"-c:a", "vorbis", "-strict", "-2", "-ac", "2", "-b:a", "256k"}};
    else if (ext == ".opus" || ext == ".webm")
        encoders = {{"-c:a", "libopus", "-b:a", "160k"}, {"-c:a", "opus", "-strict", "-2", "-ar", "48000", "-b:a", "160k"}};
    else if (ext == ".flac") encoders = {{"-c:a", "flac", "-sample_fmt", "s32"}};
    else encoders = {{"-c:a", "aac", "-b:a", "256k"}};  // .m4a, .mp4, .mkv, .mov, ...
    std::string messages;
    int rc = -1;
    for (const Args& enc : encoders) {
        Args args = base;
        args.insert(args.end(), enc.begin(), enc.end());
        args.push_back(pk::path_to_utf8(part));
        rc = tmx::run_ffmpeg(args, &messages);
        if (rc == 0 || (messages.find("Unknown encoder") == std::string::npos &&
                        messages.find("Encoder not found") == std::string::npos))
            break;
    }
    fs::remove(tmp);
    if (rc != 0) {
        std::error_code ec;
        fs::remove(part, ec);
        if (messages.find("Unknown encoder") != std::string::npos || messages.find("Encoder not found") != std::string::npos)
            throw std::runtime_error("this ffmpeg cannot write " + ext.substr(1) +
                                     " files (its build has no such encoder); choose WAV, FLAC or M4A, or use an "
                                     "ffmpeg build with it");
        throw std::runtime_error("ffmpeg could not write " + pk::path_to_utf8(out.filename()) +
                                 (messages.empty() ? std::string() : ": " + messages));
    }
    pk::replace_file(part, out);
}

double number(const std::vector<std::string>& a, size_t& i, const char* name) {
    if (i + 1 >= a.size()) die(std::string(name) + " needs a value");
    char* end = nullptr;
    const double v = std::strtod(a[++i].c_str(), &end);
    if (!end || *end) die(std::string(name) + ": not a number: " + a[i]);
    return v;
}

Settings parse(const std::vector<std::string>& a) {
    Settings s;
    for (size_t i = 1; i < a.size(); ++i) {
        const std::string& x = a[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= a.size()) die(x + " needs a value");
            return a[++i];
        };
        if (x == "-h" || x == "--help") {
            usage();
            std::exit(0);
        } else if (x == "--version") {
            std::printf("enhance_audio %s\n", kVersion);
            std::exit(0);
        } else if (x == "-o" || x == "--output") s.output = value();
        else if (x == "--out-dir") s.out_dir = value();
        else if (x == "-f" || x == "--format") s.format = lower(value());
        else if (x == "--denoise-only") s.opt.denoise_only = true;
        else if (x == "--lambd") s.opt.lambd = (float)number(a, i, "--lambd");
        else if (x == "--tau") s.opt.tau = (float)number(a, i, "--tau");
        else if (x == "--nfe") s.opt.nfe = (int)number(a, i, "--nfe");
        else if (x == "--solver") {
            const std::string v = lower(value());
            if (!enh::parse_solver(v, s.opt.solver)) die("--solver: midpoint, rk4 or euler, not " + v);
        } else if (x == "--seed") s.opt.seed = (uint64_t)number(a, i, "--seed");
        else if (x == "--chunk-seconds") s.opt.chunk_seconds = number(a, i, "--chunk-seconds");
        else if (x == "--device") {
            const std::string v = lower(value());
            if (v == "auto") s.device = enh::Device::Auto;
            else if (v == "gpu" || v == "dml" || v == "directml") s.device = enh::Device::Gpu;
            else if (v == "cpu") s.device = enh::Device::Cpu;
            else die("--device: auto, gpu or cpu, not " + v);
        } else if (x == "--gpu") s.gpu = (int)number(a, i, "--gpu");
        else if (x == "--threads") s.threads = (int)number(a, i, "--threads");
        else if (x == "--models-dir") s.models_dir = value();
        else if (x == "-y" || x == "--overwrite") s.overwrite = true;
        else if (x == "--benchmark") s.benchmark = true;
        else if (x == "--gentle") s.gentle = true;
        else if (x == "--log") s.log = value();
        else if (x == "--no-log") s.no_log = true;
        else if (x.size() > 1 && x[0] == '-') die("unknown option " + x + " (see --help)");
        else s.inputs.push_back(x);
    }
    if (s.inputs.empty() && !s.benchmark) {
        usage();
        std::exit(2);
    }
    if (!s.output.empty() && s.inputs.size() > 1) die("-o names one output; use --out-dir for several inputs");
    if (s.opt.nfe < 1 || s.opt.nfe > 128) die("--nfe must be 1..128");
    if (s.opt.lambd < 0 || s.opt.lambd > 1) die("--lambd must be 0..1");
    if (s.opt.tau < 0 || s.opt.tau > 1) die("--tau must be 0..1");
    if (s.opt.chunk_seconds < 2 || s.opt.chunk_seconds > 600) die("--chunk-seconds must be 2..600");
    const std::string f = "." + s.format;
    if (s.format != "video" && !is_audio_ext(f) && !is_video_ext(f)) die("unknown --format " + s.format);
    if (!s.output.empty()) {
        const std::string ext = lower(pk::path_to_utf8(pk::path_from_utf8(s.output).extension()));
        if (!is_audio_ext(ext) && !is_video_ext(ext))
            die("unknown output type " + ext + " (.wav .flac .mp3 .m4a .ogg .opus .mp4 .mkv .mov .webm)");
    }
    if (s.models_dir.empty()) s.models_dir = pk::path_to_utf8(pk::path_from_utf8(program_dir()) / "models");
    return s;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // A 1 ms timer: waiting for the GPU otherwise wakes up in 15.6 ms steps, which
    // adds up over the hundreds of short GPU runs of the CFM solver.
    timeBeginPeriod(1);
#endif
    const Settings s = parse(utf8_argv(argc, argv));
    if (!tmx::ffmpeg_available()) die("ffmpeg was not found (put ffmpeg.exe next to this program or on PATH)");

    // The run log ("crash recorder"). A previous run whose log has no end stopped in
    // the middle: a crash, a kill, or the computer stopping.
    std::unique_ptr<enh::Recorder> recorder;
    if (!s.no_log) {
        std::string path = s.log;
        if (path.empty()) {
            const std::string dir = enh::Recorder::default_dir();
            const std::string unfinished = enh::Recorder::unfinished_previous(dir);
            if (!unfinished.empty()) say("Note: the previous run did not finish. Its log ends with: " + unfinished);
            enh::Recorder::prune(dir, 19);
            const std::time_t t = std::time(nullptr);
            std::tm tm{};
            localtime_s(&tm, &t);
            char name[64];
            std::strftime(name, sizeof(name), "enhance-%Y%m%d-%H%M%S.log", &tm);
            path = pk::path_to_utf8(pk::path_from_utf8(dir) / name);
        }
        recorder = std::make_unique<enh::Recorder>(path);
        if (recorder->ok()) {
            say("Log: " + path);
            std::string cmd = "command:";
            for (const auto& a : utf8_argv(argc, argv)) cmd += " " + a;
            recorder->note(cmd);
        } else {
            recorder.reset();
        }
    }

    // Check the outputs before the long part.
    for (const auto& in : s.inputs)
        if (!fs::exists(pk::path_from_utf8(in))) die("no such file: " + in);

    say("Loading models from " + s.models_dir + " ...");
    const auto t_load = std::chrono::steady_clock::now();
    std::unique_ptr<enh::Models> models;
    try {
        models = std::make_unique<enh::Models>(s.models_dir, s.device, s.opt.denoise_only, s.threads, s.gpu);
    } catch (const std::exception& e) {
        die(std::string("could not load the models: ") + e.what());
    }
    const auto& eng = models->engine();
    say("Engine: " + eng.backend + (eng.detail.empty() ? "" : " (" + eng.detail + ")"));
    if (recorder) {
        recorder->note("engine " + eng.backend + " " + eng.detail);
        if (models->on_gpu())
            recorder->watch_gpu(eng.detail, [&m = *models](double& u, double& b) { return m.gpu_memory(u, b); });
    }
    if (s.gentle) {
        models->set_pace(1.0);
        say("Gentle mode: the GPU rests after each run as long as the run took.");
    }
    say("Models loaded in " +
        format_seconds(std::chrono::duration<double>(std::chrono::steady_clock::now() - t_load).count()));
    if (s.opt.denoise_only) say("Mode: denoise only");
    else {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "Mode: denoise %.2f + enhance (%s, %d evaluations, temperature %.2f)",
                      s.opt.lambd, enh::solver_name(s.opt.solver), s.opt.nfe, s.opt.tau);
        say(buf);
    }

    if (s.benchmark) {
        // --benchmark: the time of one CFM evaluation and one vocoder window (diagnostics)
        enh::NormalGen g(1);
        for (int T : {210, 1000, 3150}) {
            enh::Mat psi(enh::kLatent, T), cond(enh::kMels, T);
            g.fill(psi.v.data(), psi.v.size());
            g.fill(cond.v.data(), cond.v.size());
            models->velocity(psi, cond, 0.3f);  // the first run prepares the network
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < 10; ++i) models->velocity(psi, cond, 0.1f * i);
            const double ms = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() * 100;
            char buf[128];
            std::snprintf(buf, sizeof(buf), "Benchmark: one CFM evaluation, %d frames (%.0f s of audio): %.1f ms", T,
                          T * (double)enh::kHop / enh::kRate, ms);
            say(buf);
        }
        return 0;
    }

    int failed = 0;
    for (size_t k = 0; k < s.inputs.size(); ++k) {
        const std::string& input = s.inputs[k];
        try {
            if (s.inputs.size() > 1) say("File " + std::to_string(k + 1) + " of " + std::to_string(s.inputs.size()) + ": " + input);
            const Probe p = probe(input);
            const fs::path out = output_for(s, input, p);
            if (fs::exists(out) && !s.overwrite)
                throw std::runtime_error(pk::path_to_utf8(out) + " exists (use -y to replace it)");
            if (fs::exists(out) && fs::equivalent(out, pk::path_from_utf8(input)))
                throw std::runtime_error("the output would replace the input");

            say("Decoding " + input);
            std::vector<float> wav = load_mono(input, p);
            const double seconds = (double)wav.size() / p.rate;
            if (recorder) recorder->stage("file " + std::to_string(k + 1) + "/" + std::to_string(s.inputs.size()) +
                                          ": " + input);
            say("Audio : " + format_seconds(seconds) + ", " + std::to_string(p.rate) + " Hz, " +
                (p.channels == 1 ? std::string("mono") : std::to_string(std::max(p.channels, 1)) + " channels"));
            wav = enh::resample(wav, p.rate, enh::kRate);

            const auto t0 = std::chrono::steady_clock::now();
            int last = -1;
            std::string last_stage;
            auto progress = [&](double frac, const std::string& stage) {
                const int pct = (int)std::floor(frac * 1000.0);  // 0.1 % steps
                if (pct == last && stage == last_stage) return;
                last = pct;
                last_stage = stage;
                char buf[96];
                std::snprintf(buf, sizeof(buf), "Progress: %.1f%% %s", pct / 10.0, stage.c_str());
                say(buf);
            };
            enh::Options opt = s.opt;
            if (models->on_gpu()) {
                opt.chunk_seconds = enh::fit_chunk_seconds(*models, s.opt.chunk_seconds);
                if (opt.chunk_seconds < s.opt.chunk_seconds) {
                    char buf[200];
                    std::snprintf(buf, sizeof(buf), "Note: little GPU memory is free; using %.0f s pieces instead of "
                                  "%.0f s.", opt.chunk_seconds, s.opt.chunk_seconds);
                    say(buf);
                }
            }
            std::vector<float> result = enh::process(*models, wav, opt, progress);
            wav.clear();
            wav.shrink_to_fit();
            const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

            float peak = 0.0f;
            for (float v : result) peak = std::max(peak, std::fabs(v));
            const std::string ext = lower(pk::path_to_utf8(out.extension()));
            if (peak > 1.0f && ext != ".wav") {
                // Lossy and integer formats would clip; scale down to just below full scale.
                const float g = 0.999f / peak;
                for (float& v : result) v *= g;
                char buf[128];
                std::snprintf(buf, sizeof(buf), "Note: the result peaked at %.2f dB over full scale; lowered by %.2f dB.",
                              20 * std::log10(peak), -20 * std::log10(g));
                say(buf);
            }
            save(out, input, result, p);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "Processed in %s (%.2fx real time)", format_seconds(took).c_str(),
                          took > 0 ? seconds / took : 0.0);
            say(buf);
            say("Wrote: " + pk::path_to_utf8(out));
        } catch (const std::exception& e) {
            ++failed;
            say("Error: " + input + ": " + e.what());
            if (recorder) recorder->note(std::string("error: ") + e.what());
        }
    }
    if (failed) return s.inputs.size() == 1 ? 1 : 4;
    return 0;
}
