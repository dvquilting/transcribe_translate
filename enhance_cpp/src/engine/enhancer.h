// enhancer.h - Resemble Enhance's inference (resemble_enhance/inference.py and
// enhancer/enhancer.py) on 44.1 kHz mono audio.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "models.h"

namespace enh {

enum class Solver { Midpoint, Rk4, Euler };

struct Options {
    bool denoise_only = false;  // only the denoiser (enhancer.inference.denoise)
    int nfe = 64;               // CFM function evaluations, 1..128
    Solver solver = Solver::Midpoint;
    float lambd = 0.9f;         // denoise strength before enhancing, 0..1
    float tau = 0.5f;           // CFM prior temperature, 0..1
    double chunk_seconds = 30.0;
    double overlap_seconds = 1.0;
    uint64_t seed = 0;          // the random numbers (CFM prior, vocoder noise)
};

// Called with the fraction done (0..1) and what is being done.
using Progress = std::function<void(double fraction, const std::string& stage)>;

// inference(): 44.1 kHz mono in, 44.1 kHz mono out (same length). `models` must
// have the enhancer networks unless o.denoise_only.
std::vector<float> process(Models& models, const std::vector<float>& wav, const Options& o, const Progress& progress);

// The GPU memory the denoiser needs for pieces of this length (GB).
double denoiser_memory_gb(double chunk_seconds);
// The piece length to use: `wanted`, or shorter (down to 10 s) when the GPU memory
// that other programs leave free is too small for the denoiser on `wanted`.
double fit_chunk_seconds(const Models& models, double wanted);

const char* solver_name(Solver s);
bool parse_solver(const std::string& s, Solver& out);

}  // namespace enh
