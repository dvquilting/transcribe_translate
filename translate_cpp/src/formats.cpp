#include "formats.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

#include "text.h"

namespace tl {

std::string Layout::render(const std::vector<std::string>& translations) const {
    std::string out;
    for (const auto& p : parts) {
        out += p.before;
        if (p.unit >= 0) out += p.unit < (int)translations.size() ? translations[p.unit] : units[p.unit];
        out += p.after;
    }
    return out;
}

namespace {

std::vector<std::string> split_lines(const std::string& text, bool* trailing_newline) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') lines.push_back(cur), cur.clear();
        else cur += c;
    }
    *trailing_newline = !text.empty() && text.back() == '\n';
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

bool blank(const std::string& s) { return trim(s).empty(); }

void add_literal(Layout& L, const std::string& s) {
    if (!L.parts.empty() && L.parts.back().unit < 0) L.parts.back().before += s;
    else L.parts.push_back({s, -1, ""});
}

void add_unit(Layout& L, const std::string& before, const std::string& text, const std::string& after) {
    if (!has_letters(text)) {
        add_literal(L, before + text + after);
        return;
    }
    L.units.push_back(text);
    L.parts.push_back({before, (int)L.units.size() - 1, after});
}

size_t leading_spaces(const std::string& s) {
    size_t n = 0;
    while (n < s.size() && (s[n] == ' ' || s[n] == '\t')) ++n;
    return n;
}

}  // namespace

// ---------------------------------------------------------------------------
// Plain text
// ---------------------------------------------------------------------------

Layout parse_plain_text(const std::string& text) {
    Layout L;
    bool trailing = false;
    const auto lines = split_lines(text, &trailing);
    auto nl = [&](size_t k) { return (k + 1 < lines.size() || trailing) ? std::string("\n") : std::string(); };
    for (size_t i = 0; i < lines.size();) {
        if (blank(lines[i])) {
            add_literal(L, lines[i] + nl(i));
            ++i;
            continue;
        }
        size_t j = i;
        std::string body;
        while (j < lines.size() && !blank(lines[j])) body += (body.empty() ? "" : "\n") + lines[j++];
        add_unit(L, "", body, nl(j - 1));
        i = j;
    }
    return L;
}

// ---------------------------------------------------------------------------
// Markdown
// ---------------------------------------------------------------------------

