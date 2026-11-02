// host_test.cpp - the engine API (engine_api.h) on a computer, without a phone.
//
// Always: the model catalog, the error paths, paragraphs.
// With PK_TEST_MODELS=<folder with the phone models> and PK_TEST_AUDIO=<16 kHz
// mono float32 raw file> (ffmpeg -i x.wav -ac 1 -ar 16000 -f f32le x.raw):
// a real transcription with every speech model, a corrected copy and a
// translation with every translation model found in the folder.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "engine_api.h"
#include "json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

static int failures = 0;
#define CHECK(x)                                                              \
    do {                                                                      \
        if (!(x)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

int main() {
    const fs::path tmp = fs::temp_directory_path() / "pk_engine_host_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    // ---- the catalog ----
    const json cat = json::parse(pkm::catalog_json());
    CHECK(cat["asr"].size() == 3 && cat["translation"].size() == 3);
    CHECK(cat["asr"][0]["key"] == "parakeet-ultra" && cat["asr"][0]["chunked"] == false);
    for (const auto& m : cat["asr"]) CHECK(m["url"].get<std::string>().rfind("https://huggingface.co/", 0) == 0);
    for (const auto& m : cat["translation"])
        CHECK(m["url"].get<std::string>().find(m["file"].get<std::string>()) != std::string::npos);
    CHECK(cat["polish"]["key"] == "qwen3.5-2b");
    CHECK(cat["aligner"]["file"].get<std::string>().find("forced-aligner") != std::string::npos);
    CHECK(cat["languages"].size() > 50);

    // ---- errors come back as results, never as crashes ----
    std::vector<std::string> asked;
    pkm::Callbacks cb;
    cb.log = [](const std::string& s) { std::printf("  | %s\n", s.c_str()); };
    cb.model_file = [&](const std::string& file, const std::string&, const std::string& display, const std::string&) {
        asked.push_back(file);
        throw std::runtime_error(display + " is not downloaded (test)");
        return std::string();
    };
    {
        const json r = json::parse(pkm::transcribe(
            json{{"pcm", (tmp / "none.raw").string()}, {"model", "parakeet-ultra"}, {"out_dir", tmp.string()}}.dump(), cb));
        CHECK(r["ok"] == false && r["error"].get<std::string>().find("not downloaded") != std::string::npos);
        CHECK(asked.size() == 1 && asked[0] == "parakeet-ultra-q4_k.gguf");
    }
    CHECK(json::parse(pkm::transcribe(json{{"model", "nope"}, {"out_dir", tmp.string()}}.dump(), cb))["ok"] == false);
    CHECK(json::parse(pkm::transcribe("not json", cb))["ok"] == false);
    {
        std::ofstream(tmp / "in.txt") << "Hello world.\n\nSecond paragraph.\n";
        const json r = json::parse(pkm::translate(json{{"input", (tmp / "in.txt").string()},
                                                       {"output", (tmp / "out.txt").string()},
                                                       {"model", "hy-mt2-1.8b"}, {"to", "de"}}.dump(), cb));
        CHECK(r["ok"] == false && r["error"].get<std::string>().find("not downloaded") != std::string::npos);
        CHECK(json::parse(pkm::translate(json{{"input", (tmp / "in.txt").string()}, {"output", "x"},
                                              {"model", "hy-mt2-1.8b"}, {"to", "xx"}}.dump(), cb))["ok"] == false);
        CHECK(json::parse(pkm::translate(json{{"input", (tmp / "missing.txt").string()}, {"output", "x"},
                                              {"model", "hy-mt2-1.8b"}, {"to", "de"}}.dump(), cb))["ok"] == false);
    }
    CHECK(pkm::paragraphs("One. Two. Three. Four. Five. Six.").rfind("One. Two. Three. Four. Five.\n\nSix.", 0) == 0);

    // ---- real models (optional) ----
    const char* models = std::getenv("PK_TEST_MODELS");
    const char* audio = std::getenv("PK_TEST_AUDIO");
    if (models && audio) {
        pkm::Callbacks real = cb;
        real.model_file = [&](const std::string& file, const std::string&, const std::string& display,
                              const std::string&) {
            const fs::path p = fs::path(models) / file;
            if (!fs::exists(p)) throw std::runtime_error(display + " is not in PK_TEST_MODELS");
            return p.string();
        };
        real.progress = [](const std::string& phase, long long d, long long t, const std::string& u) {
            std::printf("  progress %s %lld/%lld %s\n", phase.c_str(), d, t, u.c_str());
        };
        for (const auto& m : cat["asr"]) {
            if (!fs::exists(fs::path(models) / m["file"].get<std::string>())) continue;
            const std::string key = m["key"];
            std::printf("transcribe with %s\n", key.c_str());
            const json r = json::parse(pkm::transcribe(
                json{{"pcm", audio}, {"model", key}, {"out_dir", (tmp / key).string()}, {"name", "talk"},
                     {"formats", {"txt", "srt", "docx"}}, {"word_timing", true},
                     {"polish", key == "parakeet-ultra"}}.dump(),
                real));
            std::printf("%s\n", r.dump(2).substr(0, 1500).c_str());
            CHECK(r["ok"] == true && !r["text"].get<std::string>().empty());
        }
        for (const auto& m : cat["translation"]) {
            if (!fs::exists(fs::path(models) / m["file"].get<std::string>())) continue;
            const std::string key = m["key"];
            std::ofstream(tmp / "talk.srt") << "1\n00:00:01,000 --> 00:00:03,000\nGood morning, everyone.\n\n"
                                               "2\n00:00:03,500 --> 00:00:06,000\nToday we talk about the budget.\n\n";
            std::printf("translate with %s\n", key.c_str());
            const json r = json::parse(pkm::translate(json{{"input", (tmp / "talk.srt").string()},
                                                           {"output", (tmp / ("talk." + key + ".de.srt")).string()},
                                                           {"model", key}, {"from", "en"}, {"to", "de"}}.dump(),
                                                      real));
            std::printf("%s\n", r.dump(2).c_str());
            CHECK(r["ok"] == true && r["text"].get<std::string>().find("-->") != std::string::npos);
        }
        // Cancelled before it starts: stops at once, reported as cancelled.
        std::atomic<bool> stop{true};
        pkm::Callbacks cancelled = real;
        cancelled.cancel = &stop;
        for (const auto& m : cat["translation"]) {
            if (!fs::exists(fs::path(models) / m["file"].get<std::string>())) continue;
            const json r = json::parse(pkm::translate(json{{"input", (tmp / "talk.srt").string()},
                                                           {"output", (tmp / "cancelled.srt").string()},
                                                           {"model", m["key"]}, {"from", "en"}, {"to", "de"}}.dump(),
                                                      cancelled));
            CHECK(r["ok"] == false && r["cancelled"] == true && !fs::exists(tmp / "cancelled.srt"));
            break;
        }
    } else {
        std::printf("real-model tests skipped (set PK_TEST_MODELS and PK_TEST_AUDIO)\n");
    }

    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    else std::printf("engine host tests passed\n");
    return failures ? 1 : 0;
}
