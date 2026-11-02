#include "utf8_text.h"

#include <cstdint>

namespace pk {

namespace {

bool is_space(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

// Length of the valid UTF-8 sequence at s[i] (1 for ASCII), or 0 if the byte
// there does not start one (stray continuation byte, overlong form, surrogate,
// beyond U+10FFFF, cut short).
size_t utf8_sequence(const std::string& s, size_t i) {
    const unsigned char c = (unsigned char)s[i];
    if (c < 0x80) return 1;
    const size_t n = c >= 0xC2 && c <= 0xDF ? 2 : (c & 0xF0) == 0xE0 ? 3 : c >= 0xF0 && c <= 0xF4 ? 4 : 0;
    if (n == 0 || i + n > s.size()) return 0;
    for (size_t k = 1; k < n; ++k)
        if (((unsigned char)s[i + k] & 0xC0) != 0x80) return 0;
    const unsigned char c1 = (unsigned char)s[i + 1];
    if ((c == 0xE0 && c1 < 0xA0) || (c == 0xED && c1 >= 0xA0) || (c == 0xF0 && c1 < 0x90) || (c == 0xF4 && c1 >= 0x90))
        return 0;
    return n;
}

void append_utf8(std::string& o, uint32_t cp) {
    if (cp < 0x80) {
        o += (char)cp;
    } else if (cp < 0x800) {
        o += (char)(0xC0 | (cp >> 6));
        o += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        o += (char)(0xE0 | (cp >> 12));
        o += (char)(0x80 | ((cp >> 6) & 0x3F));
        o += (char)(0x80 | (cp & 0x3F));
    } else {
        o += (char)(0xF0 | (cp >> 18));
        o += (char)(0x80 | ((cp >> 12) & 0x3F));
        o += (char)(0x80 | ((cp >> 6) & 0x3F));
        o += (char)(0x80 | (cp & 0x3F));
    }
}

// Windows-1252 bytes 0x80-0x9F (where it differs from Latin-1). The five
// undefined bytes (0x81, 0x8D, 0x8F, 0x90, 0x9D) map to the C1 control of the
// same value, as Windows' own conversion does.
const uint16_t kCp1252High[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,  // 80-87
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,  // 88-8F
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,  // 90-97
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,  // 98-9F
};

}  // namespace

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && is_space((unsigned char)s[a])) ++a;
    while (b > a && is_space((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string normalize_space(const std::string& s) {
    std::string out;
    bool pending = false;
    for (unsigned char c : s) {
        if (is_space(c)) {
            pending = !out.empty();
        } else {
            if (pending) out += ' ';
            pending = false;
            out += (char)c;
        }
    }
    return out;
}

size_t utf8_length(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

std::string clean_input_text(const std::string& raw) {
    std::string s = raw;
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
        s.erase(0, 3);
    // Invalid UTF-8 is read as Windows-1252 (a superset of Latin-1 apart from
    // 0x80-0x9F). A file that also has valid multi-byte sequences is UTF-8 with
    // a few stray bytes (e.g. one line pasted from an old editor): only those
    // bytes are converted, so its accented letters stay as they are. A file
    // without any is converted as a whole.
    size_t multibyte = 0, invalid = 0;
    for (size_t i = 0; i < s.size();) {
        const size_t n = utf8_sequence(s, i);
        if (n == 0) ++invalid;
        else if (n > 1) ++multibyte;
        i += n ? n : 1;
    }
    auto from_cp1252 = [](std::string& o, unsigned char c) {
        append_utf8(o, c >= 0x80 && c < 0xA0 ? kCp1252High[c - 0x80] : c);
    };
    if (invalid > 0) {
        std::string u;
        u.reserve(s.size() + s.size() / 4);
        if (multibyte > 0) {
            for (size_t i = 0; i < s.size();) {
                const size_t n = utf8_sequence(s, i);
                if (n == 0) {
                    from_cp1252(u, (unsigned char)s[i++]);
                } else {
                    u.append(s, i, n);
                    i += n;
                }
            }
        } else {
            for (unsigned char c : s) from_cp1252(u, c);
        }
        s = u;
    }
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') {
            out += '\n';
            if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
        } else {
            out += s[i];
        }
    }
    return out;
}

}  // namespace pk
