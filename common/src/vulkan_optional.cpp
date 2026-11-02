// vulkan_optional.cpp - run without the Vulkan runtime (Windows, MSVC builds).
//
// ggml's Vulkan backend imports vkGetInstanceProcAddr from vulkan-1.dll, which
// graphics drivers install. On a computer without it (an old Intel graphics
// driver, a stripped-down Windows, a virtual machine) Windows wouldn't even
// start the program ("vulkan-1.dll was not found"). So the DLL is delay-loaded
// (/DELAYLOAD:vulkan-1.dll, see transcribe_cpp/cmake/static_cli.cmake and
// translate_cpp/CMakeLists.txt)
// and, when it can't be found, this hook hands ggml a stand-in loader: it
// reports every Vulkan function as failing (VK_ERROR_INITIALIZATION_FAILED),
// ggml's Vulkan backend gives up the way it does on a computer without a
// Vulkan GPU, and the models run on the processor (or CUDA).
//
// ggml uses Vulkan-Hpp's dynamic dispatcher: vkGetInstanceProcAddr is the only
// function it imports; everything else comes through it. Linked into
// transcribe_multi.exe and translate_text.exe (MSVC builds with Vulkan only).
#if defined(_WIN32) && defined(_MSC_VER)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <delayimp.h>

#include <cstdint>

extern "C" IMAGE_DOS_HEADER __ImageBase;  // this .exe or .dll

namespace {

constexpr int32_t kVkErrorInitializationFailed = -3;

// Every global Vulkan function returns a VkResult (or nothing); on x64 there is
// one calling convention, and the arguments are simply not looked at.
int32_t vk_unavailable() { return kVkErrorInitializationFailed; }

// vkGetInstanceProcAddr: every function "exists" and fails. (A null pointer
// would not do: one of the ggml versions calls vkEnumerateInstanceVersion
// without checking it.)
void* vk_get_instance_proc_addr_unavailable(void* /*instance*/, const char* /*name*/) {
    return reinterpret_cast<void*>(&vk_unavailable);
}

bool is_vulkan_loader(const char* dll) { return dll && lstrcmpiA(dll, "vulkan-1.dll") == 0; }

FARPROC WINAPI delay_load_failed(unsigned reason, PDelayLoadInfo info) {
    if (!info || !is_vulkan_loader(info->szDll)) return nullptr;  // anything else (cuBLAS): as before
    if (reason == dliFailLoadLib) {
        // Any module will do: its lookups fail and come back here as dliFailGetProc.
        return reinterpret_cast<FARPROC>(reinterpret_cast<HMODULE>(&__ImageBase));
    }
    if (reason == dliFailGetProc) {
        const bool by_name = info->dlp.fImportByName && info->dlp.szProcName;
        if (by_name && lstrcmpA(info->dlp.szProcName, "vkGetInstanceProcAddr") == 0)
            return reinterpret_cast<FARPROC>(&vk_get_instance_proc_addr_unavailable);
        return reinterpret_cast<FARPROC>(&vk_unavailable);
    }
    return nullptr;
}

}  // namespace

// delayimp.h (Visual Studio 2015 and later) declares the hooks const.
#ifdef DELAYIMP_INSECURE_WRITABLE_HOOKS
extern "C" PfnDliHook __pfnDliFailureHook2 = delay_load_failed;
#else
extern "C" const PfnDliHook __pfnDliFailureHook2 = delay_load_failed;
#endif

#endif
