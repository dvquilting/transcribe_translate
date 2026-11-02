#!/usr/bin/env bash
# =============================================================================
#  Quick Linux build: transcribe_multi loads libcrispasr.so at run time, plus
#  the Qt6 GUI. Output: build/bin/
#
#  Usage:   CRISPASR_DIR=/path/to/crispasr/libs ./build.sh
#
#  CRISPASR_DIR is the folder holding libcrispasr.so and the libggml*.so files:
#  the unpacked CrispASR Linux release, or a CrispASR build directory configured
#  with -DBUILD_SHARED_LIBS=ON. The libraries are copied into build/bin/.
#
#  Optional environment variables:
#    QT_DIR=/opt/Qt/6.11.0/gcc_64   Qt to use if not the system one
#    BUILD_GUI=0                    skip the GUI
#    JOBS=8                         parallel compile jobs (default: all cores)
#
#  Packages (Debian/Ubuntu): build-essential cmake ninja-build ffmpeg qt6-base-dev
# =============================================================================
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"

die() { echo "[ERROR] $*" >&2; exit 1; }

[[ -n "${CRISPASR_DIR:-}" ]] || die "set CRISPASR_DIR to the folder containing libcrispasr.so, e.g.
    CRISPASR_DIR=~/crispasr-linux-x86_64 ./build.sh"
[[ -d "$CRISPASR_DIR" ]] || die "CRISPASR_DIR=$CRISPASR_DIR does not exist"

command -v cmake >/dev/null || die "cmake not found (sudo apt install cmake)"
command -v c++ >/dev/null || die "no C++ compiler found (sudo apt install build-essential)"
command -v ffmpeg >/dev/null || echo "[WARN] ffmpeg not found: video and M4A input won't work (sudo apt install ffmpeg)"

GEN="Unix Makefiles"; command -v ninja >/dev/null && GEN="Ninja"
JOBS="${JOBS:-$(nproc)}"

QT_ARGS=()
[[ -n "${QT_DIR:-}" ]] && QT_ARGS=(-DCMAKE_PREFIX_PATH="$QT_DIR")
BUILD_GUI="${BUILD_GUI:-1}"
if [[ "$BUILD_GUI" == 1 && -z "${QT_DIR:-}" ]] && ! pkg-config --exists Qt6Widgets 2>/dev/null \
   && [[ ! -d /usr/lib/x86_64-linux-gnu/cmake/Qt6Widgets && ! -d /usr/lib64/cmake/Qt6Widgets ]]; then
    echo "[WARN] Qt6 Widgets not found (sudo apt install qt6-base-dev, or set QT_DIR): building without the GUI"
    BUILD_GUI=0
fi
[[ "$BUILD_GUI" == 1 ]] && GUI_FLAG=ON || GUI_FLAG=OFF

echo "[INFO] Configuring ..."
cmake -S . -B build -G "$GEN" -DCMAKE_BUILD_TYPE=Release \
      -DCRISPASR_DIR="$CRISPASR_DIR" -DTRANSCRIBE_BUILD_GUI="$GUI_FLAG" "${QT_ARGS[@]}"

echo "[INFO] Building ..."
cmake --build build -j "$JOBS"

echo "[INFO] Running unit tests ..."
ctest --test-dir build --output-on-failure

echo
echo "[SUCCESS] Programs are in $(pwd)/build/bin"
[[ "$GUI_FLAG" == ON ]] && echo "  GUI: build/bin/transcribe_multi_gui"
echo "  CLI: build/bin/transcribe_multi --help"
