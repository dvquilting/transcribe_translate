#!/usr/bin/env bash
# Linux: build transcribe_cpp, translate_cpp and Speech Translate into one dist/.
#   ./build_all.sh          build everything
#   ./build_all.sh quick    reuse ../transcribe_cpp/dist and ../translate_cpp/dist
# Environment variables of the other build scripts (CUDA, VULKAN, QT_DIR, JOBS) apply.
set -euo pipefail
ROOT="$(dirname "$(readlink -f "$0")")"
cd "$ROOT"
[[ "${1:-}" == quick ]] || { ../transcribe_cpp/build_static.sh; ../translate_cpp/build_static.sh; }
[[ -x ../transcribe_cpp/dist/transcribe_multi ]] || { echo "[ERROR] ../transcribe_cpp/dist is not built"; exit 1; }
[[ -x ../translate_cpp/dist/translate_text ]] || { echo "[ERROR] ../translate_cpp/dist is not built"; exit 1; }
QT_ARGS=(); [[ -n "${QT_DIR:-}" ]] && QT_ARGS=(-DCMAKE_PREFIX_PATH="$QT_DIR")
GEN="Unix Makefiles"; command -v ninja >/dev/null && GEN="Ninja"
cmake -S . -B build-gui -G "$GEN" -DCMAKE_BUILD_TYPE=Release "${QT_ARGS[@]}"
cmake --build build-gui -j "${JOBS:-$(nproc)}"
./build-gui/bin/speech_translate_tests >/dev/null || { echo "[ERROR] Unit tests failed"; exit 1; }
echo "[INFO] Unit tests passed"
rm -rf dist && mkdir dist
cp -r ../transcribe_cpp/dist/. dist/
cp -r ../translate_cpp/dist/. dist/
cp build-gui/bin/speech_translate_gui dist/
echo "[SUCCESS] $ROOT/dist"; ls -lh dist | sed 1d