namespace {

// "  - [ ] text" -> prefix "  - [ ] ", rest "text". Returns false if not a list item.
bool list_item(const std::string& line, std::string& prefix, std::string& rest) {
    size_t i = leading_spaces(line);
    size_t j = i;
    if (j < line.size() && (line[j] == '-' || line[j] == '*' || line[j] == '+')) {
        ++j;
    } else {
        while (j < line.size() && std::isdigit((unsigned char)line[j])) ++j;
        if (j == i || j - i > 9 || j >= line.size() || (line[j] != '.' && line[j] != ')')) return false;
        ++j;
    }
    if (j >= line.size() || line[j] != ' ') return false;
    while (j < line.size() && line[j] == ' ') ++j;
    if (line.compare(j, 3, "[ ]") == 0 || line.compare(j, 3, "[x]") == 0 || line.compare(j, 3, "[X]") == 0) {
        j += 3;
        while (j < line.size() && line[j] == ' ') ++j;
    }
    prefix = line.substr(0, j);
    rest = line.substr(j);
    return true;
}

bool horizontal_rule(const std::string& line) {
    const std::string t = trim(line);
    if (t.size() < 3) return false;
    const char c = t[0];
    if (c != '-' && c != '*' && c != '_') return false;
    for (char x : t)
        if (x != c && x != ' ') return false;
    return true;
}

// A code fence: 3+ backticks or tildes, indented at most 3 spaces.
struct Fence {
    char ch = 0;
    size_t len = 0;
};

bool fence(const std::string& line, Fence* f) {
    size_t i = 0;
    while (i < line.size() && line[i] == ' ') ++i;
    if (i > 3 || i >= line.size() || (line[i] != '`' && line[i] != '~')) return false;
    size_t j = i;
    while (j < line.size() && line[j] == line[i]) ++j;
    if (j - i < 3) return false;
    if (line[i] == '`' && line.find('`', j) != std::string::npos) return false;  // inline code such as ```x```
    if (f) *f = {line[i], j - i};
    return true;
}

// Does `line` close a block opened by `open`? The same character, at least as
// many of them, and nothing else on the line (so a ```` block can contain ```).
bool closes_fence(const std::string& line, const Fence& open) {
    Fence f;
    if (!fence(line, &f) || f.ch != open.ch || f.len < open.len) return false;
    return trim(line).size() == f.len;
}

// Strip only block containers for identifying code; copy the original lines verbatim.
std::string code_content(const std::string& line, size_t list_indent = 0) {
    std::string s = line;
    if (list_indent && leading_spaces(s) >= list_indent) s.erase(0, list_indent);
    size_t at = leading_spaces(s);
    while (at < s.size() && s[at] == '>') {
        ++at;
        if (at < s.size() && s[at] == ' ') ++at;
        s.erase(0, at);
        at = leading_spaces(s);
    }
    std::string prefix, rest;
    if (list_item(s, prefix, rest)) s = rest;
    return s;
}

bool block_fence(const std::string& line, Fence* f, size_t list_indent = 0) {
    return fence(code_content(line, list_indent), f);
}

size_t quote_depth(const std::string& line) {
    size_t depth = 0, at = 0;
    while (at < line.size() && (line[at] == ' ' || line[at] == '>')) {
        if (line[at++] == '>') ++depth;
    }
    return depth;
}

// A setext heading underline ("=====" or "-----" under a paragraph line).
bool setext_underline(const std::string& line) {
    if (leading_spaces(line) > 3) return false;
    const std::string t = trim(line);
    if (t.empty() || (t[0] != '=' && t[0] != '-')) return false;
    return t.find_first_not_of(t[0]) == std::string::npos;
}

bool table_separator(const std::string& line) {
    const std::string t = trim(line);
    if (t.empty()) return false;
    for (char c : t)
        if (c != '|' && c != '-' && c != ':' && c != ' ') return false;
    return t.find('-') != std::string::npos;
}

void add_table_row(Layout& L, const std::string& line) {
    // | cell | cell |  ->  literal pipes, each cell a unit
    size_t i = 0;
    std::string lit;
    while (i < line.size()) {
        if (line[i] == '|') {
            lit += '|';
            ++i;
            continue;
        }
        size_t j = i;
        while (j < line.size() && !(line[j] == '|' && (j == 0 || line[j - 1] != '\\'))) ++j;
        const std::string cell = line.substr(i, j - i);
        const size_t a = leading_spaces(cell);
        size_t b = cell.size();
        while (b > a && (cell[b - 1] == ' ' || cell[b - 1] == '\t')) --b;
        add_literal(L, lit + cell.substr(0, a));
        lit.clear();
        add_unit(L, "", cell.substr(a, b - a), cell.substr(b));
        i = j;
    }
    add_literal(L, lit);
}

}  // namespace

