#!/usr/bin/env bash
# =============================================================================
# Linux: build Transcribe & Translate into dist/:
#   transcribe_translate   the window (Qt 6)
#   tt_engine              the engine: CrispASR + ggml linked in
#   libtt_translate.so     translation: llama.cpp with its own ggml
#
#   ./build_all.sh         build everything (only what changed is recompiled)
#   ./build_all.sh gui     only the window and the tests (engine kept)
#
# Packages (Debian/Ubuntu):
#   sudo apt install build-essential cmake ninja-build git pkg-config ffmpeg qt6-base-dev
#   optional, for AMD/Intel/NVIDIA graphics cards: libvulkan-dev glslc
#   optional, for NVIDIA: the CUDA Toolkit (nvcc)
#
# Environment: CUDA=auto|on|off, VULKAN=auto|on|off, CUDA_ARCHS, QT_DIR (a Qt
# kit not found by CMake), JOBS (parallel compile jobs).
# =============================================================================
set -euo pipefail
ROOT="$(dirname "$(readlink -f "$0")")"
cd "$ROOT"

die() { echo "[ERROR] $*" >&2; exit 1; }
on()  { [[ "$1" == on || "$1" == ON || "$1" == 1 ]]; }

# Tested versions. cmake/patch_crispasr.cmake and src/engine/crispasr_api.h match
# this CrispASR; update them together.
CRISPASR_COMMIT=199de52d9068aeba366dd95d09a96bb45b2a7a14
LLAMA_COMMIT=bd4eeaa047006cb1fe71999fbd11134b5836e167
MINIZ_COMMIT=2948eebafc5b9f1c69bebed80333e403a09bd62d
# PDFium, for reading PDFs: a prebuilt release of
# https://github.com/bblanchon/pdfium-binaries (chromium/NNNN).
PDFIUM_BUILD=8138
CUDA_ARCHS="${CUDA_ARCHS:-75-virtual;86-real;89-real;120-real}"
JOBS="${JOBS:-$(nproc)}"
CUDA="${CUDA:-auto}"
VULKAN="${VULKAN:-auto}"
ONLY_GUI=0; [[ "${1:-}" == gui ]] && ONLY_GUI=1

for tool in cmake git c++; do command -v "$tool" >/dev/null || die "$tool not found (see the package list at the top)"; done
command -v ffmpeg >/dev/null || echo "[WARN] ffmpeg not found: video and M4A input and dubbing won't work (sudo apt install ffmpeg)"
GEN="Unix Makefiles"; command -v ninja >/dev/null && GEN="Ninja"

if [[ "$CUDA" == auto ]]; then
    if ! command -v nvcc >/dev/null && [[ -x /usr/local/cuda/bin/nvcc ]]; then export PATH="/usr/local/cuda/bin:$PATH"; fi
    command -v nvcc >/dev/null && CUDA=on || CUDA=off
elif on "$CUDA" && ! command -v nvcc >/dev/null; then
    [[ -x /usr/local/cuda/bin/nvcc ]] && export PATH="/usr/local/cuda/bin:$PATH" \
        || die "CUDA=on but nvcc was not found. Install the CUDA Toolkit or add it to PATH."
fi
if [[ "$VULKAN" == auto ]]; then
    if command -v glslc >/dev/null && { pkg-config --exists vulkan 2>/dev/null || [[ -f /usr/include/vulkan/vulkan.h ]] \
         || [[ -n "${VULKAN_SDK:-}" ]]; }; then VULKAN=on; else VULKAN=off; fi
elif on "$VULKAN" && ! command -v glslc >/dev/null; then
    die "VULKAN=on but glslc was not found (sudo apt install libvulkan-dev glslc, or the LunarG SDK)."
fi
on "$CUDA" && CUDA_FLAG=ON || CUDA_FLAG=OFF
on "$VULKAN" && VULKAN_FLAG=ON || VULKAN_FLAG=OFF

# ---- 1. sources at the tested versions -----------------------------------------
fetch() {  # NAME URL COMMIT
    local dir="$ROOT/third_party/$1"
    if [[ ! -d "$dir/.git" ]]; then
        echo "[INFO] Downloading $1 ${3:0:12} ..."
        mkdir -p "$dir"
        ( cd "$dir" && git init -q && git remote add origin "$2" && git fetch -q --depth 1 origin "$3" \
          && git checkout -q FETCH_HEAD ) || { rm -rf "$dir"; die "downloading $1 failed"; }
    elif [[ "$(git -C "$dir" rev-parse HEAD)" != "$3" ]]; then
        echo "[INFO] Switching $1 to the tested version ${3:0:12} ..."
        local depth=(); [[ "$(git -C "$dir" rev-parse --is-shallow-repository)" == true ]] && depth=(--depth 1)
        # -f: CrispASR's files carry our source patches; they are re-applied at configure time.
        git -C "$dir" fetch -q ${depth[@]+"${depth[@]}"} origin "$3" && git -C "$dir" checkout -q -f FETCH_HEAD \
            || die "updating $1 failed (delete $dir and run again)"
    else
        return 0
    fi
    if [[ -f "$dir/.gitmodules" ]]; then
        git -C "$dir" submodule update -q --init --recursive --depth 1 --force 2>/dev/null \
            || git -C "$dir" submodule update -q --init --recursive --force || die "$1: submodules failed"
    fi
}
mkdir -p third_party
fetch CrispASR https://github.com/CrispStrobe/CrispASR "$CRISPASR_COMMIT"
fetch llama.cpp https://github.com/ggml-org/llama.cpp "$LLAMA_COMMIT"
fetch miniz https://github.com/richgel999/miniz "$MINIZ_COMMIT"
fetch_pdfium() {
    local dir="$ROOT/third_party/pdfium" arch
    case "$(uname -m)" in
        x86_64) arch=x64 ;;
        aarch64|arm64) arch=arm64 ;;
        *) echo "[WARN] no PDFium build for $(uname -m): the program will not read PDFs"; return 0 ;;
    esac
    grep -qx "BUILD=$PDFIUM_BUILD" "$dir/VERSION" 2>/dev/null && [[ -f "$dir/lib/libpdfium.so" ]] && return 0
    echo "[INFO] Downloading PDFium $PDFIUM_BUILD (for reading PDFs) ..."
    command -v curl >/dev/null || die "curl not found (sudo apt install curl)"
    rm -rf "$dir" && mkdir -p "$dir"
    curl -fsSL "https://github.com/bblanchon/pdfium-binaries/releases/download/chromium/$PDFIUM_BUILD/pdfium-linux-$arch.tgz" \
        | tar -xz -C "$dir" && [[ -f "$dir/lib/libpdfium.so" ]] || { rm -rf "$dir"; die "downloading PDFium failed"; }
}
fetch_pdfium

