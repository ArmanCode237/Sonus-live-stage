#!/usr/bin/env python3
"""Application icons from the design team's logo (resources/logo).

  Windows  resources/SonusLiveStage.ico          16-256 px, full square (exe, window, taskbar, installer)
  macOS    resources/macos/SonusLiveStage.icns   16-1024 px, rounded tile on Apple's icon grid (Dock, Finder)
  Plug-in  resources/logo/sonus-icon-1024.png    rounded tile for JUCE (VST3 / AU bundle icon)

Source (first found): --png <file>, resources/logo/sonus-logo-2.png (logo without the
version, the app icon), sonus-logo.svg rendered with `sips` on macOS, the largest PNG.
Requires Pillow (pip install pillow).
Usage: python3 scripts/make_icons.py [--png path/to/logo.png]
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
LOGO = ROOT / "resources" / "logo"
ICO = ROOT / "resources" / "SonusLiveStage.ico"
ICNS = ROOT / "resources" / "macos" / "SonusLiveStage.icns"
PLUGIN_PNG = LOGO / "sonus-icon-1024.png"

CANVAS = 1024
TILE = 824            # Apple macOS icon grid: 824 px tile centred on a 1024 canvas
RADIUS = 185          # ~22.5% corner radius, as macOS app icons


def load_source(argv):
    if "--png" in argv:
        return Image.open(argv[argv.index("--png") + 1]).convert("RGBA")
    preferred = LOGO / "sonus-logo-2.png"
    if preferred.exists():
        return Image.open(preferred).convert("RGBA")
    svg = LOGO / "sonus-logo.svg"
    if svg.exists() and shutil.which("sips"):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "logo.png"
            subprocess.run(["sips", "-s", "format", "png", "-z", str(CANVAS), str(CANVAS), str(svg), "--out", str(out)],
                           check=True, stdout=subprocess.DEVNULL)
            return Image.open(out).convert("RGBA")
    pngs = sorted(LOGO.glob("*.png"), key=lambda p: Image.open(p).width, reverse=True)
    pngs = [p for p in pngs if p != PLUGIN_PNG]
    if not pngs:
        sys.exit("No hay logo en resources/logo (sonus-logo.svg o un .png)")
    return Image.open(pngs[0]).convert("RGBA")


def square(img, size):
    side = min(img.size)
    left, top = (img.width - side) // 2, (img.height - side) // 2
    return img.crop((left, top, left + side, top + side)).resize((size, size), Image.LANCZOS)


def rounded_tile(img):
    """macOS style: rounded tile with transparent margin (4x supersampled mask)."""
    tile = square(img, TILE)
    mask = Image.new("L", (TILE * 4, TILE * 4), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, TILE * 4 - 1, TILE * 4 - 1), RADIUS * 4, fill=255)
    tile.putalpha(mask.resize((TILE, TILE), Image.LANCZOS))
    canvas = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    offset = (CANVAS - TILE) // 2
    canvas.paste(tile, (offset, offset), tile)
    return canvas


def main():
    src = load_source(sys.argv)
    full = square(src, CANVAS)
    full.save(ICO, format="ICO", sizes=[(s, s) for s in (16, 20, 24, 32, 40, 48, 64, 128, 256)])
    mac = rounded_tile(src)
    if shutil.which("iconutil"):          # complete set (Pillow omits 16/32 px @1x)
        with tempfile.TemporaryDirectory() as tmp:
            iconset = Path(tmp) / "SonusLiveStage.iconset"
            iconset.mkdir()
            for s in (16, 32, 128, 256, 512):
                mac.resize((s, s), Image.LANCZOS).save(iconset / f"icon_{s}x{s}.png")
                mac.resize((s * 2, s * 2), Image.LANCZOS).save(iconset / f"icon_{s}x{s}@2x.png")
            subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(ICNS)], check=True)
    else:
        mac.save(ICNS, format="ICNS")
    mac.save(PLUGIN_PNG, format="PNG")
    for path in (ICO, ICNS, PLUGIN_PNG):
        print(f"OK: {path.relative_to(ROOT)} ({path.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
