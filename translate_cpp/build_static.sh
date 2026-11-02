#!/usr/bin/env bash
# =============================================================================
#  Linux build with llama.cpp + ggml compiled INTO translate_text, plus the Qt6
#  GUI. Result: the dist/ folder.
#
#  GPU backends are detected automatically:
#    CUDA   - when nvcc is found (PATH or /usr/local/cuda). The CUDA runtime and
#             cuBLAS are linked statically; the NVIDIA driver is used if present.
#    Vulkan - when the Vulkan headers and glslc are found. Needs libvulkan.so.1
#             on the machine that runs the program (package libvulkan1).
#    CPU    - always.
#
#  Usage:   ./build_static.sh
#
#  Optional environment variables:
#    CUDA=auto|on|off   VULKAN=auto|on|off   (default auto)
#    CUDA_ARCHS="120-real"        CUDA architectures to compile (default below)
#    QT_DIR=/opt/Qt/6.11.0/gcc_64 Qt to use if not the system one
#    BUILD_GUI=0                  skip the GUI
#    JOBS=8                       parallel compile jobs (default: all cores)
#
#  Packages (Debian/Ubuntu):
#    build-essential cmake ninja-build git curl qt6-base-dev
#    Vulkan: libvulkan-dev glslc        CUDA: NVIDIA CUDA Toolkit 12.8+
#  curl is also needed at run time, to download the models.
# =============================================================================
set -euo pipefail
ROOT="$(dirname "$(readlink -f "$0")")"
cd "$ROOT"

die() { echo "[ERROR] $*" >&2; exit 1; }
on()  { [[ "$1" == on || "$1" == ON || "$1" == 1 ]]; }

LLAMA_COMMIT=bd4eeaa047006cb1fe71999fbd11134b5836e167
MINIZ_COMMIT=2948eebafc5b9f1c69bebed80333e403a09bd62d
CUDA_ARCHS="${CUDA_ARCHS:-75-virtual;86-real;89-real;120-real}"
JOBS="${JOBS:-$(nproc)}"
CUDA="${CUDA:-auto}"
VULKAN="${VULKAN:-auto}"
BUILD_GUI="${BUILD_GUI:-1}"

for tool in cmake git c++; do command -v "$tool" >/dev/null || die "$tool not found (see the package list at the top)"; done
command -v curl >/dev/null || echo "[WARN] curl not found: the program needs it to download models (sudo apt install curl)"
GEN="Unix Makefiles"; command -v ninja >/dev/null && GEN="Ninja"

# ---- detect GPU toolchains ---------------------------------------------------
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
echo "[INFO] Backends: CPU, CUDA=$CUDA_FLAG, Vulkan=$VULKAN_FLAG"

# ---- 1. sources ----------------------------------------------------------------
fetch() {  # name url commit
    local dir="third_party/$1"
    if [[ -d "$dir/.git" ]]; then
        # Already cloned: make sure it is at the pinned commit (it changes when
        # LLAMA_COMMIT / MINIZ_COMMIT above are updated).
        [[ "$(git -C "$dir" rev-parse HEAD 2>/dev/null)" == "$3" ]] && return 0
        echo "[INFO] Updating $1 to $3 ..."
        local depth=()
        [[ "$(git -C "$dir" rev-parse --is-shallow-repository)" == true ]] && depth=(--depth 1)
        git -C "$dir" fetch -q ${depth[@]+"${depth[@]}"} origin "$3" && git -C "$dir" checkout -q FETCH_HEAD \
            || die "updating $1 failed (local changes? delete $dir and run again)"
        return 0
    fi
    echo "[INFO] Downloading $1 ..."
    mkdir -p "third_party/$1"
    ( cd "third_party/$1" && git init -q && git remote add origin "$2" && git fetch -q --depth 1 origin "$3" \
      && git checkout -q FETCH_HEAD ) || { rm -rf "third_party/$1"; die "downloading $1 failed"; }
}
fetch llama.cpp https://github.com/ggml-org/llama.cpp "$LLAMA_COMMIT"
fetch miniz https://github.com/richgel999/miniz "$MINIZ_COMMIT"

# ---- 2. static command-line program -------------------------------------------
# GGML_STATIC links the CUDA runtime and cuBLAS statically; glibc and the GPU
# driver libraries stay dynamic, as they must.
echo "[INFO] Configuring the static build ..."
cmake -S . -B build-static -G "$GEN" -DCMAKE_BUILD_TYPE=Release -DTRANSLATE_BUILD_GUI=OFF \
      -DBUILD_SHARED_LIBS=OFF -DGGML_NATIVE=OFF -DGGML_BACKEND_DL=OFF -DGGML_STATIC=ON \
      -DGGML_CUDA="$CUDA_FLAG" -DGGML_CUDA_NO_VMM=ON "-DCMAKE_CUDA_ARCHITECTURES=$CUDA_ARCHS" \
      -DGGML_VULKAN="$VULKAN_FLAG"
echo "[INFO] Compiling with $JOBS jobs (the CUDA kernels take a long time on the first build) ..."
cmake --build build-static --target translate_text translate_tests -j "$JOBS"
# (`cmd && echo` would not stop the script on failure: set -e ignores the left side of &&.)
./build-static/bin/translate_tests >/dev/null || die "unit tests failed (run ./build-static/bin/translate_tests)"
echo "[INFO] Unit tests passed"

# ---- 3. GUI --------------------------------------------------------------------
QT_ARGS=()
[[ -n "${QT_DIR:-}" ]] && QT_ARGS=(-DCMAKE_PREFIX_PATH="$QT_DIR")
if [[ "$BUILD_GUI" == 1 ]]; then
    echo "[INFO] Building the GUI ..."
    if cmake -S . -B build-gui -G "$GEN" -DCMAKE_BUILD_TYPE=Release -DTRANSLATE_BUILD_CLI=OFF \
             -DTRANSLATE_BUILD_TESTS=OFF "${QT_ARGS[@]}" && [[ -f build-gui/CMakeCache.txt ]] \
             && grep -q "Qt6_DIR:PATH=/" build-gui/CMakeCache.txt; then
        cmake --build build-gui -j "$JOBS"
    else
        echo "[WARN] Qt6 not found (sudo apt install qt6-base-dev, or set QT_DIR): skipping the GUI"
        BUILD_GUI=0
    fi
fi

# ---- 4. dist/ ------------------------------------------------------------------
rm -rf dist && mkdir -p dist/licenses
cp build-static/bin/translate_text dist/
[[ "$BUILD_GUI" == 1 ]] && cp build-gui/bin/translate_gui dist/
cp third_party/llama.cpp/LICENSE dist/licenses/llama.cpp-LICENSE.txt
cp third_party/miniz/LICENSE dist/licenses/miniz-LICENSE.txt
cp installer/license.txt dist/licenses/Translate-LICENSES.txt
strip dist/translate_text 2>/dev/null || true

echo
echo "[SUCCESS] $ROOT/dist"
ls -lh dist | sed 1d
echo
echo "Shared libraries translate_text still needs (all system/driver libraries):"
ldd dist/translate_text | awk '{print "  " $1}' | grep -v -e linux-vdso -e ld-linux
