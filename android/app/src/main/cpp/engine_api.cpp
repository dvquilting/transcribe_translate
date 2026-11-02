// engine_api.cpp - see engine_api.h.
#include "engine_api.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "json.hpp"  // nlohmann/json (MIT), from CrispASR's examples/

#include "docx_writer.h"  // ../common
#include "fileio.h"
#include "model_catalog.h"
#include "utf8_text.h"

#include "transcribe_cpp/src/llm.h"
#include "transcribe_cpp/src/pipeline.h"
#include "transcribe_cpp/src/transcript.h"

#include "translate_cpp/src/docx.h"
#include "translate_cpp/src/formats.h"
#include "translate_cpp/src/languages.h"
#include "translate_cpp/src/llm.h"
#include "translate_cpp/src/models.h"
#include "translate_cpp/src/translator.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace pkm {

namespace {

constexpr int kSentencesPerParagraph = 5;
// The corrected copy is made by the general-purpose model of the translation
// list, so one download serves both.
constexpr const char* kPolishModel = "qwen3.5-2b";

json asr_json(const pk::AsrModel& m) {
    return {{"key", m.key},         {"display", m.display},   {"file", m.file},
            {"url", m.url},         {"size", m.size},         {"chunked", m.chunked},
            {"needs_language", m.needs_language}, {"description", m.description}};
}

json translation_json(const tl::ModelInfo& m) {
    return {{"key", m.key},   {"display", m.display}, {"file", m.file},          {"url", m.url},
            {"size", m.size}, {"langs", m.langs},     {"description", m.description}};
}

double since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
}

void say(const Callbacks& cb, const std::string& s) {
    if (cb.log) cb.log(s);
}

bool cancelled(const Callbacks& cb) { return cb.cancel && cb.cancel->load(); }

std::string file_of(const Callbacks& cb, const std::string& file, const std::string& url, const std::string& display,
                    const std::string& size) {
    if (!cb.model_file) throw std::runtime_error(display + " is not downloaded");
    return cb.model_file(file, url, display, size);
}

int threads_of(const json& r) {
    const int t = r.value("threads", 0);
    if (t > 0) return t;
    // The big cores: a phone's little cores slow the others down in ggml's
    // evenly split work. 4 of 8 on most phones.
    const unsigned n = std::thread::hardware_concurrency();
    return n >= 8 ? 4 : n >= 4 ? (int)n / 2 + 1 : 1;
}

std::vector<float> read_pcm(const std::string& path) {
    std::ifstream f(fs::path(path), std::ios::binary);
    if (!f) throw std::runtime_error("could not read the decoded audio");
    f.seekg(0, std::ios::end);
    const std::streamoff bytes = f.tellg();
    f.seekg(0);
    if (bytes <= 0 || bytes % 4) throw std::runtime_error("the decoded audio is empty or damaged");
    std::vector<float> pcm((size_t)bytes / 4);
    f.read(reinterpret_cast<char*>(pcm.data()), bytes);
    if (!f) throw std::runtime_error("could not read the decoded audio");
    return pcm;
}

// Qwen 3.5 for the corrected copy, through the translator's direct llama.cpp
// access: its chat format with thinking switched off (a phone can't spend
// minutes on reasoning the answer doesn't use).
class QwenChat : public tmx::ChatModel {
public:
    QwenChat(tl::Llm& llm) : llm_(llm) {}
    std::string ask(const std::string& system, const std::string& user, int max_tokens, float temperature) override {
        tl::Sampling s;
        s.temperature = temperature;
        if (temperature > 0) s.top_k = 20, s.top_p = 0.8f;
        const tl::Generation g = llm_.generate(prompt(system, user), false, max_tokens, s);
        return tmx::strip_reasoning(g.text);
    }
    int count_tokens(const std::string& system, const std::string& user) override {
        return llm_.count_prompt_tokens(prompt(system, user), false);
    }
    int context_size() const override { return llm_.context_size(); }

private:
    static tl::Prompt prompt(const std::string& system, const std::string& user) {
        tl::Prompt p;
        p.special("<|im_start|>").text("system\n" + system).special("<|im_end|>").text("\n");
        p.special("<|im_start|>").text("user\n" + user).special("<|im_end|>").text("\n");
        p.special("<|im_start|>").text("assistant\n").special("<think>").text("\n\n").special("</think>").text("\n\n");
        return p;
    }
    tl::Llm& llm_;
};

