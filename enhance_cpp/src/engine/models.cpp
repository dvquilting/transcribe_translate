// models.cpp - see models.h.
#include "models.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "dml_provider_factory.h"
#endif

#include "monitor.h"

#include "onnxruntime_cxx_api.h"
#include "utf8_path.h"

namespace enh {

namespace {

constexpr const char* kFilters = "filters.bin";
constexpr const char* kDenoiser = "denoiser.onnx";
constexpr const char* kEncoder = "encoder.onnx";
constexpr const char* kVelocity = "velocity.onnx";
constexpr const char* kDecoder = "decoder.onnx";
constexpr const char* kVocoder = "vocoder.onnx";

#ifdef _WIN32
std::string narrow(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// The name and memory of DXGI adapter `index` (the numbering DirectML uses).
bool gpu_description(int index, std::string& name, double& gb) {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return false;
    IDXGIAdapter1* adapter = nullptr;
    const bool ok = SUCCEEDED(factory->EnumAdapters1((UINT)index, &adapter));
    if (ok) {
        DXGI_ADAPTER_DESC1 d{};
        adapter->GetDesc1(&d);
        name = narrow(d.Description);
        gb = (double)d.DedicatedVideoMemory / (1024.0 * 1024.0 * 1024.0);
        if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) name += " (software)";
        adapter->Release();
    }
    factory->Release();
    return ok;
}
#endif

Ort::Value tensor(const Ort::MemoryInfo& mem, const float* data, size_t count, std::vector<int64_t> shape) {
    return Ort::Value::CreateTensor<float>(mem, const_cast<float*>(data), count, shape.data(), shape.size());
}

Mat to_mat(Ort::Value& v) {
    const auto shape = v.GetTensorTypeAndShapeInfo().GetShape();  // (1, C, T)
    if (shape.size() != 3) throw std::runtime_error("unexpected model output rank");
    Mat m((int)shape[1], (int)shape[2]);
    const float* p = v.GetTensorData<float>();
    std::copy(p, p + m.v.size(), m.v.begin());
    return m;
}

}  // namespace

#ifdef _WIN32
using Microsoft::WRL::ComPtr;

// One Direct3D 12 device, one compute queue and one DirectML device for all the
// networks (ONNX Runtime would otherwise create a device for every session: five
// per stage, and again for each group of chunks of a long file). A compute queue
// keeps the work off the queue the desktop is drawn with.
struct GpuDevice {
    ComPtr<ID3D12Device> d3d;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDMLDevice> dml;

    explicit GpuDevice(int index) {
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) throw std::runtime_error("DXGI is not available");
        ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(factory->EnumAdapters1((UINT)index, &adapter))) throw std::runtime_error("no such GPU");
        if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d))))
            throw std::runtime_error("Direct3D 12 is not available on this GPU");
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        if (FAILED(d3d->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))))
            throw std::runtime_error("cannot create a GPU command queue");
        // DirectML.dll next to this program (Windows' own copy is too old for ONNX Runtime).
        std::wstring dir(32768, L'\0');
        dir.resize(GetModuleFileNameW(nullptr, dir.data(), (DWORD)dir.size()));
        dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);
        HMODULE lib = LoadLibraryW((dir + L"DirectML.dll").c_str());
        if (!lib) lib = LoadLibraryW(L"DirectML.dll");
        using Create = HRESULT(WINAPI*)(ID3D12Device*, DML_CREATE_DEVICE_FLAGS, REFIID, void**);
        auto create = lib ? reinterpret_cast<Create>(GetProcAddress(lib, "DMLCreateDevice")) : nullptr;
        if (!create || FAILED(create(d3d.Get(), DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&dml))))
            throw std::runtime_error("DirectML could not be set up");
    }

    // Why the GPU stopped working (S_OK while it works).
    HRESULT removed() const { return d3d ? d3d->GetDeviceRemovedReason() : S_OK; }
};
#endif

struct Models::Impl {
    // ENHANCE_ORT_VERBOSE=1: ONNX Runtime's own log, e.g. which device runs each node (development).
    Ort::Env env{std::getenv("ENHANCE_ORT_VERBOSE") ? ORT_LOGGING_LEVEL_VERBOSE : ORT_LOGGING_LEVEL_ERROR, "enhance"};
    Ort::MemoryInfo cpu = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::filesystem::path folder;
    int threads = 0;
    int gpu = -1;  // DXGI adapter for DirectML, -1: CPU
    double pace = 0;  // after each GPU run, rest this many times its length
#ifdef _WIN32
    std::unique_ptr<GpuDevice> device;  // declared before the sessions: destroyed after them
#endif
    std::unique_ptr<Ort::Session> sessions[5];

