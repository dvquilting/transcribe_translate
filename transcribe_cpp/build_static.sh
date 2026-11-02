#!/usr/bin/env bash
# =============================================================================
#  Linux build with CrispASR + ggml compiled INTO transcribe_multi, plus the Qt6
#  GUI. Result: the dist/ folder.
#
#  GPU backends are detected automatically:
#    CUDA   - when nvcc is found (PATH or /usr/local/cuda). The CUDA runtime and
#             cuBLAS are linked statically; the NVIDIA driver is used if present.
#    Vulkan - when the Vulkan headers and glslc are found. Needs libvulkan.so.1
#             on the machine that runs the program (it comes with GPU drivers;
#             package libvulkan1).
#    CPU    - always.
#
#  Usage:   ./build_static.sh
#
#  Optional environment variables:
#    CUDA=auto|on|off   VULKAN=auto|on|off   (default auto)
#    CUDA_ARCHS="120-real"        CUDA architectures to compile (default below)
#    CRISPASR_SRC=/path/CrispASR  existing checkout (default: third_party/CrispASR,
#                                 cloned automatically)
#    QT_DIR=/opt/Qt/6.11.0/gcc_64 Qt to use if not the system one
#    BUILD_GUI=0                  skip the GUI
#    JOBS=8                       parallel compile jobs (default: all cores)
#
#  Packages (Debian/Ubuntu):
#    build-essential cmake ninja-build git ffmpeg qt6-base-dev
#    Vulkan: libvulkan-dev glslc        CUDA: NVIDIA CUDA Toolkit 12.8+
# =============================================================================
set -euo pipefail
ROOT="$(dirname "$(readlink -f "$0")")"
cd "$ROOT"

die() { echo "[ERROR] $*" >&2; exit 1; }
on()  { [[ "$1" == on || "$1" == ON || "$1" == 1 ]]; }

# CrispASR version this program is tested with. cmake/patch_crispasr.cmake and
# src/crispasr_api.h match it; update all three together.
CRISPASR_COMMIT=199de52d9068aeba366dd95d09a96bb45b2a7a14
CRISPASR_URL=https://github.com/CrispStrobe/CrispASR
CRISPASR_OWN=1; [[ -n "${CRISPASR_SRC:-}" ]] && CRISPASR_OWN=0
CRISPASR_SRC="${CRISPASR_SRC:-$ROOT/third_party/CrispASR}"
# 75-virtual = PTX for RTX 20xx and newer; native code for RTX 30xx/40xx/50xx.
# Fewer entries = much faster build (e.g. CUDA_ARCHS=120-real for one RTX 50 card).
CUDA_ARCHS="${CUDA_ARCHS:-75-virtual;86-real;89-real;120-real}"
JOBS="${JOBS:-$(nproc)}"
CUDA="${CUDA:-auto}"
VULKAN="${VULKAN:-auto}"
BUILD_GUI="${BUILD_GUI:-1}"

for tool in cmake git c++; do command -v "$tool" >/dev/null || die "$tool not found (see the package list at the top)"; done
command -v ffmpeg >/dev/null || echo "[WARN] ffmpeg not found: video and M4A input won't work (sudo apt install ffmpeg)"
GEN="Unix Makefiles"; command -v ninja >/dev/null && GEN="Ninja"

# ---- detect GPU toolchains ---------------------------------------------------
if [[ "$CUDA" == auto ]]; then
    if ! command -v nvcc >/dev/null && [[ -x /usr/local/cuda/bin/nvcc ]]; then
        export PATH="/usr/local/cuda/bin:$PATH"
    fi
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

# ---- 1. CrispASR source --------------------------------------------------------
# third_party/CrispASR is kept at CRISPASR_COMMIT. (Its files carry our source
# patches, so switching commits discards local changes: they are re-applied at
# configure time.) A checkout given with CRISPASR_SRC is left alone; a different
# commit there only gets a warning.
crispasr_submodules() {
    git -C "$CRISPASR_SRC" submodule update -q --init --recursive --depth 1 --force 2>/dev/null \
        || git -C "$CRISPASR_SRC" submodule update -q --init --recursive --force
}
if [[ ! -d "$CRISPASR_SRC/.git" ]]; then
    [[ $CRISPASR_OWN == 1 ]] || die "CRISPASR_SRC=$CRISPASR_SRC is not a git checkout of CrispASR"
    echo "[INFO] Downloading CrispASR ${CRISPASR_COMMIT:0:12} into $CRISPASR_SRC ..."
    mkdir -p "$CRISPASR_SRC"
    ( cd "$CRISPASR_SRC" && git init -q && git remote add origin "$CRISPASR_URL" \
      && git fetch -q --depth 1 origin "$CRISPASR_COMMIT" && git checkout -q FETCH_HEAD ) \
        && crispasr_submodules || { rm -rf "$CRISPASR_SRC"; die "downloading CrispASR failed"; }
