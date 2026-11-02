// languages.h - the languages the translator offers.
#pragma once

#include <string>
#include <vector>

namespace tl {

struct Language {
    std::string code;     // ISO 639-1 (plus a region/script where it matters, e.g. "zh-Hant", "pt-BR")
    std::string name;     // English name, used in prompts and menus ("Portuguese (Brazil)")
    std::string native;   // name in the language itself, for the GUI
    std::string chinese;  // Chinese name (Hy-MT2's Chinese prompts use it)
};

const std::vector<Language>& languages();

// nullptr if unknown. Accepts "pt_BR" as well as "pt-BR", any case.
const Language* find_language(const std::string& code);

// "English (en)" style label for logs.
std::string language_label(const std::string& code);

}  // namespace tl
