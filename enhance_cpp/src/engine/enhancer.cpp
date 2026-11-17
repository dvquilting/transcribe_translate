// enhancer.cpp - see enhancer.h. Each step names the Python code it follows.
//
// The original runs each 30 s chunk through all networks before the next chunk.
// Here the chunks go through one stage at a time (all denoising, then all CFM
// solving, then all vocoding; a long file in groups of chunks), and each stage's
// networks are released before the next: every ONNX Runtime session keeps the
// memory of its largest run, so all of them together would need several GB more
// GPU memory than any one stage. The result is the same: each chunk's random
// numbers come from the same place in the random stream as in the original order.
#include "enhancer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace enh {

namespace {

constexpr int kDenoiserFft = kHop * 4;  // Denoiser.stft_cfg: n_fft = win = 1680
constexpr int kBins = kDenoiserFft / 2 + 1;
constexpr int kChunkPad = 441;          // inference_chunk's npad
constexpr double kGroupSeconds = 600;   // audio held between the stages at most

float abs_max(const float* x, size_t n) {
    float m = 0.0f;
    for (size_t i = 0; i < n; ++i) m = std::max(m, std::fabs(x[i]));
    return m;
}

// ENHANCE_PROFILE=1: the time of each stage on stderr, with the GPU memory in use (development).
struct Timer {
    const Models* models = nullptr;
    bool on = std::getenv("ENHANCE_PROFILE") != nullptr;
    std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
    void lap(const char* what) {
        if (!on) return;
        const auto now = std::chrono::steady_clock::now();
        const double s = std::chrono::duration<double>(now - t).count();
        double used = 0, budget = 0;
        if (models && models->gpu_memory(used, budget))
            std::fprintf(stderr, "  [profile] %-12s %8.3f s  GPU memory %.2f of %.2f GB\n", what, s, used, budget);
        else
            std::fprintf(stderr, "  [profile] %-12s %8.3f s\n", what, s);
        t = now;
    }
};

// Denoiser.forward: STFT, the UNet's magnitude mask and phase rotation, inverse STFT.
std::vector<float> run_denoiser(Models& models, const std::vector<float>& input) {
    std::vector<float> x(input);
    const float scale = abs_max(x.data(), x.size()) + 1e-7f;  // _normalize
    for (float& v : x) v /= scale;
    auto spec = stft(x, kDenoiserFft, kHop, kDenoiserFft, PadMode::Reflect);
    spec.pop_back();  // s[..., :-1]
    const int T = (int)spec.size();
    Mat mag(kBins, T), cs(kBins, T), sn(kBins, T);
    for (int t = 0; t < T; ++t)
        for (int k = 0; k < kBins; ++k) {
            const std::complex<float> c = spec[t][k];
            const float phi = std::arg(c);
            mag.at(k, t) = std::abs(c);
            cs.at(k, t) = std::cos(phi);
            sn.at(k, t) = std::sin(phi);
        }
    spec.clear();
    models.denoise(mag, cs, sn);
    // _istft: complex spectrum, the last frame repeated ("replicate" pad)
    std::vector<std::vector<std::complex<float>>> sep(T + 1, std::vector<std::complex<float>>(kBins));
    for (int t = 0; t < T; ++t)
        for (int k = 0; k < kBins; ++k)
            sep[t][k] = std::complex<float>(mag.at(k, t) * cs.at(k, t), mag.at(k, t) * sn.at(k, t));
    sep[T] = sep[T - 1];
    std::vector<float> o = istft(sep, kDenoiserFft, kHop);
    o.resize(x.size(), 0.0f);  // F.pad(o, (0, npad))
    return o;
}

int solver_steps(int nfe, Solver& solver) {
    if (nfe == 1 && solver != Solver::Euler) solver = Solver::Euler;  // Solver.configurate_
    switch (solver) {
    case Solver::Euler: return nfe;
    case Solver::Midpoint: return nfe / 2;
    case Solver::Rk4: return nfe / 4;
    }
    return nfe;
}

// a + s * b, elementwise
Mat axpy(const Mat& a, double s, const Mat& b) {
    Mat r(a.rows, a.cols);
    const float sf = (float)s;
    for (size_t i = 0; i < r.v.size(); ++i) r.v[i] = a.v[i] + sf * b.v[i];
    return r;
}

// The columns [lo, lo + n) of m, zero beyond its end.
Mat columns(const Mat& m, int lo, int n) {
    Mat r(m.rows, n);
    const int avail = std::max(0, std::min(n, m.cols - lo));
    for (int c = 0; c < m.rows; ++c) std::copy(m.row(c) + lo, m.row(c) + lo + avail, r.row(c));
    return r;
}

// One chunk of inference(): its place in the audio, the peak it is normalized by
// (inference_chunk), and what the stages hand on.
struct Chunk {
    size_t start = 0, length = 0;
    float peak = 1.0f;
    int frames = 0;          // mel frames T = (length + 441) / 420
    NormalGen gen{0};        // the random stream at this chunk's place
    Mat mel, cond;           // stage 1: normalized mel of the input, and the CFM condition
    Mat features;            // stage 2: the decoder's output
    std::vector<float> out;  // stage 3 / denoise only: the result, scaled back
};

// inference_chunk's input: the chunk / peak, padded with 441 zeros, then
// Enhancer.forward's / Denoiser.forward's own normalization (x / (max |x| + 1e-7)).
std::vector<float> chunk_input(const std::vector<float>& wav, const Chunk& c, bool second_normalization) {
    std::vector<float> x(wav.begin() + (long)c.start, wav.begin() + (long)(c.start + c.length));
    for (float& v : x) v /= c.peak;
    x.resize(c.length + kChunkPad, 0.0f);
    if (second_normalization) {
        const float s = abs_max(x.data(), x.size()) + 1e-7f;
        for (float& v : x) v /= s;
    }
    return x;
}

// Stage 1 of Enhancer.forward: x_mel_original and x_mel_denoised (lambd-mixed).
void stage_mels(Models& models, const std::vector<float>& wav, Chunk& c, float lambd) {
    const std::vector<float> x = chunk_input(wav, c, true);
    c.mel = enhancer_mel(x, models.filters());
    c.cond = c.mel;
    if (lambd != 0.0f) {
        const Mat den = enhancer_mel(run_denoiser(models, x), models.filters());
        for (size_t i = 0; i < c.cond.v.size(); ++i) c.cond.v[i] = lambd * den.v[i] + (1.0f - lambd) * c.mel.v[i];
    }
}

// Stage 2: LCFM.forward - the prior from the encoded original mel, the CFM ODE from
// t = 0 to 1 (Solver.solve), decoded. `report(fraction)` after each step.
template <class Report>
void stage_solve(Models& models, Chunk& c, const Options& o, const Report& report) {
    const Filters& f = models.filters();
    const int T = c.frames;
    Mat psi = models.encode(c.mel);  // psi0 = tau * noise + (1 - tau) * scale(encode(mel))
    {
        Mat noise(kLatent, T);
        c.gen.fill(noise.v.data(), noise.v.size());
        for (size_t i = 0; i < psi.v.size(); ++i) psi.v[i] = o.tau * noise.v[i] + (1.0f - o.tau) * (psi.v[i] * f.z_scale);
    }
    Solver solver = o.solver;
    const int steps = solver_steps(o.nfe, solver);
    const auto ts = time_points(steps, f.time_a);
    auto v = [&](double t, const Mat& p) { return models.velocity(p, c.cond, (float)t); };
    for (int i = 0; i < steps; ++i) {
        const double t = ts[i], dt = ts[i + 1] - ts[i];
        switch (solver) {
        case Solver::Euler:
            psi = axpy(psi, dt, v(t, psi));
            break;
        case Solver::Midpoint:
            psi = axpy(psi, dt, v(t + dt / 2, axpy(psi, dt / 2, v(t, psi))));
            break;
        case Solver::Rk4: {
            const Mat k1 = v(t, psi);
            const Mat k2 = v(t + dt / 2, axpy(psi, dt / 2, k1));
            const Mat k3 = v(t + dt / 2, axpy(psi, dt / 2, k2));
            const Mat k4 = v(t + dt, axpy(psi, dt, k3));
            Mat r(psi.rows, psi.cols);
            for (size_t j = 0; j < r.v.size(); ++j)
                r.v[j] = psi.v[j] + (float)dt * (k1.v[j] + 2 * k2.v[j] + 2 * k3.v[j] + k4.v[j]) / 6;
            psi = std::move(r);
            break;
        }
        }
        report((double)(i + 1) / std::max(steps, 1));
    }
    for (float& p : psi.v) p /= f.z_scale;  // unscale
    c.features = models.decode(psi);
    c.mel = Mat();
    c.cond = Mat();
}

// Stage 3: UnivNet.forward - the features padded with 10 zero frames, the noise for
// all T + 10 frames, the output trimmed to 420 T samples. Run in windows of frames:
// the vocoder's receptive field is under 16 frames, so a window with 24 frames of
// context on both sides gives the same samples as one run over everything (to
// float rounding), with a fraction of the memory (a 30 s chunk in one piece needs
// ~3 GB per block for its location-variable kernels). All windows have the same
// size, so a GPU prepares the network only once.
template <class Report>
void stage_vocode(Models& models, Chunk& c, int core, const Report& report) {
    constexpr int kContext = 24;
    const int window = core + 2 * kContext;
    const int T = c.frames, total = T + kVocoderPad;
    Mat noise(kNoise, total);
    c.gen.fill(noise.v.data(), noise.v.size());
    const Mat padded = columns(c.features, 0, total);
    c.features = Mat();
    std::vector<float> y;
    if (total <= window) {
        y = models.vocode(padded, noise);
        y.resize((size_t)T * kHop);
    } else {
        y.assign((size_t)T * kHop, 0.0f);
        for (int a = 0; a < T; a += core) {
            const int b = std::min(a + core, T);
            const int lo = std::min(std::max(0, a - kContext), total - window);
            const std::vector<float> w = models.vocode(columns(padded, lo, window), columns(noise, lo, window));
            std::copy(w.begin() + (long)(a - lo) * kHop, w.begin() + (long)(b - lo) * kHop, y.begin() + (long)a * kHop);
            report((double)b / T);
        }
    }
    y.resize(c.length);  // hwav[:length]
    for (float& v : y) v *= c.peak;
    c.out = std::move(y);
}

// The vocoder window (frames kept per run) for the GPU memory that is free.
int vocoder_core(const Models& models) {
    double used = 0, budget = 0;
    if (!models.gpu_memory(used, budget)) return 512;
    const double free_gb = budget - used;
    if (free_gb >= 4.5) return 512;
    if (free_gb >= 2.5) return 256;
    return 128;
}

}  // namespace

