#include "dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>

#include "pathutil.h"

namespace tmx {

namespace {
constexpr double kPi = 3.14159265358979323846;

// Modified Bessel function I0 (for the Kaiser window).
double bessel_i0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 30; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-12 * sum) break;
    }
    return sum;
}
}  // namespace

std::vector<float> resample(const std::vector<float>& in, int from_rate, int to_rate) {
    if (from_rate == to_rate || in.empty()) return in;
    const double ratio = (double)to_rate / from_rate;
    const size_t n_out = (size_t)std::llround(in.size() * ratio);
    std::vector<float> out(n_out);
    const double cutoff = 0.97 * std::min(1.0, ratio);  // relative to the input Nyquist
    const int zero_crossings = 16;
    const int reach = (int)std::ceil(zero_crossings / std::min(1.0, ratio));  // taps each side
    // Kaiser-windowed sinc, tabulated over [0, reach + 1] input samples.
    const int res = 512;
    const double beta = 8.0, i0b = bessel_i0(beta);
    std::vector<float> table((size_t)(reach + 1) * res + 2);
    for (size_t k = 0; k < table.size(); ++k) {
        const double d = (double)k / res;
        const double x = d * cutoff;
        const double sinc = x < 1e-9 ? 1.0 : std::sin(kPi * x) / (kPi * x);
        const double r = d / (reach + 1);
        table[k] = r >= 1.0 ? 0.0f : (float)(sinc * bessel_i0(beta * std::sqrt(1.0 - r * r)) / i0b);
    }
    auto kernel = [&](double d) {
        const double pos = std::fabs(d) * res;
        const size_t k = (size_t)pos;
        if (k + 1 >= table.size()) return 0.0f;
        const float f = (float)(pos - k);
        return table[k] + (table[k + 1] - table[k]) * f;
    };
    for (size_t j = 0; j < n_out; ++j) {
        const double t = j / ratio;  // position in input samples
        const long c = (long)std::floor(t);
        const long lo = std::max(0L, c - reach + 1), hi = std::min((long)in.size() - 1, c + reach);
        float acc = 0.0f, wsum = 0.0f;
        for (long i = lo; i <= hi; ++i) {
            const float k = kernel(t - (double)i);
            acc += k * in[(size_t)i];
            wsum += k;
        }
        out[j] = wsum != 0.0f ? acc / wsum : 0.0f;
    }
    return out;
}

std::vector<float> time_stretch(const std::vector<float>& in, int rate, double speed) {
    speed = std::clamp(speed, 0.5, 2.0);
    if (std::fabs(speed - 1.0) < 1e-3 || in.size() < (size_t)rate / 10) return in;
    // WSOLA: overlap-add windows taken from the input at `speed`-scaled positions,
    // each shifted (within +-tol) to line up with what was already written.
    const int win = std::max(64, rate * 30 / 1000);  // 30 ms
    const int hop = win / 2;
    const int tol = rate * 8 / 1000;                  // +-8 ms search
    std::vector<float> w(win);
    for (int i = 0; i < win; ++i) w[i] = (float)(0.5 - 0.5 * std::cos(2.0 * kPi * i / win));

    const size_t n_out = (size_t)(in.size() / speed);
    std::vector<float> out(n_out + win, 0.0f), norm(n_out + win, 0.0f);
    long prev = 0;  // input position used for the previous frame
    for (size_t o = 0; o + win <= out.size(); o += hop) {
        const long nominal = (long)std::llround(o * speed);
        long best = nominal;
        if (o > 0) {
            // Natural continuation of the previous frame, compared against candidates.
            const long natural = prev + hop;
            double best_score = -1e30;
            for (long d = -tol; d <= tol; ++d) {
                const long cand = nominal + d;
                if (cand < 0 || cand + win > (long)in.size()) continue;
                double score = 0.0;
                for (int i = 0; i < hop; i += 2) {
                    const long a = natural + i;
                    if (a < 0 || a >= (long)in.size()) break;
                    score += (double)in[(size_t)a] * in[(size_t)(cand + i)];
                }
                if (score > best_score) best_score = score, best = cand;
            }
        }
        if (best < 0) best = 0;
        if (best + win > (long)in.size()) break;
        for (int i = 0; i < win; ++i) {
            out[o + i] += in[(size_t)best + i] * w[i];
            norm[o + i] += w[i];
        }
        prev = best;
    }
    for (size_t i = 0; i < out.size(); ++i)
        if (norm[i] > 1e-3f) out[i] /= norm[i];
    out.resize(n_out);
    return out;
}

