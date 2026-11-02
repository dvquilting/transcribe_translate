// Tests of the shared, dependency-free code: text cleaning, the SubRip reader
// and writer, atomic file writes and the model catalog.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "docx_writer.h"
#include "fileio.h"
#include "model_catalog.h"
#include "speakers.h"
#include "srt.h"
#include "utf8_path.h"
#include "utf8_text.h"

namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static void test_text() {
    CHECK(pk::trim(" \t a b \r\n") == "a b");
    CHECK(pk::normalize_space("  a \n b\t c ") == "a b c");
    CHECK(pk::utf8_length("caf\xC3\xA9") == 4);
    CHECK(pk::clean_input_text("\xEF\xBB\xBFline1\r\nline2\rline3") == "line1\nline2\nline3");
    CHECK(pk::clean_input_text("caf\xE9") == "caf\xC3\xA9");  // Latin-1 -> UTF-8
    // Windows-1252: smart quotes, euro, dash; undefined 0x81 kept as U+0081
    CHECK(pk::clean_input_text("\x93Hi\x94 \x80 5\x96" "5 caf\xE9\x81") ==
          "\xE2\x80\x9CHi\xE2\x80\x9D \xE2\x82\xAC 5\xE2\x80\x93" "5 caf\xC3\xA9\xC2\x81");
    // UTF-8 with one stray Windows-1252 byte: only that byte is converted, the
    // UTF-8 accents are kept (not garbled into "cafÃ©").
    CHECK(pk::clean_input_text("caf\xC3\xA9 na\xC3\xAFve \x93quote\x94 \xE9t\xE9") ==
          "caf\xC3\xA9 na\xC3\xAFve \xE2\x80\x9Cquote\xE2\x80\x9D \xC3\xA9t\xC3\xA9");
    CHECK(pk::clean_input_text("\xE2\x82\xAC 5 \x80") == "\xE2\x82\xAC 5 \xE2\x82\xAC");
    // A truncated sequence at the end and an overlong form count as invalid bytes.
    CHECK(pk::clean_input_text("\xC3\xA9\xC3") == "\xC3\xA9\xC3\x83");
    CHECK(pk::clean_input_text("\xC3\xA9 \xC0\xAF") == "\xC3\xA9 \xC3\x80\xC2\xAF");
    // Valid UTF-8 (4-byte emoji included) is left alone.
    CHECK(pk::clean_input_text("\xF0\x9F\x98\x80 caf\xC3\xA9") == "\xF0\x9F\x98\x80 caf\xC3\xA9");
}

static void test_srt() {
    {
        // no blank line between cues; a blank line inside a cue; CRLF + BOM
        const std::string doc =
            "\xEF\xBB\xBF" "1\r\n00:00:01,000 --> 00:00:02,000\r\nHello\r\n"
            "2\r\n00:00:03,000 --> 00:00:04,000\r\nWorld\r\n\r\n"
            "3\r\n00:00:05,000 --> 00:00:06,000\r\nLine a\r\n\r\nLine b\r\n\r\n"
            "4\r\n00:00:07,000 --> 00:00:08,000\r\nEnd.\r\n";
        auto cues = pk::parse_srt(doc);
        CHECK(cues.size() == 4);
        if (cues.size() == 4) {
            CHECK(cues[0].index == "1" && cues[0].text == "Hello" && cues[0].timed);
            CHECK(cues[1].index == "2" && cues[1].text == "World" && cues[1].start == 3.0);
            CHECK(cues[2].index == "3" && cues[2].text == "Line a\nLine b");
            CHECK(cues[3].index == "4" && cues[3].text == "End." && cues[3].end == 8.0);
            CHECK(cues[3].timing == "00:00:07,000 --> 00:00:08,000");
        }
        auto noidx = pk::parse_srt("00:00:01,000 --> 00:00:02,000\nA\n00:00:03,000 --> 00:00:04,000\nB\n");
        CHECK(noidx.size() == 2 && noidx[0].text == "A" && noidx[1].text == "B" && noidx[1].index.empty());
    }
    {
        // Windows-1252 text, '.' before the milliseconds, a timing line that can't be read
        auto cues = pk::parse_srt("1\n00:00:01.500 --> 00:00:02.250\nCaf\xE9\n\n2\nsoon --> later\nX\n");
        CHECK(cues.size() == 2);
        if (cues.size() == 2) {
            CHECK(cues[0].text == "Caf\xC3\xA9" && cues[0].start == 1.5 && cues[0].end == 2.25 && cues[0].timed);
            CHECK(!cues[1].timed && cues[1].text == "X");
        }
        CHECK(pk::parse_srt("").empty() && pk::parse_srt("no cues here\n").empty());
    }
    {
        // the writer writes back what the reader read
        const std::string doc = "1\n00:00:01,000 --> 00:00:02,000\n<i>Hello</i>\nthere\n\n"
                                "2\n00:00:03,000 --> 00:00:04,500\nWorld\n\n";
        CHECK(pk::format_srt(pk::parse_srt(doc)) == doc);
        pk::SrtCue c;
        c.timing = pk::srt_timing(1.0, 2.5);
        c.text = "no number";
        CHECK(pk::format_srt({c}) == "00:00:01,000 --> 00:00:02,500\nno number\n\n");
    }
    double t = -1;
    CHECK(pk::parse_srt_time("01:02:03,004", &t) && t == 3723.004);
    CHECK(!pk::parse_srt_time("abc", &t));
    // The fraction is decimal whatever its length (",5" is half a second, not 5 ms).
    for (const char* half : {"00:00:01,5", "00:00:01,50", "00:00:01,500", "00:00:01.5000", "00:00:01,500000"})
        CHECK(pk::parse_srt_time(half, &t) && std::fabs(t - 1.5) < 1e-9);
    CHECK(pk::parse_srt_time("00:00:01,0625", &t) && std::fabs(t - 1.0625) < 1e-9);
    CHECK(pk::parse_srt_time("00:00:02,05", &t) && std::fabs(t - 2.05) < 1e-9);
    CHECK(pk::parse_srt_time("00:00:02", &t) && t == 2.0);    // no fraction
    CHECK(pk::parse_srt_time("00:00:02,", &t) && t == 2.0);   // empty fraction
    CHECK(pk::parse_srt_time("00:00:03,25 ", &t) && std::fabs(t - 3.25) < 1e-9);
    CHECK(pk::srt_time(3661.2345) == "01:01:01,235" || pk::srt_time(3661.2345) == "01:01:01,234");
    CHECK(pk::srt_time(59.9996) == "00:01:00,000");
    CHECK(pk::srt_time(-1.0) == "00:00:00,000");
    CHECK(pk::strip_srt_tags("<i>Hi</i> {\\an8}there <font color=\"red\">you</font>") == "Hi there you");
}

