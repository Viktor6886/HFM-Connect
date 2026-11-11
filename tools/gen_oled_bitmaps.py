#!/usr/bin/env python3
"""Generate firmware/HFMConnect/OledBitmaps.h: monochrome bitmaps of the OLED UI
(smileys, car logo, state icons) as XBM arrays for U8g2 drawXBMP().

    python tools/gen_oled_bitmaps.py            # rewrite the header
    python tools/gen_oled_bitmaps.py --png DIR  # also save enlarged PNG previews

The pictures are drawn here (no external image files), so they can be tuned in
one place. Requires Pillow.
"""
import pathlib
import sys

from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parents[1]
DST = ROOT / "firmware" / "HFMConnect" / "OledBitmaps.h"


def canvas(w, h):
    im = Image.new("1", (w, h), 0)
    return im, ImageDraw.Draw(im)


def face(mouth, eyes="open", tear=None, brows=False):
    """44x44 face: thick round outline, eyes, mouth ('smile' / 'frown')."""
    im, d = canvas(44, 44)
    d.ellipse([0, 0, 43, 43], outline=1, width=3)
    if eyes == "open":
        d.ellipse([12, 12, 17, 21], fill=1)
        d.ellipse([26, 12, 31, 21], fill=1)
    else:  # closed, smiling eyes
        d.arc([11, 14, 18, 22], 200, 340, fill=1, width=2)
        d.arc([25, 14, 32, 22], 200, 340, fill=1, width=2)
    if brows:
        d.line([(10, 10), (17, 7)], fill=1, width=2)
        d.line([(26, 7), (33, 10)], fill=1, width=2)
    if mouth == "smile":
        d.arc([9, 9, 34, 35], 25, 155, fill=1, width=3)
    else:
        d.arc([12, 28, 31, 44], 205, 335, fill=1, width=3)
    if tear is not None:
        d.polygon([(8, tear), (6, tear + 4), (10, tear + 4)], fill=1)
        d.ellipse([6, tear + 3, 10, tear + 7], fill=1)
    return im


def car():
    """64x24 saloon, side view."""
    im, d = canvas(64, 24)
    d.polygon([(15, 10), (23, 2), (42, 2), (51, 10)], fill=1)       # cabin
    d.rounded_rectangle([1, 9, 62, 18], radius=4, fill=1)           # body
    d.polygon([(19, 9), (25, 4), (31, 4), (31, 9)], fill=0)         # front window
    d.polygon([(34, 4), (41, 4), (47, 9), (34, 9)], fill=0)         # rear window
    for x in (10, 43):
        d.ellipse([x - 2, 10, x + 12, 24], fill=0)                  # wheel arch
        d.ellipse([x, 12, x + 10, 22], fill=1)                      # tyre
        d.ellipse([x + 3, 15, x + 7, 19], fill=0)                   # hub
    d.rectangle([58, 11, 61, 12], fill=0)                           # tail lamp
    d.rectangle([2, 11, 4, 12], fill=0)                             # head lamp
    return im


def icon_plug():
    """28x28 USB plug: adapter not connected."""
    im, d = canvas(28, 28)
    d.rectangle([8, 1, 19, 11], outline=1, width=2)
    d.rectangle([11, 4, 12, 7], fill=1)
    d.rectangle([15, 4, 16, 7], fill=1)
    d.rounded_rectangle([5, 11, 22, 21], radius=3, fill=1)
    d.line([(13, 21), (13, 27)], fill=1, width=3)
    return im


def icon_ecu():
    """28x28 control unit (chip with pins): waiting for the ECU."""
    im, d = canvas(28, 28)
    d.rounded_rectangle([6, 6, 21, 21], radius=2, outline=1, width=2)
    d.rectangle([11, 11, 16, 16], fill=1)
    for p in (9, 13, 17):
        d.line([(p, 1), (p, 5)], fill=1, width=2)
        d.line([(p, 22), (p, 26)], fill=1, width=2)
        d.line([(1, p), (5, p)], fill=1, width=2)
        d.line([(22, p), (26, p)], fill=1, width=2)
    return im