double rms(const float* x, size_t n) {
    if (n == 0) return 0.0;
    double s = 0.0;
    for (size_t i = 0; i < n; ++i) s += (double)x[i] * x[i];
    return std::sqrt(s / n);
}

double active_rms(const float* x, size_t n, int rate) {
    const size_t block = std::max<size_t>(1, (size_t)rate / 50);
    const double floor = std::pow(10.0, -50.0 / 20.0);
    double s = 0.0;
    size_t count = 0;
    for (size_t i = 0; i + block <= n; i += block) {
        const double r = rms(x + i, block);
        if (r > floor) s += r * r * block, count += block;
    }
    return count ? std::sqrt(s / count) : 0.0;
}

void high_pass(std::vector<float>& x, int rate, double hz) {
    if (x.empty() || hz <= 0) return;
    // RBJ biquad, Q = 1/sqrt(2)
    const double w = 2.0 * kPi * hz / rate, cw = std::cos(w), alpha = std::sin(w) / (2.0 * std::sqrt(0.5));
    const double a0 = 1 + alpha;
    const double b0 = (1 + cw) / 2 / a0, b1 = -(1 + cw) / a0, b2 = b0, a1 = -2 * cw / a0, a2 = (1 - alpha) / a0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (float& v : x) {
        const double y = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1, x1 = v, y2 = y1, y1 = y;
        v = (float)y;
    }
}

namespace {
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    float run(float v) {
        const double y = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1, x1 = v, y2 = y1, y1 = y;
        return (float)y;
    }
};
// RBJ cookbook filters; Q = 1.414 is about one octave wide.
Biquad bandpass(int rate, double f, double q) {
    const double w = 2 * kPi * f / rate, al = std::sin(w) / (2 * q), a0 = 1 + al;
    Biquad b;
    b.b0 = al / a0, b.b1 = 0, b.b2 = -al / a0, b.a1 = -2 * std::cos(w) / a0, b.a2 = (1 - al) / a0;
    return b;
}
Biquad peaking(int rate, double f, double q, double gain_db) {
    const double A = std::pow(10.0, gain_db / 40.0), w = 2 * kPi * f / rate, al = std::sin(w) / (2 * q);
    const double a0 = 1 + al / A;
    Biquad b;
    b.b0 = (1 + al * A) / a0, b.b1 = -2 * std::cos(w) / a0, b.b2 = (1 - al * A) / a0;
    b.a1 = -2 * std::cos(w) / a0, b.a2 = (1 - al / A) / a0;
    return b;
}
constexpr double kBandQ = 1.414;
}  // namespace

std::vector<double> band_levels_db(const std::vector<float>& x, int rate, const std::vector<double>& centres) {
    std::vector<double> out(centres.size(), -200.0);
    for (size_t k = 0; k < centres.size(); ++k) {
        if (centres[k] * 1.42 >= rate / 2.0 || x.empty()) continue;
        // Two band-pass stages in a row: sharper skirts, so a band reads its own octave.
        Biquad f1 = bandpass(rate, centres[k], kBandQ), f2 = bandpass(rate, centres[k], kBandQ);
        double e = 0;
        for (float v : x) {
            const double y = f2.run(f1.run(v));
            e += y * y;
        }
        out[k] = 10.0 * std::log10(e / (double)x.size() + 1e-20);
    }
    return out;
}

void apply_eq(std::vector<float>& x, int rate, const std::vector<double>& centres, const std::vector<double>& gains_db) {
    for (size_t k = 0; k < centres.size() && k < gains_db.size(); ++k) {
        if (centres[k] * 1.42 >= rate / 2.0 || std::fabs(gains_db[k]) < 0.05) continue;
        Biquad f = peaking(rate, centres[k], kBandQ, gains_db[k]);
        for (float& v : x) v = f.run(v);
    }
}

