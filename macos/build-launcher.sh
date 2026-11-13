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
HELPER_APP="$APP/Contents/Helpers/OpenBGI Player.app"
rm -rf "$APP" && cp -R "$OUT/derived/Build/Products/Release/OpenBGI.app" "$APP"
mkdir -p "$HELPER_APP/Contents/MacOS" "$HELPER_APP/Contents/Resources" "$APP/Contents/Resources/shaders" "$APP/Contents/Frameworks"
cp "$OBGI_RUN" "$HELPER_APP/Contents/MacOS/obgi_run"
cp "$ROOT/packaging/icons/OpenBGI.icns" "$HELPER_APP/Contents/Resources/OpenBGI.icns"
printf 'APPL????' > "$HELPER_APP/Contents/PkgInfo"
cat > "$HELPER_APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleDisplayName</key>
    <string>OpenBGI</string>
    <key>CFBundleExecutable</key>
    <string>obgi_run</string>
    <key>CFBundleIconFile</key>
    <string>OpenBGI</string>
    <key>CFBundleIdentifier</key>
    <string>${OPENBGI_BUNDLE_ID}.player</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>OpenBGI</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key>
    <true/>
</dict>
</plist>
EOF
cp "$SHADER_PACK" "$APP/Contents/Resources/shaders/"
cp "$OBGI_PROBE" "$APP/Contents/Frameworks/"

codesign --force --sign - "$HELPER_APP"
codesign --force --sign - "$APP/Contents/Frameworks/libobgi_probe.dylib"
codesign --force --sign - "$APP"
codesign --verify --deep --strict "$APP"

DMG="$OUT/OpenBGI-$VERSION-macOS-launcher.dmg"
rm -rf "$OUT/dmg" "$DMG" && mkdir -p "$OUT/dmg"
cp -R "$APP" "$OUT/dmg/" && ln -s /Applications "$OUT/dmg/Applications"
hdiutil create -quiet -volname "OpenBGI" -srcfolder "$OUT/dmg" -ov -format UDZO "$DMG"
echo "App: $APP"
echo "DMG: $DMG"
