#pragma once

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "fileio.h"  // ../common: replace_file

namespace tmx {

// All dubbing writes are staged beside the destination, so committing is a
// same-volume atomic replacement. Destruction removes only this job's files.
class OutputTransaction {
public:
    explicit OutputTransaction(const std::filesystem::path& output,
                               const std::filesystem::path& work = {})
        : output_(std::filesystem::absolute(output)) {
        namespace fs = std::filesystem;
        fs::create_directories(output_.parent_path());
        if (!work.empty()) {
            dir_ = fs::absolute(work);
            if (!fs::is_directory(dir_) || dir_.parent_path() != output_.parent_path())
                throw std::runtime_error("dub work directory must exist beside the output file");
        } else {
            const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
            for (unsigned i = 0; i < 100; ++i) {
                dir_ = output_.parent_path() / (".dub-" + std::to_string(nonce) + "-" + std::to_string(i));
                if (fs::create_directory(dir_)) { own_dir_ = true; break; }
            }
            if (!own_dir_) throw std::runtime_error("cannot create dub work directory");
        }
        rendered_ = dir_ / (std::string("rendered") + output_.extension().string());
    }
    ~OutputTransaction() {
        std::error_code ec;
        for (const auto& p : {rendered_, audio(), subtitles(), voice()}) std::filesystem::remove(p, ec);
        for (size_t k = 0; k < voices_; ++k) std::filesystem::remove(voice(k), ec);
        if (own_dir_) std::filesystem::remove(dir_, ec);
    }
    OutputTransaction(const OutputTransaction&) = delete;
    OutputTransaction& operator=(const OutputTransaction&) = delete;
    std::filesystem::path rendered() const { return rendered_; }
    std::filesystem::path audio() const { return dir_ / "audio.wav"; }
    std::filesystem::path subtitles() const { return dir_ / "subtitles.srt"; }
    std::filesystem::path voice() const { return dir_ / "voice.wav"; }
    // The voice sample of speaker k (several speakers).
    std::filesystem::path voice(size_t k) const {
        voices_ = std::max(voices_, k + 1);
        return dir_ / ("voice_" + std::to_string(k + 1) + ".wav");
    }
    // Same volume (the work directory is beside the output): one atomic replacement.
    void commit() { pk::replace_file(rendered_, output_); }
    std::filesystem::path save_fallback() const {
        auto keep = output_;
        keep.replace_extension();
        keep += std::filesystem::path(".dub-");
        keep += dir_.filename().native();
        keep += std::filesystem::path(".wav");
        // copy_options::none must not overwrite an existing file, including a race.
        std::filesystem::copy_file(audio(), keep, std::filesystem::copy_options::none);
        return keep;
    }
private:
    std::filesystem::path output_, dir_, rendered_;
    bool own_dir_ = false;
    mutable size_t voices_ = 0;
};

}  // namespace tmx
