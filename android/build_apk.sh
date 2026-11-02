#!/usr/bin/env bash
# =============================================================================
#  Build the Android app (Transcribe & Translate): dist/TranscribeTranslate.apk
#
#  Needs: the Android SDK (ANDROID_HOME, e.g. from Android Studio), git, JDK 17+.
#  The SDK packages below (platform, build tools, NDK, CMake) are installed with
#  sdkmanager when missing. The sources it needs next to this folder:
#    ../transcribe_cpp/third_party/CrispASR  (fetched here at the pinned commit)
#    ../translate_cpp/third_party/miniz      (fetched here at the pinned commit)
#
#  Usage:  ./build_apk.sh            release APK (signed with the debug key, for sideloading)
#          COMPATIBLE_CPU=1 ./build_apk.sh   for phones without i8mm (before Snapdragon 8 Gen 1)
#          INSTALL=1 ./build_apk.sh  also install it on the phone connected with USB debugging
# =============================================================================
set -euo pipefail
ROOT="$(dirname "$(readlink -f "$0")")"
cd "$ROOT"
die() { echo "[ERROR] $*" >&2; exit 1; }

# Pinned versions: the same CrispASR as the desktop static build (keep in step with
# ../transcribe_cpp/build_static.sh), miniz as ../translate_cpp/build_static.sh.
CRISPASR_COMMIT=$(grep -m1 '^CRISPASR_COMMIT=' ../transcribe_cpp/build_static.sh | cut -d= -f2)
MINIZ_COMMIT=$(grep -m1 '^MINIZ_COMMIT=' ../translate_cpp/build_static.sh | cut -d= -f2)
[[ -n "$CRISPASR_COMMIT" && -n "$MINIZ_COMMIT" ]] || die "could not read the pinned commits from the build_static.sh scripts"
SDK_PACKAGES=("platforms;android-36" "build-tools;36.0.0" "ndk;28.2.13676358" "cmake;3.31.6" "platform-tools")

fetch() {  # dir url commit
    local dir="$1"
    if [[ -d "$dir/.git" ]]; then
        [[ "$(git -C "$dir" rev-parse HEAD 2>/dev/null)" == "$3" ]] && return 0
        echo "[INFO] Updating $dir to ${3:0:12} ..."
        git -C "$dir" fetch -q --depth 1 origin "$3" && git -C "$dir" checkout -q -f FETCH_HEAD \
            || die "updating $dir failed (delete it and run again)"
    else
        echo "[INFO] Downloading $(basename "$dir") ${3:0:12} ..."
        mkdir -p "$dir"
        ( cd "$dir" && git init -q && git remote add origin "$2" && git fetch -q --depth 1 origin "$3" \
          && git checkout -q FETCH_HEAD ) || { rm -rf "$dir"; die "downloading $2 failed"; }
    fi
    git -C "$dir" submodule update -q --init --recursive --depth 1 2>/dev/null || true
}
fetch ../transcribe_cpp/third_party/CrispASR https://github.com/CrispStrobe/CrispASR "$CRISPASR_COMMIT"
fetch ../translate_cpp/third_party/miniz https://github.com/richgel999/miniz "$MINIZ_COMMIT"

# ---- Android SDK ------------------------------------------------------------------
: "${ANDROID_HOME:=${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}"
export ANDROID_HOME
[[ -d "$ANDROID_HOME" ]] || die "no Android SDK at $ANDROID_HOME (install Android Studio, or set ANDROID_HOME)"
SDKMANAGER=$(ls "$ANDROID_HOME"/cmdline-tools/*/bin/sdkmanager 2>/dev/null | head -1 || true)
missing=()
for p in "${SDK_PACKAGES[@]}"; do [[ -d "$ANDROID_HOME/${p//;//}" ]] || missing+=("$p"); done
if ((${#missing[@]})); then
    [[ -n "$SDKMANAGER" ]] || die "missing SDK packages (${missing[*]}) and no sdkmanager to install them"
    echo "[INFO] Installing ${missing[*]} ..."
    yes | "$SDKMANAGER" --sdk_root="$ANDROID_HOME" --licenses >/dev/null || true
    "$SDKMANAGER" --sdk_root="$ANDROID_HOME" "${missing[@]}" >/dev/null
fi
echo "sdk.dir=$ANDROID_HOME" > local.properties

# ---- build --------------------------------------------------------------------------
ARGS=()
[[ "${COMPATIBLE_CPU:-0}" == 1 ]] && ARGS+=(-PcompatibleCpu=true)
echo "[INFO] Building (the first build compiles CrispASR and takes a while) ..."
./gradlew --no-daemon ${ARGS[@]+"${ARGS[@]}"} :app:assembleRelease
mkdir -p dist
cp app/build/outputs/apk/release/app-release.apk dist/TranscribeTranslate.apk
echo "[SUCCESS] $ROOT/dist/TranscribeTranslate.apk ($(du -h dist/TranscribeTranslate.apk | cut -f1))"

if [[ "${INSTALL:-0}" == 1 ]]; then
    "$ANDROID_HOME/platform-tools/adb" install -r dist/TranscribeTranslate.apk
fi
