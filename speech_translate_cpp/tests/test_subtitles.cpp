#include <cstdio>
#include <string>

#include "formats.h"
#include "subtitles.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main() {
    const std::string src =
        "1\n00:00:01,000 --> 00:00:02,000\nHello there,\n\n"
        "2\n00:00:02,000 --> 00:00:03,000\nmy friend. How are you?\n\n"
        "3\n00:00:04,000 --> 00:00:05,000\n<i>Fine.</i>\n";
    const std::string tr =
        "1\n00:00:01,000 --> 00:00:02,000\nHallo,\n\n"
        "2\n00:00:02,000 --> 00:00:03,000\nmein Freund. Wie geht's?\n\n"
        "3\n00:00:04,000 --> 00:00:05,000\n<i>Gut.</i>\n";
    const auto s = tl::parse_srt(src), t = tl::parse_srt(tr);
    const std::string bi = st::bilingual_srt(s, t);
    CHECK(bi.find("1\n00:00:01,000 --> 00:00:02,000\nHello there,\nHallo,\n\n") == 0);
    CHECK(bi.find("<i>Fine.</i>\n<i>Gut.</i>") != std::string::npos);
    CHECK(st::subtitles_to_text(t, 2) == "Hallo, mein Freund. Wie geht's?\n\nGut.\n");
    CHECK(st::with_suffix("C:\\a.b\\talk.mp3", ".de.srt") == "C:\\a.b\\talk.de.srt");
    CHECK(st::with_suffix("/x/noext", ".srt") == "/x/noext.srt");
    if (failures) return 1;
    std::printf("all tests passed\n");
    return 0;
}