const char* solver_name(Solver s) {
    switch (s) {
    case Solver::Midpoint: return "midpoint";
    case Solver::Rk4: return "rk4";
    case Solver::Euler: return "euler";
    }
    return "?";
}

bool parse_solver(const std::string& s, Solver& out) {
    if (s == "midpoint") out = Solver::Midpoint;
    else if (s == "rk4") out = Solver::Rk4;
    else if (s == "euler") out = Solver::Euler;
    else return false;
    return true;
}

std::vector<float> process(Models& models, const std::vector<float>& wav, const Options& o, const Progress& progress) {
    if (o.nfe < 1 || o.nfe > 128) throw std::invalid_argument("nfe must be 1..128");
    if (o.lambd < 0 || o.lambd > 1) throw std::invalid_argument("lambd must be 0..1");
    if (o.tau < 0 || o.tau > 1) throw std::invalid_argument("tau must be 0..1");
    if (wav.empty()) throw std::invalid_argument("no audio");

    // inference(): chunks of chunk_seconds, overlapping by overlap_seconds
    const size_t chunk_length = (size_t)(kRate * o.chunk_seconds);
    const size_t overlap_length = (size_t)(kRate * o.overlap_seconds);
    if (chunk_length <= overlap_length) throw std::invalid_argument("chunks must be longer than their overlap");
    const size_t hop_length = chunk_length - overlap_length;
    std::vector<Chunk> chunks;
    NormalGen stream(o.seed);
    for (size_t start = 0; start < wav.size(); start += hop_length) {
        Chunk c;
        c.start = start;
        c.length = std::min(wav.size(), start + chunk_length) - start;
        c.peak = std::max(abs_max(wav.data() + start, c.length), 1e-7f);
        c.frames = (int)((c.length + kChunkPad) / kHop);
        c.gen = stream;
        if (!o.denoise_only) {  // the CFM prior's noise, then the vocoder's
            stream.skip((size_t)kLatent * c.frames);
            stream.skip((size_t)kNoise * (c.frames + kVocoderPad));
        }
        chunks.push_back(std::move(c));
    }

    const size_t n = chunks.size();
    const size_t group = std::max<size_t>(1, (size_t)(kGroupSeconds / std::max(1.0, o.chunk_seconds - o.overlap_seconds)));
    // Progress: each chunk's share, split between the stages by their usual cost.
    const bool denoise_stage = o.denoise_only || o.lambd != 0.0f;
    const double w_mel = o.denoise_only ? 1.0 : (denoise_stage ? 0.08 : 0.01), w_solve = o.denoise_only ? 0 : 0.6,
                 w_voc = o.denoise_only ? 0 : 0.3;
    const double w_sum = w_mel + w_solve + w_voc;
    auto report = [&](size_t done_chunks, double stage_base, double stage_w, double frac, const char* stage) {
        if (progress)
            progress(((double)done_chunks + (stage_base + stage_w * frac) / w_sum) / (double)n, stage);
    };

    Timer timer;
    timer.models = &models;
    for (size_t g0 = 0; g0 < n; g0 += group) {
        const size_t g1 = std::min(n, g0 + group);
        if (o.denoise_only) {
            for (size_t i = g0; i < g1; ++i) {
                report(i, 0, w_mel, 0, "Denoising");
                Chunk& c = chunks[i];
                std::vector<float> y = run_denoiser(models, chunk_input(wav, c, false));
                y.resize(c.length);
                for (float& v : y) v *= c.peak;
                c.out = std::move(y);
            }
            timer.lap("denoise");
            continue;
        }
        for (size_t i = g0; i < g1; ++i) {
            report(i, 0, w_mel, 0, denoise_stage ? "Denoising" : "Analysing");
            stage_mels(models, wav, chunks[i], o.lambd);
        }
        timer.lap("denoise");
        models.release({Net::Denoiser});
        for (size_t i = g0; i < g1; ++i) {
            report(i, w_mel, w_solve, 0, "Enhancing");
            stage_solve(models, chunks[i], o,
                        [&](double f) { report(i, w_mel, w_solve, f, "Enhancing"); });
        }
        timer.lap("solve");
        models.release({Net::Encoder, Net::Velocity, Net::Decoder});
        const int core = vocoder_core(models);
        for (size_t i = g0; i < g1; ++i) {
            report(i, w_mel + w_solve, w_voc, 0, "Vocoding");
            stage_vocode(models, chunks[i], core,
                         [&](double f) { report(i, w_mel + w_solve, w_voc, f, "Vocoding"); });
        }
        timer.lap("vocode");
        models.release({Net::Vocoder});
    }
    if (progress) progress(1.0, o.denoise_only ? "Denoising" : "Enhancing");

    std::vector<std::vector<float>> outs;
    outs.reserve(n);
    for (auto& c : chunks) outs.push_back(std::move(c.out));
    chunks.clear();
    return merge_chunks(outs, chunk_length, hop_length, wav.size(), models.filters());
}

}  // namespace enh