Layout parse_markdown(const std::string& text) {
    Layout L;
    bool trailing = false;
    const auto lines = split_lines(text, &trailing);
    size_t i = 0;
    bool first = true;
    auto newline = [&] {
        if (!first) add_literal(L, "\n");
        first = false;
    };

    // YAML front matter
    if (!lines.empty() && trim(lines[0]) == "---") {
        size_t j = 1;
        while (j < lines.size() && trim(lines[j]) != "---") ++j;
        if (j < lines.size()) {
            for (size_t k = 0; k <= j; ++k) newline(), add_literal(L, lines[k]);
            i = j + 1;
        }
    }

    bool prev_blank = true;
    size_t list_indent = 0;
    while (i < lines.size()) {
        const std::string& line = lines[i];
        std::string prefix, rest;
        Fence marker;
        if (blank(line)) {
            newline();
            add_literal(L, line);
            prev_blank = true;
            ++i;
            continue;
        }
        if (list_indent && leading_spaces(line) < list_indent) list_indent = 0;
        if (list_item(line, prefix, rest)) {
            // Content column of the marker (before an optional task checkbox).
            size_t marker_end = leading_spaces(line);
            while (marker_end < line.size() && line[marker_end] != ' ') ++marker_end;
            while (marker_end < line.size() && line[marker_end] == ' ') ++marker_end;
            list_indent = marker_end;
        }
        if (block_fence(line, &marker, list_indent)) {  // fenced code block: copy verbatim
            const size_t quotes = quote_depth(line);
            newline();
            add_literal(L, line);
            ++i;
            while (i < lines.size()) {
                // An unclosed nested fence ends with its container; ordinary
                // prose after the list/quote still needs translation.
                if ((!blank(lines[i]) && list_indent && leading_spaces(lines[i]) < list_indent) ||
                    (quotes && quote_depth(lines[i]) < quotes)) break;
                newline();
                add_literal(L, lines[i]);
                const bool end = closes_fence(code_content(lines[i], list_indent), marker);
                ++i;
                if (end) break;
            }
            prev_blank = false;
            continue;
        }
        const size_t ind = leading_spaces(line);
        if (prev_blank && (line.compare(0, 4, "    ") == 0 || line[0] == '\t') && !list_item(line, prefix, rest)) {
            // An entire indented block, including all continuation lines, is literal.
            do {
                newline();
                add_literal(L, lines[i++]);
            } while (i < lines.size() && (blank(lines[i]) || leading_spaces(lines[i]) >= 4 || lines[i][0] == '\t'));
            prev_blank = true;
            continue;
        }
        prev_blank = false;
        const std::string t = trim(line);
        if (horizontal_rule(line) || t[0] == '<' || (t[0] == '[' && t.find("]:") != std::string::npos) ||
            table_separator(line)) {
            newline();
            add_literal(L, line);  // rules, HTML, link definitions
            ++i;
            continue;
        }
        if (t[0] == '|') {  // table row
            newline();
            add_table_row(L, line);
            ++i;
            continue;
        }
        if (ind <= 3 && t[0] == '#') {  // heading
            size_t h = ind;
            while (h < line.size() && line[h] == '#') ++h;
            if (h - ind <= 6 && (h == line.size() || line[h] == ' ')) {
                while (h < line.size() && line[h] == ' ') ++h;
                std::string body = line.substr(h), closing;
                size_t e = body.size();
                while (e > 0 && (body[e - 1] == '#' || body[e - 1] == ' ')) --e;
                if (e < body.size() && body.find('#', e) != std::string::npos) closing = body.substr(e);
                else e = body.size();
                newline();
                add_unit(L, line.substr(0, h), body.substr(0, e), closing);
                ++i;
                continue;
            }
        }
        if (list_item(line, prefix, rest) || t[0] == '>') {
            // A list item or quote line, plus its continuation lines (joined).
            std::string body;
            if (t[0] == '>' && !list_item(line, prefix, rest)) {
                size_t q = ind;
                while (q < line.size() && (line[q] == '>' || line[q] == ' ')) ++q;
                prefix = line.substr(0, q);
                rest = line.substr(q);
                body = rest;
                ++i;
                while (i < lines.size() && !blank(lines[i]) && trim(lines[i])[0] == '>') {
                    std::string p2, r2;
                    const std::string& l2 = lines[i];
                    size_t q2 = leading_spaces(l2);
                    while (q2 < l2.size() && (l2[q2] == '>' || l2[q2] == ' ')) ++q2;
                    if (q2 == l2.size() || l2.substr(0, q2) != prefix || block_fence(l2, nullptr) || list_item(l2.substr(q2), p2, r2) || l2[q2] == '#') break;
                    body += " " + trim(l2.substr(q2));
                    ++i;
                }
            } else {
                body = rest;
                ++i;
                while (i < lines.size() && !blank(lines[i]) && leading_spaces(lines[i]) > ind) {
                    std::string p2, r2;
                    if (list_item(lines[i], p2, r2) || block_fence(lines[i], nullptr, list_indent)) break;
                    body += " " + trim(lines[i]);
                    ++i;
                }
            }
            newline();
            add_unit(L, prefix, body, "");
            continue;
        }
        // Paragraph: consecutive ordinary lines.
        std::string body = line;
        ++i;
        while (i < lines.size() && !blank(lines[i]) && !block_fence(lines[i], nullptr, list_indent)) {
            const std::string t2 = trim(lines[i]);
            if (t2[0] == '#' || t2[0] == '|' || t2[0] == '>' || t2[0] == '<' || horizontal_rule(lines[i]) ||
                list_item(lines[i], prefix, rest))
                break;
            if (setext_underline(lines[i])) break;
            body += "\n" + lines[i];
            ++i;
        }
        newline();
        add_unit(L, "", body, "");
        if (i < lines.size() && setext_underline(lines[i])) {
            // Setext heading: the text above is the heading; the underline stays as it is.
            newline();
            add_literal(L, lines[i]);
            ++i;
        }
    }
    if (trailing) add_literal(L, "\n");
    return L;
}

