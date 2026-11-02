// transcribe_multi - transcribe an audio or video file with a local ASR model
// (Parakeet Ultra, ARK-ASR-3B or Qwen3-ASR-1.7B via the CrispASR C++ runtime)
// and write a paragraph-formatted .txt transcript and/or .srt subtitles.
//
// Examples:
//   transcribe_multi talk.mp3                      parakeet-ultra -> talk.txt
//   transcribe_multi talk.mp3 -m ark -f both       talk.txt + talk.srt
//   transcribe_multi lecture.mp4 -m qwen -f srt    video works too (via ffmpeg)
//
// Output lines starting with "progress:" are machine-readable for the GUI:
//   progress: <phase> <done>/<total> <unit>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "audio.h"
#include "docx_writer.h"
#include "dub.h"
#include "crispasr_api.h"
#include "fileio.h"
#include "pathutil.h"
#include "engine.h"
#include "llm.h"
#include "pipeline.h"
#include "transcript.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;
using namespace tmx;

namespace {

struct Args {
    std::string input;
    std::string model = "parakeet-ultra";
    std::string format = "txt";  // comma list of txt, srt, docx (or both = txt,srt; none)
    bool want_txt = false, want_srt = false, want_docx = false;  // from `format`
    std::string text_dir;        // also write the text outputs here (for the GUI's PDFs)
    std::string output;
    std::string model_file;      // local GGUF override
    std::string models_dir;      // cache override
    std::string device = "auto";
    std::string language = "en";
    int threads = 0;
    double chunk_seconds = 30.0;
    bool align = true;
    int sentences_per_paragraph = 5;
    bool polish = false;         // LLM spelling/grammar cleanup -> .clean.txt
    bool summary = false;        // LLM summary -> .summary.txt
    bool smart_paragraphs = false;  // LLM chooses paragraph breaks
    std::string punctuation = "auto";  // PCS punctuation restoration: auto|on|off
    bool ark_punctuation = false;      // ask ARK itself to write punctuation (experimental)
    std::string llm_file;        // local GGUF chat model override
    int llm_ctx = 12288;
};

void print_usage() {
    std::printf(
        "usage: transcribe_multi INPUT [options]\n"
        "\n"
        "Transcribe an audio/video file with Parakeet Ultra, ARK-ASR-3B, Qwen3-ASR-1.7B,\n"
        "Granite Speech 4.1 Plus or Cohere Transcribe.\n"
        "\n"
        "options:\n"
        "  -m, --model NAME          parakeet-ultra (default), ark, qwen, granite, cohere,\n"
        "                            parakeet-redux\n"
        "  -f, --format FMT          txt (default), srt, docx (Word), or several separated by\n"
        "                            commas (txt,srt,docx); both = txt,srt\n"
        "  -o, --output PATH         output path; extension is replaced by .txt/.srt/.docx\n"
        "                            (default: next to the input file)\n"
        "      --language CODE       spoken language (ISO code: en, fr, de, ...), used by\n"
        "                            ark/qwen/granite/cohere; 'auto' to detect (not cohere)\n"
        "                            (default: en)\n"
        "      --device DEV          auto (default: CUDA, else Vulkan, else CPU), cuda, vulkan, cpu\n"
        "      --threads N           CPU threads (default: number of cores, max 16)\n"
        "      --chunk-seconds S     max chunk length for ark/qwen/granite/cohere (default 30)\n"
        "      --no-align            chunked models, SRT: skip forced alignment (cues = whole chunks)\n"
        "      --sentences-per-paragraph N   paragraph size in the .txt (default 5)\n"
        "      --ark-punctuation     ask ARK-ASR to write punctuation and capitals itself\n"
        "                            (experimental: changes its instruction); PCS is then\n"
        "                            only used on chunks that still come back unpunctuated\n"
        "      --punctuation MODE    restore punctuation and capitals with the PCS model\n"
        "                            (163 MB download): auto (default: always for ark,\n"
        "                            otherwise for chunks that come back unpunctuated),\n"
        "                            on, off\n"
        "\n"
        "post-processing with a local language model (runs on this computer):\n"
        "      --polish              fix spelling, punctuation and grammar -> NAME.clean.txt\n"
        "      --summary             write a summary -> NAME.summary.txt\n"
        "      --smart-paragraphs    let the model choose paragraph breaks (topic changes,\n"
        "                            pauses) instead of every N sentences\n"
        "      --llm-file PATH       use this local chat-model .gguf instead of the default\n"
        "                            (Qwen3-4B-Instruct-2507, downloaded on first use, 2.5 GB)\n"
        "      --llm-ctx N           language-model context size in tokens (default 12288)\n"
        "      --text-dir DIR        also write the transcript (and .clean/.summary) as text\n"
        "                            into DIR, listed as \"text PATH\" lines (used by the GUI\n"
        "                            to make PDFs); with -f none only these are written\n"
        "      --model-file PATH     use this local .gguf instead of downloading\n"
        "      --models-dir DIR      where models are cached (default %%USERPROFILE%%\\.cache\\crispasr)\n"
        "  -h, --help                show this help\n"
        "\n"
        "other commands (each with --help):\n"
        "  transcribe_multi speakers MEDIA --srt FILE -o SPEAKERS.txt   who speaks when\n"
        "  transcribe_multi dub MEDIA --srt FILE -o OUTPUT              speak translated subtitles\n");
}

[[noreturn]] void die(const std::string& msg) {
    std::fprintf(stderr, "Error: %s\n", msg.c_str());
    std::fflush(stderr);
    std::exit(1);
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

// Whole-string numeric option values: std::stoi would throw (uncaught: abort,
// whose exit code 3 on Windows reads as "post-processing failed") and would
// accept "8x" as 8.
long long parse_int(const std::string& opt, const std::string& v, long long lo, long long hi) {
    errno = 0;
    char* end = nullptr;
    const long long n = std::strtoll(v.c_str(), &end, 10);
    if (v.empty() || end != v.c_str() + v.size() || errno == ERANGE)
        die(opt + " needs a whole number, not '" + v + "'");
    if (n < lo || n > hi) die(opt + " must be between " + std::to_string(lo) + " and " + std::to_string(hi));
    return n;
}

double parse_number(const std::string& opt, const std::string& v, double lo, double hi) {
    errno = 0;
    char* end = nullptr;
    const double x = std::strtod(v.c_str(), &end);
    if (v.empty() || end != v.c_str() + v.size() || errno == ERANGE || !std::isfinite(x))
        die(opt + " needs a number, not '" + v + "'");
    if (x < lo || x > hi) {
        char b[160];
        std::snprintf(b, sizeof(b), "%s must be between %g and %g", opt.c_str(), lo, hi);
        die(b);
    }
    return x;
}

Args parse_args(const std::vector<std::string>& av) {
    Args a;
    auto need = [&](size_t& i) -> const std::string& {
        if (i + 1 >= av.size()) die("option " + av[i] + " needs a value");
        return av[++i];
    };
    for (size_t i = 1; i < av.size(); ++i) {
        const std::string& o = av[i];
        if (o == "-h" || o == "--help") {
            print_usage();
            std::exit(0);
        } else if (o == "-m" || o == "--model") a.model = need(i);
        else if (o == "-f" || o == "--format") a.format = need(i);
        else if (o == "-o" || o == "--output") a.output = need(i);
        else if (o == "--text-dir") a.text_dir = need(i);
        else if (o == "--language") a.language = need(i);
        else if (o == "--device") a.device = need(i);
        else if (o == "--threads") a.threads = (int)parse_int(o, need(i), 0, 1024);  // 0 = automatic
        else if (o == "--chunk-seconds") a.chunk_seconds = parse_number(o, need(i), 1.0, 3600.0);
        else if (o == "--no-align") a.align = false;
        else if (o == "--sentences-per-paragraph") a.sentences_per_paragraph = (int)parse_int(o, need(i), 1, 1000);
        else if (o == "--model-file") a.model_file = need(i);
        else if (o == "--models-dir") a.models_dir = need(i);
        else if (o == "--polish") a.polish = true;
        else if (o == "--summary") a.summary = true;
        else if (o == "--smart-paragraphs") a.smart_paragraphs = true;
        else if (o == "--ark-punctuation") a.ark_punctuation = true;
        else if (o == "--punctuation") {
            a.punctuation = need(i);
            if (a.punctuation != "auto" && a.punctuation != "on" && a.punctuation != "off")
                die("--punctuation must be auto, on or off");
        }
        else if (o == "--llm-file") a.llm_file = need(i);
        // Below 2048 tokens a prompt and a chunk of transcript don't fit.
        else if (o == "--llm-ctx") a.llm_ctx = (int)parse_int(o, need(i), 2048, 1 << 20);
        else if (!o.empty() && o[0] == '-' && o.size() > 1) die("unknown option " + o + " (see --help)");
        else if (a.input.empty()) a.input = o;
        else die("unexpected argument " + o);
    }
    if (a.input.empty()) {
        print_usage();
        std::exit(2);
    }
    {
        size_t start = 0;
        while (start <= a.format.size()) {
            const size_t comma = a.format.find(',', start);
            const std::string f = a.format.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (f == "txt") a.want_txt = true;
            else if (f == "srt") a.want_srt = true;
            else if (f == "docx") a.want_docx = true;
            else if (f == "both") a.want_txt = a.want_srt = true;
            else if (f != "none") die("--format takes txt, srt, docx, both or none (or a list: txt,docx)");
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        if (!a.want_txt && !a.want_srt && !a.want_docx && a.text_dir.empty())
            die("--format none writes nothing (add --text-dir, or choose txt, srt or docx)");
    }
    if (!find_model(a.model)) die("unknown model '" + a.model + "' (parakeet-ultra, ark, qwen, parakeet-redux)");
    return a;
}

void progress_line(const char* phase, long long done, long long total, const char* unit) {
    std::printf("progress: %s %lld/%lld %s\n", phase, done, total, unit);
    std::fflush(stdout);
}

void say(const std::string& s) {
    std::printf("%s\n", s.c_str());
    std::fflush(stdout);
}

// Through a temporary file next to the target (../common/src/fileio.h), so a
// full disk, an error or a cancel never leaves a truncated file behind or
// destroys an existing one. Throws on failure (the caller decides whether
// that is fatal: after the transcript is saved, a failed AI output is not).
void write_file(const fs::path& p, const std::string& content) { pk::write_file_atomic(p, content); }

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    {
        // "transcribe_multi dub ..." - speak translated subtitles (see dub.h)
        std::vector<std::string> av = utf8_argv(argc, argv);
        if (av.size() >= 2 && av[1] == "dub") return dub_main(std::vector<std::string>(av.begin() + 1, av.end()));
        // "transcribe_multi speakers ..." - who speaks when (see dub.h)
        if (av.size() >= 2 && av[1] == "speakers")
            return speakers_main(std::vector<std::string>(av.begin() + 1, av.end()));
    }
    const Args args = parse_args(utf8_argv(argc, argv));
    const ModelInfo& model = *find_model(args.model);

    const fs::path in_path = path_from_utf8(args.input);
    std::error_code ec;  // not the throwing overload: this is outside the try below
    if (!fs::is_regular_file(in_path, ec)) die("input file not found: " + args.input);
    const bool have_ffmpeg = ffmpeg_available();
    try {
        crispasr();  // load crispasr.dll now so a setup problem is reported before any work
    } catch (const std::exception& e) {
        die(e.what());
    }

    const bool want_txt = args.want_txt, want_srt = args.want_srt, want_docx = args.want_docx;
    const char* language = (args.language.empty() || args.language == "auto") ? nullptr : args.language.c_str();
    // Cohere Transcribe can't detect the language; without one it assumes English.
    const bool cohere_assumes_english = !language && model.backend == "cohere";
    if (cohere_assumes_english) language = "en";

    EngineOptions eopts;
    eopts.threads = args.threads;
    std::string device_note;
    try {
        eopts.device = choose_device(args.device, &device_note);
    } catch (const std::exception& e) {
        die(e.what());
    }

    const auto t0 = std::chrono::steady_clock::now();
    say("Input : " + args.input);
    say("Model : " + model.key + " (" + model.display + ")");
    if (cohere_assumes_english)
        say("Note: Cohere Transcribe can't detect the language; using English. Choose the spoken language "
            "with --language.");

    try {
        if (!device_note.empty()) say("Note: " + device_note);
        say(have_ffmpeg ? "Decoding audio with ffmpeg..." : "Decoding audio (built-in decoder; ffmpeg not found)...");
        std::vector<float> wav = have_ffmpeg ? load_audio(args.input) : load_audio_builtin(args.input);
        const double duration = (double)wav.size() / kSampleRate;
        char buf[128];
        {
            const long long t = (long long)(duration + 0.5);
            std::snprintf(buf, sizeof(buf), "Audio : %lld:%02lld:%02lld (%.2f s)", t / 3600, (t / 60) % 60, t % 60,
                          duration);
        }
        say(buf);

        std::string model_path = args.model_file;
        if (model_path.empty()) {
            say("Loading " + model.display + " (first run downloads " + model.size + ")...");
            model_path = ensure_model(model, args.models_dir);
        } else {
            say("Loading " + model_path + "...");
        }

        PipelineOptions po;
        po.model = &model;
        po.model_path = model_path;
        po.language = language ? language : "";
        po.engine = eopts;
        po.punctuation = args.punctuation;
        po.ark_punctuation = args.ark_punctuation;
        po.chunk_seconds = args.chunk_seconds;
        po.want_cues = want_srt;
        po.align = args.align;
        po.more_models_follow = args.polish || args.summary || args.smart_paragraphs;
        PipelineHooks hooks;
        hooks.say = say;
        hooks.progress = progress_line;
        hooks.model_file = [&](const ModelInfo& m) { return ensure_model(m, args.models_dir); };
        PipelineResult transcribed = transcribe_audio(std::move(wav), po, hooks);
        const std::string& text = transcribed.text;
        const std::vector<Segment>& cues = transcribed.cues;
        const std::vector<Word>& timed_words = transcribed.words;
        if (text.empty()) {
            say("Warning: no speech was transcribed.");
            return 1;
        }

        fs::path base = args.output.empty() ? in_path : path_from_utf8(args.output);
        if (base.has_parent_path()) fs::create_directories(base.parent_path());
        if (!args.text_dir.empty()) fs::create_directories(path_from_utf8(args.text_dir));
        std::vector<fs::path> written, text_copies;
        const std::string title = path_to_utf8(in_path.stem());
        // One text result - the transcript, or the corrected copy / summary under
        // `suffix` (".clean", ".summary") headed by `note` - in each chosen document
        // format, plus the copy for the GUI's PDF. An AI result falls back to .txt
        // when no document format was chosen (subtitles only), so it is never lost.
        auto write_result = [&](const std::string& suffix, const std::string& note, const std::string& body,
                                bool record) {
            const bool transcript = suffix.empty();
            const bool txt = want_txt || (!transcript && !want_docx && args.text_dir.empty());
            auto named = [&](const char* ext) {
                fs::path p = base;
                p.replace_extension(suffix + ext);
                return p;
            };
            const std::string plain = note.empty() ? body : note + "\n\n" + body;
            if (txt) {
                const fs::path p = named(".txt");
                write_file(p, plain);
                if (record) written.push_back(p);
            }
            if (want_docx) {
                const fs::path p = named(".docx");
                pk::DocxText d;
                d.title = title;
                d.note = note;
                d.body = body;
                write_file(p, pk::make_docx(d));
                if (record) written.push_back(p);
            }
            if (!args.text_dir.empty()) {
                const fs::path p = path_from_utf8(args.text_dir) / named(".txt").filename();
                write_file(p, plain);
                if (record) text_copies.push_back(p);
            }
        };
        write_result("", "", format_paragraphs(text, args.sentences_per_paragraph), true);
        if (want_srt) {
            fs::path p = base;
            p.replace_extension(".srt");
            write_file(p, format_srt(cues));
            written.push_back(p);
        }

        {
            const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::snprintf(buf, sizeof(buf), "Transcribed in %.1f s (%.1fx real time)", t, duration / std::max(t, 1e-6));
            say(buf);
        }

        // ---- optional post-processing with a local language model ----------
        // A failure here must not lose the transcript, which is already saved.
        std::string post_error;
        const bool use_llm = args.polish || args.summary || args.smart_paragraphs;
        if (use_llm) try {
            const ModelInfo& lm = default_llm_model();
            std::string llm_path = args.llm_file;
            std::string llm_name = lm.display;
            if (llm_path.empty()) {
                say("Loading " + lm.display + " for post-processing (first run downloads " + lm.size + ")...");
                llm_path = ensure_model(lm, args.models_dir);
            } else {
                llm_name = path_to_utf8(path_from_utf8(llm_path).stem());
                say("Loading " + llm_path + " for post-processing...");
            }
            LlmOptions lo;
            lo.device = eopts.device;
            lo.threads = args.threads;
            lo.n_ctx = args.llm_ctx;
            LlmSession llm(llm_path, lo);
            std::string paragraphs = format_paragraphs(text, args.sentences_per_paragraph);

            if (args.smart_paragraphs) {
                say("Finding paragraph breaks...");
                // Unpunctuated runs are cut at 80 words; a "sentence" still too long for
                // the model's context (e.g. text without spaces) keeps the fixed paragraphs.
                const std::vector<std::string> sentences = split_sentences(text, 80);
                const std::vector<double> pauses = sentence_pauses(sentences, timed_words);
                ParagraphStats ps;
                const std::vector<int> breaks = smart_paragraph_breaks(
                    llm, sentences, pauses, args.sentences_per_paragraph,
                    [&](int d, int t) { progress_line("paragraphs", d, t, "parts"); }, &ps);
                paragraphs = join_paragraphs(sentences, breaks);
                std::snprintf(buf, sizeof(buf), "Paragraphs: %zu (%s)", breaks.size() + 1,
                              pauses.empty() ? "from the text" : "from the text and pauses");
                say(buf);
                if (ps.skipped > 0) {
                    std::snprintf(buf, sizeof(buf),
                                  "Note: %d of %d parts used the fixed paragraph size (a sentence too long for the "
                                  "model's context).", ps.skipped, ps.windows);
                    say(buf);
                }
                if (ps.fallback > 0) {
                    std::snprintf(buf, sizeof(buf),
                                  "Note: %d of %d parts used the fixed paragraph size (unusable model reply).",
                                  ps.fallback, ps.windows);
                    say(buf);
                }
                write_result("", "", paragraphs, false);  // replaces the fixed-size paragraphs
            }
            // The transcript the user gets (the GUI turns the text copy into a PDF).
            const std::string original_name = [&] {
                fs::path p = base;
                p.replace_extension(want_txt ? ".txt" : want_docx ? ".docx" : !args.text_dir.empty() ? ".pdf" : ".txt");
                return path_to_utf8(p.filename());
            }();

            if (args.polish) {
                say("Correcting spelling and grammar...");
                PolishStats st;
                const std::string clean = polish_transcript(
                    llm, paragraphs, [&](int d, int t) { progress_line("polish", d, t, "chunks"); }, &st);
                if (st.kept_original > 0) {
                    std::snprintf(buf, sizeof(buf),
                                  "Note: %d of %d sections were left unchanged because the model's correction "
                                  "changed too many words.", st.kept_original, st.chunks);
                    say(buf);
                }
                write_result(".clean",
                             "[Spelling and grammar corrected by a local AI model (" + llm_name +
                                 "). Where exact wording matters, check against " + original_name + ".]",
                             clean, true);
            }
            if (args.summary) {
                say("Writing summary...");
                const std::string sum = summarize_transcript(
                    llm, paragraphs, [&](int d, int t) { progress_line("summary", d, t, "parts"); });
                write_result(".summary",
                             "[Summary written by a local AI model (" + llm_name + ") from " + original_name +
                                 ". It may contain mistakes.]",
                             sum, true);
            }
        } catch (const std::exception& e) {
            post_error = e.what();
        }

        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (use_llm)  // the real-time figure was printed for the transcription
            std::snprintf(buf, sizeof(buf), "\nDone in %.1f s", elapsed);
        else
            std::snprintf(buf, sizeof(buf), "\nDone in %.1f s (%.1fx real time)", elapsed,
                          duration / std::max(elapsed, 1e-6));
        say(buf);
        for (const auto& p : written) say("  wrote " + path_to_utf8(fs::absolute(p)));
        for (const auto& p : text_copies) say("  text " + path_to_utf8(fs::absolute(p)));
        size_t cut = std::min<size_t>(text.size(), 200);
        while (cut > 0 && cut < text.size() && ((unsigned char)text[cut] & 0xC0) == 0x80) --cut;  // UTF-8 boundary
        std::string preview = text.substr(0, cut);
        say("\nPreview: " + preview + (text.size() > 200 ? "..." : ""));
        if (!post_error.empty()) {
            std::fprintf(stderr, "\nError: AI post-processing failed: %s\nThe transcript itself was saved.\n",
                         post_error.c_str());
            return 3;
        }
    } catch (const std::exception& e) {
        die(e.what());
    }
    return 0;
}
