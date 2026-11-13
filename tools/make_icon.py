#!/usr/bin/env python3
"""Draws the OpenBGI application icon (original artwork) and writes macOS, iOS, and Android assets.

Concept: An open luminous script-book / tome with a faceted amber-coral Play prism rising from its pages
over a deep cobalt-teal gradient backdrop.

Outputs (relative to repository root):
  packaging/icons/svg/*.svg                         Master SVGs (light, macOS, dark, tinted, Android layers)
  packaging/icons/OpenBGI.{png,icns}                macOS app icon + 512px PNG
  packaging/ios/Assets.xcassets/AppIcon.appiconset  iOS 1024px icons (light / dark / tinted)
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
N = 1024

BG = [("0", "#091533"), ("0.52", "#123b68"), ("1", "#1d708c")]


def squircle(x0, y0, size, r):
    x1, y1 = x0 + size, y0 + size
    a, b, c, d, e, f = (
        1.52866483 * r,
        1.08849299 * r,
        0.86840701 * r,
        0.63149399 * r,
        0.37282401 * r,
        0.16905201 * r,
    )
    g, h = 0.07491100 * r, 0.02044300 * r

    def corner(px, py, sx, sy, horizontal_first):
        pts = [(a, 0), (b, 0), (c, h), (d, g), (e, f), (f, e), (g, d), (h, c), (0, b), (0, a)]
        if not horizontal_first:
            pts = [(v, u) for u, v in pts]
        return [(px + sx * u, py + sy * v) for u, v in pts]

    segs = []
    for px, py, sx, sy, hf in ((x1, y0, -1, 1, True), (x1, y1, -1, -1, False), (x0, y1, 1, -1, True), (x0, y0, 1, 1, False)):
        q = corner(px, py, sx, sy, hf)
        segs.append((q[0], [q[1:4], q[4:7], q[7:10]]))
    out = f"M{segs[-1][1][-1][-1][0]:.2f},{segs[-1][1][-1][-1][1]:.2f}"
    for start, curves in segs:
        out += f" L{start[0]:.2f},{start[1]:.2f}"
        for c1, c2, end in curves:
            out += f" C{c1[0]:.2f},{c1[1]:.2f} {c2[0]:.2f},{c2[1]:.2f} {end[0]:.2f},{end[1]:.2f}"
    return out + " Z"


def stops(spec):
    return "".join(f'<stop offset="{o}" stop-color="{c}"/>' for o, c in spec)


def defs(ids):
    return f"""
    <linearGradient id="{ids}bg" x1="0.1" y1="0" x2="0.85" y2="1">{stops(BG)}</linearGradient>
    <radialGradient id="{ids}halo" cx="0.5" cy="0.46" r="0.48">
      <stop offset="0" stop-color="#4de2e2" stop-opacity="0.34"/>
      <stop offset="0.55" stop-color="#2da6d4" stop-opacity="0.14"/>
      <stop offset="1" stop-color="#1d708c" stop-opacity="0"/>
    </radialGradient>
    <linearGradient id="{ids}cover" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#2b649c"/>
      <stop offset="1" stop-color="#142d56"/>
    </linearGradient>
    <linearGradient id="{ids}pageL" x1="0" y1="0" x2="1" y2="0.8">
      <stop offset="0" stop-color="#ffffff"/>
      <stop offset="1" stop-color="#d8e9ff"/>
    </linearGradient>
    <linearGradient id="{ids}pageR" x1="1" y1="0" x2="0" y2="0.8">
      <stop offset="0" stop-color="#f4f9ff"/>
      <stop offset="1" stop-color="#c6dfff"/>
    </linearGradient>
    <linearGradient id="{ids}prismTop" x1="0" y1="0" x2="1" y2="0.8">
      <stop offset="0" stop-color="#ffe58f"/>
      <stop offset="1" stop-color="#ff8f6b"/>
    </linearGradient>
    <linearGradient id="{ids}prismBot" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#ff9966"/>
      <stop offset="1" stop-color="#e84a7a"/>
    </linearGradient>
    <linearGradient id="{ids}ribbon" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#ff8570"/>
      <stop offset="1" stop-color="#e04372"/>
    </linearGradient>
    <filter id="{ids}dropLg" x="-30%" y="-30%" width="160%" height="170%">
      <feGaussianBlur in="SourceAlpha" stdDeviation="20"/>
      <feOffset dy="20" result="b"/>
      <feFlood flood-color="#050c20" flood-opacity="0.52"/>
      <feComposite in2="b" operator="in"/>
      <feMerge><feMergeNode/><feMergeNode in="SourceGraphic"/></feMerge>
    </filter>
    <filter id="{ids}glowPrism" x="-40%" y="-40%" width="180%" height="180%">
      <feGaussianBlur in="SourceAlpha" stdDeviation="16"/>
      <feOffset dy="12" result="b"/>
      <feFlood flood-color="#06122b" flood-opacity="0.45"/>
      <feComposite in2="b" operator="in"/>
      <feMerge><feMergeNode/><feMergeNode in="SourceGraphic"/></feMerge>
    </filter>"""


# Open book / script-tome + faceted Play prism centered at (512, 512)
BOOK_COVER = (
    "M-310,-62 C-310,-86 -292,-104 -268,-102 L-22,-78 C-8,-76 0,-70 0,-60 "
    "C0,-70 8,-76 22,-78 L268,-102 C292,-104 310,-86 310,-62 "
    "V196 C310,220 292,238 268,236 L24,212 C8,210 0,218 0,228 "
    "C0,218 -8,210 -24,212 L-268,236 C-292,238 -310,220 -310,196 Z"
)
PAGE_LEFT = (
    "M-274,-96 C-274,-116 -258,-132 -238,-130 L-18,-100 C-4,-98 0,-86 0,-72 "
    "V184 C0,174 -8,166 -22,164 L-244,186 C-262,188 -274,174 -274,156 Z"
)
PAGE_RIGHT = (
    "M274,-96 C274,-116 258,-132 238,-130 L18,-100 C4,-98 0,-86 0,-72 "
    "V184 C0,174 8,166 22,164 L244,186 C262,188 274,174 274,156 Z"
)
RIBBON = "M46,168 L104,174 V276 L75,250 L46,276 Z"

# Faceted rounded Play prism floating above the center spine
PRISM_OUTER = "M-94,-248 C-94,-274 -66,-290 -44,-276 L132,-164 C154,-150 154,-118 132,-104 L-44,8 C-66,22 -94,6 -94,-20 Z"
PRISM_TOP = "M-94,-248 C-94,-274 -66,-290 -44,-276 L132,-164 C143,-157 148,-146 148,-134 L-94,-134 Z"
PRISM_BOT = "M-94,-134 L148,-134 C148,-122 143,-111 132,-104 L-44,8 C-66,22 -94,6 -94,-20 Z"


def artwork(ids, scale):
    return f"""
  <g transform="translate(512,530) scale({scale})">
    <g filter="url(#{ids}dropLg)">
      <path d="{BOOK_COVER}" fill="url(#{ids}cover)"/>
      <path d="{RIBBON}" fill="url(#{ids}ribbon)"/>
      <path d="{PAGE_LEFT}" fill="url(#{ids}pageL)"/>
      <path d="{PAGE_RIGHT}" fill="url(#{ids}pageR)"/>
      <path d="M0,-72 V184" stroke="#8cb6e8" stroke-width="4" stroke-linecap="round" opacity="0.55"/>
      <g stroke="#80a8d8" stroke-width="11" stroke-linecap="round" opacity="0.45">
        <line x1="-224" y1="-52" x2="-46" y2="-30"/>
        <line x1="-224" y1="-8" x2="-46" y2="14"/>
        <line x1="-224" y1="36" x2="-78" y2="54"/>
        <line x1="-224" y1="80" x2="-46" y2="102"/>
        <line x1="-224" y1="124" x2="-104" y2="138"/>
        <line x1="46" y1="-30" x2="224" y2="-52"/>
        <line x1="46" y1="14" x2="224" y2="-8"/>
        <line x1="78" y1="54" x2="224" y2="36"/>
        <line x1="46" y1="102" x2="224" y2="80"/>
      </g>
    </g>
    <g filter="url(#{ids}glowPrism)">
      <clipPath id="{ids}pclip"><path d="{PRISM_OUTER}"/></clipPath>
      <g clip-path="url(#{ids}pclip)">
        <path d="{PRISM_TOP}" fill="url(#{ids}prismTop)"/>
        <path d="{PRISM_BOT}" fill="url(#{ids}prismBot)"/>
        <path d="M-94,-260 L148,-134 L-94,-134 Z" fill="#ffffff" opacity="0.16"/>
      </g>
      <path d="{PRISM_OUTER}" fill="none" stroke="#fff6df" stroke-width="3" opacity="0.55"/>
    </g>
    <path d="M214,-248 L224,-222 L250,-212 L224,-202 L214,-176 L204,-202 L178,-212 L204,-222 Z" fill="#9ef7ff" opacity="0.9"/>
  </g>"""


def background(ids):
    return f'<rect width="{N}" height="{N}" fill="url(#{ids}bg)"/><rect width="{N}" height="{N}" fill="url(#{ids}halo)"/>'


def svg(body, size=N):
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" viewBox="0 0 {N} {N}">{body}</svg>\n'


def icon_square():
    return svg(f"<defs>{defs('a')}</defs>{background('a')}{artwork('a', 1.0)}")


def icon_dark():
    return svg(f"<defs>{defs('a')}</defs>{artwork('a', 1.0)}")


def mono_artwork(scale, color="#fff"):
    return f"""
  <g transform="translate(512,530) scale({scale})">
    <path d="{PAGE_LEFT}" fill="{color}" opacity="0.92"/>
    <path d="{PAGE_RIGHT}" fill="{color}" opacity="0.78"/>
    <path d="{RIBBON}" fill="{color}" opacity="0.65"/>
    <path d="{PRISM_OUTER}" fill="{color}"/>
  </g>"""


def icon_tinted():
    return svg(mono_artwork(1.0))


def icon_macos():
    k = 824 / N
    return svg(
        f"""<defs>{defs('a')}
    <clipPath id="body"><path d="{squircle(100, 100, 824, 185)}"/></clipPath>
    <filter id="drop" x="-20%" y="-20%" width="140%" height="150%">
      <feGaussianBlur in="SourceAlpha" stdDeviation="14"/>
      <feOffset dy="12" result="b"/>
      <feFlood flood-color="#000" flood-opacity="0.32"/>
      <feComposite in2="b" operator="in"/>
      <feMerge><feMergeNode/><feMergeNode in="SourceGraphic"/></feMerge>
    </filter>
    <linearGradient id="rim" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#fff" stop-opacity="0.28"/>
      <stop offset="0.5" stop-color="#fff" stop-opacity="0"/>
    </linearGradient>
  </defs>
  <g filter="url(#drop)"><path d="{squircle(100, 100, 824, 185)}" fill="#000"/></g>
  <g clip-path="url(#body)">
    <g transform="translate(512,512) scale({k}) translate(-512,-512)">{background('a')}{artwork('a', 1.0)}</g>
  </g>
  <path d="{squircle(101, 101, 822, 184)}" fill="none" stroke="url(#rim)" stroke-width="2"/>"""
    )


ANDROID_SCALE = 0.72


def android_background():
    return svg(f"<defs>{defs('a')}</defs>{background('a')}")


def android_foreground():
    return svg(f"<defs>{defs('a')}</defs>{artwork('a', ANDROID_SCALE)}")


def android_monochrome():
    return svg(mono_artwork(ANDROID_SCALE, "#000"))


def render(svg_path, png_path, size, opaque=False):
    args = ["rsvg-convert", "-w", str(size), "-h", str(size), "-o", png_path]
    if opaque:
        args[1:1] = ["-b", BG[0][1]]
    subprocess.run(args + [svg_path], check=True)


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def main():
    if not shutil.which("rsvg-convert"):
        sys.exit("make_icon.py: rsvg-convert not found (brew install librsvg)")

    svgdir = os.path.join(ROOT, "packaging/icons/svg")
    masters = {
        "OpenBGI.svg": icon_square(),
        "OpenBGI-macOS.svg": icon_macos(),
        "OpenBGI-dark.svg": icon_dark(),
        "OpenBGI-tinted.svg": icon_tinted(),
        "android-background.svg": android_background(),
        "android-foreground.svg": android_foreground(),
        "android-monochrome.svg": android_monochrome(),
    }
    for name, text in masters.items():
        write(os.path.join(svgdir, name), text)

    S = lambda n: os.path.join(svgdir, n)
    icons = os.path.join(ROOT, "packaging/icons")
    render(S("OpenBGI-macOS.svg"), os.path.join(icons, "OpenBGI.png"), 512)

    with tempfile.TemporaryDirectory() as tmp:
        iconset = os.path.join(tmp, "OpenBGI.iconset")
        os.makedirs(iconset)
        for pt in (16, 32, 128, 256, 512):
            render(S("OpenBGI-macOS.svg"), os.path.join(iconset, f"icon_{pt}x{pt}.png"), pt)
            render(S("OpenBGI-macOS.svg"), os.path.join(iconset, f"icon_{pt}x{pt}@2x.png"), pt * 2)
        if shutil.which("iconutil"):
            subprocess.run(["iconutil", "-c", "icns", iconset, "-o", os.path.join(icons, "OpenBGI.icns")], check=True)
        else:
            print("make_icon.py: iconutil not found, OpenBGI.icns skipped")

    ios_appicon = os.path.join(ROOT, "packaging/ios/Assets.xcassets/AppIcon.appiconset")
    os.makedirs(ios_appicon, exist_ok=True)
    render(S("OpenBGI.svg"), os.path.join(ios_appicon, "AppIcon-1024.png"), 1024, opaque=True)
    render(S("OpenBGI-dark.svg"), os.path.join(ios_appicon, "AppIcon-1024-dark.png"), 1024)
    render(S("OpenBGI-tinted.svg"), os.path.join(ios_appicon, "AppIcon-1024-tinted.png"), 1024)

    contents = {
        "images": [
            {"filename": "AppIcon-1024.png", "idiom": "universal", "platform": "ios", "size": "1024x1024"},
            {
                "appearances": [{"appearance": "luminosity", "value": "dark"}],
                "filename": "AppIcon-1024-dark.png",
                "idiom": "universal",
                "platform": "ios",
                "size": "1024x1024",
            },
            {
                "appearances": [{"appearance": "luminosity", "value": "tinted"}],
                "filename": "AppIcon-1024-tinted.png",
                "idiom": "universal",
                "platform": "ios",
                "size": "1024x1024",
            },
        ],
        "info": {"author": "xcode", "version": 1},
    }
    write(os.path.join(ios_appicon, "Contents.json"), json.dumps(contents, indent=2) + "\n")
    write(
        os.path.join(ROOT, "packaging/ios/Assets.xcassets/Contents.json"),
        json.dumps({"info": {"author": "xcode", "version": 1}}, indent=2) + "\n",
    )

    # Android adaptive icon mipmaps (108dp * density)
    for density, px in (("mdpi", 108), ("hdpi", 162), ("xhdpi", 216), ("xxhdpi", 324), ("xxxhdpi", 432)):
        mdir = os.path.join(ROOT, f"android/launcher/res/mipmap-{density}")
        os.makedirs(mdir, exist_ok=True)
        render(S("android-background.svg"), os.path.join(mdir, "ic_launcher_background.png"), px, opaque=True)
        render(S("android-foreground.svg"), os.path.join(mdir, "ic_launcher_foreground.png"), px)
        render(S("android-monochrome.svg"), os.path.join(mdir, "ic_launcher_monochrome.png"), px)

    print("Generated redesigned OpenBGI icons successfully.")


if __name__ == "__main__":
    main()
