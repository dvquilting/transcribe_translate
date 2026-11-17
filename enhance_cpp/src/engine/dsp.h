// dsp.h - the signal processing of Resemble Enhance, ported from its PyTorch code
// (resemble_enhance/inference.py, melspec.py, denoiser/denoiser.py) and the parts of
// torch/torchaudio it relies on (stft/istft, MelSpectrogram, sinc resampling). The
// neural networks run in ONNX Runtime (models.h); everything around them is here.
#pragma once

#include <complex>
#include <cstddef>
#include <string>
#include <vector>

namespace enh {

constexpr int kRate = 44100;   // the model's sample rate
constexpr int kHop = 420;      // mel / STFT hop (9.5 ms)
constexpr int kMels = 128;
constexpr int kLatent = 64;
constexpr int kFeatures = 160; // decoder output = vocoder input (128 mel + 32 extra)
constexpr int kNoise = 128;    // vocoder noise channels
constexpr int kVocoderPad = 10;  // frames the vocoder pads its input with

// Channel-major (C, T) float matrix, as the ONNX models take them (batch 1).
struct Mat {
    int rows = 0, cols = 0;
    std::vector<float> v;
    Mat() = default;
    Mat(int r, int c, float fill = 0.0f) : rows(r), cols(c), v((size_t)r * c, fill) {}
    float* row(int r) { return v.data() + (size_t)r * cols; }
    const float* row(int r) const { return v.data() + (size_t)r * cols; }
    float& at(int r, int c) { return v[(size_t)r * cols + c]; }
    float at(int r, int c) const { return v[(size_t)r * cols + c]; }
};

enum class PadMode { Constant, Reflect };

// torch.stft(x, n_fft, hop, win_length, window=hann_window(win_length) [periodic],
// center=True, pad_mode, onesided=True), batch 1. Result: frames x (n_fft/2 + 1),
// frames = 1 + x.size() / hop. A window shorter than n_fft is centred in it, as torch does.
std::vector<std::vector<std::complex<float>>> stft(const std::vector<float>& x, int n_fft, int hop, int win_length,
                                                   PadMode pad);

// torch.istft(spec, n_fft, hop, win_length=n_fft, window=hann_window, center=True),
// spec = frames x (n_fft/2 + 1); returns hop * (frames - 1) samples. Like
// Denoiser._istft, NaN samples become 0.
std::vector<float> istft(const std::vector<std::vector<std::complex<float>>>& spec, int n_fft, int hop);

// Mel filterbanks and the model constants, from models/filters.bin (export_onnx.py).
struct Filters {
    float norm_mean = 0, norm_std = 1;  // Enhancer.normalizer (running mean / std)
    float z_scale = 6;                  // LCFM z_scale
    float time_a = 0;                   // the CFM time mapping's base, h(t) = (a^t - 1) / (a - 1)
    Mat mel_fb;    // 1025 x 128: torchaudio MelSpectrogram(44100, n_fft 2048, slaney)
    Mat align_fb;  // 513 x 80: torchaudio MelSpectrogram(44100, n_fft 1024, htk) for merge_chunks
};
Filters load_filters(const std::string& path_utf8);

// MelSpectrogram.forward (melspec.py): pre-emphasis, |STFT| (2048 / 420), mel, dB,
// normalized; then Enhancer.to_mel drops the last frame and the Normalizer
// standardizes it. Result: 128 x (x.size() / 420).
Mat enhancer_mel(const std::vector<float>& x, const Filters& f);

// torchaudio.functional.resample(x, from, to, lowpass_filter_width=64,
// rolloff=0.9475937167399596, resampling_method="sinc_interp_kaiser",
// beta=14.769656459379492), as inference() calls it.
std::vector<float> resample(const std::vector<float>& x, int from, int to);

// merge_chunks (inference.py): overlap-add with linear fades, each chunk shifted by
// the offset its overlap's mel spectrogram correlates best at (compute_offset).
std::vector<float> merge_chunks(const std::vector<std::vector<float>>& chunks, size_t chunk_length,
                                size_t hop_length, size_t length, const Filters& f);

// compute_offset (inference.py), exposed for the tests.
long compute_offset(const std::vector<float>& a, const std::vector<float>& b, const Filters& f);

// Solver.solve's time points: exponential_decay_mapping(linspace(0, 1, n + 1)).
std::vector<double> time_points(int n_steps, double a);

// Standard normal numbers: splitmix64 + Box-Muller. tools/re_model.py's NoiseGen
// produces the same stream, so a run can be compared with the PyTorch original.
class NormalGen {
public:
    explicit NormalGen(unsigned long long seed) : state_(seed) {}
    void fill(float* out, size_t n);
    // Move on as fill(n) would, without producing the numbers.
    void skip(size_t n) { state_ += 0x9E3779B97F4A7C15ULL * (unsigned long long)(2 * ((n + 1) / 2)); }
private:
    unsigned long long next();
    unsigned long long state_;
};

}  // namespace enh
