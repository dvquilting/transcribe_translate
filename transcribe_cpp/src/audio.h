// audio.h - decoding (via ffmpeg) and silence-aware chunking.
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace tmx {

constexpr int kSampleRate = 16000;

// Decode any audio/video file to mono float32 PCM at 16 kHz using the ffmpeg
// executable on PATH. `path` is UTF-8. Throws std::runtime_error on failure.
std::vector<float> load_audio(const std::string& path);

// Decode to float32 PCM at any rate and channel count (interleaved when
// channels > 1), via ffmpeg. Throws on failure. `spare` floats of extra
// capacity are reserved, so the caller can grow the result later without
// reallocating (and briefly holding it twice); untouched spare costs no RAM.
std::vector<float> load_audio_as(const std::string& path, int rate, int channels, size_t spare = 0);

// True if an `ffmpeg` executable can be found on PATH.
bool ffmpeg_available();

// Run ffmpeg with these arguments (each quoted for the shell). Returns ffmpeg's exit code (-1 if it could not be started).
// `messages` (optional) receives what ffmpeg printed (its errors).
int run_ffmpeg(const std::vector<std::string>& args, std::string* messages = nullptr);

// [start, end) sample ranges no longer than max_seconds. Each cut is placed at the
// quietest window within the last `search_seconds` of the allowed span. Ranges are
// contiguous and cover the whole signal.
std::vector<std::pair<size_t, size_t>> split_on_silence(const float* wav, size_t n, int sr, double max_seconds,
                                                        double search_seconds = 5.0, double window_ms = 50.0);

double rms_dbfs(const float* x, size_t n);

struct Chunk {
    std::vector<float> audio;  // padded to at least kMinChunkSeconds
    double offset = 0.0;       // seconds from start of file
    double duration = 0.0;     // real (unpadded) duration
};

constexpr double kSilenceDbfs = -60.0;    // chunks quieter than this are skipped
constexpr double kMinChunkSeconds = 1.0;  // shorter chunks are zero-padded

// Chunk audio, drop silent chunks, pad very short ones.
std::vector<Chunk> make_chunks(const std::vector<float>& wav, int sr, double max_seconds, int* skipped = nullptr);

}  // namespace tmx