// ---------------------------------------------------------------------------
// SRT
// ---------------------------------------------------------------------------

namespace {

bool ends_sentence(const std::string& t) {
    std::string s = trim(t);
    while (!s.empty() && (s.back() == '"' || s.back() == '\'' || s.back() == ')' || s.back() == ']')) s.pop_back();
    if (s.empty()) return false;
    const char c = s.back();
    if (c == '.' || c == '?' || c == '!') return true;
    for (const char* e : {"\xE2\x80\xA6", "\xE3\x80\x82", "\xEF\xBC\x81", "\xEF\xBC\x9F"})  // … 。 ！ ？
        if (s.size() >= 3 && s.compare(s.size() - 3, 3, e) == 0) return true;
    return false;
}

bool dialogue(const std::string& text) {
    // Two speakers in one cue: "- Hello.\n- Hi."
    int dashes = 0, lines = 0;
    std::istringstream in(text);
    std::string l;
    while (std::getline(in, l)) {
        ++lines;
        const std::string t = trim(pk::strip_srt_tags(l));
        if (!t.empty() && t[0] == '-') ++dashes;
    }
    return lines > 1 && dashes >= 2;
}

}  // namespace

SrtPlan plan_srt(const std::vector<Cue>& cues) {
    SrtPlan p;
    p.cues = cues;
    p.italic.resize(cues.size());
    std::vector<size_t> group;
    auto close = [&] {
        if (group.empty()) return;
        std::string u;
        for (size_t i : group) u += (u.empty() ? "" : " ") + normalize_space(pk::strip_srt_tags(cues[i].text));
        p.groups.push_back(group);
        p.units.push_back(u);
        group.clear();
    };
    for (size_t i = 0; i < cues.size(); ++i) {
        const std::string t = trim(cues[i].text);
        p.italic[i] = t.size() > 7 && t.compare(0, 3, "<i>") == 0 && t.compare(t.size() - 4, 4, "</i>") == 0;
        if (dialogue(cues[i].text)) {  // keep the two speakers' lines apart
            close();
            std::string u;
            std::istringstream in(cues[i].text);
            std::string l;
            while (std::getline(in, l))
                if (!trim(pk::strip_srt_tags(l)).empty()) u += (u.empty() ? "" : "\n") + trim(pk::strip_srt_tags(l));
            p.groups.push_back({i});
            p.units.push_back(u);
            continue;
        }
        if (!group.empty() && cues[i].start - cues[group.back()].end > 2.0) close();
        group.push_back(i);
        if (ends_sentence(pk::strip_srt_tags(cues[i].text)) || group.size() >= 6) close();
    }
    close();
    return p;
}

