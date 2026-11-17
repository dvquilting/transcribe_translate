// dsp.cpp - see dsp.h. Each function names the PyTorch code it reproduces.
#include "dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

#include "pocketfft_hdronly.hpp"
#include "utf8_path.h"

namespace enh {

namespace {

constexpr double kPi = 3.14159265358979323846;

using cfloat = std::complex<float>;

// torch.hann_window(n) (periodic=True)
std::vector<float> hann(int n) {
    std::vector<float> w(n);
    for (int i = 0; i < n; ++i) w[i] = (float)(0.5 - 0.5 * std::cos(2.0 * kPi * i / n));
    return w;
}

// The window as torch.stft uses it: win_length centred within n_fft.
std::vector<float> padded_window(int n_fft, int win_length) {
    std::vector<float> w(n_fft, 0.0f);
    const auto h = hann(win_length);
    const int left = (n_fft - win_length) / 2;
    std::copy(h.begin(), h.end(), w.begin() + left);
    return w;
}

// Index into x for position i of x padded by `pad` on both sides.
inline float padded_at(const std::vector<float>& x, long i, PadMode mode) {
    const long n = (long)x.size();
    if (i >= 0 && i < n) return x[i];
    if (mode == PadMode::Constant || n == 1) return i >= 0 && i < n ? x[i] : 0.0f;
    // reflect (without repeating the edge sample), folded for very short inputs
    const long period = 2 * (n - 1);
    long j = i % period;
    if (j < 0) j += period;
    if (j >= n) j = period - j;
    return x[j];
}

void rfft(const float* in, cfloat* out, size_t n) {
    pocketfft::shape_t shape{n};
    pocketfft::stride_t si{(ptrdiff_t)sizeof(float)}, so{(ptrdiff_t)sizeof(cfloat)};
    pocketfft::r2c(shape, si, so, 0, pocketfft::FORWARD, in, out, 1.0f);
}

void irfft(const cfloat* in, float* out, size_t n) {  // scaled by 1/n, as torch.fft.irfft
    pocketfft::shape_t shape{n};
    pocketfft::stride_t si{(ptrdiff_t)sizeof(cfloat)}, so{(ptrdiff_t)sizeof(float)};
    pocketfft::c2r(shape, si, so, 0, pocketfft::BACKWARD, in, out, 1.0f / (float)n);
}

void cfft(std::vector<std::complex<double>>& x, bool forward) {
    pocketfft::shape_t shape{x.size()};
    pocketfft::stride_t s{(ptrdiff_t)sizeof(std::complex<double>)};
    pocketfft::c2c(shape, s, s, {0}, forward, x.data(), x.data(), forward ? 1.0 : 1.0 / (double)x.size());
}

// For each mel band the range of frequency bins with non-zero weight.
struct Band { int lo, hi; };
std::vector<Band> bands(const Mat& fb) {
    std::vector<Band> out(fb.cols, Band{0, 0});
    for (int m = 0; m < fb.cols; ++m) {
        int lo = -1, hi = -1;
        for (int k = 0; k < fb.rows; ++k)
            if (fb.at(k, m) != 0.0f) {
                if (lo < 0) lo = k;
                hi = k + 1;
            }
        if (lo >= 0) out[m] = Band{lo, hi};
    }
    return out;
}

// mel[m][t] = sum_k fb[k][m] * spec[t][k]^power   (torchaudio MelScale on Spectrogram)
Mat apply_mel(const std::vector<std::vector<cfloat>>& spec, const Mat& fb, int power) {
    const int frames = (int)spec.size();
    const auto bs = bands(fb);
    Mat mel(fb.cols, frames);
    std::vector<float> mag(fb.rows);
    for (int t = 0; t < frames; ++t) {
        for (int k = 0; k < fb.rows; ++k) {
            const float a = std::abs(spec[t][k]);
            mag[k] = power == 2 ? a * a : a;
        }
        for (int m = 0; m < fb.cols; ++m) {
            float s = 0.0f;
            for (int k = bs[m].lo; k < bs[m].hi; ++k) s += fb.at(k, m) * mag[k];
            mel.at(m, t) = s;
        }
    }
    return mel;
}

}  // namespace

std::vector<std::vector<cfloat>> stft(const std::vector<float>& x, int n_fft, int hop, int win_length, PadMode pad) {
    const auto window = padded_window(n_fft, win_length);
    const long half = n_fft / 2;
    const int frames = 1 + (int)(x.size() / (size_t)hop);
    const int bins = n_fft / 2 + 1;
    std::vector<std::vector<cfloat>> out(frames, std::vector<cfloat>(bins));
    std::vector<float> buf(n_fft);
    for (int f = 0; f < frames; ++f) {
        const long start = (long)f * hop - half;
        for (int i = 0; i < n_fft; ++i) buf[i] = padded_at(x, start + i, pad) * window[i];
        rfft(buf.data(), out[f].data(), (size_t)n_fft);
    }
    return out;
}

std::vector<float> istft(const std::vector<std::vector<cfloat>>& spec, int n_fft, int hop) {
    const int frames = (int)spec.size();
    if (frames < 1) return {};
    const auto window = hann(n_fft);
    const size_t full = (size_t)n_fft + (size_t)hop * (frames - 1);
    std::vector<double> y(full, 0.0), env(full, 0.0);
    std::vector<float> buf(n_fft);
    for (int f = 0; f < frames; ++f) {
        irfft(spec[f].data(), buf.data(), (size_t)n_fft);
        const size_t at = (size_t)f * hop;
        for (int i = 0; i < n_fft; ++i) {
            y[at + i] += (double)buf[i] * window[i];
            env[at + i] += (double)window[i] * window[i];
        }
    }
    const size_t start = (size_t)n_fft / 2;
    const size_t length = (size_t)hop * (frames - 1);
    std::vector<float> out(length);
    for (size_t i = 0; i < length; ++i) {
        const double e = env[start + i];
        if (e <= 1e-11) throw std::runtime_error("istft: window overlap-add is zero");
        const float v = (float)(y[start + i] / e);
        out[i] = std::isnan(v) ? 0.0f : v;
    }
    return out;
}

Filters load_filters(const std::string& path_utf8) {
    std::ifstream in(pk::path_from_utf8(path_utf8), std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path_utf8);
    char magic[4];
    int32_t version = 0;
    in.read(magic, 4);
    in.read(reinterpret_cast<char*>(&version), 4);
    if (!in || std::memcmp(magic, "REF1", 4) != 0 || version != 1)
        throw std::runtime_error(path_utf8 + " is not a filters file of this program");
    float c[4];
    in.read(reinterpret_cast<char*>(c), sizeof(c));
    Filters f;
    f.norm_mean = c[0];
    f.norm_std = c[1];
    f.z_scale = c[2];
    f.time_a = c[3];
    for (Mat* m : {&f.mel_fb, &f.align_fb}) {
        int32_t dims[2] = {0, 0};
        in.read(reinterpret_cast<char*>(dims), sizeof(dims));
        if (!in || dims[0] <= 0 || dims[1] <= 0 || dims[0] > 100000 || dims[1] > 10000)
            throw std::runtime_error(path_utf8 + " is damaged");
        *m = Mat(dims[0], dims[1]);
        in.read(reinterpret_cast<char*>(m->v.data()), (std::streamsize)(m->v.size() * sizeof(float)));
    }
    if (!in) throw std::runtime_error(path_utf8 + " is incomplete");
    if (f.mel_fb.rows != 2048 / 2 + 1 || f.mel_fb.cols != kMels || f.align_fb.rows != 1024 / 2 + 1)
        throw std::runtime_error(path_utf8 + " has unexpected filter sizes");
    return f;
}

Mat enhancer_mel(const std::vector<float>& x, const Filters& f) {
    // pre-emphasis: y[n] = x[n] - 0.97 x[n-1], x[-1] = 0
    std::vector<float> y(x.size());
    float prev = 0.0f;
    for (size_t i = 0; i < x.size(); ++i) {
        y[i] = x[i] - 0.97f * prev;
        prev = x[i];
    }
    const auto spec = stft(y, 2048, kHop, 2048, PadMode::Constant);
    Mat mel = apply_mel(spec, f.mel_fb, 1);
    // _amp_to_db, _normalize (min_level_db = 20 log10(1e-4) = -80, headroom 15 dB), drop
    // the last frame, then the Normalizer.
    const int frames = mel.cols - 1;
    Mat out(kMels, std::max(frames, 0));
    const double min_db = 20.0 * std::log10(1e-4);
    for (int m = 0; m < kMels; ++m)
        for (int t = 0; t < frames; ++t) {
            const float db = std::log10(std::max(mel.at(m, t), 1e-4f)) * 20.0f;
            const float normed = (float)((db - min_db) / (-min_db + 15.0));
            out.at(m, t) = (normed - f.norm_mean) / f.norm_std;
        }
    return out;
}

std::vector<float> resample(const std::vector<float>& x, int from, int to) {
    if (from == to) return x;
    auto gcd = [](long a, long b) { while (b) { long t = a % b; a = b; b = t; } return a; };
    const long g = gcd(from, to);
    const long orig = from / g, nw = to / g;
    const double width_f = 64.0, rolloff = 0.9475937167399596, beta = 14.769656459379492;
    const double base = std::min(orig, nw) * rolloff;
    const long width = (long)std::ceil(width_f * orig / base);
    const long taps = 2 * width + orig;
    // _get_sinc_resample_kernel (sinc_interp_kaiser)
    auto i0 = [](double v) {  // modified Bessel function of the first kind, order 0
        double sum = 1.0, term = 1.0, q = v * v / 4.0;
        for (int k = 1; k < 500; ++k) {
            term *= q / ((double)k * k);
            sum += term;
            if (term < sum * 1e-17) break;
        }
        return sum;
    };
    const double i0_beta = i0(beta);
    std::vector<float> kernel((size_t)nw * taps);
    for (long j = 0; j < nw; ++j)
        for (long k = 0; k < taps; ++k) {
            double t = (-(double)j / nw + (double)(k - width) / orig) * base;
            t = std::clamp(t, -width_f, width_f);
            const double r = t / width_f;
            const double window = i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0_beta;
            const double tp = t * kPi;
            const double sinc = tp == 0.0 ? 1.0 : std::sin(tp) / tp;
            kernel[(size_t)j * taps + k] = (float)(sinc * window * base / orig);
        }
    // _apply_sinc_resample_kernel: pad (width, width + orig), strided conv
    const long length = (long)x.size();
    const long blocks = length / orig + 1;
    const long target = (long)std::ceil((double)nw * length / orig);
    std::vector<float> out((size_t)std::max(0L, target));
    for (long b = 0; b < blocks; ++b)
        for (long j = 0; j < nw; ++j) {
            const long o = b * nw + j;
            if (o >= target) break;
            const float* kr = kernel.data() + (size_t)j * taps;
            const long first = b * orig - width;  // input index of tap 0
            const long k0 = std::max(0L, -first), k1 = std::min(taps, length - first);
            double s = 0.0;
            for (long k = k0; k < k1; ++k) s += (double)kr[k] * x[first + k];
            out[o] = (float)s;
        }
    return out;
}

long compute_offset(const std::vector<float>& a, const std::vector<float>& b, const Filters& f) {
    const int hop = kRate / 200, win = hop * 4, n_fft = 1024;
    auto spec = [&](const std::vector<float>& x) {
        Mat m = apply_mel(stft(x, n_fft, hop, win, PadMode::Reflect), f.align_fb, 2);
        for (float& v : m.v) v = std::log1p(v);
        return m;
    };
    const Mat s1 = spec(a), s2 = spec(b);
    const int n = s1.cols;
    std::vector<double> corr(n, 0.0);
    std::vector<std::complex<double>> x(n), y(n);
    for (int r = 0; r < s1.rows; ++r) {
        for (int t = 0; t < n; ++t) {
            x[t] = s1.at(r, t);
            y[t] = s2.at(r, t);
        }
        cfft(x, true);
        cfft(y, true);
        for (int t = 0; t < n; ++t) x[t] *= std::conj(y[t]);
        cfft(x, false);
        for (int t = 0; t < n; ++t) corr[t] += std::abs(x[t]);
    }
    long argmax = 0;
    for (int t = 1; t < n; ++t)
        if (corr[t] > corr[argmax]) argmax = t;
    if (argmax > n / 2) argmax -= n;
    return -argmax * hop;
}

std::vector<float> merge_chunks(const std::vector<std::vector<float>>& chunks, size_t chunk_length,
                                size_t hop_length, size_t length, const Filters& f) {
    if (chunks.empty()) return {};
    const size_t signal_length = (chunks.size() - 1) * hop_length + chunk_length;
    const size_t overlap = chunk_length - hop_length;
    std::vector<float> signal(signal_length, 0.0f);
    std::vector<float> fadein(chunk_length, 1.0f), fadeout(chunk_length, 1.0f);
    for (size_t i = 0; i < overlap; ++i) {
        const float v = overlap > 1 ? (float)i / (float)(overlap - 1) : 0.0f;  // torch.linspace(0, 1, overlap)
        fadein[i] = v;
        fadeout[hop_length + i] = overlap > 1 ? 1.0f - v : 1.0f;
    }
    if (overlap == 1) fadeout[hop_length] = 1.0f;  // linspace(1, 0, 1) = [1]
    for (size_t i = 0; i < chunks.size(); ++i) {
        long start = (long)(i * hop_length);
        long end = start + (long)chunk_length;
        std::vector<float> chunk = chunks[i];
        if (chunk.size() < chunk_length) chunk.resize(chunk_length, 0.0f);
        if (i > 0) {
            const auto& prev = chunks[i - 1];
            const size_t pn = std::min(overlap, prev.size());
            std::vector<float> pre(prev.end() - (long)pn, prev.end());
            std::vector<float> cur(chunk.begin(), chunk.begin() + (long)overlap);
            const long offset = compute_offset(pre, cur, f);
            start -= offset;
            end -= offset;
        }
        for (size_t k = 0; k < chunk_length; ++k) {
            if (i == 0) chunk[k] *= fadeout[k];
            else if (i == chunks.size() - 1) chunk[k] *= fadein[k];
            else chunk[k] *= fadein[k] * fadeout[k];
        }
        // signal[start:end] += chunk[: len(signal[start:end])], with Python slice semantics
        const long n = (long)signal_length;
        long s = start < 0 ? std::max(0L, n + start) : std::min(start, n);
        long e = end < 0 ? std::max(0L, n + end) : std::min(end, n);
        for (long k = 0; s + k < e; ++k) signal[s + k] += chunk[k];
    }
    signal.resize(std::min(length, signal_length));
    return signal;
}

std::vector<double> time_points(int n_steps, double a) {
    std::vector<double> ts(n_steps + 1);
    for (int i = 0; i <= n_steps; ++i) {
        const double t = n_steps > 0 ? (double)i / n_steps : 0.0;  // np.linspace(0, 1, n + 1)
        ts[i] = (std::pow(a, t) - 1.0) / (a - 1.0);
    }
    return ts;
}

unsigned long long NormalGen::next() {
    state_ += 0x9E3779B97F4A7C15ULL;
    unsigned long long z = state_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void NormalGen::fill(float* out, size_t n) {
    for (size_t i = 0; i < n; i += 2) {
        const double u1 = (double)((next() >> 11) + 1) * (1.0 / 9007199254740993.0);
        const double u2 = (double)(next() >> 11) * (1.0 / 9007199254740992.0);
        const double r = std::sqrt(-2.0 * std::log(u1));
        out[i] = (float)(r * std::cos(2.0 * kPi * u2));
        if (i + 1 < n) out[i + 1] = (float)(r * std::sin(2.0 * kPi * u2));
    }
}

}  // namespace enh