std::string failure(const std::exception& e, bool was_cancelled) {
    return json{{"ok", false}, {"error", e.what()}, {"cancelled", was_cancelled}}.dump();
}

}  // namespace

std::string paragraphs(const std::string& text) { return tmx::format_paragraphs(text, kSentencesPerParagraph); }

std::string catalog_json() {
    json j;
    j["asr"] = json::array();
    for (const auto& m : pk::phone_asr_models()) j["asr"].push_back(asr_json(m));
    j["aligner"] = asr_json(pk::phone_aligner_model());
    j["punctuation"] = asr_json(pk::punctuation_model());
    j["translation"] = json::array();
    for (const auto& m : tl::phone_model_table()) j["translation"].push_back(translation_json(m));
    j["polish"] = translation_json(*tl::find_phone_model(kPolishModel));
    j["languages"] = json::array();
    for (const auto& l : tl::languages())
        j["languages"].push_back({{"code", l.code}, {"name", l.name}, {"native", l.native}});
    return j.dump();
}

std::string transcribe(const std::string& request_json, const Callbacks& cb) {
    const auto t0 = std::chrono::steady_clock::now();
    try {
        const json r = json::parse(request_json);
        const pk::AsrModel* model = pk::find_phone_asr_model(r.at("model").get<std::string>());
        if (!model) throw std::runtime_error("unknown speech model " + r.at("model").get<std::string>());
        std::vector<std::string> formats = r.value("formats", std::vector<std::string>{"txt"});
        auto wants = [&](const char* f) { return std::find(formats.begin(), formats.end(), f) != formats.end(); };
        const fs::path out_dir = fs::path(r.at("out_dir").get<std::string>());
        const std::string name = r.value("name", std::string("transcript"));
        const std::string title = r.value("title", name);
        fs::create_directories(out_dir);

        tmx::PipelineOptions po;
        po.model = model;
        say(cb, "Model : " + model->display);
        po.model_path = file_of(cb, model->file, model->url, model->display, model->size);
        std::string language = r.value("language", std::string("auto"));
        language = language.substr(0, language.find('-'));  // "pt-BR" -> "pt": the speech models take plain codes
        po.language = language == "auto" ? "" : language;
        po.engine.device = "cpu";
        po.engine.threads = threads_of(r);
        po.punctuation = r.value("punctuation", std::string("auto"));
        po.want_cues = wants("srt");
        po.align = r.value("word_timing", false);
        po.aligner = &pk::phone_aligner_model();
        const bool polish = r.value("polish", false);
        po.more_models_follow = polish;
        tmx::PipelineHooks h;
        h.say = [&](const std::string& s) { say(cb, s); };
        h.progress = [&](const char* phase, long long done, long long total, const char* unit) {
            if (cb.progress) cb.progress(phase, done, total, unit);
        };
        h.model_file = [&](const pk::AsrModel& m) { return file_of(cb, m.file, m.url, m.display, m.size); };
        h.cancelled = [&] { return cancelled(cb); };

        std::vector<float> pcm = read_pcm(r.at("pcm").get<std::string>());
        const double audio_seconds = (double)pcm.size() / 16000.0;
        tmx::PipelineResult res = tmx::transcribe_audio(std::move(pcm), po, h);
        if (res.text.empty()) throw std::runtime_error("no speech was found in the audio");

        std::vector<std::string> files;
        // One text result in each chosen document format (always at least the .txt,
        // which the app shows and makes the PDF from).
        auto write_result = [&](const std::string& suffix, const std::string& note, const std::string& body) {
            const std::string plain = note.empty() ? body : note + "\n\n" + body;
            const fs::path txt = out_dir / (name + suffix + ".txt");
            pk::write_file_atomic(txt, plain);
            files.push_back(txt.string());
            if (wants("docx")) {
                pk::DocxText d;
                d.title = title;
                d.note = note;
                d.body = body;
                const fs::path p = out_dir / (name + suffix + ".docx");
                pk::write_file_atomic(p, pk::make_docx(d));
                files.push_back(p.string());
            }
        };
        const std::string paras = paragraphs(res.text);
        write_result("", "", paras);
        if (po.want_cues) {
            const fs::path p = out_dir / (name + ".srt");
            pk::write_file_atomic(p, tmx::format_srt(res.cues));
            files.push_back(p.string());
        }
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Transcribed in %.1f s (%.1fx real time)", since(t0),
                      audio_seconds / std::max(since(t0), 1e-6));
        say(cb, buf);

        // The corrected copy: a failure here keeps the transcript, which is saved.
        std::string clean, polish_error;
        if (polish) {
            try {
                const tl::ModelInfo& lm = *tl::find_phone_model(kPolishModel);
                const std::string path = file_of(cb, lm.file, lm.url, lm.display, lm.size);
                say(cb, "Loading " + lm.display + " ...");
                tl::EngineOptions eo;
                eo.device = "cpu";
                eo.threads = threads_of(r);
                eo.n_ctx = 4096;  // polish works on ~300-word sections
                tl::Llm llm(path, eo);
                llm.set_cancel_flag(cb.cancel);
                QwenChat chat(llm);
                say(cb, "Correcting spelling and grammar...");
                tmx::PolishStats st;
                clean = tmx::polish_transcript(
                    chat, paras,
                    [&](int d, int t) {
                        if (cb.progress) cb.progress("polish", d, t, "sections");
                    },
                    &st);
                if (cancelled(cb)) throw tmx::PipelineCancelled();
                if (st.kept_original > 0) {
                    std::snprintf(buf, sizeof(buf),
                                  "Note: %d of %d sections were left unchanged because the model's correction "
                                  "changed too many words.", st.kept_original, st.chunks);
                    say(cb, buf);
                }
                write_result(".clean",
                             "[Spelling and grammar corrected by a local AI model (" + lm.display +
                                 "). Where exact wording matters, check against the transcript.]",
                             clean);
            } catch (const tmx::PipelineCancelled&) {
                throw;
            } catch (const std::exception& e) {
                if (cancelled(cb)) throw tmx::PipelineCancelled();
                polish_error = e.what();
                say(cb, "Error: the corrected copy failed: " + polish_error);
            }
        }
        std::snprintf(buf, sizeof(buf), "Done in %.1f s", since(t0));
        say(cb, buf);
        return json{{"ok", true},   {"text", paras},          {"clean", clean},       {"files", files},
                    {"seconds", since(t0)}, {"audio_seconds", audio_seconds}, {"polish_error", polish_error}}
            .dump();
    } catch (const tmx::PipelineCancelled& e) {
        return failure(e, true);
    } catch (const std::exception& e) {
        return failure(e, cancelled(cb));
    }
}

