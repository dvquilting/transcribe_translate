// utf8_path.h - UTF-8 <-> std::filesystem::path that compiles as C++17 and C++20.
// All three programs keep paths as UTF-8 std::string (command lines are converted
// to UTF-8 on Windows); these are the only conversions to and from the native form.
#pragma once

#include <filesystem>
#include <string>

namespace pk {

inline std::filesystem::path path_from_utf8(const std::string& s) {
#if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
#else
    return std::filesystem::u8path(s);
#endif
}

inline std::string path_to_utf8(const std::filesystem::path& p) {
    const auto s = p.u8string();  // std::string in C++17, std::u8string in C++20
    return std::string(s.begin(), s.end());
}

}  // namespace pk