    // Run one network, timed for the run log; a GPU that was reset (a driver
    // timeout) is reported as such.
    template <class F>
    auto run(Net n, int frames, F&& f) {
        static const char* names[] = {"denoiser", "encoder", "velocity", "decoder", "vocoder"};
        Recorder* rec = Recorder::current();
        Ort::Session& session = get(n);
        if (rec) rec->run_begin(names[(int)n], frames);
        const auto t0 = std::chrono::steady_clock::now();
        try {
            auto out = f(session);
            if (rec) rec->run_end();
            if (gpu >= 0 && pace > 0) {
                const auto took = std::chrono::steady_clock::now() - t0;
                std::this_thread::sleep_for(
                    std::chrono::duration_cast<std::chrono::milliseconds>(took * pace));
            }
            return out;
        } catch (const std::exception& e) {
            if (rec) {
                rec->run_end();
                rec->note(std::string("GPU run failed: ") + names[(int)n] + ": " + e.what());
            }
#ifdef _WIN32
            if (device && device->removed() != S_OK) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "0x%08lX", (unsigned long)device->removed());
                if (rec) rec->note(std::string("GPU device removed, reason ") + buf);
                throw std::runtime_error(
                    std::string("the graphics driver stopped this program's GPU work (device removed, reason ") +
                    buf + "): Windows probably reset the driver after a GPU timeout. Try again with --gentle, "
                    "shorter pieces (--chunk-seconds 10) or --device cpu");
            }
#endif
            throw;
        }
    }

    Ort::SessionOptions options() const {
        Ort::SessionOptions o;
        o.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        if (threads > 0) o.SetIntraOpNumThreads(threads);
#ifdef _WIN32
        if (gpu >= 0) {
            // DirectML needs these (see the DirectML execution provider's documentation).
            o.DisableMemPattern();
            o.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
            const OrtDmlApi* dml = nullptr;
            Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION,
                                                                    reinterpret_cast<const void**>(&dml)));
            Ort::ThrowOnError(dml->SessionOptionsAppendExecutionProvider_DML1(o, device->dml.Get(), device->queue.Get()));
        }
#endif
        return o;
    }

    Ort::Session& get(Net n) {
        auto& s = sessions[(int)n];
        if (!s) {
            static const char* files[] = {kDenoiser, kEncoder, kVelocity, kDecoder, kVocoder};
            const auto path = folder / files[(int)n];
#ifdef _WIN32
            s = std::make_unique<Ort::Session>(env, path.wstring().c_str(), options());
#else
            s = std::make_unique<Ort::Session>(env, path.string().c_str(), options());
#endif
        }
        return *s;
    }
};

std::vector<std::string> Models::required_files(bool denoiser_only) {
    if (denoiser_only) return {kFilters, kDenoiser};
    return {kFilters, kDenoiser, kEncoder, kVelocity, kDecoder, kVocoder};
}

Models::Models(const std::string& dir, Device device, bool denoiser_only, int threads, int gpu_index)
    : impl_(std::make_unique<Impl>()) {
    const auto folder = pk::path_from_utf8(dir);
    for (const auto& f : required_files(denoiser_only))
        if (!std::filesystem::exists(folder / f))
            throw std::runtime_error("model file missing: " + pk::path_to_utf8(folder / f));
    filters_ = load_filters(pk::path_to_utf8(folder / kFilters));
    impl_->folder = folder;
    impl_->threads = threads;

#ifdef _WIN32
    if (device != Device::Cpu) {
        try {
            impl_->device = std::make_unique<GpuDevice>(gpu_index);
            impl_->gpu = gpu_index;
            impl_->get(Net::Denoiser);  // DirectML works if this loads
        } catch (const std::exception& e) {
            impl_->gpu = -1;
            impl_->sessions[(int)Net::Denoiser].reset();
            impl_->device.reset();
            if (device == Device::Gpu) throw std::runtime_error(std::string("DirectML (GPU) failed: ") + e.what());
            std::printf("Warning: the GPU could not be used (%s); using the CPU.\n", e.what());
            std::fflush(stdout);
        }
    }
#else
    if (device == Device::Gpu) throw std::runtime_error("the GPU (DirectML) is only available on Windows");
#endif
    if (impl_->gpu < 0) impl_->get(Net::Denoiser);  // fail early on a damaged file

    if (impl_->gpu >= 0) {
        gpu_index_ = gpu_index;
        engine_.backend = "DirectML";
#ifdef _WIN32
        std::string name;
        double gb = 0;
        if (gpu_description(gpu_index, name, gb)) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), ", %.1f GB", gb);
            engine_.detail = name + (gb > 0.05 ? buf : "");
        }
#endif
    } else {
        engine_.backend = "CPU";
        const unsigned n = threads > 0 ? (unsigned)threads : std::thread::hardware_concurrency();
        engine_.detail = std::to_string(n) + " threads";
    }
}

void Models::load(std::initializer_list<Net> nets) {
    for (Net n : nets) impl_->get(n);
}

