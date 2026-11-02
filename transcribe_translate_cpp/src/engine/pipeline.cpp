// pipeline.cpp - see pipeline.h.
#include "pipeline.h"

#include <algorithm>
#include <cstdio>
#include <memory>

#include "audio.h"

namespace tmx {

PipelineResult transcribe_audio(std::vector<float> wav, const PipelineOptions& o, const PipelineHooks& h) {
    if (!o.model) throw std::runtime_error("no speech model chosen");
    const ModelInfo& model = *o.model;
    auto say = [&](const std::string& s) {
        if (h.say) h.say(s);
    };
    auto progress = [&](const char* phase, long long done, long long total, const char* unit) {
        if (h.progress) h.progress(phase, done, total, unit);
    };
    auto check_cancel = [&] {
        if (h.cancelled && h.cancelled()) throw PipelineCancelled();
    };
    auto helper_file = [&](const ModelInfo& m) {
        if (!h.model_file) throw std::runtime_error("no file for " + m.display);
        return h.model_file(m);
    };
    const char* language = o.language.empty() || o.language == "auto" ? nullptr : o.language.c_str();
    const double duration = (double)wav.size() / kSampleRate;
    char buf[256];

    PipelineResult r;
    std::string& text = r.text;
    std::vector<Segment>& cues = r.cues;
    std::vector<Word>& timed_words = r.words;

    // ---- punctuation restoration (PCS), loaded only when first needed ----
    std::unique_ptr<PuncModel> punc;
    bool punc_failed = false;  // e.g. no internet for the first download: carry on without it
    auto wants_punctuation = [&](const std::string& t) {
        if (punc_failed || o.punctuation == "off" || normalize_space(t).empty()) return false;
        if (o.punctuation == "on") return true;
        // ARK writes no punctuation unless asked to (--ark-punctuation).
        return (model.key == "ark" && !o.ark_punctuation) || lacks_punctuation(t);
    };
    auto restore_punctuation = [&](std::string& t) {
        if (punc_failed) return false;
        try {
            if (!punc) {
                const ModelInfo& pm = punctuation_model();
                say("Loading " + pm.display + " (first run downloads " + pm.size + ")...");
                punc = std::make_unique<PuncModel>(helper_file(pm));
            }
            t = punc->restore(t);
            return true;
        } catch (const PipelineCancelled&) {
            throw;
        } catch (const std::exception& e) {
            punc_failed = true;
            say(std::string("Warning: punctuation restoration is unavailable (") + e.what() +
                "); continuing without it.");
            return false;
        }
    };
    auto report_punctuation = [&](int restored, int total) {
        if (restored == 0) return;
        std::snprintf(buf, sizeof(buf), "Punctuation: restored by the PCS model in %d of %d %s", restored, total,
                      total == 1 ? "part" : "parts");
        say(buf);
    };

    if (!model.chunked) {
        // ---- Parakeet: CrispASR does bounded long-form chunking itself ----
        AsrSession session(o.model_path, model.backend, o.engine);
        say("Engine: " + describe_engine(active_engine(o.engine.device)));
        say("Transcribing...");
        const long long total_s = (long long)duration;
        auto segments = session.transcribe_long(wav, nullptr, [&](int done, int total) {
            if (total > 0) progress("transcribe", (long long)done * total_s / total, total_s, "s");
        });
        wav.clear();  // the audio isn't needed any more (duration is kept)
        wav.shrink_to_fit();
        check_cancel();
        std::vector<Word>& words = timed_words;
        // Segments whose restored punctuation could not be put onto their
        // word timings: their subtitles are made from the segment text.
        std::vector<bool> whole(segments.size(), false);
        {
            std::string all;
            for (const auto& s : segments) all += s.text + " ";
            if (wants_punctuation(all)) {  // Parakeet normally punctuates; this is a fallback
                int n = 0, nonempty = 0;
                for (size_t i = 0; i < segments.size(); ++i) {
                    Segment& s = segments[i];
                    if (normalize_space(s.text).empty()) continue;
                    ++nonempty;
                    const std::string before = s.text;
                    if (!restore_punctuation(s.text)) continue;
                    ++n;
                    // Subtitles are built from the words: give them the restored text too.
                    if (s.text != before && !s.words.empty() && !apply_restored_text(s.words, s.text)) whole[i] = true;
                }
                report_punctuation(n, nonempty);
            }
            punc.reset();  // free it before the language model
        }
        for (const auto& s : segments) {
            if (!text.empty()) text += ' ';
            text += s.text;
            words.insert(words.end(), s.words.begin(), s.words.end());
        }
        if (o.want_cues) {
            if (words.empty()) {
                cues = segments;
            } else {
                // Word-timed cues, except one cue for a segment without word
                // timings or whose words didn't take the restored text.
                std::vector<Word> run;
                auto flush = [&] {
                    auto c = words_to_cues(run);
                    cues.insert(cues.end(), c.begin(), c.end());
                    run.clear();
                };
                for (size_t i = 0; i < segments.size(); ++i) {
                    if (whole[i] || segments[i].words.empty()) {
                        flush();
                        cues.push_back({segments[i].start, segments[i].end, segments[i].text, {}});
                    } else {
                        run.insert(run.end(), segments[i].words.begin(), segments[i].words.end());
                    }
                }
                flush();
            }
        }
    } else {
        // ---- ARK / Qwen: LLM decoders, fed <= 30 s chunks cut at pauses ----
        double chunk_s = o.chunk_seconds;
        if (chunk_s > model.max_chunk_seconds) {
            std::snprintf(buf, sizeof(buf), "Note: %s accepts at most %g s per chunk; using that.",
                          model.display.c_str(), model.max_chunk_seconds);
            say(buf);
            chunk_s = model.max_chunk_seconds;
        }
        if (chunk_s > 30.0) chunk_s = 30.0;  // CrispASR re-chunks anything longer at 30 s anyway
        if (chunk_s < 5.0) chunk_s = 5.0;

        int skipped = 0;
        std::vector<Chunk> chunks = make_chunks(wav, kSampleRate, chunk_s, &skipped);
        wav.clear();
        wav.shrink_to_fit();
        std::snprintf(buf, sizeof(buf), "Chunks: %zu (<= %g s each%s)", chunks.size(), chunk_s,
                      skipped ? (", " + std::to_string(skipped) + " silent skipped").c_str() : "");
        say(buf);
        if (chunks.empty()) throw std::runtime_error("the audio appears to be silent.");

        std::vector<std::string> texts(chunks.size());
        {
            AsrSession session(o.model_path, model.backend, o.engine);
            say("Engine: " + describe_engine(active_engine(o.engine.device)));
            // Experimental: ask ARK for punctuated text. It was trained with one
            // fixed instruction, so a chunk that comes back empty is retried with
            // the default one.
            std::string instruction;
            if (model.key == "ark" && o.ark_punctuation) {
                instruction = std::string("Transcribe the audio") +
                              (language ? " in " + language_name(language) : std::string()) +
                              ", with punctuation and capitalization.";
                session.set_instruction(instruction);
                say("ARK instruction: " + instruction);
            }
            int retried = 0;
            say("Transcribing...");
            for (size_t i = 0; i < chunks.size(); ++i) {
                check_cancel();
                auto run = [&] {
                    std::string t;
                    for (const auto& s : session.transcribe_chunk(chunks[i].audio.data(), chunks[i].audio.size(),
                                                                  chunks[i].offset, language))
                        t += (t.empty() ? "" : " ") + s.text;
                    return t;
                };
                texts[i] = run();
                if (!instruction.empty() && strip_punctuation(texts[i]).empty()) {  // e.g. only "."
                    session.set_instruction("");  // default instruction for this chunk
                    texts[i] = run();
                    session.set_instruction(instruction);
                    if (!strip_punctuation(texts[i]).empty()) ++retried;
                }
                progress("transcribe", (long long)i + 1, (long long)chunks.size(), "chunks");
            }
            if (retried > 0) {
                std::snprintf(buf, sizeof(buf),
                              "Note: %d chunk(s) came back empty with the punctuation instruction and were "
                              "redone with ARK's default one.", retried);
                say(buf);
            }
        }  // free the ASR model before loading the aligner

        // Restore punctuation before alignment, so subtitle cues get it too.
        {
            int restored = 0, nonempty = 0;
            for (auto& t : texts) {
                if (normalize_space(t).empty()) continue;
                ++nonempty;
                if (wants_punctuation(t) && restore_punctuation(t)) ++restored;
            }
            report_punctuation(restored, nonempty);
            punc.reset();  // free it before the aligner
        }

        for (const auto& t : texts) {
            if (t.empty()) continue;
            if (!text.empty()) text += ' ';
            text += t;
        }

        if (o.want_cues) {
            if (!o.align) {
                for (size_t i = 0; i < chunks.size(); ++i)
                    if (!texts[i].empty())
                        cues.push_back({chunks[i].offset, chunks[i].offset + chunks[i].duration, texts[i], {}});
            } else {
                const ModelInfo& al = o.aligner ? *o.aligner : aligner_model();
                say("Loading " + al.display + " for word timestamps (first run downloads " + al.size + ")...");
                const std::string al_path = helper_file(al);
                const int threads = o.engine.threads > 0 ? o.engine.threads : default_threads();
                std::vector<Word> words;
                for (size_t i = 0; i < chunks.size(); ++i) {
                    check_cancel();
                    if (!texts[i].empty()) {
                        auto w = align_words(al_path, texts[i], chunks[i].audio.data(), chunks[i].audio.size(),
                                             chunks[i].offset, threads);
                        if (w.empty()) {
                            // Alignment failed for this chunk: fall back to one cue for it.
                            cues.push_back({chunks[i].offset, chunks[i].offset + chunks[i].duration, texts[i], {}});
                        } else {
                            const double limit = chunks[i].offset + chunks[i].duration;
                            for (auto& x : w) {
                                x.start = std::min(x.start, limit);
                                x.end = std::max(x.start, std::min(x.end, limit));
                            }
                            timed_words.insert(timed_words.end(), w.begin(), w.end());
                            auto c = words_to_cues(w);
                            cues.insert(cues.end(), c.begin(), c.end());
                        }
                    }
                    progress("align", (long long)i + 1, (long long)chunks.size(), "chunks");
                }
                // CrispASR keeps the aligner (~1 GB) loaded for further calls; free it
                // now so it isn't still taking memory when the post-processing model loads.
                if (!free_aligner() && o.more_models_follow)
                    say("Note: this CrispASR build can't unload the word-timing model; it stays in memory "
                        "during post-processing.");
            }
        }
    }
    text = normalize_space(text);
    return r;
}

}  // namespace tmx