static void test_fileio(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    const fs::path utf8_target = pk::path_from_utf8(pk::path_to_utf8(dir) + "/sub dir/caf\xC3\xA9.srt");
    CHECK(pk::path_to_utf8(pk::path_from_utf8("caf\xC3\xA9.srt")) == "caf\xC3\xA9.srt");

    pk::write_file_atomic(utf8_target, "first");  // creates the missing folders
    CHECK(pk::read_file(utf8_target) == "first");
    pk::write_file_atomic(utf8_target, "second\r\nline");  // replaces, bytes unchanged
    CHECK(pk::read_file(utf8_target) == "second\r\nline");
    CHECK(!fs::exists(fs::path(utf8_target) += ".tmp"));

    // A target that can't be replaced (here: a folder) fails without leaving the temporary file.
    const fs::path blocked = dir / "blocked";
    fs::create_directories(blocked / "inside");
    bool threw = false;
    try {
        pk::write_file_atomic(blocked, "data");
    } catch (const std::exception& e) {
        threw = std::string(e.what()).find("blocked") != std::string::npos;
    }
    CHECK(threw && fs::is_directory(blocked) && !fs::exists(fs::path(blocked) += ".tmp"));

    // replace_file moves over an existing file
    const fs::path a = dir / "a.txt", b = dir / "b.txt";
    pk::write_file_atomic(a, "A");
    pk::write_file_atomic(b, "B");
    pk::replace_file(a, b);
    CHECK(!fs::exists(a) && pk::read_file(b) == "A");

    threw = false;
    try {
        pk::read_file(dir / "missing.txt");
    } catch (const std::exception& e) {
        threw = std::string(e.what()).rfind("cannot open ", 0) == 0;
    }
    CHECK(threw);
    fs::remove_all(dir, ec);
}

static void test_catalog() {
    std::set<std::string> keys;
    for (const auto& m : pk::asr_models()) {
        CHECK(keys.insert(m.key).second);
        CHECK(!m.display.empty() && !m.file.empty() && m.url.rfind("https://", 0) == 0 && !m.size.empty());
        CHECK(!m.description.empty());
        CHECK(m.chunked == (m.max_chunk_seconds > 0));
    }
    CHECK(pk::find_asr_model("parakeet-ultra") && !pk::find_asr_model("nope"));
    CHECK(pk::find_asr_model("cohere") && pk::find_asr_model("cohere")->needs_language);
    CHECK(pk::find_asr_model("qwen") && !pk::find_asr_model("qwen")->needs_language);
    // CrispASR chooses the model type from these file names.
    CHECK(pk::aligner_model().file.find("forced-aligner") != std::string::npos);
    CHECK(pk::punctuation_model().file.find("pcs") != std::string::npos);
    for (const auto& m : pk::tts_models()) CHECK(!m.backend.empty() && !m.description.empty());
    const pk::TtsModel* q = pk::find_tts_model("qwen3-tts");
    CHECK(q && q->display == "Qwen3-TTS 1.7B" && q->size == "2.0 GB" && q->langs.size() == 10);
    CHECK(pk::find_tts_model("omnivoice") && pk::find_tts_model("omnivoice")->langs.empty());
}