std::vector<double> tone_match_gains(const std::vector<double>& want, const std::vector<double>& have, double max_db,
                                     double strength) {
    const size_t n = std::min(want.size(), have.size());
    std::vector<double> d(n, 0.0);
    std::vector<bool> ok(n, false);
    double mean = 0;
    size_t used = 0;
    for (size_t k = 0; k < n; ++k) {
        ok[k] = want[k] > -150 && have[k] > -150;
        if (ok[k]) d[k] = want[k] - have[k], mean += d[k], ++used;
    }
    if (used == 0) return std::vector<double>(n, 0.0);
    mean /= (double)used;  // overall loudness is matched separately
    for (size_t k = 0; k < n; ++k) d[k] = ok[k] ? std::clamp(d[k] - mean, -max_db, max_db) : 0.0;
    std::vector<double> g(n, 0.0);
    for (size_t k = 0; k < n; ++k) {
        if (!ok[k]) continue;
        const double l = k > 0 && ok[k - 1] ? d[k - 1] : d[k], r = k + 1 < n && ok[k + 1] ? d[k + 1] : d[k];
        g[k] = strength * (0.25 * l + 0.5 * d[k] + 0.25 * r);
    }
    return g;
}

std::vector<size_t> chunk_bounds(size_t n, size_t max_len) {
    const size_t parts = max_len == 0 || n <= max_len ? 1 : (n + max_len - 1) / max_len;
    std::vector<size_t> b(parts + 1);
    // Equal parts rather than full ones and a short remainder: a part of a few
    // seconds would give the model little to go on.
    for (size_t k = 0; k <= parts; ++k) b[k] = (size_t)((double)n * k / parts);
    b[parts] = n;
    return b;
}

float chunk_weight(const std::vector<size_t>& bounds, size_t k, size_t t, size_t fade) {
    // Rising edge across bound `at`: 0 before at - fade, 1 from at + fade on.
    auto rise = [&](size_t at) {
        if (fade == 0) return t >= at ? 1.0f : 0.0f;
        if (t + fade < at) return 0.0f;
        if (t >= at + fade) return 1.0f;
        return (float)(((double)t + fade - at + 0.5) / (2.0 * fade));
    };
    const size_t last = bounds.size() - 2;
    float w = 1.0f;
    if (k > 0) w = rise(bounds[k]);
    if (k < last) w = std::min(w, 1.0f - rise(bounds[k + 1]));
    return w;
}

void fade_edges(std::vector<float>& x, int rate, double ms) {
    const size_t n = std::min(x.size() / 2, (size_t)(rate * ms / 1000.0));
    for (size_t i = 0; i < n; ++i) {
        const float g = (float)i / n;
        x[i] *= g;
        x[x.size() - 1 - i] *= g;
    }
}

