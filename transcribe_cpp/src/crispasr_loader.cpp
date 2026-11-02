// Access to the CrispASR functions: direct (static build) or run-time loaded.
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "crispasr_api.h"
#include "pathutil.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace tmx {

#ifdef TMX_STATIC_CRISPASR

const CrispApi& crispasr() {
    static const CrispApi api = [] {
        CrispApi a;
#define TMX_BIND(ret, name, args) a.name = &::name;
        CRISPASR_FUNCTIONS(TMX_BIND)
#undef TMX_BIND
        return a;
    }();
    return api;
}

const CrispOptionalApi& crispasr_optional() {
    static const CrispOptionalApi api = [] {
        CrispOptionalApi a;
#ifdef TMX_HAVE_ALIGN_FREE
        a.crispasr_align_free_cache_abi = &::crispasr_align_free_cache_abi;
#endif
        a.crispasr_diarize_segments_turns_abi = &::crispasr_diarize_segments_turns_abi;
        return a;
    }();
    return api;
}

const GgmlApi* ggml_api() {
    static const GgmlApi api = [] {
        GgmlApi a;
#define TMX_BIND(ret, name, args) a.name = &::name;
        GGML_FUNCTIONS(TMX_BIND)
#undef TMX_BIND
        return a;
    }();
    return &api;
}

#else  // dynamic

namespace {

#ifdef _WIN32
using LibHandle = HMODULE;
const char* kLibName = "crispasr.dll";

fs::path exe_dir() {
    std::wstring buf(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
    buf.resize(n);
    return fs::path(buf).parent_path();
}

LibHandle open_lib(const fs::path& p) {
    // LOAD_WITH_ALTERED_SEARCH_PATH: resolve ggml*.dll / CUDA DLLs from the library's own folder.
    return LoadLibraryExW(p.wstring().c_str(), nullptr, p.is_absolute() ? LOAD_WITH_ALTERED_SEARCH_PATH : 0);
}

void* find_sym(LibHandle h, const char* name) { return reinterpret_cast<void*>(GetProcAddress(h, name)); }

std::string last_error() {
    const DWORD e = GetLastError();
    char* msg = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, e, 0, reinterpret_cast<LPSTR>(&msg), 0, nullptr);
    std::string s = msg ? msg : ("error " + std::to_string(e));
    if (msg) LocalFree(msg);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}
#else
using LibHandle = void*;
const char* kLibName = "libcrispasr.so";

fs::path exe_dir() {
    std::error_code ec;
    const fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::current_path() : p.parent_path();
}

LibHandle open_lib(const fs::path& p) {
    // libcrispasr.so's own RUNPATH points at the folder it was built in, so once the
    // libraries are copied elsewhere its ggml dependencies would not be found. Load
    // them from the same folder first; the dynamic linker then reuses them (matched
    // by SONAME) when it resolves libcrispasr.so's dependencies.
    if (p.has_parent_path()) {
        for (const char* dep : {"libggml-base.so", "libggml-cpu.so", "libggml.so"}) {
            const fs::path d = p.parent_path() / dep;
            std::error_code ec;
            if (fs::exists(d, ec)) dlopen(d.c_str(), RTLD_NOW | RTLD_GLOBAL);
        }
    }
    return dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL);
}
void* find_sym(LibHandle h, const char* name) { return dlsym(h, name); }
std::string last_error() {
    const char* e = dlerror();
    return e ? e : "unknown error";
}
#endif

CrispApi g_api;
CrispOptionalApi g_optional;
std::string g_error;
std::once_flag g_once;
fs::path g_lib_dir;  // folder crispasr was loaded from (empty = system search)

void load() {
    std::vector<fs::path> candidates = {exe_dir() / kLibName};
    if (const char* d = std::getenv("CRISPASR_DIR")) candidates.push_back(path_from_utf8(d) / kLibName);
    candidates.push_back(kLibName);  // PATH / system search

    LibHandle h = nullptr;
    std::string tried;
    for (const auto& c : candidates) {
        h = open_lib(c);
        if (h) {
            if (c.has_parent_path()) g_lib_dir = c.parent_path();
            break;
        }
        tried += "\n  " + path_to_utf8(c) + ": " + last_error();
    }
    if (!h) {
        g_error = std::string("could not load ") + kLibName +
                  ". Copy the CrispASR DLLs next to this program (the build does this) or set CRISPASR_DIR." + tried;
        return;
    }

    std::string missing;
#define TMX_RESOLVE(ret, name, args)                                                  \
    g_api.name = reinterpret_cast<ret(*) args>(find_sym(h, #name));                   \
    if (!g_api.name) missing += std::string(missing.empty() ? "" : ", ") + #name;
    CRISPASR_FUNCTIONS(TMX_RESOLVE)
#undef TMX_RESOLVE
    g_optional.crispasr_align_free_cache_abi =
        reinterpret_cast<void (*)(void)>(find_sym(h, "crispasr_align_free_cache_abi"));
    g_optional.crispasr_diarize_segments_turns_abi =
        reinterpret_cast<crispasr_diarize_segments_turns_fn>(find_sym(h, "crispasr_diarize_segments_turns_abi"));
    if (!missing.empty())
        g_error = std::string(kLibName) + " is too old or not CrispASR (missing: " + missing + ")";
}

}  // namespace

const CrispApi& crispasr() {
    std::call_once(g_once, load);
    if (!g_error.empty()) throw std::runtime_error(g_error);
    return g_api;
}

const CrispOptionalApi& crispasr_optional() {
    try {
        crispasr();
    } catch (const std::exception&) {
    }
    return g_optional;
}

const GgmlApi* ggml_api() {
    static GgmlApi api;
    static bool ok = false;
    static std::once_flag once;
    std::call_once(once, [] {
        try {
            crispasr();  // ggml is loaded as a dependency of crispasr; reuse that copy
        } catch (const std::exception&) {
            return;
        }
#ifdef _WIN32
        const char* libs[] = {"ggml.dll", "ggml-base.dll"};
#else
        const char* libs[] = {"libggml.so", "libggml-base.so"};
#endif
        std::vector<LibHandle> handles;
        for (const char* name : libs) {
            LibHandle h = open_lib(g_lib_dir.empty() ? fs::path(name) : g_lib_dir / name);
            if (h) handles.push_back(h);
        }
        ok = true;
#define TMX_RESOLVE(ret, name, args)                                              \
    for (LibHandle h : handles)                                                   \
        if (!api.name) api.name = reinterpret_cast<ret(*) args>(find_sym(h, #name)); \
    ok = ok && api.name != nullptr;
        GGML_FUNCTIONS(TMX_RESOLVE)
#undef TMX_RESOLVE
    });
    return ok ? &api : nullptr;
}

#endif  // TMX_STATIC_CRISPASR

}  // namespace tmx