void Models::release(std::initializer_list<Net> nets) {
    for (Net n : nets) impl_->sessions[(int)n].reset();
}

Models::~Models() = default;

void Models::set_pace(double rest) { impl_->pace = std::max(0.0, rest); }

bool Models::gpu_memory(double& used_gb, double& budget_gb) const {
#ifdef _WIN32
    if (gpu_index_ < 0) return false;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return false;
    bool ok = false;
    IDXGIAdapter1* adapter = nullptr;
    if (SUCCEEDED(factory->EnumAdapters1((UINT)gpu_index_, &adapter))) {
        IDXGIAdapter3* a3 = nullptr;
        if (SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&a3)))) {
            DXGI_QUERY_VIDEO_MEMORY_INFO info{};
            if (SUCCEEDED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
                used_gb = (double)info.CurrentUsage / (1024.0 * 1024.0 * 1024.0);
                budget_gb = (double)info.Budget / (1024.0 * 1024.0 * 1024.0);
                ok = true;
            }
            a3->Release();
        }
        adapter->Release();
    }
    factory->Release();
    return ok;
#else
    (void)used_gb;
    (void)budget_gb;
    return false;
#endif
}

void Models::denoise(Mat& mag, Mat& cos, Mat& sin) {
    const int64_t F = mag.rows, T = mag.cols;
    const char* in_names[] = {"mag", "cos", "sin"};
    const char* out_names[] = {"sep_mag", "sep_cos", "sep_sin"};
    Ort::Value in[] = {tensor(impl_->cpu, mag.v.data(), mag.v.size(), {1, F, T}),
                       tensor(impl_->cpu, cos.v.data(), cos.v.size(), {1, F, T}),
                       tensor(impl_->cpu, sin.v.data(), sin.v.size(), {1, F, T})};
    auto out = impl_->run(Net::Denoiser, (int)T, [&](Ort::Session& s) {
        return s.Run(Ort::RunOptions{nullptr}, in_names, in, 3, out_names, 3);
    });
    mag = to_mat(out[0]);
    cos = to_mat(out[1]);
    sin = to_mat(out[2]);
}

Mat Models::encode(const Mat& mel) {
    const char* in_names[] = {"mel"};
    const char* out_names[] = {"latent"};
    Ort::Value in[] = {tensor(impl_->cpu, mel.v.data(), mel.v.size(), {1, mel.rows, mel.cols})};
    auto out = impl_->run(Net::Encoder, (int)mel.cols, [&](Ort::Session& s) {
        return s.Run(Ort::RunOptions{nullptr}, in_names, in, 1, out_names, 1);
    });
    return to_mat(out[0]);
}

Mat Models::velocity(const Mat& psi, const Mat& cond, float t) {
    const char* in_names[] = {"psi", "cond", "t"};
    const char* out_names[] = {"velocity"};
    Ort::Value in[] = {tensor(impl_->cpu, psi.v.data(), psi.v.size(), {1, psi.rows, psi.cols}),
                       tensor(impl_->cpu, cond.v.data(), cond.v.size(), {1, cond.rows, cond.cols}),
                       tensor(impl_->cpu, &t, 1, {1})};
    auto out = impl_->run(Net::Velocity, psi.cols, [&](Ort::Session& s) {
        return s.Run(Ort::RunOptions{nullptr}, in_names, in, 3, out_names, 1);
    });
    return to_mat(out[0]);
}

Mat Models::decode(const Mat& latent) {
    const char* in_names[] = {"latent"};
    const char* out_names[] = {"features"};
    Ort::Value in[] = {tensor(impl_->cpu, latent.v.data(), latent.v.size(), {1, latent.rows, latent.cols})};
    auto out = impl_->run(Net::Decoder, latent.cols, [&](Ort::Session& s) {
        return s.Run(Ort::RunOptions{nullptr}, in_names, in, 1, out_names, 1);
    });
    return to_mat(out[0]);
}

std::vector<float> Models::vocode(const Mat& features, const Mat& noise) {
    const char* in_names[] = {"features", "noise"};
    const char* out_names[] = {"wav"};
    Ort::Value in[] = {tensor(impl_->cpu, features.v.data(), features.v.size(), {1, features.rows, features.cols}),
                       tensor(impl_->cpu, noise.v.data(), noise.v.size(), {1, noise.rows, noise.cols})};
    auto out = impl_->run(Net::Vocoder, features.cols, [&](Ort::Session& s) {
        return s.Run(Ort::RunOptions{nullptr}, in_names, in, 2, out_names, 1);
    });
    const auto shape = out[0].GetTensorTypeAndShapeInfo().GetShape();  // (1, samples)
    const float* p = out[0].GetTensorData<float>();
    return std::vector<float>(p, p + shape.back());
}

}  // namespace enh
