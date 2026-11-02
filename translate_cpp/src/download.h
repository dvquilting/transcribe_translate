// download.h - fetch model files over HTTPS into the models folder.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace tl {

// %LOCALAPPDATA%\Translate\models on Windows, $XDG_CACHE_HOME/translate (or
// ~/.cache/translate) elsewhere.
std::string default_models_dir();

// Called with bytes received so far and the total (0 if unknown).
using DownloadProgress = std::function<void(uint64_t done, uint64_t total)>;

// Download `url` to `dest`. The data goes to dest + ".part" first (an
// interrupted download resumes from there) and is renamed when complete.
// `expected_bytes` (0 = unknown) is checked at the end: a complete download
// of another size is removed (not kept to resume). Throws on failure.
void download_file(const std::string& url, const std::string& dest, uint64_t expected_bytes,
                   const DownloadProgress& progress);

}  // namespace tl
