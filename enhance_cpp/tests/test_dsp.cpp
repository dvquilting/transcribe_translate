// test_dsp.cpp - the C++ signal processing against PyTorch / torchaudio / Resemble
// Enhance's own code (reference data: tools/make_test_data.py -> test_data/dsp).
//
//   enhance_tests TEST_DATA_DIR
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "dsp.h"
#include "utf8_path.h"

namespace {

std::string g_dir;
int g_failed = 0;

struct Array {
    std::vector<int> shape;
    std::vector<float> v;
};

Array load(const std::string& name) {
    Array a;
    std::ifstream in(pk::path_from_utf8(g_dir + "/dsp/" + name + ".bin"), std::ios::binary);
    if (!in) {
        std::printf("FAIL: missing %s.bin (run tools/make_test_data.py)\n", name.c_str());
        ++g_failed;
        return a;
    }
    int32_t ndim = 0;
    in.read(reinterpret_cast<char*>(&ndim), 4);
    size_t n = 1;
    for (int i = 0; i < ndim; ++i) {
        int32_t d = 0;
        in.read(reinterpret_cast<char*>(&d), 4);
        a.shape.push_back(d);
        n *= (size_t)d;
    }
    a.v.resize(n);
    in.read(reinterpret_cast<char*>(a.v.data()), (std::streamsize)(n * 4));
    return a;
}

// max |got - want| relative to max |want|
void compare(const char* what, const std::vector<float>& got, const std::vector<float>& want, double tol) {
    if (got.size() != want.size()) {
        std::printf("FAIL %-28s size %zu, expected %zu\n", what, got.size(), want.size());
        ++g_failed;
        return;
    }
    double err = 0, scale = 1e-12;
    size_t worst = 0;
    for (size_t i = 0; i < got.size(); ++i) {
        const double e = std::fabs((double)got[i] - want[i]);
        if (e > err) {
            err = e;
            worst = i;
        }
        scale = std::max(scale, std::fabs((double)want[i]));
    }
    const double rel = err / scale;
    const bool ok = rel <= tol;
    std::printf("%s %-28s max relative difference %.2e (limit %.0e)", ok ? "ok  " : "FAIL", what, rel, tol);
    if (!ok) std::printf(" at [%zu]: %.6g, expected %.6g", worst, got[worst], want[worst]);
    std::printf("\n");
    if (!ok) ++g_failed;
}

void check(const char* what, bool ok, const std::string& detail = {}) {
    std::printf("%s %-28s %s\n", ok ? "ok  " : "FAIL", what, detail.c_str());
    if (!ok) ++g_failed;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: enhance_tests TEST_DATA_DIR\n");
        return 2;
    }
    g_dir = argv[1];
    const enh::Filters f = enh::load_filters(g_dir + "/dsp/filters.bin");

    for (int sr : {48000, 22050, 16000}) {
        const auto in = load("resample_" + std::to_string(sr) + "_in");
        const auto want = load("resample_" + std::to_string(sr) + "_out");
        compare(("resample " + std::to_string(sr) + " -> 44100").c_str(), enh::resample(in.v, sr, 44100), want.v, 5e-5);
    }

    {
        const auto in = load("mel_in");
        const auto want = load("mel_out");
        const enh::Mat mel = enh::enhancer_mel(in.v, f);
        check("mel shape", (int)want.shape.size() == 2 && mel.rows == want.shape[0] && mel.cols == want.shape[1],
              std::to_string(mel.rows) + " x " + std::to_string(mel.cols));
        // Quiet mel bins are in dB, where float32 FFT rounding shows: torch's own result
        // differs from a float64 computation by up to 0.02 there. So the worst bin is
        // checked loosely and the average closely.
        compare("enhancer mel (worst bin)", mel.v, want.v, 2e-2);
        double sum = 0;
        for (size_t i = 0; i < mel.v.size() && i < want.v.size(); ++i) sum += std::fabs(mel.v[i] - want.v[i]);
        const double mean = mel.v.empty() ? 1 : sum / (double)mel.v.size();
        check("enhancer mel (average)", mean < 1e-3, "mean difference " + std::to_string(mean));

        // The denoiser's STFT (1680 / 420, reflect), last frame dropped as Denoiser._stft does.
        std::vector<float> x(in.v);
        auto spec = enh::stft(x, 1680, 420, 1680, enh::PadMode::Reflect);
        spec.pop_back();
        const auto mag_want = load("stft_mag"), cos_want = load("stft_cos");
        const int bins = 841, T = (int)spec.size();
        std::vector<float> mag((size_t)bins * T), cs((size_t)bins * T);
        for (int t = 0; t < T; ++t)
            for (int k = 0; k < bins; ++k) {
                mag[(size_t)k * T + t] = std::abs(spec[t][k]);
                cs[(size_t)k * T + t] = std::cos(std::arg(spec[t][k]));
            }
        compare("denoiser stft magnitude", mag, mag_want.v, 1e-5);
        compare("denoiser stft cos", cs, cos_want.v, 5e-3);  // phase of near-zero bins is ill-defined

        // _istft(mag * 0.7, cos, sin): the last frame repeated
        for (auto& frame : spec)
            for (auto& c : frame) c *= 0.7f;
        spec.push_back(spec.back());
        compare("denoiser istft", enh::istft(spec, 1680, 420), load("istft_out").v, 1e-5);
    }

    {
        const auto a = load("offset_a"), b = load("offset_b"), want = load("offset_out");
        const long got = enh::compute_offset(a.v, b.v, f);
        check("compute_offset", !want.v.empty() && got == (long)want.v[0],
              std::to_string(got) + " (expected " + (want.v.empty() ? "?" : std::to_string((long)want.v[0])) + ")");
    }

    {
        std::vector<std::vector<float>> chunks;
        for (int i = 0; i < 3; ++i) chunks.push_back(load("merge_chunk" + std::to_string(i)).v);
        compare("merge_chunks", enh::merge_chunks(chunks, 3 * 44100, 2 * 44100, 6 * 44100 + 17, f),
                load("merge_out").v, 1e-6);
    }

    {
        const auto ts = enh::time_points(32, f.time_a);
        compare("time points", std::vector<float>(ts.begin(), ts.end()), load("time_points_32").v, 1e-6);
    }

    {
        enh::NormalGen g(7);
        std::vector<float> n(17);
        g.fill(n.data(), 11);
        g.fill(n.data() + 11, 6);
        compare("noise generator", n, load("noise_7").v, 1e-6);
    }

    std::printf(g_failed ? "%d test(s) FAILED\n" : "All tests passed\n", g_failed);
    return g_failed ? 1 : 0;
}
