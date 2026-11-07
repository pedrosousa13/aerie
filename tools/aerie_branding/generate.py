#!/usr/bin/env python3
"""Draws Aerie's placeholder art into branding/art/.

The mark is a white "A" built from two legs and a crossbar, on a teal rounded
square. It is a stand-in until issue #21 delivers the final artwork; replace
the files in branding/art/ (same names, same sizes) and nothing else changes.

Needs Pillow and the Noto Sans Bold font (only to draw the word "Aerie"; the
generated files are committed, so the build needs neither):

    python3 tools/aerie_branding/generate.py [--font PATH]
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

TEAL = (14, 107, 115, 255)
DARK = (22, 40, 44, 255)
GRAY = (60, 64, 67, 255)
WHITE = (255, 255, 255, 255)
CANVAS = 256.0
SIZES = (16, 24, 32, 48, 64, 128, 256)
DEFAULT_FONT = "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf"
HEADER = ("// Copyright 2026 The Aerie Authors\n"
          "// You can use, redistribute, and/or modify this source code under\n"
          "// the terms of the GPL-3.0 license that can be found in the LICENSE file.\n\n")

Polygon = list[tuple[float, float]]


def _segment(a: tuple[float, float], b: tuple[float, float], width: float) -> Polygon:
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(dx, dy)
    nx, ny = -dy / length * width / 2, dx / length * width / 2
    return [(a[0] + nx, a[1] + ny), (b[0] + nx, b[1] + ny), (b[0] - nx, b[1] - ny), (a[0] - nx, a[1] - ny)]


def mark() -> list[Polygon]:
    """The A on a 256 x 256 canvas: two legs that meet at the top and a crossbar."""
    apex = (128.0, 52.0)
    return [
        _segment(apex, (70.0, 204.0), 30.0),
        _segment(apex, (186.0, 204.0), 30.0),
        _segment((94.0, 154.0), (162.0, 154.0), 24.0),
    ]


def _scaled(polys: list[Polygon], size: float, ox: float = 0.0, oy: float = 0.0) -> list[Polygon]:
    k = size / CANVAS
    return [[(ox + x * k, oy + y * k) for x, y in p] for p in polys]


def _draw_mark(draw: ImageDraw.ImageDraw, polys: list[Polygon], fill) -> None:
    for p in polys:
        draw.polygon(p, fill=fill)


def square_icon(size: int) -> Image.Image:
    """Teal rounded square with the white mark, drawn 8x and scaled down."""
    big = size * 8
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((0, 0, big - 1, big - 1), radius=big * 67 / 256, fill=TEAL)
    _draw_mark(d, _scaled(mark(), big), WHITE)
    return img.resize((size, size), Image.LANCZOS)


def mono_mark(size: int, color) -> Image.Image:
    big = size * 8
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    _draw_mark(ImageDraw.Draw(img), _scaled(mark(), big), color)
    return img.resize((size, size), Image.LANCZOS)


def wordmark(height: int, mark_color, text_color, font_path: str) -> Image.Image:
    """The mark followed by the word Aerie, 140 x 32 at height 32 (Chromium's slot for the logo)."""
    scale = height / 32.0
    width = int(140 * scale)
    big = 8
    img = Image.new("RGBA", (width * big, height * big), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    mark_px = height * big
    _draw_mark(d, _scaled(mark(), mark_px), mark_color)
    font = ImageFont.truetype(font_path, int(height * big * 0.82))
    left, top, right, bottom = d.textbbox((0, 0), "Aerie", font=font)
    d.text((mark_px + 6 * big * scale - left, (height * big - (bottom - top)) / 2 - top), "Aerie", font=font, fill=text_color)
    return img.resize((width, height), Image.LANCZOS)


def svg() -> str:
    paths = "".join("M" + " L".join(f"{x:.2f} {y:.2f}" for x, y in p) + " Z " for p in mark())
    return ('<svg width="256" height="256" viewBox="0 0 256 256" fill="none" xmlns="http://www.w3.org/2000/svg">'
            f'<rect width="256" height="256" rx="67" fill="#0E6B73"/><path d="{paths.strip()}" fill="#FFFFFF"/></svg>\n')


def _icon_polygons() -> str:
    lines = []
    for p in mark():
        (x0, y0), rest = p[0], p[1:]
        lines.append(f"MOVE_TO, {x0:.2f}f, {y0:.2f}f,")
        lines += [f"LINE_TO, {x:.2f}f, {y:.2f}f," for x, y in rest]
        lines.append("CLOSE,")
    return "\n".join(lines)


def icon_mono() -> str:
    """Vector icon (Chromium .icon format) of the mark alone, on the 56 px canvas of Helium's file."""
    k = 56 / CANVAS
    lines = []
    for p in mark():
        (x0, y0), rest = p[0], p[1:]
        lines.append(f"MOVE_TO, {x0 * k:.2f}f, {y0 * k:.2f}f,")
        lines += [f"LINE_TO, {x * k:.2f}f, {y * k:.2f}f," for x, y in rest]
        lines.append("CLOSE,")
    return HEADER + "CANVAS_DIMENSIONS, 56,\nFILL_RULE_NONZERO,\n" + "\n".join(lines) + "\n"


def icon_color() -> str:
    return (HEADER + "CANVAS_DIMENSIONS, 256,\nNEW_PATH,\nPATH_COLOR_ARGB, 0xFF, 0x0E, 0x6B, 0x73,\n"
            "ROUND_RECT, 0, 0, 256, 256, 67,\nNEW_PATH,\nFILL_RULE_NONZERO,\n"
            "PATH_COLOR_ARGB, 0xFF, 0xFF, 0xFF, 0xFF,\n" + _icon_polygons() + "\n")


def generate(out: Path, font_path: str) -> list[str]:
    out.mkdir(parents=True, exist_ok=True)
    made: list[str] = []

    def save(name: str, image: Image.Image) -> None:
        target = out / name
        target.parent.mkdir(parents=True, exist_ok=True)
        image.save(target, optimize=True)
        made.append(name)

    for size in SIZES:
        save(f"app_icon/{size}.png", square_icon(size))
    save("product_logo.png", wordmark(32, TEAL, DARK, font_path))
    save("product_logo_200.png", wordmark(64, TEAL, DARK, font_path))
    save("product_logo_white.png", wordmark(32, WHITE, WHITE, font_path))
    save("product_logo_white_200.png", wordmark(64, WHITE, WHITE, font_path))
    save("product_logo_22_mono.png", mono_mark(22, GRAY))
    save("favicon_ntp_16.png", mono_mark(16, GRAY))
    save("favicon_ntp_32.png", mono_mark(32, GRAY))
    for name, text in (("product_logo.svg", svg()), ("product_logo.icon", icon_mono()),
                       ("product_logo_color.icon", icon_color())):
        (out / name).write_text(text, encoding="utf-8")
        made.append(name)
    return made


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--font", default=DEFAULT_FONT)
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parents[2] / "branding" / "art")
    args = parser.parse_args()
    for name in generate(args.out, args.font):
        print(name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
