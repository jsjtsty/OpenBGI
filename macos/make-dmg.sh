#!/usr/bin/env bash
# Package OpenBGI.app into a styled macOS DMG with custom Retina background,
# volume icon, and Finder icon layout.
#
# Usage:
#   macos/make-dmg.sh [APP_PATH] [OUT_DMG] [VERSION]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${3:-$(sed -n 's/^project(OpenBGI VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")}"
APP_PATH="${1:-$ROOT/build/launcher-macos/OpenBGI.app}"
OUT_DMG="${2:-$ROOT/build/launcher-macos/OpenBGI-$VERSION-macOS-launcher.dmg}"
VOL_NAME="OpenBGI"
ICON_ICNS="$ROOT/packaging/icons/OpenBGI.icns"

[[ -d "$APP_PATH" ]] || { echo "missing app bundle: $APP_PATH" >&2; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

STAGE="$WORK/stage"
RW_DMG="$WORK/rw.dmg"
mkdir -p "$STAGE/.background"

cp -R "$APP_PATH" "$STAGE/OpenBGI.app"
ln -s /Applications "$STAGE/Applications"

if [[ -f "$ICON_ICNS" ]]; then
    cp "$ICON_ICNS" "$STAGE/.VolumeIcon.icns"
fi

# Generate a Retina-ready (660x420 @1x + 1320x840 @2x) Finder background.
BG_SVG="$WORK/bg.svg"
cat > "$BG_SVG" <<EOF
<svg xmlns="http://www.w3.org/2000/svg" width="660" height="420" viewBox="0 0 660 420">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0%" stop-color="#0B162C"/>
      <stop offset="55%" stop-color="#12345C"/>
      <stop offset="100%" stop-color="#1A5E7A"/>
    </linearGradient>
    <radialGradient id="glowL" cx="50%" cy="50%" r="50%">
      <stop offset="0%" stop-color="#4CC9F0" stop-opacity="0.18"/>
      <stop offset="100%" stop-color="#4CC9F0" stop-opacity="0"/>
    </radialGradient>
    <radialGradient id="glowR" cx="50%" cy="50%" r="50%">
      <stop offset="0%" stop-color="#F7B267" stop-opacity="0.16"/>
      <stop offset="100%" stop-color="#F7B267" stop-opacity="0"/>
    </radialGradient>
    <linearGradient id="arrowGrad" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0%" stop-color="#7DD3FC" stop-opacity="0.7"/>
      <stop offset="100%" stop-color="#FDE68A" stop-opacity="0.95"/>
    </linearGradient>
  </defs>

  <rect width="660" height="420" fill="url(#bg)"/>
  <circle cx="180" cy="195" r="125" fill="url(#glowL)"/>
  <circle cx="480" cy="195" r="125" fill="url(#glowR)"/>

  <!-- Subtle glass card framing the drop area -->
  <rect x="44" y="74" width="572" height="264" rx="22"
        fill="#FFFFFF" fill-opacity="0.05"
        stroke="#FFFFFF" stroke-opacity="0.14" stroke-width="1"/>

  <!-- Header -->
  <text x="330" y="46" text-anchor="middle"
        font-family="-apple-system, BlinkMacSystemFont, SF Pro Display, Helvetica Neue, sans-serif"
        font-size="18" font-weight="600" fill="#F8FAFC" letter-spacing="0.4">
    OpenBGI  <tspan fill="#93C5FD" font-weight="400" font-size="14">v${VERSION}</tspan>
  </text>

  <!-- Arrow from OpenBGI.app to Applications -->
  <g transform="translate(0, 195)">
    <line x1="278" y1="0" x2="372" y2="0" stroke="url(#arrowGrad)" stroke-width="3.5" stroke-linecap="round"/>
    <polyline points="360,-10 374,0 360,10" fill="none" stroke="#FDE68A" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round"/>
  </g>

  <!-- Footer hint -->
  <text x="330" y="378" text-anchor="middle"
        font-family="-apple-system, BlinkMacSystemFont, SF Pro Text, Helvetica Neue, sans-serif"
        font-size="13" font-weight="400" fill="#CBD5E1">
    Drag OpenBGI to Applications to install
  </text>
</svg>
EOF

BG_1X="$WORK/bg_1x.png"
BG_2X="$WORK/bg_2x.png"
BG_OUT="$STAGE/.background/background.png"

if command -v rsvg-convert >/dev/null 2>&1; then
    rsvg-convert -w 660 -h 420 "$BG_SVG" -o "$BG_1X"
    rsvg-convert -w 1320 -h 840 "$BG_SVG" -o "$BG_2X"
    if command -v tiffutil >/dev/null 2>&1; then
        BG_OUT="$STAGE/.background/background.tiff"
        tiffutil -cathidpicheck "$BG_1X" "$BG_2X" -out "$BG_OUT"
    else
        cp "$BG_1X" "$BG_OUT"
    fi
elif command -v qlmanage >/dev/null 2>&1; then
    qlmanage -t -s 660 -o "$WORK" "$BG_SVG" >/dev/null 2>&1 || true
    [[ -f "$WORK/bg.svg.png" ]] && cp "$WORK/bg.svg.png" "$BG_OUT"
fi

BG_BASENAME="$(basename "$BG_OUT")"

# Size the writable image with ~32 MB headroom for HFS+ metadata.
APP_KB="$(du -sk "$STAGE" | awk '{print $1}')"
DMG_MB="$(( (APP_KB / 1024) + 32 ))"

hdiutil create -quiet -srcfolder "$STAGE" -volname "$VOL_NAME" \
    -fs HFS+ -fsargs "-c c=64,a=16,e=16" -format UDRW -size "${DMG_MB}m" "$RW_DMG"

ATTACH_OUT="$(hdiutil attach -readwrite -noverify -noautoopen "$RW_DMG")"
MOUNT_DIR="$(printf '%s\n' "$ATTACH_OUT" | sed -n 's|.*\(/Volumes/.*\)$|\1|p' | tail -n 1)"

if [[ -n "$MOUNT_DIR" && -d "$MOUNT_DIR" ]]; then
    if command -v SetFile >/dev/null 2>&1 && [[ -f "$MOUNT_DIR/.VolumeIcon.icns" ]]; then
        SetFile -a C "$MOUNT_DIR" || true
    fi

    if command -v osascript >/dev/null 2>&1 && [[ -f "$MOUNT_DIR/.background/$BG_BASENAME" ]]; then
        osascript <<EOF || echo "warning: Finder DMG layout skipped (headless/non-GUI session)" >&2
tell application "Finder"
    tell disk "$VOL_NAME"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {200, 140, 860, 560}
        set viewOptions to the icon view options of container window
        set arrangement of viewOptions to not arranged
        set icon size of viewOptions to 112
        set text size of viewOptions to 13
        set background picture of viewOptions to file ".background:$BG_BASENAME"
        set position of item "OpenBGI.app" of container window to {180, 195}
        set position of item "Applications" of container window to {480, 195}
        close
        open
        update without registering applications
        delay 2
    end tell
end tell
EOF
    fi

    sync
    hdiutil detach -quiet "$MOUNT_DIR" || hdiutil detach -force -quiet "$MOUNT_DIR"
fi

mkdir -p "$(dirname "$OUT_DMG")"
rm -f "$OUT_DMG"
hdiutil convert -quiet "$RW_DMG" -format UDZO -imagekey zlib-level=9 -o "$OUT_DMG"
echo "DMG: $OUT_DMG"