void limit_peaks(std::vector<float>& x, int rate, int channels) {
    // Look-ahead limiter: a smooth gain curve that dips just before a peak and
    // recovers slowly afterwards, applied to all channels alike. Unlike clipping
    // or a waveshaper it adds no distortion (crackle) to the signal.
    //
    // Per frame i:
    //   need[i]   = gain the frame needs on its own (ceiling / peak, at most 1)
    //   m[i]      = min(need) over [i - look, i + look]
    //   target[i] = mean of m over [i - half, i + half] (centred moving average:
    //               stays <= what each frame needs, because every value
    //               averaged already covers it)
    //   g         = min(target, g recovering towards 1 with an 80 ms release)
    // It runs as a stream with only O(look) state, not whole-mix arrays (a dub of
    // a feature film is hundreds of millions of frames): frame i is written once
    // need/m are known up to i + half + look, and those later frames are still
    // untouched when their need is read, so it works in place.
    const float ceiling = 0.89f;  // about -1 dBFS
    const size_t ch = (size_t)std::max(1, channels);
    const size_t n = x.size() / ch;
    if (n == 0) return;
    auto need_of = [&](size_t i) {
        float pk = 0.0f;
        for (size_t c = 0; c < ch; ++c) pk = std::max(pk, std::fabs(x[i * ch + c]));
        return pk > ceiling ? ceiling / pk : 1.0f;
    };
    bool any = false;
    for (size_t i = 0; i < n && !any; ++i) any = need_of(i) < 1.0f;
    if (!any) return;  // nothing to limit: leave the signal bit-exact
    const size_t look = std::max<size_t>(1, (size_t)rate * 5 / 1000);  // 5 ms
    const size_t half = look / 2;

    // Sliding-window minimum (monotonic deque) over ring buffers. The deque can
    // briefly span 2 * look + 2 frames (before its stale front is dropped).
    const size_t W = 2 * look + 3;
    std::vector<float> need(W);   // need[j % W]
    std::vector<size_t> dq(W);    // frame indices, circular
    size_t dq_head = 0, dq_len = 0, next = 0;
    auto m_of = [&](size_t i) {
        const size_t hi = std::min(n - 1, i + look);
        for (; next <= hi; ++next) {
            const float v = need_of(next);
            need[next % W] = v;
            while (dq_len > 0 && need[dq[(dq_head + dq_len - 1) % W] % W] >= v) --dq_len;
            dq[(dq_head + dq_len++) % W] = next;
        }
        while (dq[dq_head] + look < i) dq_head = (dq_head + 1) % W, --dq_len;
        return need[dq[dq_head] % W];
    };
    // Prefix sums of m (pre[k] = m[0] + ... + m[k-1], in double, summed in the
    // same order as a whole-array prefix sum would be), kept for 2 * half + 1 frames.
    const size_t P = 2 * half + 3;
    std::vector<double> pre(P, 0.0);  // pre[k % P]
    double acc = 0.0;
    size_t m_done = 0;  // m known for frames [0, m_done)
    const double release = 1.0 - std::exp(-1.0 / (0.08 * rate));  // 80 ms
    double g = 1.0;
    for (size_t i = 0; i < n; ++i) {
        const size_t lo = i >= half ? i - half : 0, hi = std::min(n, i + half + 1);
        for (; m_done < hi; ++m_done) {
            acc += m_of(m_done);
            pre[(m_done + 1) % P] = acc;
        }
        const double target = (pre[hi % P] - pre[lo % P]) / (double)(hi - lo);
        g = std::min(target, g + (1.0 - g) * release);
        for (size_t c = 0; c < ch; ++c) {
            float& v = x[i * ch + c];
            v = std::clamp((float)(v * g), -ceiling, ceiling);
        }
    }
}

void write_wav(const std::string& path, const std::vector<float>& pcm, int rate, int channels) {
    // The RIFF and data chunk sizes are 32-bit: a larger file would get wrapped
    // sizes that players read as a short, broken file. (~6.7 hours of 44.1 kHz
    // stereo; checked before anything is written.)
    if ((unsigned long long)pcm.size() * 2 > 0xFFFFFFFFull - 36)
        throw std::runtime_error("cannot write " + path + ": the audio is too long for a WAV file (over 4 GB)");
    std::ofstream f(path_from_utf8(path), std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t data_bytes = (uint32_t)(pcm.size() * 2);
    f.write("RIFF", 4);
    u32(36 + data_bytes);
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16((uint16_t)channels);
    u32((uint32_t)rate);
    u32((uint32_t)(rate * channels * 2));
    u16((uint16_t)(channels * 2));
    u16(16);
    f.write("data", 4);
    u32(data_bytes);
    std::vector<int16_t> buf;
    buf.reserve(65536);
    for (size_t i = 0; i < pcm.size(); ++i) {
        const float v = std::clamp(pcm[i], -1.0f, 1.0f);
        buf.push_back((int16_t)std::lrint(v * 32767.0f));
        if (buf.size() == 65536) {
            f.write(reinterpret_cast<const char*>(buf.data()), (std::streamsize)(buf.size() * 2));
            buf.clear();
        }
    }
    f.write(reinterpret_cast<const char*>(buf.data()), (std::streamsize)(buf.size() * 2));
    f.close();  // flush now, so a full disk is reported here
    if (!f) throw std::runtime_error("cannot write " + path + " (disk full?)");
}

}  // namespace tmx
