// dsp.h - small audio tools for dubbing: resampling, tempo change without
// pitch change, levels, WAV writing.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace tmx {

// Band-limited (windowed-sinc) resampling of mono audio.
std::vector<float> resample(const std::vector<float>& in, int from_rate, int to_rate);

// Change the tempo by `speed` (1.25 = 25 % faster, shorter) without changing
// the pitch (WSOLA). speed is clamped to [0.5, 2].
std::vector<float> time_stretch(const std::vector<float>& in, int rate, double speed);

// Root-mean-square level of the samples whose absolute value matters (no gating).
double rms(const float* x, size_t n);

// RMS over the louder parts only (20 ms blocks above -50 dBFS): a speech level
// that pauses don't drag down. 0 if everything is quiet.
double active_rms(const float* x, size_t n, int rate);

// Remove content below `hz` (2nd-order Butterworth high-pass, in place, mono).
void high_pass(std::vector<float>& x, int rate, double hz);

// Tone (spectral balance) per octave band, for matching one voice's tone to
// another's. `centres` are the band centre frequencies (Hz); bands above the
// Nyquist frequency are skipped (level -200 dB, gain ignored).
std::vector<double> band_levels_db(const std::vector<float>& x, int rate, const std::vector<double>& centres);
// Graphic equaliser: one peaking filter per band (octave wide), in place, mono.
void apply_eq(std::vector<float>& x, int rate, const std::vector<double>& centres, const std::vector<double>& gains_db);
// Gains (dB) that move `have` toward `want` (both from band_levels_db), ignoring
// overall loudness: limited to +-max_db, smoothed across neighbouring bands and
// applied at `strength` (0..1) so the result stays natural.
std::vector<double> tone_match_gains(const std::vector<double>& want, const std::vector<double>& have,
                                     double max_db = 6.0, double strength = 0.8);

// Long audio processed in overlapping parts (source separation needs memory in
// proportion to its input, so an hour can't go through in one piece).
// chunk_bounds splits `n` samples into equal parts of at most `max_len`: part k
// is [bounds[k], bounds[k + 1]). Each part is processed with some extra context
// on both sides, and neighbouring results are crossfaded over
// [bound - fade, bound + fade) around each inner bound.
std::vector<size_t> chunk_bounds(size_t n, size_t max_len);
// Weight (0..1) of part k's result at sample t: 1 inside the part, a linear
// ramp across each inner bound. Neighbouring weights sum to 1, and a single part
// has weight 1 everywhere (its result is used exactly as it is).
// Parts must be at least 2 * fade long.
float chunk_weight(const std::vector<size_t>& bounds, size_t k, size_t t, size_t fade);

// Short fade in/out (in place) so clips don't click.
void fade_edges(std::vector<float>& x, int rate, double ms = 8.0);

// Look-ahead peak limiter: keeps peaks below ~-1 dBFS without distortion
// (in place; `x` interleaved with `channels` channels at `rate`).
void limit_peaks(std::vector<float>& x, int rate, int channels);

// Write 16-bit PCM WAV. `pcm` is interleaved when channels > 1. UTF-8 path.
void write_wav(const std::string& path, const std::vector<float>& pcm, int rate, int channels);

}  // namespace tmx
