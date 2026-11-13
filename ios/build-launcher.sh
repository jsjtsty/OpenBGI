#!/usr/bin/env bash
# OpenBGI iOS launcher (SwiftUI).
#
#   ios/build-launcher.sh [BUNDLE_ID]    -> build/launcher-ios/OpenBGI-<ver>-launcher-unsigned.ipa
#
# Needs: Xcode, cmake, xcodegen.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

export OPENBGI_BUNDLE_ID="${1:-${OPENBGI_BUNDLE_ID:-org.openbgi.OpenBGI}}"
VERSION="$(sed -n 's/^project(OpenBGI VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
B="$ROOT/build/ios-device"
APPLE="$ROOT/launcher/apple"
STAGE="$APPLE/Staging/ios"
OUT="$ROOT/build/launcher-ios"

# 1. Build libobgi_app and all static dependencies for iphoneos.
cmake --preset ios-device
xcodebuild -project "$B/OpenBGI.xcodeproj" -target obgi_app -target obgi_shaders -configuration Release -sdk iphoneos CODE_SIGNING_ALLOWED=NO build

rm -rf "$STAGE" && mkdir -p "$STAGE/shaders" "$STAGE/fonts"
LIBS=(
    "$B/src/Release-iphoneos/libobgi_app.a"
    "$B/src/Release-iphoneos/libobgi_runtime.a"
    "$B/src/Release-iphoneos/libobgi_bindings.a"
    "$B/src/Release-iphoneos/libobgi_services.a"
    "$B/src/Release-iphoneos/libobgi_scene.a"
    "$B/src/Release-iphoneos/libobgi_render.a"
    "$B/src/Release-iphoneos/libobgi_text.a"
    "$B/src/Release-iphoneos/libobgi_audio.a"
    "$B/src/Release-iphoneos/libobgi_video.a"
    "$B/src/Release-iphoneos/libobgi_programs.a"
    "$B/src/Release-iphoneos/libobgi_resources.a"
    "$B/src/Release-iphoneos/libobgi_vfs.a"
    "$B/src/Release-iphoneos/libobgi_vm.a"
    "$B/src/Release-iphoneos/libobgi_formats.a"
    "$B/_deps/sdl3-build/Release-iphoneos/libSDL3.a"
    "$B/ffmpeg-install/lib/libavformat.a"
    "$B/ffmpeg-install/lib/libavcodec.a"
    "$B/ffmpeg-install/lib/libswscale.a"
    "$B/ffmpeg-install/lib/libswresample.a"
    "$B/ffmpeg-install/lib/libavutil.a"
)
FREETYPE_LIB="$(find "$B/_deps/freetype-build/Release-iphoneos" -name 'libfreetype*.a' | head -n 1)"
[[ -n "$FREETYPE_LIB" ]] || { echo "missing libfreetype.a" >&2; exit 1; }
LIBS+=("$FREETYPE_LIB")

libtool -static -o "$STAGE/libobgi_all.a" "${LIBS[@]}"
cp "$B/src/shaders/obgi.pack" "$STAGE/shaders/"
if [[ -d "$ROOT/fonts" ]]; then
    cp -R "$ROOT/fonts/." "$STAGE/fonts/" 2>/dev/null || true
fi

# 2. Generate Xcode project and build unsigned iOS .app + .ipa.
( cd "$APPLE" && xcodegen generate )
xcodebuild -project "$APPLE/OpenBGI.xcodeproj" -scheme OpenBGI -configuration Release \
    -destination 'generic/platform=iOS' -derivedDataPath "$OUT/derived" \
    CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY="" ARCHS=arm64 build

APP="$OUT/derived/Build/Products/Release-iphoneos/OpenBGI.app"
[[ -d "$APP" ]] || { echo "missing $APP" >&2; exit 1; }
rm -rf "$OUT/ipa" && mkdir -p "$OUT/ipa/Payload"
cp -R "$APP" "$OUT/ipa/Payload/"
IPA="$OUT/OpenBGI-$VERSION-launcher-unsigned.ipa"
rm -f "$IPA"
( cd "$OUT/ipa" && zip -q -r -y "$IPA" Payload )
echo "App: $APP"
echo "IPA: $IPA   (bundle id $OPENBGI_BUNDLE_ID, unsigned: re-sign before installing)"
