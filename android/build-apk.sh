#!/usr/bin/env bash
# OpenBGI Android packaging pipeline: CMake/NDK (native libraries) -> staged assets -> Gradle (launcher, resources,
# R8, APK) -> apksigner.
#
#   android/build-apk.sh [--abi arm64-v8a] [--skip-native] [--release-key KEYSTORE]
#
# Defaults use the Android SDK/NDK/OpenJDK from Unity's AndroidPlayer on this machine.
# Override via OPENBGI_ANDROID_TOOLS, ANDROID_NDK_HOME, ANDROID_SDK_ROOT, JAVA_HOME, ANDROID_BUILD_TOOLS.
set -euo pipefail

ABI=arm64-v8a
SKIP_NATIVE=0
KEYSTORE=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --abi) ABI="$2"; shift 2 ;;
        --skip-native) SKIP_NATIVE=1; shift ;;
        --release-key) KEYSTORE="$2"; shift 2 ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac
done

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOOLS="${OPENBGI_ANDROID_TOOLS:-/Volumes/T7S-Projects/SDK/Unity/6000.3.25f1/PlaybackEngines/AndroidPlayer}"
export ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-$TOOLS/NDK}"
SDK="${ANDROID_SDK_ROOT:-$TOOLS/SDK}"
export JAVA_HOME="${JAVA_HOME:-$TOOLS/OpenJDK}"
BT_DIR="${ANDROID_BUILD_TOOLS:-$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)}"
MIN_SDK=26
VERSION="$(sed -n 's/^project(OpenBGI VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"

case "$ABI" in
    arm64-v8a) PRESET=android-arm64 ;;
    *) echo "ABI $ABI has no preset; add one to CMakePresets.json" >&2; exit 2 ;;
esac
BUILD="$ROOT/build/$PRESET"
WORK="$BUILD/apk"
NDK_BIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin"
export PATH="$JAVA_HOME/bin:$BT_DIR:$PATH"
export ANDROID_HOME="$SDK"

for t in "$BT_DIR/apksigner" "$JAVA_HOME/bin/java"; do
    [[ -e "$t" ]] || { echo "missing tool: $t" >&2; exit 1; }
done

# 1. Native libraries
if [[ $SKIP_NATIVE -eq 0 ]]; then
    [[ -f "$BUILD/build.ninja" ]] || cmake --preset "$PRESET"
    cmake --build --preset "$PRESET"
fi

rm -rf "$WORK"
mkdir -p "$WORK"/{lib/$ABI,assets}

# 2. Native payload (stripped)
for so in "$BUILD/src/libmain.so" "$BUILD/_deps/sdl3-build/libSDL3.so" \
          "$BUILD"/ffmpeg-install/lib/lib{avformat,avcodec,swresample,swscale,avutil}.so; do
    [[ -f "$so" ]] || { echo "missing $so" >&2; exit 1; }
    "$NDK_BIN/llvm-strip" --strip-unneeded -o "$WORK/lib/$ABI/$(basename "$so")" "$so"
done

# 3. Assets: SPIR-V shader pack + optional fonts
A="$WORK/assets"
mkdir -p "$A/shaders" "$A/fonts"
cp "$BUILD/src/shaders/obgi.pack" "$A/shaders/"
if [[ -d "$ROOT/fonts" ]]; then
    cp -R "$ROOT/fonts/." "$A/fonts/" 2>/dev/null || true
fi
( cd "$A" && find . -type f ! -name resources.lst ! -name resources.stamp | sed 's|^\./||' | sort > resources.lst )
( cd "$A" && xargs shasum < resources.lst | shasum | cut -d' ' -f1 > resources.stamp )

# 4. Gradle build (launcher + SDLActivity + R8 + unsigned APK)
SDL_JAVA="$BUILD/_deps/sdl3-src/android-project/app/src/main/java"
[[ -d "$SDL_JAVA" ]] || { echo "SDL java sources not found (configure first): $SDL_JAVA" >&2; exit 1; }
GRADLE_ARGS=()
if [[ "${HTTPS_PROXY:-${https_proxy:-}}" =~ ^https?://([^:/@]+):([0-9]+)/?$ ]]; then
    PROXY_HOST="${BASH_REMATCH[1]}"
    PROXY_PORT="${BASH_REMATCH[2]}"
    for scheme in http https; do
        GRADLE_ARGS+=("-D$scheme.proxyHost=$PROXY_HOST" "-D$scheme.proxyPort=$PROXY_PORT")
    done
    export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-} -Dhttp.proxyHost=$PROXY_HOST -Dhttp.proxyPort=$PROXY_PORT -Dhttps.proxyHost=$PROXY_HOST -Dhttps.proxyPort=$PROXY_PORT"
fi
"$ROOT/android/gradlew" -p "$ROOT/android" --quiet ${GRADLE_ARGS[@]+"${GRADLE_ARGS[@]}"} :launcher:assembleRelease \
    -Popenbgi.stage="$WORK" -Popenbgi.sdlJava="$SDL_JAVA"
UNSIGNED="$ROOT/android/launcher/build/outputs/apk/release/launcher-release-unsigned.apk"

# 5. Sign & verify
if [[ -z "$KEYSTORE" ]]; then
    KEYSTORE="$ROOT/build/openbgi-debug.keystore"
    if [[ ! -f "$KEYSTORE" ]]; then
        keytool -genkeypair -keystore "$KEYSTORE" -storepass android -keypass android -alias openbgi \
            -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=OpenBGI Debug" >/dev/null 2>&1
    fi
    SIGN_ARGS=(--ks "$KEYSTORE" --ks-pass pass:android --key-pass pass:android --ks-key-alias openbgi)
else
    SIGN_ARGS=(--ks "$KEYSTORE")
fi
OUT="$BUILD/OpenBGI-$VERSION-android-$ABI.apk"
"$BT_DIR/apksigner" sign "${SIGN_ARGS[@]}" --min-sdk-version $MIN_SDK --out "$OUT" "$UNSIGNED"
"$BT_DIR/apksigner" verify --min-sdk-version $MIN_SDK "$OUT"
echo "APK: $OUT ($(du -h "$OUT" | cut -f1))"