std::vector<std::string> split_proportionally(const std::string& text, const std::vector<size_t>& weights,
                                              bool by_characters) {
    const size_t n = weights.size();
    if (n == 0) return {};
    if (n == 1) return {trim(text)};
    // Tokens to distribute: words, or single characters.
    std::vector<std::string> toks;
    const std::string t = normalize_space(text);
    if (by_characters) {
        for (size_t i = 0; i < t.size();) {
            size_t len = 1;
            const unsigned char c = (unsigned char)t[i];
            if (c >= 0xF0) len = 4;
            else if (c >= 0xE0) len = 3;
            else if (c >= 0xC0) len = 2;
            // Keep runs of Latin letters/digits (names, numbers) together.
            if (c < 0x80 && std::isalnum(c)) {
                len = 0;
                while (i + len < t.size() && (unsigned char)t[i + len] < 0x80 && std::isalnum((unsigned char)t[i + len])) ++len;
            }
            toks.push_back(t.substr(i, len));
            i += len;
        }
    } else {
        std::istringstream in(t);
        std::string w;
        while (in >> w) toks.push_back(w);
    }
    std::vector<std::string> parts(n);
    if (toks.empty()) return parts;

    double wsum = 0;
    for (size_t w : weights) wsum += (double)std::max<size_t>(w, 1);
    std::vector<double> tok_end(toks.size());  // cumulative length after each token
    double total = 0;
    for (size_t i = 0; i < toks.size(); ++i) tok_end[i] = (total += (double)utf8_length(toks[i]) + (by_characters ? 0 : 1));

    size_t start = 0;
    double cum_w = 0;
    for (size_t k = 0; k < n; ++k) {
        size_t end;  // one past the last token of part k
        if (k == n - 1) {
            end = toks.size();
        } else {
            cum_w += (double)std::max<size_t>(weights[k], 1);
            const double target = total * cum_w / wsum;
            end = start;
            while (end < toks.size() && tok_end[end] <= target) ++end;
            // Round to the nearer boundary.
            if (end < toks.size() && end > start) {
                const double before = tok_end[end - 1], after = tok_end[end];
                if (after - target < target - before) ++end;
            } else if (end < toks.size() && end == start) {
                ++end;  // at least one token
            }
            // Leave at least one token for each remaining part.
            const size_t remaining = n - 1 - k;
            end = std::min(end, toks.size() > remaining ? toks.size() - remaining : start);
            end = std::max(end, std::min(start + 1, toks.size()));
        }
        std::string s;
        for (size_t i = start; i < end; ++i) {
            if (!s.empty() && !by_characters) s += ' ';
            s += toks[i];
        }
        parts[k] = s;
        start = end;
    }
    return parts;
}

std::string render_srt(const SrtPlan& plan, const std::vector<std::string>& translations,
                       const std::string& target_lang) {
    std::vector<std::string> text(plan.cues.size());
    const bool by_chars = is_cjk_language(target_lang);
    for (size_t g = 0; g < plan.groups.size(); ++g) {
        const auto& idx = plan.groups[g];
        const std::string tr = g < translations.size() ? translations[g] : plan.units[g];
        if (idx.size() == 1) {
            if (tr.find('\n') != std::string::npos) {
                text[idx[0]] = trim(tr);  // dialogue lines as the model wrote them
            } else {
                std::string w;
                for (const auto& l : wrap_text(tr, 42)) w += (w.empty() ? "" : "\n") + l;
                text[idx[0]] = w;
            }
            continue;
        }
        std::vector<size_t> weights;
        for (size_t i : idx) weights.push_back(utf8_length(normalize_space(pk::strip_srt_tags(plan.cues[i].text))));
        const auto parts = split_proportionally(tr, weights, by_chars);
        for (size_t k = 0; k < idx.size(); ++k) {
            std::string w;
            for (const auto& l : wrap_text(parts[k], 42)) w += (w.empty() ? "" : "\n") + l;
            text[idx[k]] = w;
        }
    }
    std::vector<Cue> out = plan.cues;  // numbers and timing lines as in the source
    for (size_t i = 0; i < out.size(); ++i) {
        std::string t = text[i].empty() ? " " : text[i];
        if (plan.italic[i]) t = "<i>" + t + "</i>";
        out[i].text = t;
    }
    return pk::format_srt(out);
}

}  // namespace tl
