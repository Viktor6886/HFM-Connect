#!/usr/bin/env python3
"""Render the OLED screens on the PC (no display needed).

    python tools/oled_preview.py [path/to/U8g2/src/clib]

Compiles the real model (OledModel.cpp) and renderer (OledRender.cpp) with the
U8g2 C sources and the three fonts the firmware uses, runs a fixed set of
states and writes build/oled_preview/screens.png (contact sheet, enlarged) plus
one PNG per frame. This checks the Russian texts, the smileys, the fault codes
and the layout; it does not check the display hardware or I2C.

Needs: the U8g2 library (default: Documents/Arduino/libraries/U8g2), Pillow, and a
C/C++ compiler (gcc/clang or `pip install ziglang`).
"""
import os
import pathlib
import re
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parents[1]
FW = ROOT / "firmware" / "HFMConnect"
OUT = ROOT / "build" / "oled_preview"
FONTS = ["u8g2_font_10x20_t_cyrillic", "u8g2_font_6x12_t_cyrillic", "u8g2_font_logisoso22_tn"]
# U8g2 C sources needed for a frame buffer without the device table (u8g2_d_setup.c).
CLIB = ["u8g2_setup.c", "u8g2_buffer.c", "u8g2_font.c", "u8g2_hvline.c", "u8g2_ll_hvline.c", "u8g2_box.c",
        "u8g2_circle.c", "u8g2_bitmap.c", "u8g2_intersection.c", "u8g2_line.c", "u8g2_cleardisplay.c",
        "u8x8_setup.c", "u8x8_display.c", "u8x8_cad.c", "u8x8_byte.c", "u8x8_gpio.c", "u8x8_8x8.c",
        "u8x8_d_ssd1306_128x64_noname.c", "u8x8_u8toa.c", "u8x8_u16toa.c", "u8x8_capture.c",
        "u8g2_kerning.c"]


def tool(cxx: bool):
    for c in (("g++", "clang++") if cxx else ("gcc", "clang")):
        if shutil.which(c):
            return [c]
    try:
        import ziglang  # noqa: F401
        return [sys.executable, "-m", "ziglang", "c++" if cxx else "cc"]
    except ImportError:
        sys.exit("no C/C++ compiler: install gcc/clang or `pip install ziglang`")


def extract_fonts(clib: pathlib.Path) -> str:
    """The three fonts as their own C file (u8g2_fonts.c is 36 MB)."""
    src = (clib / "u8g2_fonts.c").read_text(encoding="latin-1")
    out = ['#include "u8g2.h"', ""]
    for name in FONTS:
        m = re.search(r"const uint8_t " + name + r"\[\d+\]", src)
        if not m:
            sys.exit(f"font {name} not found in u8g2_fonts.c")
        end = src.index('";', m.start())  # the definition is one string literal ending with ";
        out.append(src[m.start():end + 2])
        out.append("")
    return "\n".join(out)


def main() -> int:
    default = pathlib.Path.home() / "Documents" / "Arduino" / "libraries" / "U8g2" / "src" / "clib"
    clib = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get("U8G2_CLIB", default))
    if not (clib / "u8g2.h").exists():
        sys.exit(f"U8g2 sources not found in {clib} (pass the path to .../U8g2/src/clib)")
    OUT.mkdir(parents=True, exist_ok=True)
    fonts_c = OUT / "fonts_subset.c"
    fonts_c.write_text(extract_fonts(clib), encoding="latin-1", newline="\n")
    c_srcs = [str(clib / f) for f in CLIB] + [str(fonts_c)]
    objs = []
    for f in c_srcs:  # one by one: some toolchains cannot emit several objects in one call
        obj = OUT / (pathlib.Path(f).stem + ".o")
        objs.append(str(obj))
        src_m = pathlib.Path(f).stat().st_mtime
        if obj.exists() and obj.stat().st_mtime >= src_m:
            continue
        r = subprocess.run(tool(False) + ["-O1", "-w", "-c", "-I", str(clib), "-o", str(obj), f])
        if r.returncode:
            return r.returncode
    exe = OUT / ("oled_preview.exe" if os.name == "nt" else "oled_preview")
    cpp = [str(ROOT / "tools" / "oled_preview" / "preview_main.cpp"), str(FW / "OledModel.cpp"),
           str(FW / "OledRender.cpp"), str(FW / "HfmDtc.cpp")]
    r = subprocess.run(tool(True) + ["-std=c++17", "-O1", "-Wall", "-Wno-unused-parameter", "-DHFM_HOST_TEST=1",
                                     "-I", str(FW), "-I", str(clib), "-o", str(exe)] + cpp + objs)
    if r.returncode:
        return r.returncode
    frames_bin = OUT / "frames.bin"
    r = subprocess.run([str(exe), str(frames_bin)])
    if r.returncode:
        return r.returncode
    data = frames_bin.read_bytes()
    rec = 32 + 1024
    frames = []
    for i in range(0, len(data), rec):
        name = data[i:i + 32].split(b"\0")[0].decode()
        buf = data[i + 32:i + rec]
        im = Image.new("L", (128, 64), 0)
        px = im.load()
        for page in range(8):  # U8g2 buffer: 8 pages of 128 bytes, bit 0 = top pixel
            for x in range(128):
                b = buf[page * 128 + x]
                for bit in range(8):
                    if b & (1 << bit):
                        px[x, page * 8 + bit] = 255
        frames.append((name, im))
        im.resize((384, 192), Image.NEAREST).save(OUT / f"{name}.png")
    scale, cols, pad, label = 3, 3, 10, 14
    w, h = 128 * scale, 64 * scale
    rows = (len(frames) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (w + pad) + pad, rows * (h + pad + label) + pad), (40, 44, 52))
    d = ImageDraw.Draw(sheet)
    for i, (name, im) in enumerate(frames):
        x = pad + (i % cols) * (w + pad)
        y = pad + (i // cols) * (h + pad + label)
        d.text((x, y), name, fill=(200, 205, 215))
        tile = Image.new("RGB", (w, h), (0, 0, 0))
        tile.paste(im.resize((w, h), Image.NEAREST).point(lambda v: 255 if v else 0).convert("RGB"), (0, 0))
        sheet.paste(tile, (x, y + label))
    sheet.save(OUT / "screens.png")
    print(f"{len(frames)} frames -> {OUT / 'screens.png'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
