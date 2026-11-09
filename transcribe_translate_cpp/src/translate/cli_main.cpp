// tt_engine translate - translate text, Markdown, subtitle and Word files with a
// local language model (TranslateGemma, Hy-MT2 or Qwen 3.5) through llama.cpp.
//
// Machine-readable lines for the GUI (one per line on stdout):
//   progress: <phase> <done>/<total> <unit>
//   Engine: <backend> (<device>)        Detected: <code>
//   wrote <path>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "docx.h"
#include "download.h"
#include "fileio.h"
#include "formats.h"
#include "languages.h"
#include "llm.h"
#include "models.h"
#include "speakers.h"  // ../common
#include "pathutil.h"
#include "text.h"
#include "translator.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;
using namespace tl;

namespace {

struct Args {
    std::string input;
    std::string text;            // --text: translate this string instead of a file
    std::string output;
    std::string source = "auto";
    std::string target;
    std::string model = "translategemma-12b";
    std::string format;          // txt | md | srt | docx (default: from the extension)
    std::string model_file;
    std::string models_dir;
    std::string device = "auto";
    int threads = 0;
    int ctx = 0;                 // 0 = the model's default
    bool context = true;
    bool partial = false;        // rewrite the output file as pieces finish (txt/md)
    std::string speakers;        // .srt: who says each subtitle ("tt_engine speakers" file)
    std::string draft = "off";   // off | auto | PATH: draft model for speculative decoding
    int draft_max = 7;
    bool verbose = false;
};

[[noreturn]] void die(const std::string& msg, int code = 1) {
    std::fprintf(stderr, "Error: %s\n", msg.c_str());
    std::fflush(stderr);
    std::exit(code);
}

void say(const std::string& s) {
    std::printf("%s\n", s.c_str());
    std::fflush(stdout);
}

void progress_line(const char* phase, long long done, long long total, const char* unit) {
    std::printf("progress: %s %lld/%lld %s\n", phase, done, total, unit);
    std::fflush(stdout);
}

void print_usage() {
    std::printf(
        "usage: tt_engine translate INPUT -t LANG [options]\n"
        "       tt_engine translate --text \"some text\" -t LANG [options]\n"
        "\n"
        "Translate a text (.txt), Markdown (.md), subtitle (.srt) or Word (.docx) file with a\n"
        "local language model. Nothing leaves this computer.\n"
        "\n"
        "options:\n"
        "  -t, --to LANG           target language code (see --list-languages)\n"
        "  -s, --from LANG         source language code, or auto (default)\n"
        "  -m, --model NAME        model (default translategemma-12b; see --list-models)\n"
        "  -o, --output PATH       output file (default: NAME.LANG.EXT next to the input)\n"
        "      --text TEXT         translate TEXT and print the result\n"
        "      --format FMT        txt, md, srt or docx (default: from the file extension)\n"
        "      --no-context        don't show the model the preceding text\n"
        "      --speakers FILE     subtitles of several speakers (\"tt_engine speakers\"):\n"
        "                            never translate two speakers' lines as one sentence\n"
        "      --device DEV        auto (default), cuda, vulkan or cpu\n"
        "      --threads N         CPU threads (default: automatic)\n"
        "      --ctx N             context window in tokens (default depends on the model)\n"
        "      --draft WHAT        speculative decoding, for the same translation sooner,\n"
        "                            for TranslateGemma 12B/27B on a graphics card:\n"
        "                            translategemma-4b or gemma-3-1b = that helper model\n"
        "                            (downloaded on first use), auto = the 4B if it fits\n"
        "                            beside the model, else the 1B, PATH = this .gguf\n"
        "                            (same vocabulary as the model), off = none (default)\n"
        "      --draft-max N       tokens drafted ahead at most (default 7; the length is\n"
        "                            chosen per step from measured speeds, 0 when no draft pays)\n"
        "      --model-file PATH   use this local .gguf instead of downloading\n"
        "      --models-dir DIR    where models are kept (default %s)\n"
        "      --list-models       show the models\n"
        "      --list-languages    show the language codes\n"
        "      --verbose           show llama.cpp's log\n"
        "  -h, --help              show this help\n",
        default_models_dir().c_str());
}

void list_models() {
    for (const auto& m : model_table())
        std::printf("  %-22s %-28s %8s  %s%s\n", m.key.c_str(), m.display.c_str(), m.size.c_str(),
                    m.description.c_str(), &m == &default_model() ? "  [default]" : "");
}

void list_languages() {
    for (const auto& l : languages()) {
        std::string models;
        for (const auto& m : model_table())
            if (m.family != Family::Qwen && model_supports(m, l.code) &&
                models.find(m.display.substr(0, 6)) == std::string::npos)
                models += (models.empty() ? "" : ", ") + m.display.substr(0, m.display.find(' '));
        std::printf("  %-8s %-22s %s\n", l.code.c_str(), l.name.c_str(), models.c_str());
    }
    std::printf("\nQwen 3.5 can be used for every language; the other models list the ones they were trained on.\n");
}


Args parse_args(const std::vector<std::string>& av) {
    Args a;
    auto need = [&](size_t& i) -> std::string {
        if (i + 1 >= av.size()) die("missing value for " + av[i], 2);
        return av[++i];
    };
    for (size_t i = 1; i < av.size(); ++i) {
        const std::string& o = av[i];
        if (o == "-h" || o == "--help") print_usage(), std::exit(0);
        else if (o == "--list-models") list_models(), std::exit(0);
        else if (o == "--list-languages") list_languages(), std::exit(0);
        else if (o == "-t" || o == "--to") a.target = need(i);
        else if (o == "-s" || o == "--from") a.source = need(i);
        else if (o == "-m" || o == "--model") a.model = need(i);
        else if (o == "-o" || o == "--output") a.output = need(i);
        else if (o == "--text") a.text = need(i);
        else if (o == "--format") a.format = need(i);
        else if (o == "--no-context") a.context = false;
        else if (o == "--device") a.device = need(i);
        else if (o == "--threads") a.threads = std::atoi(need(i).c_str());
        else if (o == "--ctx") a.ctx = std::atoi(need(i).c_str());
        else if (o == "--model-file") a.model_file = need(i);
        else if (o == "--models-dir") a.models_dir = need(i);
        else if (o == "--partial") a.partial = true;
        else if (o == "--speakers") a.speakers = need(i);
        else if (o == "--draft") a.draft = need(i);
        else if (o == "--draft-max") a.draft_max = std::atoi(need(i).c_str());
        else if (o == "--verbose") a.verbose = true;
        else if (!o.empty() && o[0] == '-' && o.size() > 1) die("unknown option " + o + " (see --help)", 2);
        else if (a.input.empty()) a.input = o;
        else die("unexpected argument " + o, 2);
    }
    if (a.input.empty() && a.text.empty()) print_usage(), std::exit(2);
    if (a.target.empty()) die("choose the target language with -t (see --list-languages)", 2);
    if (!find_language(a.target)) die("unknown target language '" + a.target + "' (see --list-languages)", 2);
    if (a.source != "auto" && !find_language(a.source))
        die("unknown source language '" + a.source + "' (see --list-languages)", 2);
    if (!find_model(a.model)) die("unknown model '" + a.model + "' (see --list-models)", 2);
    if (a.draft_max < 1 || a.draft_max > 32) die("--draft-max must be 1 to 32", 2);
    return a;
}

std::string read_file(const fs::path& p) {
    try {
        return pk::read_file(p);
    } catch (const std::exception& e) {
        die(e.what());
    }
}

void write_file(const fs::path& p, const std::string& content) {
    // Through a temporary file (../common/src/fileio.h): a failure or a cancel
    // (the GUI kills this process) never leaves a truncated output or destroys
    // an existing file.
    try {
        pk::write_file_atomic(p, content);
    } catch (const std::exception& e) {
        die(e.what());
    }
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

fs::path models_dir(const Args& a) {
    return path_from_utf8(a.models_dir.empty() ? default_models_dir() : a.models_dir);
}

// Downloads model m into the models folder (`what` names it in the message).
std::string download_model(const ModelInfo& m, const Args& a, const std::string& what) {
    const fs::path dir = models_dir(a);
    const fs::path file = dir / m.file;
    say("Downloading " + what + " (" + m.size + ", first use only) to " + path_to_utf8(dir) + " ...");
    int last_pct = -1;
    download_file(m.url, path_to_utf8(file), m.bytes, [&](uint64_t done, uint64_t total) {
        if (total == 0) return;
        const int pct = (int)(done * 1000 / total);  // per mille: fine-grained for the GUI
        if (pct != last_pct) {
            last_pct = pct;
            progress_line("download", (long long)(done / 1000000), (long long)(total / 1000000), "MB");
        }
    });
    return path_to_utf8(file);
}

// Local path of the model, downloading it on first use.
std::string ensure_model(const ModelInfo& m, const Args& a) {
    if (!a.model_file.empty()) {
        if (!fs::exists(path_from_utf8(a.model_file))) die("model file not found: " + a.model_file);
        return a.model_file;
    }
    const fs::path file = models_dir(a) / m.file;
    if (fs::exists(file)) return path_to_utf8(file);
    return download_model(m, a, m.display);
}

// The draft model for speculative decoding ("" = none), for --draft:
//   auto                TranslateGemma 4B if it fits beside the model on the
//                       graphics card, else Gemma 3 1B (draft_model_table())
//   translategemma-4b,  that one
//   gemma-3-1b
//   PATH                a .gguf with the model's vocabulary
// Downloaded on first use. Only for TranslateGemma 12B/27B, on a graphics card
// with room for both (roughly here; Llm checks exactly).
std::string ensure_draft(const ModelInfo& m, const std::string& model_path, const Args& a) {
    const std::string d = lower(a.draft);
    if (d == "off" || d == "none" || d.empty()) return "";
    if (d != "auto" && !find_draft_model(d)) {
        if (!fs::exists(path_from_utf8(a.draft))) die("draft model file not found: " + a.draft);
        return a.draft;
    }
    if (m.family != Family::TranslateGemma || m.key == "translategemma-4b") return "";
    const uint64_t have = gpu_free_memory(a.device);
    if (have == 0) {
        say("Note: no draft model - it only speeds up translation on a graphics card");
        return "";
    }
    std::error_code ec;
    uint64_t model_bytes = fs::file_size(path_from_utf8(model_path), ec);
    if (ec) model_bytes = 0;
    // The model's and the draft model's caches and buffers, with room to spare: an
    // estimate of 2.5 GB let TranslateGemma 12B (9.7 GB) and 4B (2.5 GB) through on
    // a 16 GB card where they then didn't fit.
    const uint64_t overhead = 3000000000ull;

    struct Choice {
        const ModelInfo* info;
        std::string path;  // an existing file, or "" = download info
        uint64_t bytes;
    };
    std::vector<Choice> choices;
    for (const std::string& key : d == "auto" ? std::vector<std::string>{"translategemma-4b", "gemma-3-1b"}
                                              : std::vector<std::string>{d}) {
        const ModelInfo* info = find_draft_model(key);
        Choice c{info, "", info->bytes ? info->bytes : 2600000000ull};
        // An existing download: the draft table's file, or TranslateGemma 4B's Q8_0
        // from the model list (bigger, so only when it fits too).
        std::vector<std::string> files{info->file};
        if (key == "translategemma-4b")
            if (const ModelInfo* q8 = find_model("translategemma-4b")) files.push_back(q8->file);
        for (const std::string& f : files) {
            const fs::path p = models_dir(a) / f;
            const uint64_t size = fs::file_size(p, ec);
            if (!ec && model_bytes + size + overhead <= have) {
                c.path = path_to_utf8(p);
                c.bytes = size;
                break;
            }
        }
        choices.push_back(c);
    }
    for (size_t i = 0; i < choices.size(); ++i) {
        const Choice& c = choices[i];
        if (model_bytes + c.bytes + overhead > have) continue;
        if (i > 0)
            say("Note: faster mode with " + c.info->display + ": " + choices[0].info->display +
                " doesn't fit beside " + m.display + " on the graphics card");
        if (!c.path.empty()) return c.path;
        return download_model(*c.info, a, c.info->display + ", the draft model that speeds up " + m.display);
    }
    char buf[240];
    std::snprintf(buf, sizeof(buf),
                  "Note: no draft model - the graphics card has %.1f GB free, %.1f GB are needed for %s with %s",
                  have / 1e9, (model_bytes + choices.back().bytes + overhead) / 1e9, m.display.c_str(),
                  choices.back().info->display.c_str());
    say(buf);
    return "";
}

std::string default_output(const fs::path& in, const std::string& target, const std::string& format) {
    fs::path out = in;
    const std::string ext = in.has_extension() ? path_to_utf8(in.extension()) : "." + format;
    out.replace_extension();
    out += "." + target + ext;
    return path_to_utf8(out);
}

int count_words(const std::string& s) {
    int n = 0;
    bool in = false;
    for (unsigned char c : s) {
        const bool sp = c == ' ' || c == '\n' || c == '\t' || c == '\r';
        if (!sp && !in) ++n;
        in = !sp;
    }
    return n;
}

}  // namespace

// "tt_engine translate ARGS" (av[0] = "translate"): run from the engine
// through tt_translate_main below (this library has its own llama.cpp).
int translate_main(const std::vector<std::string>& av) {
    const Args args = parse_args(av);
    const ModelInfo& model = *find_model(args.model);
    const std::string target = find_language(args.target)->code;

    try {
        // ---- read the input -------------------------------------------------
        fs::path in_path;
        std::string format = lower(args.format);
        if (args.text.empty()) {
            in_path = path_from_utf8(args.input);
            if (!fs::is_regular_file(in_path)) die("input file not found: " + args.input);
            if (format.empty()) {
                const std::string ext = lower(path_to_utf8(in_path.extension()));
                format = ext == ".md" || ext == ".markdown" ? "md"
                         : ext == ".srt"                    ? "srt"
                         : ext == ".docx"                   ? "docx"
                                                            : "txt";
                if (ext == ".doc") die("old .doc files are not supported; save it as .docx in Word first");
            }
        } else if (format.empty()) {
            format = "txt";
        }
        if (format != "txt" && format != "md" && format != "srt" && format != "docx")
            die("--format must be txt, md, srt or docx");
        if (format == "docx" && !args.text.empty())
            die("--format docx needs a Word file as the input; --text can only be txt, md or srt", 2);

        Layout layout;
        SrtPlan srt;
        std::unique_ptr<DocxDocument> docx;
        std::vector<std::string> units;
        if (format == "docx") {
            docx = std::make_unique<DocxDocument>(path_to_utf8(in_path));
            units = docx->units();
        } else {
            const std::string text = args.text.empty() ? clean_input_text(read_file(in_path)) : clean_input_text(args.text);
            if (format == "srt") {
                const auto cues = parse_srt(text);
                std::vector<int> who;
                if (!args.speakers.empty()) {
                    pk::SpeakerMap map;
                    std::string why;
                    if (!pk::parse_speakers(read_file(path_from_utf8(args.speakers)), &map, &why))
                        die(args.speakers + ": " + why);
                    for (const auto& c : cues) {
                        const pk::SpeakerCue* s = c.timed ? map.at(c.start, c.end, false) : nullptr;
                        who.push_back(s ? s->speaker : -1);
                    }
                }
                srt = plan_srt(cues, who);
                if (srt.cues.empty()) die("no subtitles found in " + args.input);
                units = srt.units;
            } else {
                layout = format == "md" ? parse_markdown(text) : parse_plain_text(text);
                units = layout.units;
            }
        }
        if (units.empty()) die("there is no text to translate");

        std::string out_path;
        if (!args.output.empty()) out_path = args.output;
        else if (args.text.empty()) out_path = default_output(in_path, target, format);

        say("Input : " + (args.text.empty() ? args.input : std::string("(text)")) + " (" + format + ", " +
            std::to_string(units.size()) + " parts)");
        {
            int words = 0;
            for (const auto& u : units) words += count_words(u);
            say("Words : " + std::to_string(words));
        }
        say("Model : " + model.key + " (" + model.display + ")");
        if (!model_supports(model, target))
            say("Note: " + model.display + " was not trained on " + language_label(target) +
                "; the translation may be poor. Qwen 3.5 handles more languages.");

        // ---- load the model -----------------------------------------------------
        const auto t0 = std::chrono::steady_clock::now();
        const std::string model_path = ensure_model(model, args);
        const std::string draft_path = ensure_draft(model, model_path, args);
        say("Loading " + model.display + " ...");
        EngineOptions eo;
        eo.draft_model_path = draft_path;
        eo.draft_max = args.draft_max;
        // A file the user chose: use it anywhere (the helper models only on a graphics card).
        eo.draft_on_cpu = lower(args.draft) != "auto" && !find_draft_model(lower(args.draft));
        eo.device = args.device;
        eo.threads = args.threads;
        eo.n_ctx = args.ctx > 0 ? args.ctx : model.n_ctx;
        eo.verbose = args.verbose;
        std::string note;
        Llm llm(model_path, eo, &note);
        if (!note.empty()) say("Note: " + note);
        say("Engine: " + describe_engine(llm.engine()));
        say("Placement: " + llm.engine().placement);
        if (llm.has_draft())
            say("Draft : " + path_to_utf8(path_from_utf8(draft_path).filename()) +
                " (speculative decoding: the same translation, sooner)");
        else if (!draft_path.empty())
            say("Note: draft model " + llm.draft_info());

        TranslateOptions to;
        to.source = args.source;
        to.target = target;
        to.use_context = args.context;
        Translator tr(llm, model, to);

        // ---- source language ----------------------------------------------------
        std::string source = args.source == "auto" ? "auto" : find_language(args.source)->code;
        if (source == "auto" && model.family == Family::TranslateGemma) {
            // TranslateGemma's prompt names the source language: ask the model.
            std::string sample;
            for (const auto& u : units) {
                if (sample.size() > 600) break;
                sample += (sample.empty() ? "" : "\n") + u;
            }
            source = tr.detect_language(sample);
            if (source.empty()) die("couldn't tell which language the text is in; choose it with --from");
            say("Detected: " + source + " (" + language_label(source) + ")");
            tr.set_source(source);
        }
        if (source != "auto" && source == target) die("the text is already in " + language_label(target));
        if (source != "auto" && !model_supports(model, source))
            say("Note: " + model.display + " was not trained on " + language_label(source) +
                "; the translation may be poor.");
        say("Languages: " + (source == "auto" ? std::string("auto-detect") : language_label(source)) + " -> " +
            language_label(target));

        // ---- translate ------------------------------------------------------------
        const auto t1 = std::chrono::steady_clock::now();
        say("Translating ...");
        std::vector<std::string> done(units.size());
        std::vector<bool> have(units.size(), false);
        // The share translated, in tenths of a percent, as the model writes
        // ("progress: translate" below counts finished pieces).
        int last_permille = -1;
        tr.set_fraction_progress([&](double f) {
            const int pm = (int)(f * 1000.0);
            if (pm == last_permille) return;
            last_permille = pm;
            progress_line("translating", pm, 1000, "permille");
        });
        bool partial_failed = false;
        auto render = [&](const std::vector<std::string>& tr_units) -> std::string {
            if (format == "srt") return render_srt(srt, tr_units, target);
            return layout.render(tr_units);
        };
        std::vector<std::string> result = tr.translate_units(
            units,
            [&](int d, int t) {
                // The partial file first, then the progress line: the GUI reads the
                // file when it sees the line, and replacing it while the GUI has it
                // open fails on Windows.
                if (args.partial && !out_path.empty() && (format == "txt" || format == "md")) {
                    std::vector<std::string> sofar(units.size());
                    for (size_t i = 0; i < units.size(); ++i) sofar[i] = have[i] ? done[i] : std::string();
                    // Units not yet translated are left out, so the text fills in progressively.
                    Layout partial = layout;
                    try {
                        pk::write_file_atomic(path_from_utf8(out_path), partial.render(sofar));
                    } catch (const std::exception& e) {
                        // Only a preview: a failed update doesn't stop the translation
                        // (the final write below must still succeed).
                        if (!partial_failed) {
                            std::fprintf(stderr, "Note: could not update the partial output (%s); continuing\n",
                                         e.what());
                            std::fflush(stderr);
                        }
                        partial_failed = true;
                    }
                }
                progress_line("translate", d, t, "pieces");
            },
            [&](size_t i, const std::string& s) {
                done[i] = s;
                have[i] = true;
            });

        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
        int words = 0;
        for (const auto& u : units) words += count_words(u);
        char buf[160];
        std::snprintf(buf, sizeof(buf), "Translated in %.1f s (%d words, %.0f words/min)", secs, words,
                      secs > 0 ? words * 60.0 / secs : 0.0);
        say(buf);
        if (llm.has_draft()) {
            const DraftStats& ds = llm.draft_stats();
            if (ds.drafted > 0) {
                std::snprintf(buf, sizeof(buf),
                              "Draft : %lld of %lld drafted tokens accepted (%.0f%%), %.2f tokens per step with a draft",
                              ds.accepted, ds.drafted, 100.0 * (double)ds.accepted / (double)ds.drafted,
                              ds.steps ? (double)ds.tokens / (double)ds.steps : 0.0);
                say(buf);
            }
            const long long all_steps = ds.steps + ds.plain_steps;
            if (all_steps > 0 && ds.seconds > 0 && ds.plain_seconds > 0) {
                // Drafting is left out where it doesn't pay off (a hard text, a slow draft model).
                std::snprintf(buf, sizeof(buf), "Draft : drafted in %.0f%% of the steps; writing took %.1f s, about %.1f s without the draft model (%.2fx)",
                              100.0 * (double)ds.steps / (double)all_steps, ds.seconds, ds.plain_seconds,
                              ds.plain_seconds / ds.seconds);
                say(buf);
            }
        }
        const TranslateStats& st = tr.stats();
        if (st.problems > 0)
            say("Note: " + std::to_string(st.problems) +
                " unit(s) could not be translated; their complete original text was retained.");

        // ---- write ------------------------------------------------------------------
        if (format == "docx") {
            docx->save(out_path, result);
        } else {
            const std::string out = render(result);
            if (out_path.empty()) {
                std::printf("\n%s\n", out.c_str());
            } else {
                write_file(path_from_utf8(out_path), out);
            }
        }
        const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::snprintf(buf, sizeof(buf), "Done in %.1f s", total);
        say(buf);
        if (!out_path.empty()) say("  wrote " + path_to_utf8(fs::absolute(path_from_utf8(out_path))));
        if (st.problems > 0) return 3;  // Saved partial success; callers must not treat it as a complete translation.
    } catch (const std::exception& e) {
        die(e.what());
    }
    return 0;
}

// The library's one entry point (see engine/main.cpp, which loads this library
// when it is asked to translate). argv[0] is "translate"; the strings are UTF-8.
#ifdef _WIN32
#define TT_EXPORT extern "C" __declspec(dllexport)
#else
#define TT_EXPORT extern "C" __attribute__((visibility("default")))
#endif
TT_EXPORT int tt_translate_main(int argc, const char* const* argv) {
    const int code = translate_main(std::vector<std::string>(argv, argv + argc));
    // This library has its own C runtime (and stdout buffer) on Windows: flush it
    // before going back to tt_engine.
    std::fflush(stdout);
    std::fflush(stderr);
    return code;
}
