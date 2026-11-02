// fileio.h - read input files and write output files safely.
#pragma once

#include <filesystem>
#include <string>

namespace pk {

// The whole file, unchanged. Throws std::runtime_error("cannot open PATH").
std::string read_file(const std::filesystem::path& path);

// Move `from` over `to` in one step, replacing an existing `to` (both on the
// same volume). On Windows MoveFileExW(MOVEFILE_REPLACE_EXISTING |
// MOVEFILE_WRITE_THROUGH): std::filesystem::rename does not replace with every
// MinGW libstdc++; a target that another program holds open for a moment (a
// sharing, lock or access error) is tried again for about half a second.
// Throws std::runtime_error naming `to` on failure (e.g. the target is open in
// another program); `from` is left in place.
void replace_file(const std::filesystem::path& from, const std::filesystem::path& to);

// Write `bytes` to `target`. The data goes to "TARGET.tmp" next to the target
// first and replaces the target only when it has been written completely, so a
// full disk, an error or a cancel half-way (the GUIs kill the tools) never
// leaves a truncated file or destroys the one that was there. Creates missing
// parent folders. Throws std::runtime_error on failure; the temporary file is
// removed then.
void write_file_atomic(const std::filesystem::path& target, const std::string& bytes);

}  // namespace pk
