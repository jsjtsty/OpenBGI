#!/usr/bin/env bash
# OpenBGI macOS launcher (SwiftUI).
#
#   macos/build-launcher.sh    -> build/launcher-macos/OpenBGI.app + .dmg
#
# Needs: Xcode, cmake, ninja, xcodegen.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

VERSION="$(sed -n 's/^project(OpenBGI VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
B="$ROOT/build/macos"
APPLE="$ROOT/launcher/apple"
STAGE="$APPLE/Staging/macos"
OUT="$ROOT/build/launcher-macos"
export OPENBGI_BUNDLE_ID="${OPENBGI_BUNDLE_ID:-org.openbgi.OpenBGI}"

# 1. Build obgi_run, obgi_probe, and obgi.pack (Release).
[[ -f "$B/build.ninja" ]] || cmake --preset macos
cmake --build "$B" --config Release --target obgi_run obgi_probe obgi_shaders

OBGI_RUN="$B/src/Release/obgi_run"
OBGI_PROBE="$B/src/Release/libobgi_probe.dylib"
SHADER_PACK="$B/src/shaders/obgi.pack"
for f in "$OBGI_RUN" "$OBGI_PROBE" "$SHADER_PACK"; do
    [[ -e "$f" ]] || { echo "missing build artifact: $f" >&2; exit 1; }
done

rm -rf "$STAGE" && mkdir -p "$STAGE"
cp "$OBGI_PROBE" "$STAGE/"

# 2. Build SwiftUI launcher.
( cd "$APPLE" && xcodegen generate )
ARCH="$(uname -m)"
xcodebuild -project "$APPLE/OpenBGI.xcodeproj" -scheme OpenBGI-macOS -configuration Release \
    -destination 'generic/platform=macOS' -derivedDataPath "$OUT/derived" \
    CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY="" ARCHS="$ARCH" build

# 3. Assemble and ad-hoc sign.
APP="$OUT/OpenBGI.app"
rm -rf "$APP" && cp -R "$OUT/derived/Build/Products/Release/OpenBGI.app" "$APP"
mkdir -p "$APP/Contents/Helpers" "$APP/Contents/Resources/shaders" "$APP/Contents/Frameworks"
cp "$OBGI_RUN" "$APP/Contents/Helpers/obgi_run"
cp "$SHADER_PACK" "$APP/Contents/Resources/shaders/"
cp "$OBGI_PROBE" "$APP/Contents/Frameworks/"

codesign --force --sign - "$APP/Contents/Helpers/obgi_run"
codesign --force --sign - "$APP/Contents/Frameworks/libobgi_probe.dylib"
codesign --force --sign - "$APP"
codesign --verify --deep --strict "$APP"

DMG="$OUT/OpenBGI-$VERSION-macOS-launcher.dmg"
rm -rf "$OUT/dmg" "$DMG" && mkdir -p "$OUT/dmg"
cp -R "$APP" "$OUT/dmg/" && ln -s /Applications "$OUT/dmg/Applications"
hdiutil create -quiet -volname "OpenBGI" -srcfolder "$OUT/dmg" -ov -format UDZO "$DMG"
echo "App: $APP"
echo "DMG: $DMG"
