#include "dub.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>

#include "audio.h"
#include "crispasr_api.h"
#include "dsp.h"
#include "engine.h"
#include "fileio.h"
#include "pathutil.h"
#include "output_transaction.h"
#include "transcript.h"

namespace fs = std::filesystem;

namespace tmx {

// ---------------------------------------------------------------------------
// TTS / separation sessions
// ---------------------------------------------------------------------------

namespace {

struct Bundle {
    std::string primary, companion;
    std::vector<std::string> extras;
};

// Download every file a backend needs (into the CrispASR model cache).
Bundle ensure_bundle(const std::string& backend, const std::string& models_dir) {
    const CrispApi& api = crispasr();
    char canon[128], license[256];
    int32_t needs_acceptance = 0;
    const int n = api.crispasr_registry_default_bundle_info_abi(backend.c_str(), canon, sizeof(canon), license,
                                                                sizeof(license), &needs_acceptance);
    if (n <= 0) throw std::runtime_error("CrispASR has no download information for '" + backend + "'");
    Bundle b;
    for (int i = 0; i < n; ++i) {
        int32_t kind = 0;
        char file[512], url[1024], size[64];
        if (api.crispasr_registry_default_bundle_artifact_abi(backend.c_str(), i, &kind, file, sizeof(file), url,
                                                               sizeof(url), size, sizeof(size)) != 0)
            continue;
        char path[4096];
        if (api.crispasr_cache_ensure_file_abi(file, url, 0, models_dir.empty() ? nullptr : models_dir.c_str(), path,
                                               (int32_t)sizeof(path)) != 0)
            throw std::runtime_error(std::string("could not download ") + file + " from " + url);
        if (kind == 0) b.primary = path;
        else if (kind == 1) b.companion = path;
        else b.extras.push_back(path);
    }
    if (b.primary.empty()) throw std::runtime_error("no model file for '" + backend + "'");
    return b;
}

class Session {
public:
    Session(const std::string& model_path, const std::string& backend, const EngineOptions& opts) {
        const bool cpu = opts.device == "cpu";
        // Set on every open (an earlier session may have chosen another device).
        // CrispASR treats "" as automatic; "auto" would be looked up as a device name.
        crispasr().crispasr_set_gpu_backend(opts.device == "auto" ? "" : opts.device.c_str());
        crispasr_open_params_v1 p{};
        p.abi_version = 2;
        p.n_threads = opts.threads > 0 ? opts.threads : default_threads();
        p.use_gpu = cpu ? 0 : 1;
        p.verbosity = 0;
        p.flash_attn = 1;
        p.n_gpu_layers = -1;
        s_ = crispasr().crispasr_session_open_with_params(model_path.c_str(), backend.c_str(), &p);
        if (!s_) throw std::runtime_error("failed to load '" + model_path + "' (" + backend + ")");
    }
    ~Session() {
        if (s_) crispasr().crispasr_session_close(s_);
    }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    crispasr_session* get() const { return s_; }

