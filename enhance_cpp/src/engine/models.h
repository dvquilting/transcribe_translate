// models.h - the five ONNX networks of Resemble Enhance in ONNX Runtime
// (DirectML on any DirectX 12 GPU, else the CPU).
//
// A network is loaded when it is first used and stays loaded until release():
// the enhancer releases each stage's networks before the next stage, because each
// keeps the memory of its largest run and together they would need several GB
// more than any one of them.
#pragma once

#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "dsp.h"

namespace enh {

enum class Device { Auto, Gpu, Cpu };

enum class Net { Denoiser, Encoder, Velocity, Decoder, Vocoder };

struct EngineInfo {
    std::string backend;  // "DirectML" or "CPU"
    std::string detail;   // the GPU's name and memory, or the thread count
};

class Models {
public:
    // Checks the model files in `dir` (all, or only the denoiser's), loads filters.bin
    // and chooses the device: Device::Auto uses the GPU when DirectML can be set up
    // (tried by loading the denoiser), else the CPU; Device::Gpu fails without it.
    // `threads` = 0: ONNX Runtime's default.
    Models(const std::string& dir, Device device, bool denoiser_only, int threads, int gpu_index = 0);
    ~Models();
    Models(const Models&) = delete;
    Models& operator=(const Models&) = delete;

    const EngineInfo& engine() const { return engine_; }
    const Filters& filters() const { return filters_; }
    bool on_gpu() const { return gpu_index_ >= 0; }

    // Load these networks now (they are otherwise loaded on first use).
    void load(std::initializer_list<Net> nets);
    // Unload these networks, freeing their memory.
    void release(std::initializer_list<Net> nets);

    // Denoiser._predict + _separate on magnitude / cos / sin (841 x T each), in place.
    void denoise(Mat& mag, Mat& cos, Mat& sin);
    Mat encode(const Mat& mel);                                        // 128 x T -> 64 x T
    Mat velocity(const Mat& psi, const Mat& cond, float t);            // 64 x T, 128 x T -> 64 x T
    Mat decode(const Mat& latent);                                     // 64 x T -> 160 x T
    std::vector<float> vocode(const Mat& features, const Mat& noise);  // 160 x N, 128 x N -> 420 N

    // This process's use of the GPU's own memory and the budget Windows gives it
    // (what is free for it, given the other programs), in GB; false on the CPU.
    bool gpu_memory(double& used_gb, double& budget_gb) const;

    // The files a complete models folder holds.
    static std::vector<std::string> required_files(bool denoiser_only);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    EngineInfo engine_;
    Filters filters_;
    int gpu_index_ = -1;  // the DXGI adapter in use, -1 on the CPU
};

}  // namespace enh
