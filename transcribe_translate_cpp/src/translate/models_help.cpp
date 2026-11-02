#include "models_help.h"

#include <algorithm>

#include "languages.h"
#include "models.h"

namespace tl {

namespace {

std::string html_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
    return out;
}

std::string row(const std::string& name_html, const std::string& text_html) {
    return "<tr><td valign=\"top\" width=\"175\"><b>" + name_html + "</b></td><td>" + text_html + "</td></tr>";
}

}  // namespace

std::string language_names(const std::vector<std::string>& codes, bool base_only) {
    std::vector<std::string> names;
    for (const auto& c : codes) {
        const Language* l = find_language(c);
        std::string n = l ? l->name : c;
        if (base_only) {
            const size_t paren = n.find(" (");
            if (paren != std::string::npos) n.erase(paren);
        }
        names.push_back(html_escape(n));
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    std::string out;
    for (const auto& n : names) out += (out.empty() ? "" : ", ") + n;
    return out;
}

std::string translation_model_rows() {
    const auto& table = model_table();
    std::vector<bool> done(table.size(), false);
    std::string out;
    for (size_t i = 0; i < table.size(); ++i) {
        if (done[i]) continue;
        std::string names = html_escape(table[i].display);
        for (size_t j = i + 1; j < table.size(); ++j)
            if (!done[j] && table[j].langs == table[i].langs) {
                names += "<br>" + html_escape(table[j].display);
                done[j] = true;
            }
        const auto& langs = table[i].langs;
        const std::string text =
            langs.empty() ? std::string("Every language in the menus (general-purpose model; quality varies with how "
                                        "common the language is).")
                          : std::to_string(langs.size()) + " languages: " + language_names(langs) + ".";
        out += row(names, text);
    }
    return out;
}

}  // namespace tl