# ---- 2. the window and the tests -------------------------------------------------
echo "[INFO] ===== the window (Qt) and the tests ====="
QT_ARGS=(); [[ -n "${QT_DIR:-}" ]] && QT_ARGS=(-DCMAKE_PREFIX_PATH="$QT_DIR")
cmake -S . -B build-gui -G "$GEN" -DCMAKE_BUILD_TYPE=Release ${QT_ARGS[@]+"${QT_ARGS[@]}"}
cmake --build build-gui -j "$JOBS"
QT_QPA_PLATFORM=offscreen ctest --test-dir build-gui --output-on-failure || die "unit tests failed"
[[ -x build-gui/bin/transcribe_translate ]] || die "the window was not built (is Qt 6 installed? sudo apt install qt6-base-dev)"

if [[ $ONLY_GUI == 0 ]]; then
    echo "[INFO] Backends: CPU, CUDA=$CUDA_FLAG, Vulkan=$VULKAN_FLAG"
    COMMON=(-DCMAKE_BUILD_TYPE=Release -DGGML_NATIVE=OFF -DGGML_BACKEND_DL=OFF -DGGML_STATIC=ON
            -DGGML_CUDA="$CUDA_FLAG" -DGGML_CUDA_NO_VMM=ON "-DCMAKE_CUDA_ARCHITECTURES=$CUDA_ARCHS"
            -DGGML_VULKAN="$VULKAN_FLAG")
    # ---- 3. tt_engine: CrispASR's own build with our program injected ----------
    echo "[INFO] ===== tt_engine (CrispASR) ====="
    cmake -S third_party/CrispASR -B build-engine -G "$GEN" "${COMMON[@]}" \
          -DCMAKE_PROJECT_crispasr_INCLUDE="$ROOT/cmake/engine_overlay.cmake" \
          -DBUILD_SHARED_LIBS=OFF -DCRISPASR_BUILD_TESTS=OFF -DCRISPASR_BUILD_EXAMPLES=OFF -DCRISPASR_BUILD_SERVER=OFF
    echo "[INFO] Compiling with $JOBS jobs (the CUDA kernels take a long time on the first build) ..."
    cmake --build build-engine --target tt_engine -j "$JOBS"
    # ---- 4. libtt_translate.so: llama.cpp with its own ggml -----------------------
    echo "[INFO] ===== libtt_translate.so (llama.cpp) ====="
    cmake -S src/translate -B build-translate -G "$GEN" "${COMMON[@]}"
    cmake --build build-translate --target tt_translate -j "$JOBS"
fi

# ---- 5. dist/ ------------------------------------------------------------------------
[[ -x build-engine/bin/tt_engine ]] || die "build-engine is not built (run ./build_all.sh without \"gui\")"
[[ -f build-translate/bin/libtt_translate.so ]] || die "build-translate is not built (run ./build_all.sh without \"gui\")"
rm -rf dist && mkdir -p dist/licenses
cp build-gui/bin/transcribe_translate build-engine/bin/tt_engine build-translate/bin/libtt_translate.so dist/
cp installer/license.txt dist/licenses/TranscribeTranslate-LICENSES.txt
cp third_party/CrispASR/LICENSE dist/licenses/CrispASR-LICENSE.txt
[[ -f third_party/CrispASR/THIRD_PARTY_NOTICES.txt ]] && cp third_party/CrispASR/THIRD_PARTY_NOTICES.txt dist/licenses/CrispASR-THIRD_PARTY_NOTICES.txt
cp third_party/llama.cpp/LICENSE dist/licenses/llama.cpp-LICENSE.txt
cp third_party/miniz/LICENSE dist/licenses/miniz-LICENSE.txt
# PDFium: the window reads PDFs with it, from next to itself (it starts without it).
if [[ -f third_party/pdfium/lib/libpdfium.so ]]; then
    cp third_party/pdfium/lib/libpdfium.so dist/
    cp third_party/pdfium/LICENSE dist/licenses/pdfium-binaries-LICENSE.txt
    [[ -d third_party/pdfium/licenses ]] && cp -r third_party/pdfium/licenses dist/licenses/PDFium
else
    echo "[WARN] No PDFium: the program will not read PDFs"
fi
# The library's symbols stay its own (CrispASR in tt_engine has a ggml of the same names).
if command -v nm >/dev/null && nm -D --defined-only dist/libtt_translate.so | grep -qE ' T (ggml_|llama_)'; then
    die "libtt_translate.so exports ggml/llama symbols"
fi
dist/tt_engine translate --list-models >/dev/null || die "dist/tt_engine could not use libtt_translate.so"

echo "[SUCCESS] $ROOT/dist"
ls -lh dist | sed 1d
echo "Start it with: $ROOT/dist/transcribe_translate"