def icon_magnifier():
    """28x28 magnifier: diagnostics running."""
    im, d = canvas(28, 28)
    d.ellipse([2, 2, 19, 19], outline=1, width=3)
    d.line([(17, 17), (25, 25)], fill=1, width=4)
    return im


def icon_check():
    """28x28 tick in a circle: ECU on line."""
    im, d = canvas(28, 28)
    d.ellipse([0, 0, 27, 27], outline=1, width=2)
    d.line([(7, 14), (12, 19)], fill=1, width=3)
    d.line([(12, 19), (21, 8)], fill=1, width=3)
    return im


def icon_lost():
    """28x28 signal bars with a cross: link lost."""
    im, d = canvas(28, 28)
    for i, hgt in enumerate((7, 13, 19)):
        x = 1 + i * 7
        d.rectangle([x, 26 - hgt, x + 4, 26], fill=1)
    d.line([(18, 2), (26, 10)], fill=1, width=3)
    d.line([(26, 2), (18, 10)], fill=1, width=3)
    return im


def icon_warning():
    """28x28 warning triangle: readout failed."""
    im, d = canvas(28, 28)
    d.polygon([(14, 1), (27, 26), (0, 26)], outline=1, width=2)
    d.line([(14, 2), (26, 25)], fill=1, width=2)
    d.line([(14, 2), (1, 25)], fill=1, width=2)
    d.line([(1, 25), (26, 25)], fill=1, width=2)
    d.rectangle([13, 10, 14, 18], fill=1)
    d.rectangle([13, 21, 14, 22], fill=1)
    return im


BITMAPS = [
    ("face_happy", lambda: face("smile")),
    ("face_happy_blink", lambda: face("smile", eyes="closed")),
    ("face_sad", lambda: face("frown", tear=22, brows=True)),
    ("face_sad_tear", lambda: face("frown", tear=27, brows=True)),
    ("car", car),
    ("icon_plug", icon_plug),
    ("icon_ecu", icon_ecu),
    ("icon_magnifier", icon_magnifier),
    ("icon_check", icon_check),
    ("icon_lost", icon_lost),
    ("icon_warning", icon_warning),
]


def xbm(im):
    w, h = im.size
    px = im.load()
    out = []
    for y in range(h):
        for bx in range(0, w, 8):
            b = 0
            for i in range(8):
                if bx + i < w and px[bx + i, y]:
                    b |= 1 << i
            out.append(b)
    return out


def main() -> int:
    lines = [
        "// AUTO-GENERATED by tools/gen_oled_bitmaps.py. Do not edit.",
        "// Monochrome bitmaps of the OLED UI, XBM bit order (U8g2 drawXBMP).",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "#ifndef U8X8_PROGMEM",
        "#define U8X8_PROGMEM",
        "#endif",
        "",
        "struct OledBitmap {",
        "  uint8_t w, h;",
        "  const uint8_t* bits;",
        "};",
        "",
    ]
    png = None
    if "--png" in sys.argv:
        png = pathlib.Path(sys.argv[sys.argv.index("--png") + 1])
        png.mkdir(parents=True, exist_ok=True)
    for name, fn in BITMAPS:
        im = fn()
        data = xbm(im)
        w, h = im.size
        lines.append(f"static const uint8_t kOledBits_{name}[] U8X8_PROGMEM = {{")
        for i in range(0, len(data), 16):
            lines.append("  " + ",".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",")
        lines.append("};")
        lines.append(f"static const OledBitmap kOled_{name} = {{{w}, {h}, kOledBits_{name}}};")
        lines.append("")
        if png:
            im.convert("L").resize((w * 6, h * 6), Image.NEAREST).save(png / f"{name}.png")
    DST.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"wrote {DST.relative_to(ROOT)} ({len(BITMAPS)} bitmaps)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