    std::vector<float> synthesize(const std::string& text) {
        int n = 0;
        float* pcm = crispasr().crispasr_session_synthesize(s_, text.c_str(), &n);
        if (!pcm || n <= 0) {
            if (pcm) crispasr().crispasr_pcm_free(pcm);
            const char* e = crispasr().crispasr_session_last_synth_error(s_);
            throw std::runtime_error(std::string("speech synthesis failed") + (e && *e ? std::string(": ") + e : ""));
        }
        std::vector<float> out(pcm, pcm + n);
        crispasr().crispasr_pcm_free(pcm);
        return out;
    }

private:
    crispasr_session* s_ = nullptr;
};

void say(const std::string& s) {
    std::printf("%s\n", s.c_str());
    std::fflush(stdout);
}

void progress_line(const char* phase, long long done, long long total, const char* unit) {
    std::printf("progress: %s %lld/%lld %s\n", phase, done, total, unit);
    std::fflush(stdout);
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Video containers the mux can write: it copies the video stream (or re-encodes
// it to H.264), adds AAC audio and a text subtitle track (mov_text in
// MP4/MOV/M4V, SRT in MKV). AVI, WebM and WMV can't hold that combination.
bool is_video_ext(const std::string& ext) {
    for (const char* e : {".mp4", ".mkv", ".mov", ".m4v"})
        if (ext == e) return true;
    return false;
}

// Length of the parts source separation works on (see `separate` in dub_main).
// 90 s keeps its memory to a few hundred MB while the joins, and the work
// repeated for the context around them, stay few. TMX_SEPARATE_CHUNK_SECONDS
// overrides it for testing the joins on short media and for tuning; it is not
// a user option. At least 20 s: the parts must be longer than the context and
// crossfade around their joins.
double separate_chunk_seconds() {
    double s = 90;
    if (const char* e = std::getenv("TMX_SEPARATE_CHUNK_SECONDS")) {
        char* end = nullptr;
        const double v = std::strtod(e, &end);
        if (end != e && std::isfinite(v)) s = std::clamp(v, 20.0, 3600.0);
    }
    return s;
}

std::string fmt_time(double s) {
    char b[32];
    const int t = (int)s;
    std::snprintf(b, sizeof(b), "%d:%02d:%02d", t / 3600, (t / 60) % 60, t % 60);
    return b;
}

struct DubArgs {
    std::string media, source_srt, target_srt, output, work_dir, debug_dir;
    std::string target = "en", source = "auto";
    std::string tts = "qwen3-tts";
    bool clone = true, consent = false;
    bool keep_accent = false;  // default: speak like a native speaker of the target language
    bool tone_match = true;    // match the dub's tone (spectral balance) to the speaker's voice
    double sample_seconds = 30;  // voice sample length for the native-accent modes
    std::string voice_file, voice_text;
    std::string speakers;  // --speakers FILE: a voice for each speaker
    std::string background = "keep";  // keep | duck | none
    std::string models_dir, device = "auto";
    int threads = 0;
    double max_speed = 1.35;
};

void dub_usage() {
    std::printf(
        "usage: transcribe_multi dub MEDIA --srt TRANSLATED.srt -o OUTPUT [options]\n"
        "\n"
        "Speak translated subtitles in the original speaker's voice, fitted to the\n"
        "original timing. OUTPUT .wav/.mp3/.m4a/.flac = audio; .mp4/.mkv/.mov/.m4v = the\n"
        "video with the new soundtrack (and the subtitles as a track).\n"
        "Output is staged in a unique work folder beside OUTPUT until complete.\n"
        "\n"
        "options:\n"
        "      --srt FILE            translated subtitles to speak (required)\n"
        "      --source-srt FILE     original-language subtitles (to find a clean voice sample)\n"
        "  -t, --to LANG             language of the translated subtitles (default en)\n"
        "  -s, --from LANG           spoken language of the original (default auto)\n"
        "      --tts NAME            qwen3-tts (default), cosyvoice3, omnivoice\n"
        "      --i-have-rights       confirm the speaker agreed to having their voice cloned;\n"
        "                            without it a built-in voice is used\n"
        "      --no-clone            always use a built-in voice\n"
        "      --voice-file WAV      clone from this recording instead (+ --voice-text TEXT;\n"
        "                            also needs --i-have-rights)\n"
        "      --speakers FILE       several speakers: who says what, from \"transcribe_multi\n"
        "                            speakers\" (give it the --split-srt subtitles, translated);\n"
        "                            each speaker's own voice is cloned (--i-have-rights:\n"
        "                            every speaker agreed), a built-in voice for a speaker\n"
        "                            with too little clear speech\n"
        "      --keep-accent         keep the speaker's original accent in the new language\n"
        "                            (default: a native accent; qwen3-tts and cosyvoice3)\n"
        "      --sample-seconds S    voice sample length for a native accent (5-60, default 30;\n"
        "                            with the original accent one ~10 s stretch is used)\n"
        "      --no-tone-match       don't adjust the dub's tone to the speaker's voice\n"
        "      --background MODE     keep (default: separate and keep music/effects),\n"
        "                            duck (original quietly underneath), none\n"
        "      --max-speed X         fastest allowed speed-up to fit a line (default 1.35)\n"
        "      --device DEV          auto, cuda, vulkan, cpu\n"
        "      --threads N, --models-dir DIR\n"
        "      --work-dir DIR        existing private work folder beside OUTPUT (for GUI cancellation)\n"
        "      --debug-dir DIR       also save the voice sample and each line exactly as the speech\n"
        "                            model returned it (to tell model output from later processing)\n");
}

DubArgs parse_dub_args(const std::vector<std::string>& av) {
    DubArgs a;
    auto die = [](const std::string& m) {
        std::fprintf(stderr, "Error: %s\n", m.c_str());
        std::exit(2);
    };
    auto need = [&](size_t& i) -> std::string {
        if (i + 1 >= av.size()) die("missing value for " + av[i]);
        return av[++i];
    };
    // Whole-string numbers only: atoi/atof would turn "x" into 0 and "2x" into 2.
    auto number = [&](const std::string& opt, const std::string& v, double lo, double hi) {
        char* end = nullptr;
        const double x = std::strtod(v.c_str(), &end);
        if (v.empty() || end != v.c_str() + v.size() || !std::isfinite(x))
            die(opt + " needs a number, not '" + v + "'");
        if (x < lo || x > hi) {
            char b[160];
            std::snprintf(b, sizeof(b), "%s must be between %g and %g", opt.c_str(), lo, hi);
            die(b);
        }
        return x;
    };
    for (size_t i = 1; i < av.size(); ++i) {
        const std::string& o = av[i];
        if (o == "-h" || o == "--help") dub_usage(), std::exit(0);
        else if (o == "--srt") a.target_srt = need(i);
        else if (o == "--source-srt") a.source_srt = need(i);
        else if (o == "-o" || o == "--output") a.output = need(i);
        else if (o == "-t" || o == "--to") a.target = need(i);
        else if (o == "-s" || o == "--from") a.source = need(i);
        else if (o == "--tts") a.tts = need(i);
        else if (o == "--i-have-rights") a.consent = true;
        else if (o == "--no-clone") a.clone = false;
        else if (o == "--keep-accent") a.keep_accent = true;
        else if (o == "--no-tone-match") a.tone_match = false;
        else if (o == "--sample-seconds") {
            const std::string v = need(i);
            char* end = nullptr;
            const double x = std::strtod(v.c_str(), &end);
            if (end == v.c_str() || *end || !(x >= 5 && x <= 60)) die("--sample-seconds must be 5-60, not '" + v + "'");
            a.sample_seconds = x;
        }
        else if (o == "--voice-file") a.voice_file = need(i);
        else if (o == "--voice-text") a.voice_text = need(i);
        else if (o == "--speakers") a.speakers = need(i);
        else if (o == "--background") a.background = need(i);
        else if (o == "--max-speed") a.max_speed = number(o, need(i), 1.0, 2.0);
        else if (o == "--device") a.device = need(i);
        else if (o == "--threads") {
            const std::string v = need(i);
            const double t = number(o, v, 0, 1024);  // 0 = automatic
            if (t != std::floor(t)) die("--threads needs a whole number, not '" + v + "'");
            a.threads = (int)t;
        }
        else if (o == "--work-dir") a.work_dir = need(i);
        else if (o == "--debug-dir") a.debug_dir = need(i);
        else if (o == "--models-dir") a.models_dir = need(i);
        else if (!o.empty() && o[0] == '-') die("unknown option " + o + " (see dub --help)");
        else if (a.media.empty()) a.media = o;
        else die("unexpected argument " + o);
    }
    if (a.media.empty() || a.target_srt.empty() || a.output.empty()) dub_usage(), std::exit(2);
    if (!a.speakers.empty() && !a.voice_file.empty())
        die("--voice-file clones one voice: it can't be used with --speakers");
    if (!find_tts_model(a.tts)) die("unknown --tts '" + a.tts + "' (qwen3-tts, cosyvoice3, omnivoice)");
    if (a.background != "keep" && a.background != "duck" && a.background != "none")
        die("--background must be keep, duck or none");
    // ISO base code ("pt-BR" -> "pt") for the speech models.
    a.target = a.target.substr(0, a.target.find_first_of("-_"));
    if (a.source != "auto") a.source = a.source.substr(0, a.source.find_first_of("-_"));
    return a;
}

}  // namespace

// ---------------------------------------------------------------------------
// The dub
// ---------------------------------------------------------------------------

int dub_main(const std::vector<std::string>& argv) {
    const DubArgs a = parse_dub_args(argv);
    const TtsModel& tm = *find_tts_model(a.tts);
    const auto t0 = std::chrono::steady_clock::now();
    try {
        const std::string out_ext = lower(path_to_utf8(path_from_utf8(a.output).extension()));
        const bool video_out = is_video_ext(out_ext);
        const bool wav_out = out_ext == ".wav";
        if (!video_out && !wav_out && out_ext != ".mp3" && out_ext != ".m4a" && out_ext != ".flac")
            throw std::runtime_error("output must end in .wav, .mp3, .m4a, .flac (audio) or .mp4, .mkv, .mov, .m4v "
                                     "(video)");
        if (!ffmpeg_available()) throw std::runtime_error("dubbing needs ffmpeg (next to this program or on PATH)");

        if (!a.debug_dir.empty()) fs::create_directories(path_from_utf8(a.debug_dir));
        OutputTransaction output(path_from_utf8(a.output), path_from_utf8(a.work_dir));
        auto target_cues = read_srt_cues(a.target_srt);
        std::vector<SubCue> source;
        if (!a.source_srt.empty()) source = read_srt_cues(a.source_srt);
        // Several speakers: each subtitle's speaker (and group: the subtitle it was
        // split from) is the one of the speakers file's cue it overlaps most.
        pk::SpeakerMap speakers;
        const bool multi = !a.speakers.empty();
        if (multi) {
            std::string why;
            if (!pk::parse_speakers(read_text_file(a.speakers), &speakers, &why))
                throw std::runtime_error(a.speakers + ": " + why);
            if (speakers.speakers.empty() || speakers.cues.empty())
                throw std::runtime_error(a.speakers + " lists no speakers");
            for (auto* cues : {&target_cues, &source})
                for (auto& c : *cues)
                    if (const pk::SpeakerCue* s = speakers.at(c.start, c.end)) {
                        c.speaker = s->speaker;
                        if (std::fabs(s->start - c.start) < 0.02 && std::fabs(s->end - c.end) < 0.02) c.group = s->group;
                    }
        }
        const auto lines = group_cues(target_cues);
        if (lines.empty()) throw std::runtime_error("no subtitles to speak in " + a.target_srt);

        EngineOptions eo;
        std::string note;
        eo.device = choose_device(a.device, &note);
        eo.threads = a.threads;
        if (!note.empty()) say("Note: " + note);
        say("TTS   : " + tm.key + " (" + tm.display + ")");
        if (!tm.langs.empty() && std::find(tm.langs.begin(), tm.langs.end(), a.target) == tm.langs.end())
            say("Note: " + tm.display + " does not list language '" + a.target +
                "'; the speech may be poor. OmniVoice covers 600+ languages.");

        // Memory: long media means large buffers (an hour of 44.1 kHz stereo
        // float is 1.3 GB), so each one is freed as soon as it has been used and
        // the original buffer is reused for the background and then the mix.
        auto release = [](std::vector<float>& v) {
            v.clear();
            v.shrink_to_fit();
        };

        // ---- 1. original audio --------------------------------------------------
        const int mix_rate = 44100;
        say("Decoding " + a.media + " ...");
        // Interleaved stereo. From step 2 on it holds the "bed" under the dub:
        // keep = the separated background (in place), duck = the original
        // (scaled when mixed), none = nothing (freed after the voice sample).
        // Spare capacity (a minute) lets it grow into the mix in step 6 - by the
        // spoken disclosure and lines running past the end - without a copy.
        std::vector<float> original = load_audio_as(a.media, mix_rate, 2, (size_t)mix_rate * 2 * 60);
        const size_t frames = original.size() / 2;
        const double total = (double)frames / mix_rate;
        {
            char b[96];
            std::snprintf(b, sizeof(b), "Audio : %s (%.2f s)", fmt_time(total).c_str(), total);
            say(b);
        }

        // ---- 2. background (and clean vocals for the voice sample) ---------------
        std::vector<float> vocals_mono;  // vocals at mix_rate
        // The separation model: loaded on first use, then kept for every later call
        // (the voice sample may need several), and closed before the speech model loads.
        std::unique_ptr<Session> separator;
        // Split `nf` frames of interleaved stereo (mix_rate) into the voice (mono,
        // nf samples into voice[]) and everything else (stereo, nf frames into
        // rest[], when given). `rest` may be the input itself. Returns false if the
        // model gave no vocals stem.
        //
        // htdemucs holds four stereo stems of its whole input, and they are copied
        // here, so an hour in one piece would need ~8 GB. The input goes through in
        // parts instead (separate_chunk_seconds), each with `context` extra on both
        // sides (the model judges each moment by what is around it), and the results
        // of neighbouring parts are crossfaded over `fade` either side of their
        // join. The extra memory is a few parts' worth however long the media is,
        // and media shorter than one part goes through in one call as before.
        //
        // Writing rest[] over the input: each part's results are only written up
        // to where the next part's input starts; the rest waits in pend_* until
        // the next part has been separated and crossfaded with it.
        auto separate = [&](const float* stereo, size_t nf, float* voice, float* rest, bool progress) {
            const size_t context = (size_t)mix_rate * 4, fade = (size_t)mix_rate;
            const auto bounds = chunk_bounds(nf, (size_t)(separate_chunk_seconds() * mix_rate));
            const size_t parts = bounds.size() - 1;
            if (progress) progress_line("separate", 0, (long long)parts, "steps");
            if (!separator) {
                const Bundle sep = ensure_bundle("htdemucs", a.models_dir);
                separator = std::make_unique<Session>(sep.primary, "htdemucs", eo);
            }
            crispasr_session* s = separator->get();
            const int sr = crispasr().crispasr_session_separate_sample_rate(s);
            const bool resampled = sr > 0 && sr != mix_rate;  // htdemucs runs at 44.1 kHz; resample otherwise
            bool found_voice = false;
            std::vector<float> pend_voice, pend_rest;  // previous part's results from this part's input start
            for (size_t k = 0; k < parts; ++k) {
                const size_t cb = bounds[k] > context ? bounds[k] - context : 0;
                const size_t ce = std::min(nf, bounds[k + 1] + context);
                const size_t cn = ce - cb;
                int n_stems = 0;
                if (resampled) {
                    std::vector<float> l(cn), r(cn);
                    for (size_t i = 0; i < cn; ++i) l[i] = stereo[2 * (cb + i)], r[i] = stereo[2 * (cb + i) + 1];
                    l = resample(l, mix_rate, sr), r = resample(r, mix_rate, sr);
                    std::vector<float> in(l.size() * 2);
                    for (size_t i = 0; i < l.size(); ++i) in[2 * i] = l[i], in[2 * i + 1] = r[i];
                    release(l), release(r);
                    if (in.size() / 2 > (size_t)INT_MAX) throw std::runtime_error("audio part too long to separate");
                    n_stems = crispasr().crispasr_session_separate(s, in.data(), (int)(in.size() / 2));
                } else {  // no copy of the input
                    if (cn > (size_t)INT_MAX) throw std::runtime_error("audio part too long to separate");
                    n_stems = crispasr().crispasr_session_separate(s, stereo + 2 * cb, (int)cn);
                }
                if (n_stems <= 0) throw std::runtime_error("separating the voice from the background failed");
                std::vector<float> v(cn, 0.0f), o(rest ? cn * 2 : 0, 0.0f);
                for (int j = 0; j < n_stems; ++j) {
                    int n = 0;
                    const float* stem = crispasr().crispasr_session_separate_stem(s, j, &n);
                    const char* stem_name = crispasr().crispasr_session_separate_stem_name(s, j);
                    const std::string name = lower(stem_name ? stem_name : "");
                    if (!stem || n <= 0) continue;
                    const bool is_voice = name.find("vocal") != std::string::npos;
                    if (!is_voice && !rest) continue;
                    std::vector<float> l((size_t)n), r((size_t)n);
                    for (int i = 0; i < n; ++i) l[i] = stem[2 * i], r[i] = stem[2 * i + 1];
                    if (resampled) l = resample(l, sr, mix_rate), r = resample(r, sr, mix_rate);
                    const size_t m = std::min(cn, l.size());
                    if (is_voice) {
                        found_voice = true;
                        for (size_t i = 0; i < m; ++i) v[i] = 0.5f * (l[i] + r[i]);
                    } else {
                        for (size_t i = 0; i < m; ++i) o[2 * i] += l[i], o[2 * i + 1] += r[i];
                    }
                }
                if (parts > 1) {
                    for (size_t i = 0; i < cn; ++i) {
                        const float w = chunk_weight(bounds, k, cb + i, fade);
                        v[i] *= w;
                        if (rest) o[2 * i] *= w, o[2 * i + 1] *= w;
                    }
                }
                for (size_t i = 0; i < pend_voice.size(); ++i) v[i] += pend_voice[i];
                for (size_t i = 0; i < pend_rest.size(); ++i) o[i] += pend_rest[i];
                // Final up to the next part's input start (parts are longer than 2 * context).
                const size_t done = k + 1 < parts ? bounds[k + 1] - context - cb : cn;
                std::copy(v.begin(), v.begin() + (long)done, voice + cb);
                pend_voice.assign(v.begin() + (long)done, v.end());
                if (rest) {
                    std::copy(o.begin(), o.begin() + (long)(2 * done), rest + 2 * cb);
                    pend_rest.assign(o.begin() + (long)(2 * done), o.end());
                }
                if (progress) progress_line("separate", (long long)k + 1, (long long)parts, "steps");
            }
            return found_voice;
        };
        float bed_gain = 1.0f;
        if (a.background == "keep") {
            say("Separating the voice from music and effects ...");
            vocals_mono.resize(frames);
            // original := background. The original is gone after this; the voice
            // sample and speech level need the vocals.
            if (!separate(original.data(), frames, vocals_mono.data(), original.data(), true))
                throw std::runtime_error("separating the voice found no vocals stem");
            separator.reset();  // the voice sample is taken from the vocals: no further use
        } else if (a.background == "duck") {
            bed_gain = 0.18f;  // about -15 dB
        }
        std::vector<float> mono;
        if (vocals_mono.empty()) {
            mono.resize(frames);
            for (size_t i = 0; i < frames; ++i) mono[i] = 0.5f * (original[2 * i] + original[2 * i + 1]);
        }
        const std::vector<float>& speech_src = vocals_mono.empty() ? mono : vocals_mono;
        // Level of the original speech, to match the dub to it.
        double speech_level = 0;
        {
            double sum = 0, n = 0;
            for (const auto& c : source) {
                const size_t b = std::min(frames, (size_t)std::max(0.0, c.start * mix_rate));
                const size_t e = std::min(frames, (size_t)std::max(0.0, c.end * mix_rate));
                if (e > b) {
                    const double r = active_rms(speech_src.data() + b, e - b, mix_rate);
                    sum += r * r * (e - b), n += (double)(e - b);
                }
            }
            speech_level = n > 0 ? std::sqrt(sum / n) : active_rms(speech_src.data(), speech_src.size(), mix_rate);
            if (speech_level < 1e-3) speech_level = 0.1;
        }

        // ---- 3. voice samples ---------------------------------------------------------
        // Taken before the speech model is loaded, so the separation model used to
        // clean them is gone by then and the large buffers above can be freed.
        const bool want_clone = a.clone && a.consent;
        if (a.clone && !a.consent)
            say(multi ? "Note: not cloning the speakers' voices: confirm with --i-have-rights that every speaker "
                        "agreed. Using a built-in voice."
                      : "Note: not cloning the speaker's voice: confirm with --i-have-rights that the speaker agreed. "
                        "Using a built-in voice.");
        // One voice per speaker (--speakers), else one voice for every line.
        struct Voice {
            int speaker = -1;
            std::string name;  // "Speaker 1" (several speakers), else empty
            bool clone = false;
            std::vector<float> ref;  // the voice sample, 24 kHz mono
            std::string ref_text;
            std::vector<double> want_tone;  // the speaker's tone, for matching (step 6)
            fs::path wav;                   // the sample handed to the speech model
        };
        std::vector<Voice> voices;
        if (multi) {
            for (const auto& s : speakers.speakers)
                for (const auto& l : lines)
                    if (l.speaker == s.id) {
                        Voice v;
                        v.speaker = s.id;
                        v.name = s.name;
                        voices.push_back(std::move(v));
                        break;
                    }
        }
        if (voices.empty()) voices.emplace_back();
        // "Speaker 1: " in front of what is said about one voice of several.
        auto who = [&](const Voice& v) { return v.name.empty() ? std::string() : v.name + ": "; };
        // With a native accent only the sound of the voice is taken from the sample
        // (Qwen3-TTS: a speaker embedding; CosyVoice3: its speaker embedding plus 10 s
        // for the timbre). Those get better with more speech, so they get ~30 s from
        // several stretches. Copying the way of speaking (original accent, OmniVoice)
        // needs one stretch with an exact transcript, and long ones leak into the output.
        const bool native_accent = (a.source == "auto" || a.source != a.target) && !a.keep_accent;
        const double sample_seconds =
            native_accent && (tm.key == "qwen3-tts" || tm.key == "cosyvoice3") ? a.sample_seconds : 0.0;
        const std::vector<double> tone_centres = {125, 250, 500, 1000, 2000, 4000, 8000};
        if (want_clone) {
            const size_t min_ref = 24000;  // 1 s: too little for any model to take a voice from
            if (!a.voice_file.empty()) {
                Voice& v = voices[0];
                v.ref = load_audio_as(a.voice_file, 24000, 1);
                v.ref_text = a.voice_text;
                if (v.ref.size() > 24000 * 20) v.ref.resize(24000 * 20);
                if (v.ref.size() < min_ref)
                    throw std::runtime_error("the voice file " + a.voice_file + " is shorter than 1 s");
                const std::vector<float> tone_ref = resample(v.ref, 24000, mix_rate);
                if (a.tone_match && tone_ref.size() > (size_t)mix_rate)
                    v.want_tone = band_levels_db(tone_ref, mix_rate, tone_centres);
                v.clone = true;
                say("Voice : cloned from " + a.voice_file);
            } else {
                // Without the original subtitles, the translated ones still give the
                // timing of the speech, but not what was said (cloned from audio only).
                // Only cues inside the media count (subtitles for a longer cut of the
                // video may run past its end).
                const bool have_source = !source.empty();
                std::vector<SubCue> timing;
                auto add = [&](double s0, double e0, const std::string& text, int speaker) {
                    if (s0 >= 0 && s0 < total && e0 > s0) timing.push_back({s0, std::min(e0, total), text, speaker});
                };
                if (have_source)
                    for (const auto& c : source) add(c.start, c.end, c.text, c.speaker);
                else
                    for (const auto& l : lines) add(l.start, l.end, l.text, l.speaker);
                bool cleaning_failed = false;  // e.g. no download or GPU: would fail for every stretch
                for (Voice& v : voices) {
                    const auto clips_ref = choose_references(timing, sample_seconds, v.speaker);
                    if (clips_ref.empty()) {
                        if (!multi)
                            throw std::runtime_error("found no 3-14 s stretch of speech to clone the voice from (use "
                                                     "--voice-file)");
                        say("Voice : " + who(v) + "built-in voice of " + tm.display +
                            " (too little clear speech to clone: no 3-14 s stretch)");
                        continue;
                    }
                    std::vector<float> sample;
                    const std::vector<float> gap((size_t)(mix_rate * 0.3), 0.0f);
                    double got_seconds = 0;
                    bool all_cleaned = true;
                    std::string where;
                    for (const auto& rc : clips_ref) {
                        const size_t e = std::min(frames, (size_t)((rc.end + 0.15) * mix_rate));
                        const size_t b = std::min(e, (size_t)std::max(0.0, (rc.start - 0.1) * mix_rate));
                        if (e - b < (size_t)mix_rate) continue;  // under 1 s: subtitles don't match the media here
                        std::vector<float> part(speech_src.begin() + (long)b, speech_src.begin() + (long)e);
                        bool cleaned = !vocals_mono.empty();
                        if (!cleaned && !cleaning_failed) {
                            // A voice clone copies everything in the sample - hiss, hum, room,
                            // music - and adds it to every line. Isolate the voice in the sample
                            // (a few short stretches, so this is quick) even when the background
                            // isn't kept.
                            try {
                                const size_t pad = (size_t)mix_rate;  // 1 s context on each side
                                const size_t sb = b > pad ? b - pad : 0, se = std::min(frames, e + pad);
                                std::vector<float> vv(se - sb);
                                if (separate(original.data() + 2 * sb, se - sb, vv.data(), nullptr, false)) {
                                    part.assign(vv.begin() + (long)(b - sb), vv.begin() + (long)(e - sb));
                                    cleaned = true;
                                }
                            } catch (const std::exception& ex) {
                                cleaning_failed = true;  // don't try (and download or load) again for the next stretch
                                say(std::string("Note: could not clean the voice sample (") + ex.what() + "); using it as is");
                            }
                        }
                        all_cleaned = all_cleaned && cleaned;
                        high_pass(part, mix_rate, 70.0);  // rumble and hum
                        fade_edges(part, mix_rate);
                        if (!sample.empty()) sample.insert(sample.end(), gap.begin(), gap.end());
                        sample.insert(sample.end(), part.begin(), part.end());
                        v.ref_text += (v.ref_text.empty() ? "" : " ") + rc.text;
                        got_seconds += rc.end - rc.start;
                        where += (where.empty() ? "" : ", ") + fmt_time(rc.start);
                    }
                    if (sample.size() < (size_t)mix_rate) {
                        if (!multi)
                            throw std::runtime_error("the voice sample is shorter than 1 s (do the subtitles match the media?)");
                        say("Voice : " + who(v) + "built-in voice of " + tm.display + " (too little clear speech to clone)");
                        v.ref_text.clear();
                        continue;
                    }
                    if (!have_source) v.ref_text.clear();
                    // The speaker's tone for step 6, measured now so the sample needn't be
                    // kept through the speech synthesis.
                    if (a.tone_match && sample.size() > (size_t)mix_rate)
                        v.want_tone = band_levels_db(sample, mix_rate, tone_centres);
                    v.ref = resample(sample, mix_rate, 24000);
                    v.clone = true;
                    release(sample);
                    char buf[320];
                    std::snprintf(buf, sizeof(buf), "cloned from %.1f s of speech (%zu stretch%s, at %s)%s", got_seconds,
                                  clips_ref.size(), clips_ref.size() == 1 ? "" : "es", where.c_str(),
                                  all_cleaned ? ", background and noise removed" : "");
                    say("Voice : " + who(v) + buf);
                }
            }
            for (Voice& v : voices) {
                if (!v.clone) continue;
                float peak = 0.0f;
                for (float x : v.ref) peak = std::max(peak, std::fabs(x));
                const double scale = 0.9 / std::max(1e-6, (double)peak);
                for (float& x : v.ref) x = (float)(x * scale);
            }
        }
        release(mono), release(vocals_mono);
        separator.reset();
        if (a.background == "none") release(original);
        bool any_clone = false;
        for (const Voice& v : voices) any_clone = any_clone || v.clone;

        // ---- 4. TTS model and voice -------------------------------------------------
        say("Loading " + tm.display + " (first run downloads " + tm.size + ") ...");
        const Bundle tb = ensure_bundle(tm.backend, a.models_dir);
        // Closed after the last line is spoken: the model is the largest thing in
        // memory and isn't needed for mixing and encoding.
        auto tts = std::make_unique<Session>(tb.primary, tm.backend, eo);
        const CrispApi& api = crispasr();
        // qwen3-tts and OmniVoice take their audio tokenizer as the companion file;
        // CosyVoice3 finds its flow/vocoder files next to the main model itself.
        if (!tb.companion.empty() && tm.key != "cosyvoice3")
            api.crispasr_session_set_codec_path(tts->get(), tb.companion.c_str());
        api.crispasr_session_set_target_language(tts->get(), a.target.c_str());
        api.crispasr_session_set_tts_seed(tts->get(), 1234);
        const int tts_rate = api.crispasr_session_output_sample_rate(tts->get());
        if (tts_rate <= 0) throw std::runtime_error("the speech model reported no sample rate");

        // Voice order matters: the AI disclosure, and every line in a built-in
        // voice, must be synthesized in a neutral voice BEFORE a clone is
        // installed (CrispASR refuses otherwise).
        //   qwen3-tts : the Base model can't speak without a voice -> load its
        //               default voice pack first;
        //   cosyvoice3: its default bank voice (zero_shot) is used until set_voice;
        //   omnivoice : speaks in its own voice without a prompt.
        if (tm.key == "qwen3-tts") {
            for (const auto& x : tb.extras)
                if (x.find("voice-default") != std::string::npos)
                    api.crispasr_session_set_voice(tts->get(), x.c_str(), nullptr);
        }

        bool cloned = false;
        std::vector<float> disclaimer;
        // Accent. A dub is (nearly always) into another language than the one
        // spoken in the sample; cloning the sample's speech itself carries its
        // accent over. For a native accent, take only the voice:
        //   qwen3-tts : speaker embedding only (x-vector), no transcript;
        //   cosyvoice3: its cross-lingual mode (it drops the transcript and the
        //               sample's speech tokens when the languages differ).
        const bool cross = a.source == "auto" || a.source != a.target;
        const bool native = cross && !a.keep_accent;
        if (any_clone) {
            int n = 0;
            if (float* d = api.crispasr_session_get_disclaimer_pcm(tts->get(), &n)) {
                if (n > 0) disclaimer = resample(std::vector<float>(d, d + n), tts_rate, mix_rate);
                api.crispasr_pcm_free(d);
            }
            if (tm.key == "cosyvoice3") {
                if (a.keep_accent) api.crispasr_session_set_tts_reference_language(tts->get(), a.target.c_str());
                else if (a.source != "auto") api.crispasr_session_set_tts_reference_language(tts->get(), a.source.c_str());
                // (source unknown: CrispASR identifies the sample's language from its transcript)
            } else if (a.source != "auto") {
                api.crispasr_session_set_tts_reference_language(tts->get(), a.source.c_str());
            }
            api.crispasr_session_accept_marking_responsibility(
                tts->get(), multi ? "transcribe_multi dub: output is watermarked and carries an AI disclosure; "
                                    "the user confirmed every speaker's consent (--i-have-rights)"
                                  : "transcribe_multi dub: output is watermarked and carries an AI disclosure; "
                                    "the user confirmed the speaker's consent (--i-have-rights)");
            // Each sample is handed over as a WAV file that stays until the end of
            // the dub: CosyVoice3 reads it again when it speaks the first line
            // (crispasr_session_set_voice_samples deletes its own temporary copy
            // straight away, so CosyVoice3 could never find it).
            for (size_t k = 0; k < voices.size(); ++k) {
                Voice& v = voices[k];
                if (!v.clone) continue;
                v.wav = multi ? output.voice(k) : output.voice();
                write_wav(path_to_utf8(v.wav), v.ref, 24000, 1);
                if (!a.debug_dir.empty()) {
                    const std::string name = multi ? "voice_sample_" + std::to_string(v.speaker + 1) + ".wav" : "voice_sample.wav";
                    try {
                        write_wav(path_to_utf8(path_from_utf8(a.debug_dir) / name), v.ref, 24000, 1);
                    } catch (const std::exception& e) {
                        std::fprintf(stderr, "Note: %s\n", e.what());
                    }
                }
                release(v.ref);
            }
        }
        if (!any_clone)
            say(multi && voices.size() > 1 ? "Voice : built-in voice of " + tm.display + " for every speaker"
                                           : "Voice : built-in voice of " + tm.display);
        // Install a speaker's cloned voice (the accent notes are said once).
        bool accent_said = false;
        auto install = [&](const Voice& v) {
            auto set_ref = [&](const char* text) {
                return api.crispasr_session_set_voice(tts->get(), path_to_utf8(v.wav).c_str(), text);
            };
            const bool say_it = !accent_said;
            accent_said = true;
            int rc = -1;
            if (native && tm.key == "qwen3-tts") {
                rc = set_ref(nullptr);
                if (!say_it) {
                } else if (rc == 0) {
                    say("Accent: native (only the voice is cloned, not the way it speaks)");
                } else {
                    say("Note: this CrispASR build can't clone the voice without its accent; the original accent "
                        "may carry over (CosyVoice3 avoids this).");
                }
            }
            if (rc != 0) {
                if (!say_it) {
                } else if (native && tm.key == "cosyvoice3") {
                    say("Accent: native (cross-language cloning)");
                } else if (native && tm.key == "omnivoice") {
                    say("Note: OmniVoice keeps some of the original accent; Qwen3-TTS or CosyVoice3 give a native accent.");
                } else if (!native && cross) {
                    say("Accent: the speaker's original accent is kept");
                }
                rc = set_ref(v.ref_text.empty() ? nullptr : v.ref_text.c_str());
            }
            if (rc != 0)
                throw std::runtime_error("the speech model could not use the voice sample" +
                                         (v.name.empty() ? std::string() : " of " + v.name) + " (code " +
                                         std::to_string(rc) + ")");
            if (!cloned)
                std::fprintf(stderr, "[CONSENT] voice clone made with the user's confirmation (--i-have-rights)\n");
            cloned = true;
        };

        // ---- 5. speak every line ------------------------------------------------------
        say("Speaking " + std::to_string(lines.size()) + " lines" +
            (voices.size() > 1 ? " in " + std::to_string(voices.size()) + " voices" : std::string()) + " ...");
        std::vector<std::vector<float>> clips(lines.size());
        std::vector<double> lengths(lines.size());
        size_t failed = 0;
        // Trim leading/trailing near-silence so the timing is accurate.
        auto trim = [&](std::vector<float> c) {
            size_t b = 0, e = c.size();
            const float thr = 0.01f;
            while (b < e && std::fabs(c[b]) < thr) ++b;
            while (e > b && std::fabs(c[e - 1]) < thr) --e;
            if (b == e) return std::vector<float>();  // nothing but silence: a failed take
            b = b > (size_t)tts_rate / 50 ? b - tts_rate / 50 : 0;
            e = std::min(c.size(), e + tts_rate / 20);
            return std::vector<float>(c.begin() + (long)b, c.begin() + (long)e);
        };
        // One call of the speech model with retries: a failed or runaway synthesis is
        // tried again with other random seeds. Returns empty audio (and *problem) on failure.
        std::string debug_index =  // --debug-dir: lines.txt lists the takes
            "file\toriginal time\tlength\tresult\ttext\n";
        auto speak = [&](const std::string& text, size_t line, size_t part, std::string* problem) {
            std::vector<float> c;
            const double limit = max_plausible_seconds(text);
            for (int attempt = 0; attempt < 3 && c.empty(); ++attempt) {
                if (attempt > 0)
                    api.crispasr_session_set_tts_seed(tts->get(), 1234 + 7919ull * (line + 1) + 131ull * part + attempt);
                try {
                    std::vector<float> raw_clip = tts->synthesize(text);
                    char name[64] = "";
                    if (!a.debug_dir.empty()) {
                        std::snprintf(name, sizeof(name), "line_%03zu_part%zu_try%d.wav", line + 1, part + 1,
                                      attempt + 1);
                        try {  // diagnostics only: never let them fail the dub
                            write_wav(path_to_utf8(path_from_utf8(a.debug_dir) / name), raw_clip, tts_rate, 1);
                        } catch (const std::exception& e) {
                            std::fprintf(stderr, "Note: %s\n", e.what());
                        }
                    }
                    std::vector<float> got = trim(std::move(raw_clip));
                    bool finite = true;
                    for (float v : got)
                        if (!std::isfinite(v)) {
                            finite = false;
                            break;
                        }
                    const double secs = (double)got.size() / tts_rate;
                    if (got.empty()) {
                        *problem = "the model produced only silence";
                    } else if (!finite) {
                        *problem = "the model produced invalid audio";
                    } else if (secs > limit) {
                        char m[160];
                        size_t chars = 0;
                        for (unsigned char ch : text) chars += (ch & 0xC0) != 0x80;
                        std::snprintf(m, sizeof(m), "the model ran away (%.1f s of audio for %zu characters)", secs,
                                      chars);
                        *problem = m;
                    } else {
                        c = std::move(got);
                    }
                    if (name[0]) {
                        // Which file is which: the line's place in the original, what was
                        // spoken and whether the take was used.
                        char head[160];
                        std::snprintf(head, sizeof(head), "%s\t%s-%s\t%.1f s\t%s\t", name,
                                      fmt_time(lines[line].start).c_str(), fmt_time(lines[line].end).c_str(), secs,
                                      c.empty() ? "REJECTED" : "used");
                        debug_index += head + text + "\n";
                        try {
                            pk::write_file_atomic(path_from_utf8(a.debug_dir) / "lines.txt", debug_index);
                        } catch (const std::exception& e) {
                            std::fprintf(stderr, "Note: %s\n", e.what());
                        }
                    }
                } catch (const std::exception& e) {
                    *problem = e.what();
                }
                if (c.empty() && attempt < 2)
                    std::fprintf(stderr, "Note: line %zu: %s - trying again\n", line + 1, problem->c_str());
            }
            api.crispasr_session_set_tts_seed(tts->get(), 1234);
            return c;
        };
        // Each line's voice, and the order to speak them in: the lines in a built-in
        // voice first (before any clone is installed), then each cloned voice's lines.
        std::vector<size_t> voice_of(lines.size(), 0);
        for (size_t i = 0; i < lines.size(); ++i)
            for (size_t k = 0; k < voices.size(); ++k)
                if (voices[k].speaker == lines[i].speaker) voice_of[i] = k;
        std::vector<size_t> order;
        for (size_t i = 0; i < lines.size(); ++i)
            if (!voices[voice_of[i]].clone) order.push_back(i);
        for (size_t k = 0; k < voices.size(); ++k)
            for (size_t i = 0; i < lines.size(); ++i)
                if (voices[k].clone && voice_of[i] == k) order.push_back(i);
        size_t beyond = 0;  // lines that start after the end of the media (subtitles of a longer cut)
        size_t done = 0, tried = 0;
        size_t installed = voices.size();  // the cloned voice the model has now (none)
        for (const size_t i : order) {
            ++done;
            if (lines[i].start >= total) {
                ++beyond;
                progress_line("speak", (long long)done, (long long)lines.size(), "lines");
                continue;  // left silent: clips[i] stays empty, lengths[i] 0
            }
            ++tried;
            if (voices[voice_of[i]].clone && installed != voice_of[i]) {
                install(voices[voice_of[i]]);
                installed = voice_of[i];
            }
            // Long lines are spoken a sentence (or clause) at a time and joined with a
            // short pause: over long stretches the speech models drift (CosyVoice3
            // slides back into the reference's accent, and the audio can break up).
            // CosyVoice3 gets shorter parts: it drifted in a 140-character (11 s)
            // part, longer than the 10 s of the voice sample it builds on.
            std::vector<float> c;
            std::string problem;
            const auto parts = split_for_speech(lines[i].text, tm.key == "cosyvoice3" ? 90 : 140);
            const std::vector<float> pause((size_t)(tts_rate * 0.15), 0.0f);
            size_t spoken = 0;
            for (size_t k = 0; k < parts.size(); ++k) {
                std::vector<float> piece = speak(parts[k], i, k, &problem);
                if (piece.empty()) continue;
                if (!c.empty()) c.insert(c.end(), pause.begin(), pause.end());
                c.insert(c.end(), piece.begin(), piece.end());
                ++spoken;
            }
            if (!c.empty() && spoken < parts.size())
                std::fprintf(stderr, "Warning: line %zu: %zu of %zu parts could not be spoken (%s)\n", i + 1,
                             parts.size() - spoken, parts.size(), problem.c_str());
            if (c.empty() && failed + 1 == tried && tried >= 3)
                // Nothing has worked so far: the same problem would repeat for every line.
                throw std::runtime_error("the first " + std::to_string(tried) + " lines could not be spoken (" +
                                         problem + ")");
            if (c.empty()) {
                std::fprintf(stderr, "Warning: line %zu (%s, \"%s\") was left silent: %s\n", i + 1,
                             fmt_time(lines[i].start).c_str(), lines[i].text.c_str(), problem.c_str());
                ++failed;
            }
            clips[i] = resample(c, tts_rate, mix_rate);
            lengths[i] = (double)clips[i].size() / mix_rate;
            progress_line("speak", (long long)done, (long long)lines.size(), "lines");
        }

        if (beyond == lines.size())
            throw std::runtime_error("every subtitle starts after the end of the media (do the subtitles belong to it?)");
        if (beyond)
            say("Note: " + std::to_string(beyond) + " subtitle line(s) start after the end of the media and were skipped");
        if (failed + beyond == lines.size()) throw std::runtime_error("speech synthesis failed for every line");
        if (failed) say("Warning: " + std::to_string(failed) + " line(s) could not be spoken and were left silent");
        tts.reset();

        // ---- 6. fit to the timing and mix -----------------------------------------------
        const auto place = place_lines(lines, lengths, a.max_speed, total);
        int sped = 0, late = 0;
        double max_used = 1.0;
        double offset = 0;  // disclosure prepended to audio-only output shifts everything
        bool disclosure_spoken = false;
        if (cloned && !disclaimer.empty()) {
            const double dlen = (double)disclaimer.size() / mix_rate + 0.4;
            if (!video_out) {
                offset = dlen;
                disclosure_spoken = true;
            } else if (place.front().at >= dlen) {
                disclosure_spoken = true;  // fits before the first line: no shift needed
            }
        }
        // The output runs to the end of the media or of the last spoken line, whichever
        // is later (lines past the end of the media were skipped above).
        double last_end = 0;
        for (size_t i = 0; i < place.size(); ++i)
            if (lengths[i] > 0) last_end = std::max(last_end, place[i].at + lengths[i] / place[i].speed);
        const size_t out_frames = frames + (size_t)(offset * mix_rate) +
                                  (size_t)(std::max(0.0, last_end - total) * mix_rate) + mix_rate / 2;
        // The bed under the dub (background / ducked original) becomes the mix in
        // its own buffer: grown (into the capacity reserved when decoding, so
        // normally without a copy), moved right by the offset and scaled in place.
        std::vector<float> mix = std::move(original);
        release(original);
        {
            const size_t bed = mix.size(), shift = (size_t)(offset * mix_rate) * 2;
            mix.resize(out_frames * 2, 0.0f);
            if (bed > 0 && shift > 0) {
                std::memmove(mix.data() + shift, mix.data(), bed * sizeof(float));
                std::fill(mix.begin(), mix.begin() + (long)shift, 0.0f);
            }
            if (bed_gain != 1.0f)
                for (size_t i = shift; i < shift + bed; ++i) mix[i] *= bed_gain;
        }
        // Tone: nudge the dub's spectral balance (bass, warmth, presence, air) toward
        // the speaker's own voice in the sample. The models output a clean
        // "studio" voice; matching the tone of the original recording is much of
        // what makes the dub sound like the same person in the same room.
        // Each cloned voice is matched to its own speaker.
        for (size_t v = 0; v < voices.size(); ++v) {
            if (!voices[v].clone || voices[v].want_tone.empty()) continue;
            const std::vector<double>& centres = tone_centres;
            const auto& want_lv = voices[v].want_tone;
            std::vector<float> probe;  // up to ~2 min of the voice's lines, for measuring
            for (size_t i = 0; i < clips.size(); ++i) {
                if (probe.size() > (size_t)mix_rate * 120) break;
                if (voice_of[i] == v) probe.insert(probe.end(), clips[i].begin(), clips[i].end());
            }
            std::vector<double> eq(centres.size(), 0.0);
            for (int pass = 0; pass < 2 && !probe.empty(); ++pass) {  // 2nd pass corrects band overlap
                std::vector<float> trial = probe;
                apply_eq(trial, mix_rate, centres, eq);
                const auto g = tone_match_gains(want_lv, band_levels_db(trial, mix_rate, centres));
                for (size_t k = 0; k < eq.size(); ++k) eq[k] = std::clamp(eq[k] + g[k], -6.0, 6.0);
            }
            for (size_t i = 0; i < clips.size(); ++i)
                if (voice_of[i] == v) apply_eq(clips[i], mix_rate, centres, eq);
            std::string desc;
            for (size_t k = 0; k < centres.size(); ++k) {
                char b[40];
                std::snprintf(b, sizeof(b), "%s%g%s %+.1f", desc.empty() ? "" : ", ",
                              centres[k] >= 1000 ? centres[k] / 1000 : centres[k], centres[k] >= 1000 ? "k" : "", eq[k]);
                desc += b;
            }
            say("Tone  : " + who(voices[v]) + "matched to the speaker's voice (dB: " + desc + ")");
        }
        // Dub level: match the original speech level.
        double dub_level = 0, dsum = 0, dn = 0;
        for (const auto& c : clips) {
            const double r = active_rms(c.data(), c.size(), mix_rate);
            dsum += r * r * c.size(), dn += (double)c.size();
        }
        dub_level = dn > 0 ? std::sqrt(dsum / dn) : 0.1;
        // Peak of the voice (99.9th percentile of |sample|), so the gain leaves
        // headroom and the limiter only has to catch the odd peak.
        double voice_peak = 0;
        {
            std::vector<float> mags;
            for (const auto& c : clips)
                for (size_t k = 0; k < c.size(); k += 4) mags.push_back(std::fabs(c[k]));
            if (!mags.empty()) {
                const size_t q = (size_t)(mags.size() * 0.999);
                std::nth_element(mags.begin(), mags.begin() + (long)std::min(q, mags.size() - 1), mags.end());
                voice_peak = mags[std::min(q, mags.size() - 1)];
            }
        }
        double want = std::clamp(speech_level / std::max(1e-4, dub_level), 0.25, 4.0);
        if (voice_peak > 0) want = std::min(want, 0.8 / voice_peak);
        const float gain = (float)want;
        if (disclosure_spoken) {
            std::vector<float> d = disclaimer;
            fade_edges(d, mix_rate);
            for (size_t i = 0; i < d.size(); ++i) {
                mix[2 * i] += d[i] * gain;
                mix[2 * i + 1] += d[i] * gain;
            }
        }
        std::vector<double> placed_lengths(lines.size());
        for (size_t i = 0; i < lines.size(); ++i) {
            // Each clip is moved (or stretched) into place and freed once mixed.
            std::vector<float> c;
            if (place[i].speed > 1.001) c = time_stretch(clips[i], mix_rate, place[i].speed);
            else c.swap(clips[i]);
            release(clips[i]);
            placed_lengths[i] = (double)c.size() / mix_rate;
            fade_edges(c, mix_rate);
            if (place[i].speed > 1.001) ++sped, max_used = std::max(max_used, place[i].speed);
            if (place[i].overrun > 0.25) ++late;
            const size_t at = (size_t)((place[i].at + offset) * mix_rate);
            for (size_t k = 0; k < c.size() && at + k < out_frames; ++k) {
                mix[2 * (at + k)] += c[k] * gain;
                mix[2 * (at + k) + 1] += c[k] * gain;
            }
        }
        clips.clear();
        clips.shrink_to_fit();
        size_t over = 0;
        for (float v : mix) over += std::fabs(v) > 0.8f;
        limit_peaks(mix, mix_rate, 2);
        {
            char b[200];
            std::snprintf(b, sizeof(b), "Level : voice gain %.2fx; peaks limited on %.2f%% of samples", gain,
                          mix.empty() ? 0.0 : 100.0 * (double)over / (double)mix.size());
            say(b);
        }
        {
            char b[200];
            std::snprintf(b, sizeof(b), "Timing: %d of %zu lines sped up (up to %.2fx); %d ran past their slot", sped,
                          lines.size(), max_used, late);
            say(b);
        }

        // ---- 7. write -------------------------------------------------------------------
        const fs::path out = path_from_utf8(a.output);
        if (out.has_parent_path()) fs::create_directories(out.parent_path());
        const fs::path wav = wav_out ? output.rendered() : output.audio();
        write_wav(path_to_utf8(wav), mix, mix_rate, 2);
        release(mix);
        int rc = 0;
        std::string ffmsg;
        const char* disclosure_text = api.crispasr_session_disclaimer_text();
        const std::string disclosure = disclosure_text ? disclosure_text : "";
        if (video_out) {
            const std::string subs = path_to_utf8(output.subtitles());
            const auto timed = retime_cues(target_cues, lines, place, placed_lengths, offset);
            // Several speakers: a subtitle that was split by speaker is shown as one
            // again, with a dash before each speaker's part.
            std::vector<pk::SrtCue> track;
            std::vector<pk::CueSpeaker> owner;
            for (const auto& c : timed) {
                pk::SrtCue s;
                s.start = c.start, s.end = c.end, s.text = c.text, s.timed = true;
                track.push_back(std::move(s));
                owner.push_back({c.speaker, c.group});
            }
            if (multi) track = pk::join_speaker_parts(track, owner);
            std::vector<Segment> subtitles;
            for (const auto& c : track) subtitles.push_back({c.start, c.end, c.text, {}});
            if (cloned && !disclosure_spoken && !subtitles.empty())
                subtitles.front().text = "[" + disclosure + "]\n" + subtitles.front().text;
            pk::write_file_atomic(output.subtitles(), format_srt(subtitles));
            auto mux = [&](const std::vector<std::string>& video_codec) {
                std::vector<std::string> args = {"-y", "-i", a.media, "-i", path_to_utf8(wav), "-i", subs,
                                                 "-map", "0:v?", "-map", "1:a", "-map", "2:s", "-c:v"};
                args.insert(args.end(), video_codec.begin(), video_codec.end());
                args.insert(args.end(), {"-c:a", "aac", "-b:a", "192k", "-c:s", out_ext == ".mkv" ? "srt" : "mov_text",
                                         "-metadata:s:a:0", "language=" + a.target, "-metadata:s:s:0",
                                         "language=" + a.target, "-metadata", "comment=" + disclosure,
                                         path_to_utf8(output.rendered())});
                return run_ffmpeg(args, &ffmsg);
            };
            rc = mux({"copy"});
            if (rc != 0) {
                // Copying the video keeps its quality and is fast, but not every codec
                // fits every container (e.g. WMV or old AVI/DivX video into MP4).
                // Re-encode the video to H.264 then; the dub itself is ready.
                if (!ffmsg.empty()) std::fprintf(stderr, "ffmpeg: %s\n", ffmsg.c_str());
                say("Note: the video can't be copied into " + out_ext + " as it is; re-encoding it with H.264 ...");
                rc = mux({"libx264", "-preset", "veryfast", "-crf", "20", "-pix_fmt", "yuv420p"});
                if (rc == 0) say("Video : re-encoded with H.264 (the original codec doesn't fit " + out_ext + ")");
                else say("Note: re-encoding the video failed too");
            }
        } else if (!wav_out) {
            std::vector<std::string> args = {"-y", "-i", path_to_utf8(wav), "-metadata", "comment=" + disclosure};
            if (out_ext == ".mp3") args.insert(args.end(), {"-c:a", "libmp3lame", "-q:a", "2"});
            else if (out_ext == ".m4a") args.insert(args.end(), {"-c:a", "aac", "-b:a", "192k"});
            args.push_back(path_to_utf8(output.rendered()));
            rc = run_ffmpeg(args, &ffmsg);
        }
        if (rc != 0) {
            // Don't lose the dub: keep the mix as a WAV next to the requested file.
            fs::path keep;
            try { keep = output.save_fallback(); } catch (...) { /* Report the original ffmpeg failure. */ }
            if (!ffmsg.empty()) std::fprintf(stderr, "ffmpeg: %s\n", ffmsg.c_str());
            std::string last = ffmsg.substr(ffmsg.find_last_of('\n') == std::string::npos ? 0 : ffmsg.find_last_of('\n') + 1);
            throw std::runtime_error("ffmpeg could not write " + a.output + (last.empty() ? "" : " (" + last + ")") +
                                     (keep.empty() ? "" : "; the dub was saved as " + path_to_utf8(keep) + " instead"));
        }

        output.commit();

        if (cloned)
            say(disclosure_spoken ? "Disclosure: spoken at the start (\"" + disclosure + "\"); audio is watermarked"
                                  : "Disclosure: included in the first subtitle of the video; audio is watermarked");
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        char b[96];
        std::snprintf(b, sizeof(b), "Dubbed in %.1f s", secs);
        say(b);
        say("  wrote " + path_to_utf8(fs::absolute(out)));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Who speaks when ("transcribe_multi speakers")
// ---------------------------------------------------------------------------

namespace {

void speakers_usage() {
    std::printf(
        "usage: transcribe_multi speakers MEDIA --srt SUBTITLES.srt -o SPEAKERS.txt [options]\n"
        "\n"
        "Find who speaks when (speaker detection with WeSpeaker + clustering, any\n"
        "language) and give each subtitle its speaker. Writes SPEAKERS.txt for\n"
        "\"transcribe_multi dub --speakers\", which then gives each speaker their own voice.\n"
        "\n"
        "options:\n"
        "      --srt FILE            the subtitles of MEDIA, in the spoken language (required)\n"
        "  -o, --output FILE         the speakers file to write (required)\n"
        "      --split-srt FILE      also write the subtitles with each one that has a\n"
        "                            speaker change split into one subtitle per speaker\n"
        "                            (the speakers file lists those; use them for the dub)\n"
        "      --num-speakers N      how many people speak (default: detect, up to 8)\n"
        "      --samples-dir DIR     write a short sample of each speaker, speaker_1.wav ...\n"
        "      --threads N, --models-dir DIR\n");
}

}  // namespace

int speakers_main(const std::vector<std::string>& av) {
    std::string media, srt, output, split_srt, samples_dir, models_dir;
    int num_speakers = 0, threads = 0;
    auto die = [](const std::string& m) {
        std::fprintf(stderr, "Error: %s\n", m.c_str());
        std::exit(2);
    };
    auto need = [&](size_t& i) -> std::string {
        if (i + 1 >= av.size()) die("missing value for " + av[i]);
        return av[++i];
    };
    auto whole = [&](const std::string& opt, const std::string& v, int lo, int hi) {
        char* end = nullptr;
        const long x = std::strtol(v.c_str(), &end, 10);
        if (v.empty() || *end || x < lo || x > hi)
            die(opt + " needs a whole number from " + std::to_string(lo) + " to " + std::to_string(hi) + ", not '" + v + "'");
        return (int)x;
    };
    for (size_t i = 1; i < av.size(); ++i) {
        const std::string& o = av[i];
        if (o == "-h" || o == "--help") speakers_usage(), std::exit(0);
        else if (o == "--srt") srt = need(i);
        else if (o == "-o" || o == "--output") output = need(i);
        else if (o == "--split-srt") split_srt = need(i);
        else if (o == "--num-speakers") num_speakers = whole(o, need(i), 0, 8);
        else if (o == "--samples-dir") samples_dir = need(i);
        else if (o == "--models-dir") models_dir = need(i);
        else if (o == "--threads") threads = whole(o, need(i), 0, 1024);
        else if (o == "--device") need(i);  // accepted for symmetry: speaker detection runs on the processor
        else if (!o.empty() && o[0] == '-') die("unknown option " + o + " (see speakers --help)");
        else if (media.empty()) media = o;
        else die("unexpected argument " + o);
    }
    if (media.empty() || srt.empty() || output.empty()) speakers_usage(), std::exit(2);

    const auto t0 = std::chrono::steady_clock::now();
    try {
        std::vector<pk::SrtCue> cues;
        for (auto& c : pk::parse_srt(read_text_file(srt)))
            if (c.timed && !normalize_space(pk::strip_srt_tags(c.text)).empty()) cues.push_back(std::move(c));
        if (cues.empty()) throw std::runtime_error("no subtitles in " + srt);
        std::stable_sort(cues.begin(), cues.end(), [](const pk::SrtCue& a, const pk::SrtCue& b) { return a.start < b.start; });

        const auto detect = crispasr_optional().crispasr_diarize_segments_turns_abi;
        if (!detect) throw std::runtime_error("this CrispASR has no speaker detection (it needs version 0.8.30 or later)");

        say("Decoding " + media + " ...");
        const std::vector<float> pcm = load_audio_as(media, 16000, 1);
        if (pcm.size() > (size_t)INT32_MAX) throw std::runtime_error("the audio is too long for speaker detection");
        const double total = (double)pcm.size() / 16000;

        say("Loading the speaker model (first run downloads about 24 MB) ...");
        const Bundle emb = ensure_bundle("wespeaker", models_dir);

        say("Finding who speaks when ...");
        std::vector<crispasr_diarize_seg_abi> segs(cues.size());
        for (size_t i = 0; i < cues.size(); ++i) {
            segs[i].t0_cs = (int64_t)std::llround(cues[i].start * 100);
            segs[i].t1_cs = (int64_t)std::llround(std::max(cues[i].start, cues[i].end) * 100);
            segs[i].speaker = -1;
        }
        crispasr_diarize_opts_abi opts{};
        opts.method = 4;  // FoxNose: WeSpeaker embeddings, speaker counting, clustering
        opts.n_threads = threads > 0 ? threads : default_threads();
        opts.foxnose_embedder_path = emb.primary.c_str();
        opts.min_speakers = 1;
        opts.max_speakers = 8;
        opts.num_speakers = num_speakers;
        std::vector<crispasr_diarize_turn_abi> turns_abi(4096);
        int32_t n_turns = 0;
        int rc = detect(pcm.data(), nullptr, (int32_t)pcm.size(), 0, segs.data(), (int32_t)segs.size(), &opts,
                        turns_abi.data(), (int32_t)turns_abi.size(), &n_turns);
        if (rc == 2) {  // more turns than room: again with enough
            turns_abi.resize((size_t)n_turns);
            rc = detect(pcm.data(), nullptr, (int32_t)pcm.size(), 0, segs.data(), (int32_t)segs.size(), &opts,
                        turns_abi.data(), (int32_t)turns_abi.size(), &n_turns);
        }
        if (rc != 0) throw std::runtime_error("speaker detection failed (code " + std::to_string(rc) + ")");
        turns_abi.resize(std::min(turns_abi.size(), (size_t)std::max(0, n_turns)));

        std::vector<SpeakerTurn> turns;
        for (const auto& t : turns_abi)
            if (t.t1_cs > t.t0_cs) turns.push_back({t.t0_cs / 100.0, t.t1_cs / 100.0, t.speaker});
        std::vector<int> labels;
        for (const auto& s : segs) labels.push_back(s.speaker);
        auto parts = split_by_speaker(cues, labels, turns);

        // Speakers numbered in the order they first speak in the subtitles.
        std::map<int, int> number;
        for (const auto& p : parts)
            if (p.speaker >= 0 && !number.count(p.speaker)) {
                const int n = (int)number.size();
                number[p.speaker] = n;
            }
        if (number.empty()) number[-1] = 0;  // nobody found: one speaker
        for (auto& p : parts) p.speaker = number.count(p.speaker) ? number[p.speaker] : 0;
        pk::SpeakerMap map;
        for (size_t k = 0; k < number.size(); ++k) {
            pk::SpeakerInfo s;
            s.id = (int)k;
            s.name = "Speaker " + std::to_string(k + 1);
            map.speakers.push_back(s);
        }
        // Speech time from the turns (from the subtitles for a speaker without any),
        // and the longest stretch (up to 8 s from its middle) as a sample to listen to.
        for (auto& s : map.speakers) {
            double longest = 0;
            for (const auto& t : turns) {
                const auto it = number.find(t.speaker);
                if (it == number.end() || it->second != s.id) continue;
                s.seconds += t.end - t.start;
                if (t.end - t.start > longest) longest = t.end - t.start, s.sample_start = t.start, s.sample_end = t.end;
            }
            if (longest == 0)
                for (const auto& p : parts)
                    if (p.speaker == s.id) {
                        s.seconds += p.end - p.start;
                        if (p.end - p.start > longest) longest = p.end - p.start, s.sample_start = p.start, s.sample_end = p.end;
                    }
            if (s.sample_end - s.sample_start > 8) {
                const double mid = 0.5 * (s.sample_start + s.sample_end);
                s.sample_start = mid - 4, s.sample_end = mid + 4;
            }
            s.sample_start = std::clamp(s.sample_start, 0.0, total);
            s.sample_end = std::clamp(s.sample_end, s.sample_start, total);
        }
        for (const auto& p : parts) map.cues.push_back({p.start, p.end, p.speaker, p.group});

        if (!samples_dir.empty()) {
            fs::create_directories(path_from_utf8(samples_dir));
            for (const auto& s : map.speakers) {
                const size_t b = (size_t)(s.sample_start * 16000), e = (size_t)(s.sample_end * 16000);
                if (e <= b || e > pcm.size()) continue;
                std::vector<float> clip(pcm.begin() + (long)b, pcm.begin() + (long)e);
                float peak = 0;
                for (float v : clip) peak = std::max(peak, std::fabs(v));
                if (peak > 1e-4f)
                    for (float& v : clip) v *= 0.7f / peak;
                fade_edges(clip, 16000);
                write_wav(path_to_utf8(path_from_utf8(samples_dir) / ("speaker_" + std::to_string(s.id + 1) + ".wav")),
                          clip, 16000, 1);
            }
        }
        if (!split_srt.empty()) {
            std::vector<pk::SrtCue> out;
            for (const auto& p : parts) {
                pk::SrtCue c;
                c.index = std::to_string(out.size() + 1);
                c.start = p.start, c.end = p.end, c.timed = true;
                c.timing = pk::srt_timing(p.start, p.end);
                c.text = p.text;
                out.push_back(std::move(c));
            }
            pk::write_file_atomic(path_from_utf8(split_srt), pk::format_srt(out));
        }
        pk::write_file_atomic(path_from_utf8(output), pk::format_speakers(map));

        size_t split = 0;
        for (size_t k = 1; k < parts.size(); ++k) split += parts[k].group == parts[k - 1].group;
        say("Speakers: " + std::to_string(map.speakers.size()) + " found" +
            (split ? " (" + std::to_string(split) + " subtitle(s) split where the speaker changes)" : std::string()));
        for (const auto& s : map.speakers) {
            size_t n = 0;
            for (const auto& c : map.cues) n += c.speaker == s.id;
            say("  " + s.name + ": " + fmt_time(s.seconds) + " of speech, " + std::to_string(n) + " subtitle(s)");
        }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        char b[64];
        std::snprintf(b, sizeof(b), "Done in %.1f s", secs);
        say(b);
        say("  wrote " + path_to_utf8(fs::absolute(path_from_utf8(output))));
        if (!split_srt.empty()) say("  wrote " + path_to_utf8(fs::absolute(path_from_utf8(split_srt))));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
    return 0;
}

}  // namespace tmx