std::string translate(const std::string& request_json, const Callbacks& cb) {
    const auto t0 = std::chrono::steady_clock::now();
    try {
        const json r = json::parse(request_json);
        const tl::ModelInfo* model = tl::find_phone_model(r.at("model").get<std::string>());
        if (!model) throw std::runtime_error("unknown translation model " + r.at("model").get<std::string>());
        const tl::Language* to_lang = tl::find_language(r.at("to").get<std::string>());
        if (!to_lang) throw std::runtime_error("unknown target language " + r.at("to").get<std::string>());
        const std::string target = to_lang->code;
        std::string source = r.value("from", std::string("auto"));
        if (source != "auto") {
            const tl::Language* l = tl::find_language(source);
            if (!l) throw std::runtime_error("unknown source language " + source);
            source = l->code;
        }
        const fs::path in_path = fs::path(r.at("input").get<std::string>());
        const std::string out_path = r.at("output").get<std::string>();
        std::string format = r.value("format", std::string());
        if (format.empty()) {
            std::string ext = in_path.extension().string();
            for (char& c : ext) c = (char)std::tolower((unsigned char)c);
            format = ext == ".md" || ext == ".markdown" ? "md" : ext == ".srt" ? "srt" : ext == ".docx" ? "docx" : "txt";
        }

        // ---- the units of text ----
        tl::Layout layout;
        tl::SrtPlan srt;
        std::unique_ptr<tl::DocxDocument> docx;
        std::vector<std::string> units;
        if (format == "docx") {
            docx = std::make_unique<tl::DocxDocument>(in_path.string());
            units = docx->units();
        } else {
            const std::string text = pk::clean_input_text(pk::read_file(in_path));
            if (format == "srt") {
                srt = tl::plan_srt(tl::parse_srt(text));
                if (srt.cues.empty()) throw std::runtime_error("no subtitles found in the file");
                units = srt.units;
            } else {
                layout = format == "md" ? tl::parse_markdown(text) : tl::parse_plain_text(text);
                units = layout.units;
            }
        }
        if (units.empty()) throw std::runtime_error("there is no text to translate");
        say(cb, "Input : " + format + ", " + std::to_string(units.size()) + " parts");
        say(cb, "Model : " + model->display);
        if (!tl::model_supports(*model, target))
            say(cb, "Note: " + model->display + " was not trained on " + tl::language_label(target) +
                        "; the translation may be poor. Qwen 3.5 handles more languages.");

        // ---- the model ----
        const std::string model_path = file_of(cb, model->file, model->url, model->display, model->size);
        say(cb, "Loading " + model->display + " ...");
        tl::EngineOptions eo;
        eo.device = "cpu";
        eo.threads = threads_of(r);
        eo.n_ctx = model->n_ctx;
        tl::Llm llm(model_path, eo);
        llm.set_cancel_flag(cb.cancel);
        tl::TranslateOptions to;
        to.source = source;
        to.target = target;
        to.use_context = r.value("context", true);
        tl::Translator tr(llm, *model, to);
        if (source == "auto" && model->family == tl::Family::TranslateGemma) {
            // TranslateGemma's prompt names the source language: ask the model.
            std::string sample;
            for (const auto& u : units) {
                if (sample.size() > 600) break;
                sample += (sample.empty() ? "" : "\n") + u;
            }
            source = tr.detect_language(sample);
            if (source.empty()) throw std::runtime_error("couldn't tell which language the text is in; choose it");
            say(cb, "Detected: " + tl::language_label(source));
            tr.set_source(source);
        }
        if (source != "auto" && source == target)
            throw std::runtime_error("the text is already in " + tl::language_label(target));
        say(cb, "Languages: " + (source == "auto" ? std::string("auto-detect") : tl::language_label(source)) + " -> " +
                    tl::language_label(target));

        // ---- translate ----
        say(cb, "Translating ...");
        const auto t1 = std::chrono::steady_clock::now();
        std::vector<std::string> result = tr.translate_units(units, [&](int d, int t) {
            if (cb.progress) cb.progress("translate", d, t, "pieces");
        });
        if (cancelled(cb)) throw tmx::PipelineCancelled();
        char buf[160];
        std::snprintf(buf, sizeof(buf), "Translated in %.1f s", since(t1));
        say(cb, buf);
        const tl::TranslateStats& st = tr.stats();
        if (st.problems > 0)
            say(cb, "Note: " + std::to_string(st.problems) +
                        " part(s) could not be translated; their original text was kept.");

        std::string text;
        if (format == "docx") {
            docx->save(out_path, result);
        } else {
            text = format == "srt" ? tl::render_srt(srt, result, target) : layout.render(result);
            pk::write_file_atomic(fs::path(out_path), text);
        }
        return json{{"ok", true},       {"output", out_path},        {"text", text},
                    {"problems", st.problems}, {"seconds", since(t0)}, {"source", source}}
            .dump();
    } catch (const tmx::PipelineCancelled& e) {
        return failure(e, true);
    } catch (const std::exception& e) {
        return failure(e, cancelled(cb));
    }
}

}  // namespace pkm