elif [[ "$(git -C "$CRISPASR_SRC" rev-parse HEAD 2>/dev/null)" != "$CRISPASR_COMMIT" ]]; then
    if [[ $CRISPASR_OWN == 1 ]]; then
        echo "[INFO] Switching CrispASR to the tested version ${CRISPASR_COMMIT:0:12} ..."
        depth=(); [[ "$(git -C "$CRISPASR_SRC" rev-parse --is-shallow-repository)" == true ]] && depth=(--depth 1)
        git -C "$CRISPASR_SRC" fetch -q ${depth[@]+"${depth[@]}"} origin "$CRISPASR_COMMIT" \
            && git -C "$CRISPASR_SRC" checkout -q -f FETCH_HEAD && crispasr_submodules \
            || die "updating CrispASR failed (delete $CRISPASR_SRC and run again)"
    else
        echo "[WARN] $CRISPASR_SRC is not at the tested CrispASR ${CRISPASR_COMMIT:0:12}; the build may fail."
    fi
fi

# ---- 2. Static command-line program ------------------------------------------
# GGML_STATIC links the CUDA runtime and cuBLAS statically. Its -static link flag
# only applies inside ggml's own directory, so transcribe_multi itself still links
# glibc and the GPU driver libraries dynamically, as it must.
echo "[INFO] Configuring the static build ..."
cmake -S "$CRISPASR_SRC" -B build-static -G "$GEN" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PROJECT_crispasr_INCLUDE="$ROOT/cmake/crispasr_overlay.cmake" \
      -DBUILD_SHARED_LIBS=OFF -DCRISPASR_BUILD_TESTS=OFF -DCRISPASR_BUILD_EXAMPLES=OFF -DCRISPASR_BUILD_SERVER=OFF \
      -DGGML_NATIVE=OFF -DGGML_BACKEND_DL=OFF -DGGML_STATIC=ON \
      -DGGML_CUDA="$CUDA_FLAG" -DGGML_CUDA_NO_VMM=ON "-DCMAKE_CUDA_ARCHITECTURES=$CUDA_ARCHS" \
      -DGGML_VULKAN="$VULKAN_FLAG"

echo "[INFO] Compiling with $JOBS jobs (the CUDA kernels take a long time on the first build) ..."
cmake --build build-static --target transcribe_multi -j "$JOBS"

# ---- 3. GUI --------------------------------------------------------------------
QT_ARGS=()
[[ -n "${QT_DIR:-}" ]] && QT_ARGS=(-DCMAKE_PREFIX_PATH="$QT_DIR")
if [[ "$BUILD_GUI" == 1 ]]; then
    echo "[INFO] Building the GUI ..."
    if cmake -S . -B build-gui -G "$GEN" -DCMAKE_BUILD_TYPE=Release -DTRANSCRIBE_BUILD_CLI=OFF "${QT_ARGS[@]}"; then
        cmake --build build-gui -j "$JOBS"
    else
        echo "[WARN] Qt6 not found (sudo apt install qt6-base-dev, or set QT_DIR): skipping the GUI"
        BUILD_GUI=0
    fi
fi

# ---- 4. Unit tests (helpers only, no model needed) -----------------------------
cmake -S . -B build-tests -G "$GEN" -DCMAKE_BUILD_TYPE=Release -DTRANSCRIBE_BUILD_GUI=OFF >/dev/null
cmake --build build-tests --target transcribe_tests -j "$JOBS" >/dev/null
# (`cmd && echo` would hide a failure: set -e ignores the left side of &&)
./build-tests/bin/transcribe_tests >/dev/null || die "unit tests failed (run ./build-tests/bin/transcribe_tests)"
echo "[INFO] Unit tests passed"

# ---- 5. dist/ ------------------------------------------------------------------
rm -rf dist && mkdir -p dist/licenses
cp build-static/bin/transcribe_multi dist/
[[ "$BUILD_GUI" == 1 ]] && cp build-gui/bin/transcribe_multi_gui dist/
cp "$CRISPASR_SRC/LICENSE" dist/licenses/CrispASR-LICENSE.txt
cp "$CRISPASR_SRC/THIRD_PARTY_NOTICES.txt" dist/licenses/CrispASR-THIRD_PARTY_NOTICES.txt 2>/dev/null || true
strip dist/transcribe_multi 2>/dev/null || true

echo
echo "[SUCCESS] $ROOT/dist"
ls -lh dist | sed 1d
echo
echo "Shared libraries transcribe_multi still needs (all system/driver libraries):"
ldd dist/transcribe_multi | awk '{print "  " $1}' | grep -v -e linux-vdso -e ld-linux
