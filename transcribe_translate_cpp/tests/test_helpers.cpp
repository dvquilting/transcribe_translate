#include <fstream>
// Unit tests for the model-independent helpers (chunking, SRT, cues, paragraphs).
// Run with `ctest` or directly: transcribe_tests
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "audio.h"
#include "dsp.h"
#include "dub.h"
#include "transcript.h"
#include "output_transaction.h"
#include "pathutil.h"
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>  // the OutputTransaction test locks a file
#endif

using namespace tmx;

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

int main() {
    const int sr = kSampleRate;

    // 13 x (6.5 s noise + 0.5 s silence), then 40 s silence, then a 0.3 s blip.
    std::mt19937 rng(0);
    std::normal_distribution<float> nd(0.0f, 0.1f);
    std::vector<float> wav;
    for (int i = 0; i < 13; ++i) {
        for (int j = 0; j < (int)(6.5 * sr); ++j) wav.push_back(nd(rng));
        wav.insert(wav.end(), (size_t)(0.5 * sr), 0.0f);
    }
    wav.insert(wav.end(), (size_t)(40 * sr), 0.0f);
    for (int j = 0; j < (int)(0.3 * sr); ++j) wav.push_back(nd(rng));

    auto ranges = split_on_silence(wav.data(), wav.size(), sr, 30.0);
    CHECK(!ranges.empty());
    CHECK(ranges.front().first == 0);
    CHECK(ranges.back().second == wav.size());
    int in_silence = 0;
    for (size_t i = 0; i < ranges.size(); ++i) {
        CHECK(ranges[i].second - ranges[i].first <= (size_t)(30 * sr));
        if (i + 1 < ranges.size()) {
            CHECK(ranges[i].second == ranges[i + 1].first);
            const size_t b = ranges[i].second;
            bool silent = true;
            for (size_t k = b - 100; k < b + 100; ++k) silent = silent && wav[k] == 0.0f;
            in_silence += silent;
        }
    }
    std::printf("ranges: %zu, cuts in silence: %d\n", ranges.size(), in_silence);
    CHECK(in_silence == (int)ranges.size() - 1);

    // A purely silent stretch must be skipped.
    std::vector<float> quiet((size_t)(70 * sr), 0.0f);
    for (int j = 0; j < 5 * sr; ++j) quiet[j] = nd(rng);
    int skipped = 0;
    auto chunks = make_chunks(quiet, sr, 30.0, &skipped);
    std::printf("quiet: chunks %zu skipped %d\n", chunks.size(), skipped);
    CHECK(chunks.size() == 1 && skipped == 2);
    for (const auto& c : chunks) CHECK(c.audio.size() >= (size_t)sr);

    // SRT timestamps
    CHECK(srt_time(3661.2345) == "01:01:01,235" || srt_time(3661.2345) == "01:01:01,234");
    CHECK(srt_time(59.9996) == "00:01:00,000");
    CHECK(srt_time(-1.0) == "00:00:00,000");

    // Cue grouping
    std::vector<Word> words = {{"Hello", 0.0, 0.4}, {"there,", 0.5, 0.9}, {"friends.", 1.0, 1.5},
                               {"This", 3.0, 3.2},  {"is", 3.3, 3.4}};
    for (int i = 0; i < 30; ++i)
        words.push_back({"word" + std::to_string(i), 3.5 + i * 0.3, 3.7 + i * 0.3});
    auto cues = words_to_cues(words);
    for (const auto& c : cues) {
        std::printf("%.2f-%.2f [%zu] %s\n", c.start, c.end, c.text.size(), c.text.c_str());
        CHECK(c.text.size() <= kCueMaxChars);
        CHECK(c.end - c.start <= kCueMaxSeconds + 0.5);
    }
    CHECK(cues.size() >= 3 && cues[0].text == "Hello there, friends.");

    const std::string srt = format_srt(cues);
    CHECK(srt.rfind("1\n00:00:00,000 --> 00:00:01,500\nHello there, friends.\n\n", 0) == 0);
    // Exact layout: empty cues skipped (numbering continues), at least 0.2 s, 42-character lines.
    CHECK(format_srt({{1.0, 1.1, "  Short   one ", {}}, {2.0, 3.0, " ", {}},
                      {3.0, 4.5, "This cue is long enough to need a second line of text here.", {}}}) ==
          "1\n00:00:01,000 --> 00:00:01,200\nShort one\n\n"
          "2\n00:00:03,000 --> 00:00:04,500\nThis cue is long enough to need a second\nline of text here.\n\n");

    // Wrapping counts code points, not bytes
    auto lines = wrap_text("caf\xC3\xA9 caf\xC3\xA9 caf\xC3\xA9", 9);
    CHECK(lines.size() == 2 && lines[0] == "caf\xC3\xA9 caf\xC3\xA9");

    // Paragraphs
    const std::string p = format_paragraphs("One. Two?  Three! Four. Five. Six. Seven", 5);
    std::printf("%s", p.c_str());
    CHECK(p == "One. Two? Three! Four. Five.\n\nSix. Seven\n");

    // Smart-paragraph helpers
    CHECK((parse_number_list("4, 11 and 19.") == std::vector<int>{4, 11, 19}));
    CHECK(parse_number_list("none").empty());
    auto ss = split_sentences("One two. Three four five six seven", 3);
    CHECK(ss.size() == 3 && ss[0] == "One two." && ss[1] == "Three four five" && ss[2] == "six seven");
    {
        std::vector<std::string> s10;
        for (int i = 0; i < 10; ++i) s10.push_back("S" + std::to_string(i) + ".");
        CHECK(join_paragraphs({"A.", "B.", "C."}, {2}) == "A. B.\n\nC.\n");
        // break at 1 leaves a one-sentence paragraph -> dropped; 9 likewise at the end
        CHECK((tidy_paragraph_breaks({1, 4, 9}, s10, {}, 12, 250) == std::vector<int>{4}));
        // too long -> split in the middle, or at the longest pause
        CHECK((tidy_paragraph_breaks({}, s10, {}, 6, 250) == std::vector<int>{5}));
        std::vector<double> pz(10, 0.1);
        pz[3] = 2.0;
        CHECK((tidy_paragraph_breaks({}, s10, pz, 6, 250) == std::vector<int>{3, 6}));
        // pauses from word timings, even when words are split differently
        std::vector<Word> tw = {{"S0.", 0, 1}, {"S1", 1.2, 1.5}, {".", 1.5, 1.6}, {"S2.", 4.0, 4.5}};
        auto pp = sentence_pauses({"S0.", "S1.", "S2."}, tw);
        CHECK(pp.size() == 3 && pp[0] < 0 && std::fabs(pp[1] - 0.2) < 1e-9 && std::fabs(pp[2] - 2.4) < 1e-9);
        CHECK(sentence_pauses({"Totally different text here."}, tw).empty());
    }

    // Punctuation helpers
    CHECK(strip_punctuation("Hello, World! It's a well-known \"fact\" -- isn't it?") ==
          "hello world it's a well-known fact isn't it");
    CHECK(strip_punctuation("\xC3\x89" "COLE caf\xC3\xA9.") == "\xC3\xA9" "cole caf\xC3\xA9");
    CHECK(lacks_punctuation("so we went to the store and then we came back home and had a nice dinner together"));
    CHECK(!lacks_punctuation("So we went to the store. Then we came back home and had a nice dinner together."));
    CHECK(!lacks_punctuation("too short to judge"));
    {
        std::string longp;  // one full stop in 80 words: still "lacks"
        for (int i = 0; i < 80; ++i) longp += "Word ";
        CHECK(lacks_punctuation(longp + "end."));
    }
    // Non-Latin scripts: Cyrillic capitals count; scripts without capitals
    // are judged by their sentence ends only.
    {
        // "Мы пошли в магазин. Потом мы вернулись домой и хорошо поужинали вместе с друзьями."
        const std::string ru =
            "\xD0\x9C\xD1\x8B \xD0\xBF\xD0\xBE\xD1\x88\xD0\xBB\xD0\xB8 \xD0\xB2 "
            "\xD0\xBC\xD0\xB0\xD0\xB3\xD0\xB0\xD0\xB7\xD0\xB8\xD0\xBD. "
            "\xD0\x9F\xD0\xBE\xD1\x82\xD0\xBE\xD0\xBC \xD0\xBC\xD1\x8B "
            "\xD0\xB2\xD0\xB5\xD1\x80\xD0\xBD\xD1\x83\xD0\xBB\xD0\xB8\xD1\x81\xD1\x8C "
            "\xD0\xB4\xD0\xBE\xD0\xBC\xD0\xBE\xD0\xB9 \xD0\xB8 "
            "\xD1\x85\xD0\xBE\xD1\x80\xD0\xBE\xD1\x88\xD0\xBE "
            "\xD0\xBF\xD0\xBE\xD1\x83\xD0\xB6\xD0\xB8\xD0\xBD\xD0\xB0\xD0\xBB\xD0\xB8 "
            "\xD0\xB2\xD0\xBC\xD0\xB5\xD1\x81\xD1\x82\xD0\xB5 \xD1\x81 "
            "\xD0\xB4\xD1\x80\xD1\x83\xD0\xB7\xD1\x8C\xD1\x8F\xD0\xBC\xD0\xB8.";
        CHECK(!lacks_punctuation(ru));
        // the same text in lower case without full stops (repeated: long enough to judge)
        std::string flat = ru;
        flat.erase(std::remove(flat.begin(), flat.end(), '.'), flat.end());
        for (size_t i = 0; i + 1 < flat.size(); ++i)  // А..П (D0 90..9F) -> а..п (D0 B0..BF)
            if (flat[i] == '\xD0' && (unsigned char)flat[i + 1] >= 0x90 && (unsigned char)flat[i + 1] <= 0x9F)
                flat[i + 1] = (char)(flat[i + 1] + 0x20);
        CHECK(lacks_punctuation(flat + " " + flat));
        std::string ar;  // Arabic (no capitals) with sentence ends: punctuated
        for (int i = 0; i < 4; ++i)
            ar += "\xD8\xB0\xD9\x87\xD8\xA8\xD9\x86\xD8\xA7 \xD8\xA5\xD9\x84\xD9\x89 "
                  "\xD8\xA7\xD9\x84\xD8\xB3\xD9\x88\xD9\x82. ";
        CHECK(!lacks_punctuation(ar));
    }

    // Punctuation restoration: only edge punctuation goes, words keep what is inside them.
    CHECK(strip_punctuation("L'\xC3\xA9t\xC3\xA9, d'\xC3\xAAtre.") == "l'\xC3\xA9t\xC3\xA9 d'\xC3\xAAtre");
    CHECK(strip_punctuation("It costs $100, about 3.5 times more at 10:30.") ==
          "it costs $100 about 3.5 times more at 10:30");
    CHECK(strip_punctuation("50% of AT&T, 24/7 in C++!") == "50% of at&t 24/7 in c++");
    CHECK(strip_punctuation("(Hello) -5 degrees... \xE2\x80\x9CQuoted\xE2\x80\x9D \xE2\x80\x94 text\xE2\x80\xA6") ==
          "hello -5 degrees quoted text");
    CHECK(same_words("L'\xC3\x89t\xC3\xA9, c'est 3.5!", "l'\xC3\xA9t\xC3\xA9 c'est 3.5"));
    CHECK(same_words("Hello, world.", "hello world"));
    CHECK(same_words("It\xE2\x80\x99s fine.", "it's fine"));
    CHECK(!same_words("L\xC3\xA9t\xC3\xA9 c'est 3.5", "l'\xC3\xA9t\xC3\xA9 c'est 3.5"));  // apostrophe dropped
    CHECK(!same_words("Dont.", "don't"));
    CHECK(!same_words("Well known.", "well-known"));
    CHECK(!same_words("3,5", "3.5"));
    CHECK(!same_words("At 1030.", "at 10:30"));
    {
        // Restored text onto timed words (Parakeet subtitles), timings kept.
        std::vector<Word> tw = {{"so", 0.0, 0.4}, {"we", 0.5, 0.7}, {"went", 0.8, 1.0}, {"home", 1.1, 1.5}};
        CHECK(apply_restored_text(tw, "So, we went home."));
        CHECK(tw[0].text == "So," && tw[3].text == "home." && tw[3].start == 1.1 && tw[3].end == 1.5);
        CHECK(!apply_restored_text(tw, "So we went."));  // different words: unchanged
        CHECK(tw[0].text == "So,");
        std::vector<Word> pw = {{"hello", 0.0, 0.5}, {"--", 0.5, 0.6}, {"world", 0.7, 1.2}};
        CHECK(apply_restored_text(pw, "Hello, world."));
        const auto pc = words_to_cues(pw);
        CHECK(pc.size() == 1 && pc[0].text == "Hello, world." && pc[0].end == 1.2);
    }

    // CJK sentence ends (no spaces between sentences)
    {
        const std::string ni = "\xE4\xBD\xA0", hao = "\xE5\xA5\xBD", wo = "\xE6\x88\x91", shi = "\xE6\x98\xAF",
                          de = "\xE7\x9A\x84", stop = "\xE3\x80\x82", q = "\xEF\xBC\x9F", ex = "\xEF\xBC\x81";
        const std::string cjk = ni + hao + stop + wo + shi + ex + ni + q + hao + de;
        const auto cs = split_sentences(cjk);
        CHECK(cs.size() == 4 && cs[0] == ni + hao + stop && cs[1] == wo + shi + ex && cs[2] == ni + q &&
              cs[3] == hao + de);
        CHECK(format_paragraphs(cjk, 2) == ni + hao + stop + wo + shi + ex + "\n\n" + ni + q + hao + de + "\n");
        CHECK(split_sentences(hao + ex + q + ni + "\xE3\x80\x8D" + hao).size() == 2);  // marks stay together
        CHECK(split_sentences(ni + stop + "\xE3\x80\x8D" + hao).front() == ni + stop + "\xE3\x80\x8D");
        CHECK(split_sentences("He said \"Stop.\" Then he left.").size() == 2);
        CHECK(split_sentences("Wait\xE2\x80\xA6 then 3.5 more").size() == 2);
    }

    // Polishing: pieces of one long paragraph are joined with spaces, not paragraph breaks.
    {
        std::string longp;
        for (int i = 0; i < 700; ++i) longp += (i ? " w" : "w") + std::to_string(i);
        const auto chunks = chunk_paragraphs("Short one.\n\n" + longp + "\n\n\nEnd.\n", 300);
        CHECK(chunks.size() == 5);
        CHECK(chunks.size() == 5 && !chunks[0].continues && !chunks[1].continues && chunks[2].continues &&
              chunks[3].continues && !chunks[4].continues && chunks[4].text == "End.");
        std::vector<std::string> outs;
        for (const auto& c : chunks) outs.push_back(c.text + "\n");
        CHECK(join_chunks(chunks, outs) == "Short one.\n\n" + longp + "\n\nEnd.");
        const auto small = chunk_paragraphs("A b.\n\nC d.\n\nE f.", 300);  // short paragraphs share a chunk
        CHECK(small.size() == 1 && small[0].text == "A b.\n\nC d.\n\nE f.");
        CHECK((split_paragraphs("one\r\ntwo\r\n\r\n  three  ") == std::vector<std::string>{"one two", "three"}));
    }

    // ---- dubbing: DSP ----
    {
        // Separation in overlapping parts: one part (weight 1) when short, equal
        // parts otherwise, and crossfade weights that always sum to 1.
        CHECK((chunk_bounds(100, 100) == std::vector<size_t>{0, 100}));
        CHECK((chunk_bounds(100, 0) == std::vector<size_t>{0, 100}));
        for (size_t t : {0, 37, 99}) CHECK(chunk_weight(chunk_bounds(100, 100), 0, t, 10) == 1.0f);
        const auto cb = chunk_bounds(1001, 300);
        CHECK(cb.size() == 5 && cb.front() == 0 && cb.back() == 1001);
        for (size_t k = 0; k + 1 < cb.size(); ++k) CHECK(cb[k + 1] - cb[k] <= 300 && cb[k + 1] - cb[k] >= 250);
        bool sums = true, monotone = true;
        for (size_t t = 0; t < 1001; ++t) {
            float s = 0;
            for (size_t k = 0; k + 1 < cb.size(); ++k) s += chunk_weight(cb, k, t, 20);
            sums = sums && std::fabs(s - 1.0f) < 1e-6f;
            if (t > 0 && t < cb[2] - 20)  // part 1 fades in across its first bound
                monotone = monotone && chunk_weight(cb, 1, t, 20) >= chunk_weight(cb, 1, t - 1, 20);
        }
        CHECK(sums);
        CHECK(monotone);
        CHECK(chunk_weight(cb, 1, cb[1] - 21, 20) == 0.0f && chunk_weight(cb, 1, cb[1] + 20, 20) == 1.0f);
        CHECK(chunk_weight(cb, 0, cb[1] + 20, 20) == 0.0f && chunk_weight(cb, 0, cb[1] - 21, 20) == 1.0f);
    }
    {
        const int r = 24000;
        std::vector<float> sine(r);  // 1 s, 440 Hz
        for (int i = 0; i < r; ++i) sine[i] = 0.5f * (float)std::sin(2 * 3.14159265358979 * 440 * i / r);
        auto up = resample(sine, r, 44100);
        CHECK(up.size() == 44100);
        CHECK(std::fabs(rms(up.data() + 1000, 40000) - 0.5 / std::sqrt(2.0)) < 0.01);
        // zero crossings preserved -> frequency preserved
        int zc = 0;
        for (size_t i = 1001; i < 41000; ++i) zc += (up[i - 1] < 0) != (up[i] < 0);
        CHECK(std::abs(zc - (int)std::lround(2 * 440 * 40000 / 44100.0)) <= 2);
        auto st = time_stretch(sine, r, 1.25);
        CHECK(st.size() == (size_t)(r / 1.25));
        int zc2 = 0;  // pitch unchanged: same crossings per second
        for (size_t i = 2001; i < 16000; ++i) zc2 += (st[i - 1] < 0) != (st[i] < 0);
        CHECK(std::abs(zc2 - (int)std::lround(2 * 440 * 14000.0 / r)) <= 4);
        CHECK(std::fabs(rms(st.data() + 2000, 14000) - rms(sine.data(), sine.size())) < 0.03);
        {
            // a loud tone: peaks held at ~-1 dBFS, a quiet one untouched, no distortion
            std::vector<float> loud(sine.size()), quiet2(sine.size());
            for (size_t i = 0; i < sine.size(); ++i) loud[i] = sine[i] * 4.0f, quiet2[i] = sine[i];
            limit_peaks(loud, r, 1);
            limit_peaks(quiet2, r, 1);
            float pk = 0;
            for (float v : loud) pk = std::max(pk, std::fabs(v));
            CHECK(pk <= 0.8901f && pk > 0.85f);
            CHECK(quiet2 == sine);
            // steady state: the limited tone is a scaled sine (THD tiny)
            double err = 0, en = 0;
            double num = 0, den = 0;
            for (int i = r / 2; i < r; ++i) num += (double)loud[i] * sine[i], den += (double)sine[i] * sine[i];
            const double k = num / den;
            for (int i = r / 2; i < r; ++i) err += std::pow(loud[i] - k * sine[i], 2), en += std::pow(k * sine[i], 2);
            CHECK(10 * std::log10(en / err) > 40);
        }
        {
            std::vector<float> dc(r, 0.5f), tone = sine;
            high_pass(dc, r, 70.0);
            high_pass(tone, r, 70.0);
            CHECK(std::fabs(dc.back()) < 1e-3);  // DC removed
            CHECK(std::fabs(rms(tone.data() + r / 2, r / 2) - rms(sine.data() + r / 2, r / 2)) < 0.01);  // 440 Hz kept
        }
        std::vector<float> quiet(r, 0.0f);
        CHECK(active_rms(quiet.data(), quiet.size(), r) == 0.0);
        for (int i = 0; i < r / 2; ++i) quiet[i] = sine[i];
        CHECK(std::fabs(active_rms(quiet.data(), quiet.size(), r) - 0.5 / std::sqrt(2.0)) < 0.01);
    }

    // ---- dubbing: subtitles, grouping, timing, voice sample ----
    {
        const std::string srt = "dub_test.srt";
        {
            std::ofstream f(srt, std::ios::binary);
            f << "\xEF\xBB\xBF" "1\r\n00:00:01,000 --> 00:00:02,500\r\n<i>Hello there,</i>\r\n\r\n"
                 "2\r\n00:00:02,600 --> 00:00:04,000\r\nmy friend.\r\n\r\n"
                 "3\r\n00:00:06,000 --> 00:00:07,000\r\nNext line\r\nwith two rows!\r\n\r\n"
                 "4\r\n00:00:07,100 --> 00:00:08,000\r\n   \r\n";
        }
        auto cues = read_srt_cues(srt);
        CHECK(cues.size() == 3);
        CHECK(cues.size() == 3 && cues[0].text == "Hello there," && std::fabs(cues[0].start - 1.0) < 1e-9);
        CHECK(cues.size() == 3 && cues[2].text == "Next line with two rows!");
        auto lines = group_cues(cues);
        CHECK(lines.size() == 2);
        CHECK(lines.size() == 2 && lines[0].text == "Hello there, my friend." && lines[0].end == 4.0);
        std::remove(srt.c_str());

        // Regression: no blank line between two cues. Splitting at blank lines read
        // "2" and the timing line as text of cue 1, so the dub spoke them.
        {
            std::ofstream f(srt, std::ios::binary);
            f << "1\n00:00:01,000 --> 00:00:02,000\nFirst line.\n"
                 "2\n00:00:02,500 --> 00:00:04,000\n{\\an8}Second line.\n\n"
                 "3\n00:00:05,000 --> 00:00:06,000\nThird line.\n";
        }
        cues = read_srt_cues(srt);
        CHECK(cues.size() == 3);
        CHECK(cues.size() == 3 && cues[0].text == "First line." && cues[0].end == 2.0);
        CHECK(cues.size() == 3 && cues[1].text == "Second line." && cues[1].start == 2.5);
        for (const auto& c : cues) CHECK(c.text.find("-->") == std::string::npos && c.text.find("00:00") == std::string::npos);
        std::remove(srt.c_str());

        // fits: no speed-up, starts on time
        auto p = place_lines(lines, {2.0, 1.0}, 1.35, 10);
        CHECK(p[0].at == 1.0 && p[0].speed == 1.0 && p[0].overrun == 0 && p[1].at == 6.0);
        // too long: sped up to the limit, then pushes the next line
        p = place_lines(lines, {7.0, 1.0}, 1.35, 10);
        CHECK(std::fabs(p[0].speed - 1.35) < 1e-9 && p[0].overrun > 0 && p[1].at > 6.0);
        // slightly long: sped up just enough
        p = place_lines(lines, {5.5, 1.0}, 1.35, 10);
        CHECK(p[0].speed > 1.0 && p[0].speed < 1.35 && p[0].overrun < 1e-6 && p[1].at == 6.0);

        std::vector<SubCue> src;
        for (int i = 0; i < 12; ++i)
            src.push_back({i * 2.0, i * 2.0 + 1.9, i % 3 == 2 ? "one two three four five." : "one two three four five"});
        double rs = 0, re = 0;
        std::string rt;
        CHECK(choose_reference(src, &rs, &re, &rt));
        CHECK(re - rs >= 5.0 && re - rs <= 14.0 && !rt.empty() && rt.back() == '.');
        CHECK(!choose_reference({}, &rs, &re, &rt));
        CHECK(max_plausible_seconds("Gracias.") == 4.0);  // short lines: at least 4 s
        CHECK(max_plausible_seconds(std::string(100, 'a')) == 36.0);
        CHECK(max_plausible_seconds("\xE4\xBD\xA0\xE5\xA5\xBD") == 4.0);  // counts codepoints, not bytes
        CHECK(find_tts_model("qwen3-tts") && find_tts_model("omnivoice") && !find_tts_model("nope"));
        // Several voice-sample stretches: best first, not overlapping, ~as long as asked.
        {
            std::vector<SubCue> talk;
            for (int i = 0; i < 40; ++i)  // 2 s cues with a 1 s pause every 5 cues
                talk.push_back({i * 2.0 + (i / 5), i * 2.0 + (i / 5) + 1.9, i % 5 == 4 ? "one two three four five." : "one two three four five"});
            const auto one = choose_references(talk, 0);
            CHECK(one.size() == 1);
            const auto many = choose_references(talk, 30);
            double sum = 0;
            for (size_t x = 0; x < many.size(); ++x) {
                sum += many[x].end - many[x].start;
                for (size_t y = 0; y < x; ++y) CHECK(many[x].end <= many[y].start || many[x].start >= many[y].end);
            }
            CHECK(many.size() >= 3 && sum >= 30 && many[0].start == one[0].start);
            CHECK(choose_references({}, 30).empty());
        }
        // Tone matching: measure, equalise, and move one spectrum toward another.
        {
            const int r = 44100;
            std::mt19937 rng(7);
            std::normal_distribution<float> nd(0.0f, 0.1f);
            std::vector<float> white(r * 3);
            for (auto& v : white) v = nd(rng);
            const std::vector<double> centres = {125, 250, 500, 1000, 2000, 4000, 8000};
            const auto flat = band_levels_db(white, r, centres);
            std::vector<float> boosted = white;
            apply_eq(boosted, r, centres, {0, 0, 0, 6, 0, 0, 0});
            const auto lb = band_levels_db(boosted, r, centres);
            CHECK(lb[3] - flat[3] > 3.5 && lb[3] - flat[3] < 7.0 && std::fabs(lb[0] - flat[0]) < 0.5);
            const auto g = tone_match_gains({0, 0, 0, 20, 0, 0, 0}, {10, 10, 10, 10, 10, 10, 10}, 6.0, 1.0);

            for (double x : g) CHECK(std::fabs(x) <= 6.0);
            CHECK(g[3] > 1.0 && g[3] > g[0] + 2.0);  // relative to the overall level, smoothed
            // A "dull" voice (highs cut) is brought most of the way back to the bright one.
            std::vector<float> dull = white;
            apply_eq(dull, r, centres, {0, 0, 0, 0, -4, -5, -5});
            const auto want = band_levels_db(white, r, centres);
            std::vector<double> eq(centres.size(), 0.0);
            for (int pass = 0; pass < 2; ++pass) {
                std::vector<float> t = dull;
                apply_eq(t, r, centres, eq);
                const auto gg = tone_match_gains(want, band_levels_db(t, r, centres));
                for (size_t k = 0; k < eq.size(); ++k) eq[k] += gg[k];
            }
            std::vector<float> fixed = dull;
            apply_eq(fixed, r, centres, eq);
            const auto lf = band_levels_db(fixed, r, centres), ld = band_levels_db(dull, r, centres);
            const double before = (want[5] - want[1]) - (ld[5] - ld[1]), after = (want[5] - want[1]) - (lf[5] - lf[1]);
            CHECK(std::fabs(after) < 0.5 * std::fabs(before));
            CHECK(band_levels_db(white, 16000, {8000})[0] < -150);  // above Nyquist: skipped
        }
        // Long lines are spoken in sentence-sized parts.
        CHECK(split_for_speech("Short line.") == std::vector<std::string>{"Short line."});
        {
            const std::string s1 = "In a world full of turmoil, conflict and worry, the Lord invites us to a place.";
            const std::string s2 = "There he teaches us his plan of happiness, gives us blessings, and helps us.";
            const auto p = split_for_speech(s1 + " " + s2, 100);
            CHECK(p.size() == 2 && p[0] == s1 && p[1] == s2);
            const auto q = split_for_speech(s1, 40);  // one long sentence: at commas, rejoined to the limit
            CHECK(q.size() >= 2);
            for (const auto& x : q) CHECK(x.size() <= 40);
            std::string joined;
            for (const auto& x : q) joined += (joined.empty() ? "" : " ") + x;
            CHECK(joined == s1);
            CHECK(split_for_speech("Version 3.5 of U.S. law is long enough here.", 20).size() >= 2);
            std::string nopunct;
            for (int i = 0; i < 60; ++i) nopunct += "word ";
            for (const auto& x : split_for_speech(nopunct, 50)) CHECK(x.size() <= 50 && !x.empty());
            // CJK counts as three per character (each "你好。" is 9).
            std::string zh;
            for (int i = 0; i < 15; ++i) zh += "\xE4\xBD\xA0\xE5\xA5\xBD\xE3\x80\x82";  // 你好。
            CHECK(split_for_speech(zh, 40).size() == 4);  // 15 x 9, four sentences per part
            // Parts of about equal length, not one full part and a stub.
            const std::string es =
                "En un mundo lleno de agitaci\xC3\xB3n, conflictos y preocupaciones, el Se\xC3\xB1or nos invita a un "
                "lugar de seguridad espiritual, armon\xC3\xAD" "a y santidad: la casa del Se\xC3\xB1or.";
            const auto e2 = split_for_speech(es, 140);
            CHECK(e2.size() == 2 && e2[0].size() < 110 && e2[1].size() > 40);
            for (const auto& x : split_for_speech(es, 90)) CHECK(x.size() <= 92);
        }
    }


    {
        const std::vector<SubCue> cues = {{0, 0.4, "First"}, {0.4, 1, "sentence."}, {1.1, 2, "Second."}};
        const auto lines = group_cues(cues);
        CHECK(lines.size() == 2 && lines[0].cue_count == 2 && lines[1].first_cue == 2);
        const auto places = place_lines(lines, {4.0, 1.0}, 1.35, 5);
        const std::vector<double> actual = {4.0 / places[0].speed, 1.0 / places[1].speed};
        const auto timed = retime_cues(cues, lines, places, actual, 2.5);
        CHECK(timed.size() == 3);
        CHECK(std::fabs(timed[0].start - (places[0].at + 2.5)) < 1e-9);
        CHECK(std::fabs(timed[1].end - (places[0].at + actual[0] + 2.5)) < 1e-9);
        CHECK(timed[2].start > 5.5 && timed[2].text == "Second.");
        CHECK(timed[0].end == timed[1].start && timed[1].end <= timed[2].start);
    }
    {
        // A line that couldn't be spoken keeps its subtitle times (shifted by the offset).
        const std::vector<SubCue> cues = {{1, 2, "Spoken."}, {3, 4.5, "Silent."}};
        const auto lines = group_cues(cues);
        const auto places = place_lines(lines, {1.0, 0.0}, 1.35, 6);
        const auto timed = retime_cues(cues, lines, places, {1.0, 0.0}, 2.0);
        CHECK(timed.size() == 2 && std::fabs(timed[1].start - 5.0) < 1e-9 && std::fabs(timed[1].end - 6.5) < 1e-9);
    }
    {
        namespace fs = std::filesystem;
        const fs::path out = "transaction-test.wav";
        auto write = [](const fs::path& p, const std::string& data) { std::ofstream f(p, std::ios::binary); f << data; };
        auto read = [](const fs::path& p) { std::ifstream f(p, std::ios::binary); return std::string(std::istreambuf_iterator<char>(f), {}); };
        write(out, "original");
        fs::path temporary;
        {
            OutputTransaction job(out);
            temporary = job.rendered();
            write(temporary, "unfinished");
            write(job.audio(), "ready mix");
            const auto fallback = job.save_fallback();
            CHECK(read(fallback) == "ready mix");
            bool collision = false;
            try { job.save_fallback(); } catch (...) { collision = true; }
            CHECK(collision && read(fallback) == "ready mix" && read(out) == "original");
            fs::remove(fallback);
        }
        CHECK(read(out) == "original" && !fs::exists(temporary));
        {
            OutputTransaction job(out);
            bool failed = false;
            try { job.commit(); } catch (...) { failed = true; }
            CHECK(failed && read(out) == "original");
            write(job.rendered(), "complete");
#ifdef _WIN32
            HANDLE locked = CreateFileW(out.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            CHECK(locked != INVALID_HANDLE_VALUE);
            failed = false;
            try { job.commit(); } catch (...) { failed = true; }
            CHECK(failed && read(out) == "original");
            if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
#endif
            job.commit();
        }
        CHECK(read(out) == "complete");
        fs::remove(out);
    }
    if (ffmpeg_available()) {
#ifdef _WIN32
        SetEnvironmentVariableW(L"PARAKEET_TEST_TOKEN", L"expanded");
#endif
        const std::string path = "literal %PARAKEET_TEST_TOKEN% caf\xC3\xA9.wav";
        const std::string converted = "converted %PARAKEET_TEST_TOKEN% caf\xC3\xA9.wav";
        write_wav(path, std::vector<float>(1600, 0.1f), 16000, 1);
        CHECK(load_audio(path).size() == 1600);
        CHECK(run_ffmpeg({"-y", "-i", path, converted}) == 0);
        CHECK(load_audio(converted).size() == 1600);
        std::filesystem::remove(path_from_utf8(path));
        std::filesystem::remove(path_from_utf8(converted));
    } else std::printf("FFmpeg integration skipped: FFmpeg unavailable\n");

    // Several speakers: where a subtitle is split, and who gets what.
    {
        const std::string t = "Are you coming? Yes, I am on my way.";
        CHECK(t.substr(text_split_point(t, 0.35)) == "Yes, I am on my way.");  // the sentence end wins
        const std::string t2 = "If you want to, we can go there now";
        CHECK(t2.substr(text_split_point(t2, 0.55)) == "we can go there now");  // then the comma
        const std::string words = "one two three four five six seven eight";
        CHECK(words.substr(text_split_point(words, 0.5)) == "five six seven eight");
        const std::string zh = "\xE4\xBD\xA0\xE5\xA5\xBD\xE5\x90\x97\xEF\xBC\x9F\xE6\x88\x91\xE5\xBE\x88\xE5\xA5\xBD";  // 你好吗？我很好
        CHECK(zh.substr(text_split_point(zh, 0.5)) == "\xE6\x88\x91\xE5\xBE\x88\xE5\xA5\xBD");
        CHECK(text_split_point("word", 0.5) == 0 && text_split_point("", 0.5) == 0);

        auto cue = [](double s, double e, const std::string& text) {
            pk::SrtCue c;
            c.start = s, c.end = e, c.text = text, c.timed = true;
            return c;
        };
        const std::vector<pk::SrtCue> cues = {cue(0, 4, "Are you coming?\nYes, I am."), cue(5, 9, "Good, see you there."),
                                              cue(10, 14, "Okay then, bye."), cue(15, 16, "Bye!")};
        // A asks, B answers in the same subtitle; a short "mm" from A inside B's
        // second subtitle doesn't split it; nobody is found in the last one.
        const std::vector<SpeakerTurn> turns = {{0.1, 1.9, 0}, {2.1, 3.9, 1}, {5.0, 7.0, 0}, {7.0, 7.3, 1},
                                                {7.3, 9.0, 0}, {10.0, 14.0, 1}};
        const auto parts = split_by_speaker(cues, {0, 0, 1, -1}, turns);
        CHECK(parts.size() == 5);
        if (parts.size() == 5) {
            CHECK(parts[0].text == "Are you coming?" && parts[0].speaker == 0 && parts[0].group == 0);
            CHECK(parts[1].text == "Yes, I am." && parts[1].speaker == 1 && parts[1].group == 0);
            CHECK(parts[0].start == 0 && std::fabs(parts[0].end - 2.0) < 1e-9 && parts[1].start == parts[0].end &&
                  parts[1].end == 4);
            CHECK(parts[2].text == "Good, see you there." && parts[2].speaker == 0 && parts[2].group == 1);
            CHECK(parts[3].speaker == 1 && parts[3].group == 2);
            CHECK(parts[4].speaker == 1 && parts[4].group == 3);  // the speaker before
        }
        // Without turns, the labels decide; a change too short to split leaves one part.
        const auto plain = split_by_speaker(cues, {2, 2, 3, 3}, {});
        CHECK(plain.size() == 4 && plain[0].speaker == 2 && plain[3].speaker == 3);
        CHECK(split_by_speaker({cue(0, 4, "Yes. No.")}, {-1}, {{0, 3.5, 0}, {3.5, 4, 1}}).size() == 1);

        // Quick turns: A asks two short sentences fast, B answers slowly. By the text's
        // length alone the change falls after "you?" and B would get "I called twice.";
        // with the word timings each word goes to the speaker heard saying it. A short
        // overlap of A inside B's answer (0.3 s) changes nothing.
        {
            const pk::SrtCue quick = cue(0, 8, "Where were you? I called twice. Sorry. My phone died. I was at the gym all day.");
            const std::vector<SpeakerTurn> qt = {{0, 1.85, 0}, {1.85, 8, 1}, {3.0, 3.3, 0}};
            const auto rough = split_by_speaker({quick}, {-1}, {{0, 1.85, 0}, {1.85, 8, 1}});
            CHECK(rough.size() == 2 && rough[0].text == "Where were you?");  // why the words are needed
            const std::vector<Word> w = {{"Where", 0, 0.3},   {"were", 0.3, 0.5},  {"you?", 0.5, 0.8},  {"I", 0.9, 1.0},
                                         {"called", 1.0, 1.3}, {"twice.", 1.3, 1.8}, {"Sorry.", 1.95, 2.6}, {"My", 3.0, 3.2},
                                         {"phone", 3.2, 3.6}, {"died.", 3.6, 4.1},  {"I", 4.5, 4.6},     {"was", 4.6, 4.8},
                                         {"at", 4.8, 4.9},    {"the", 4.9, 5.0},    {"gym", 5.0, 5.4},   {"all", 5.4, 5.6},
                                         {"day.", 5.6, 6.2}};
            const auto exact = split_by_speaker({quick}, {-1}, qt, 1.0, w);
            CHECK(exact.size() == 2);
            if (exact.size() == 2) {
                CHECK(exact[0].text == "Where were you? I called twice." && exact[0].speaker == 0);
                CHECK(exact[1].text == "Sorry. My phone died. I was at the gym all day." && exact[1].speaker == 1);
                CHECK(std::fabs(exact[0].end - 1.875) < 1e-9 && exact[1].start == exact[0].end && exact[1].group == 0);
            }
            // Three quick turns in one subtitle (A, B, A), each found.
            const std::vector<SpeakerTurn> three = {{0, 0.85, 0}, {0.85, 1.9, 1}, {1.9, 8, 0}};
            const auto t3 = split_by_speaker({quick}, {-1}, three, 1.0, w);
            CHECK(t3.size() == 3 && t3[0].text == "Where were you?" && t3[1].text == "I called twice." &&
                  t3[1].speaker == 1 && t3[2].speaker == 0);
            // The words file round trip.
            const auto back = parse_words(format_words(w) + "junk line\n");
            CHECK(back.size() == w.size() && back[2].text == "you?" && std::fabs(back[16].end - 6.2) < 1e-9);
        }

        // Lines of different speakers are never joined; a voice sample is one speaker's.
        std::vector<SubCue> sc = {{0, 2, "and then", 0}, {2.1, 4, "he left", 1}, {4.2, 9, "we waited for him there", 1},
                                  {9.3, 14, "all evening long and more", 1}};
        const auto lines = group_cues(sc);
        CHECK(lines.size() == 2 && lines[0].speaker == 0 && lines[1].speaker == 1);
        const auto refs = choose_references(sc, 0, 0);
        CHECK(refs.empty());  // speaker 0 has only 2 s
        const auto refs1 = choose_references(sc, 0, 1);
        CHECK(refs1.size() == 1 && refs1[0].start >= 2.1);
    }

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all helper tests passed\n");
    return 0;
}