// The speakers file: written and read back, speakers merged, and subtitles
// split by speaker shown as one again with dashes.
static void test_speakers() {
    pk::SpeakerMap m;
    m.speakers = {{0, "Speaker 1", 10.5, 1, 5}, {1, "Anna\tB", 3, 6, 7}, {2, "Speaker 3", 1, 8, 9}};
    m.cues = {{0, 2, 0, 0}, {2, 4, 1, 0}, {5, 9, 0, 1}, {10, 12, 2, 2}};
    const std::string text = pk::format_speakers(m);
    pk::SpeakerMap r;
    std::string error;
    CHECK(pk::parse_speakers(text, &r, &error));
    CHECK(r.speakers.size() == 3 && r.cues.size() == 4);
    CHECK(r.speakers.size() == 3 && r.speakers[1].name == "Anna B" && r.speakers[0].seconds == 10.5);
    CHECK(r.at(2.5, 3.5) && r.at(2.5, 3.5)->speaker == 1);
    CHECK(r.at(4.7, 4.9) && r.at(4.7, 4.9)->group == 1);  // nearest
    CHECK(!r.at(4.2, 4.6, false));
    CHECK(!pk::parse_speakers("speaker\t0\tx\t1\t0\t1\n", &r, &error));  // no header
    CHECK(!pk::parse_speakers("# speakers 1\ncue\t0\t1\t5\t0\n", &r, &error) && error.find("speaker 5") != std::string::npos);
    CHECK(!pk::parse_speakers("# speakers 1\ncue\t0\tx\t0\t0\n", &r, &error));

    // Speaker 3 is really Speaker 1.
    pk::SpeakerMap merged = m;
    pk::merge_speakers(&merged, {{2, 0}});
    CHECK(merged.speakers.size() == 2 && merged.speakers[0].seconds == 11.5 && merged.cues[3].speaker == 0);

    auto cue = [](double s, double e, const std::string& t) {
        pk::SrtCue c;
        c.index = "1", c.start = s, c.end = e, c.text = t, c.timed = true, c.timing = pk::srt_timing(s, e);
        return c;
    };
    const auto joined = pk::join_speaker_parts(
        {cue(0, 2, "Are you coming?"), cue(2, 4, "Yes,\nI am."), cue(5, 9, "Good."), cue(10, 12, "Bye.")}, m);
    CHECK(joined.size() == 3);
    CHECK(joined.size() == 3 && joined[0].text == "- Are you coming?\n- Yes, I am." && joined[0].start == 0 &&
          joined[0].end == 4 && joined[0].timing == pk::srt_timing(0, 4) && joined[0].index == "1");
    CHECK(joined.size() == 3 && joined[1].text == "Good." && joined[2].index == "3");
    // One group, one speaker (after a merge): joined without dashes.
    pk::SpeakerMap same = m;
    pk::merge_speakers(&same, {{1, 0}});
    const auto plain = pk::join_speaker_parts({cue(0, 2, "Are you coming?"), cue(2, 4, "Yes.")}, same);
    CHECK(plain.size() == 1 && plain[0].text == "Are you coming? Yes.");
    // Cues the map doesn't have stay as they are.
    CHECK(pk::join_speaker_parts({cue(0, 1.5, "x"), cue(2, 4, "y")}, m).size() == 2);
}

// The .docx writer: a zip with the Word parts, the text escaped, paragraphs and
// line breaks kept. (Opening the result in Word/LibreOffice was checked by hand.)
static void test_docx() {
    using V = std::vector<std::string>;
    CHECK(pk::text_paragraphs("a\nb\n\n\n  c  \r\n\r\nd") == (V{"a\nb", "c", "d"}));
    CHECK(pk::text_paragraphs("").empty() && pk::text_paragraphs("\n \n").empty());
    const std::string d = pk::make_docx({"Talk & more", "[Corrected by AI]", "x < y\nz\n\nnext \x01", "de"});
    CHECK(d.compare(0, 4, "PK\x03\x04") == 0);
    CHECK(d.find("[Content_Types].xml") != std::string::npos && d.find("word/document.xml") != std::string::npos &&
          d.find("word/styles.xml") != std::string::npos);
    CHECK(d.find("Talk &amp; more") != std::string::npos && d.find("x &lt; y</w:t></w:r><w:r><w:br/>") != std::string::npos);
    CHECK(d.find("w:val=\"Subtitle\"/></w:pPr><w:r><w:t xml:space=\"preserve\">[Corrected by AI]") != std::string::npos);
    CHECK(d.find("next </w:t>") != std::string::npos);  // the control character is dropped
    CHECK(d.find("<w:lang w:val=\"de\"/>") != std::string::npos);
    CHECK(d.compare(d.size() - 22, 4, "PK\x05\x06") == 0);  // the end of the zip
}

int main() {
    test_text();
    test_srt();
    test_fileio(fs::temp_directory_path() / "pk_common_tests");
    test_catalog();
    test_docx();
    test_speakers();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
