// models_help.h - the "languages of each translation model" part of the help
// pages (Translate and Speech Translate), built from the model table so it
// can't drift from what the program does. Plain C++ (HTML text), no Qt.
#pragma once

#include <string>
#include <vector>

namespace tl {

// English names of language codes, sorted and joined with ", ". With
// `base_only`, the country or script is left out ("pt" -> "Portuguese").
std::string language_names(const std::vector<std::string>& codes, bool base_only = false);

// HTML table rows (<tr>...</tr>, no <table>) with each translation model and its
// languages; models with the same languages share one row.
std::string translation_model_rows();

}  // namespace tl
