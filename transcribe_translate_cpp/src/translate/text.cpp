#include "text.h"

#include <cstdint>
#include <sstream>

namespace tl {

namespace {
// Decode one UTF-8 code point at s[i]; returns its length (1 for invalid bytes).
size_t utf8_next(const std::string& s, size_t i, uint32_t* cp) {
    const unsigned char c = (unsigned char)s[i];
    auto cont = [&](size_t k) { return i + k < s.size() && ((unsigned char)s[i + k] & 0xC0) == 0x80; };
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    if ((c & 0xE0) == 0xC0 && cont(1)) {
        *cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F);
        return 2;
    }
    if ((c & 0xF0) == 0xE0 && cont(1) && cont(2)) {
        *cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
        return 3;
    }
    if ((c & 0xF8) == 0xF0 && cont(1) && cont(2) && cont(3)) {
        *cp = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
        return 4;
    }
    *cp = 0xFFFD;
    return 1;
}

bool cjk_char(uint32_t cp) {
    return (cp >= 0x3040 && cp <= 0x30FF) ||  // kana
           (cp >= 0x3400 && cp <= 0x9FFF) ||  // CJK ideographs
           (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0x3000 && cp <= 0x303F) ||
           (cp >= 0xFF00 && cp <= 0xFFEF);
}
}  // namespace

bool has_letters(const std::string& s) {
    for (unsigned char c : s) {
        if (c >= 0x80) return true;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return true;
    }
    return false;
}

std::vector<std::string> split_sentences(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    const std::string t = normalize_space(text);
    for (size_t i = 0; i < t.size();) {
        uint32_t cp;
        const size_t n = utf8_next(t, i, &cp);
        cur.append(t, i, n);
        i += n;
        const bool cjk_end = cp == 0x3002 || cp == 0xFF01 || cp == 0xFF1F;  // 。！？
        bool end = cjk_end;
        if (cp == '.' || cp == '?' || cp == '!') {
            // Western punctuation ends a sentence only before a space (or the end),
            // so "3.5" and "e.g.x" stay whole. Closing quotes/brackets go along.
            size_t j = i;
            while (j < t.size() && (t[j] == '"' || t[j] == '\'' || t[j] == ')' || t[j] == ']')) cur += t[j++];
            i = j;
            end = i >= t.size() || t[i] == ' ';
        }
        if (end) {
            const std::string s = trim(cur);
            if (!s.empty()) out.push_back(s);
            cur.clear();
        }
    }
    if (!trim(cur).empty()) out.push_back(trim(cur));
    return out;
}

std::vector<std::string> group_sentences(const std::vector<std::string>& sentences, size_t max_chars) {
    std::vector<std::string> out;
    std::string cur;
    for (const auto& s : sentences) {
        if (!cur.empty() && utf8_length(cur) + 1 + utf8_length(s) > max_chars) {
            out.push_back(cur);
            cur.clear();
        }
        cur += (cur.empty() ? "" : " ") + s;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool is_cjk_language(const std::string& code) {
    return code == "zh" || code == "zh-Hant" || code == "yue" || code == "ja" || code == "th" || code == "km" ||
           code == "my" || code == "bo";
}

std::vector<std::string> split_blank_lines(const std::string& text) {
    std::vector<std::string> paras;
    std::string cur;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (trim(line).empty()) {
            if (!cur.empty()) paras.push_back(cur), cur.clear();
        } else {
            cur += (cur.empty() ? "" : "\n") + line;
        }
    }
    if (!cur.empty()) paras.push_back(cur);
    return paras;
}

std::string sanitize_utf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    const size_t n = s.size();
    for (size_t i = 0; i < n;) {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            out += (char)c;
            ++i;
            continue;
        }
        // Expected length and the allowed range of the second byte (rules out
        // overlong forms, surrogates and code points above U+10FFFF).
        size_t len = 0;
        unsigned char lo = 0x80, hi = 0xBF;
        if (c >= 0xC2 && c <= 0xDF) len = 2;
        else if (c == 0xE0) len = 3, lo = 0xA0;
        else if (c == 0xED) len = 3, hi = 0x9F;
        else if (c >= 0xE1 && c <= 0xEF) len = 3;
        else if (c == 0xF0) len = 4, lo = 0x90;
        else if (c >= 0xF1 && c <= 0xF3) len = 4;
        else if (c == 0xF4) len = 4, hi = 0x8F;
        size_t k = 1;  // bytes of a valid prefix so far
        if (len) {
            while (k < len && i + k < n) {
                const unsigned char b = (unsigned char)s[i + k];
                if (k == 1 ? (b < lo || b > hi) : (b & 0xC0) != 0x80) break;
                ++k;
            }
        }
        if (len && k == len) {
            out.append(s, i, len);
            i += len;
        } else if (len && i + k == n) {
            break;  // the text stops in the middle of a character (cut-off generation): drop the fragment
        } else {
            out += "\xEF\xBF\xBD";  // U+FFFD for the invalid byte(s)
            i += k;
        }
    }
    return out;
}

std::string tail_at_word(const std::string& s, size_t max_bytes) {
    if (s.size() <= max_bytes) return s;
    size_t start = s.size() - max_bytes;
    // Never start inside a multi-byte character.
    while (start < s.size() && ((unsigned char)s[start] & 0xC0) == 0x80) ++start;
    const size_t sp = s.find(' ', start);
    return sp == std::string::npos ? s.substr(start) : s.substr(sp + 1);
}

std::vector<std::string> wrap_text(const std::string& text, size_t width) {
    std::vector<std::string> lines;
    const std::string t = normalize_space(text);
    std::string line;
    size_t line_len = 0;
    // Tokens: words separated by spaces; a CJK character is a token on its own.
    std::vector<std::pair<std::string, bool>> toks;  // text, space before
    bool space = false;
    std::string word;
    auto flush = [&] {
        if (!word.empty()) toks.push_back({word, space}), word.clear(), space = false;
    };
    for (size_t i = 0; i < t.size();) {
        uint32_t cp;
        const size_t n = utf8_next(t, i, &cp);
        if (cp == ' ') {
            flush();
            space = true;
        } else if (cjk_char(cp)) {
            flush();
            toks.push_back({t.substr(i, n), space});
            space = false;
        } else {
            word.append(t, i, n);
        }
        i += n;
    }
    flush();
    for (const auto& [w, sp] : toks) {
        const size_t wl = utf8_length(w);
        const size_t add = (line.empty() || !sp) ? 0 : 1;
        if (!line.empty() && line_len + add + wl > width) {
            lines.push_back(line);
            line.clear();
            line_len = 0;
        }
        if (!line.empty() && sp) line += ' ', ++line_len;
        line += w;
        line_len += wl;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

}  // namespace tl
