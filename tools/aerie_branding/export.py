#!/usr/bin/env python3
"""Exports Aerie's final art (the Soar mark, Alpenglow palette) into branding/art/.

It writes every file that branding/resources.txt maps into the Chromium tree, with
fixed names and sizes, so the branding layer picks it up with no code change
(issues #5 and #21).

Sources, all in branding/source/:
  soar-mark.svg        the mark, master. One stroked path and one circle.
  soar-mark-small.svg  the same mark tuned for 16-24 px: heavier wings, smaller sun.
  fonts/               Outfit (SIL OFL 1.1), for the word "Aerie" in the logo images.

The build does not run this. The exported files are committed. You only need it
after changing a source. It needs Pillow 11.3.0 and skia-pathops 0.9.2:

    uv run --no-project --with pillow==11.3.0 --with skia-pathops==0.9.2 \\
        python3 tools/aerie_branding/export.py [--out DIR]
"""

from __future__ import annotations

import argparse
import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SOURCE = REPO / "branding" / "source"
FONT = SOURCE / "fonts" / "Outfit-Variable.ttf"
FONT_WEIGHT = b"SemiBold"

CANVAS = 256.0
SQUIRCLE_RADIUS = 58.0
# Alpenglow: peach to pink to violet, from the top-left corner towards the bottom
# (gradient vector (0.6, 1) in the icon's box, as on the concept page).
PALETTE = ((0.0, (0xFF, 0xA4, 0x63)), (0.5, (0xF0, 0x46, 0x6F)), (1.0, (0x7B, 0x3F, 0xE4)))
GRADIENT_VECTOR = (0.6, 1.0)
SOLID = (0xF0, 0x46, 0x6F)  # where a format has no gradients (Chromium .icon files)
GLYPH_GRAY = (0x3C, 0x40, 0x43)  # Chromium's default icon colour, as Helium's mono art uses
TEXT_DARK = (0x1C, 0x1A, 0x24)
WHITE = (0xFF, 0xFF, 0xFF)
SMALL_MAX = 24  # icons at this size or below use soar-mark-small.svg
SUPERSAMPLE = 8
HEADER = ("// Copyright 2026 The Aerie Authors\n"
          "// You can use, redistribute, and/or modify this source code under\n"
          "// the terms of the GPL-3.0 license that can be found in the LICENSE file.\n\n")

APP_ICON_SIZES = (16, 24, 32, 48, 64, 128, 256)
# Every file the branding layer expects, with its pixel size (None for vector files).
ARTWORK: dict[str, tuple[int, int] | None] = {
    **{f"app_icon/{s}.png": (s, s) for s in APP_ICON_SIZES},
    "favicon_ntp_16.png": (16, 16),
    "favicon_ntp_32.png": (32, 32),
    "product_logo.png": (140, 32),
    "product_logo_200.png": (280, 64),
    "product_logo_white.png": (140, 32),
    "product_logo_white_200.png": (280, 64),
    "product_logo_22_mono.png": (22, 22),
    "product_logo.svg": None,
    "product_logo.icon": None,
    "product_logo_color.icon": None,
}

Point = tuple[float, float]


class MasterError(ValueError):
    pass


@dataclass(frozen=True)
class Master:
    """The mark as drawn in a master SVG: a stroked path of cubics, plus a filled circle."""
    start: Point
    cubics: tuple[tuple[Point, Point, Point], ...]
    stroke_width: float
    sun: tuple[float, float, float]  # cx, cy, r


def parse_master(svg_text: str) -> Master:
    """Reads the small SVG subset the masters use. Anything else is an error, not a guess."""
    ns = {"s": "http://www.w3.org/2000/svg"}
    root = ET.fromstring(svg_text)
    if root.get("viewBox") != "0 0 256 256":
        raise MasterError("a master must have viewBox 0 0 256 256")
    paths, circles = root.findall("s:path", ns), root.findall("s:circle", ns)
    if len(paths) != 1 or len(circles) != 1:
        raise MasterError("a master has exactly one <path> and one <circle>")
    path = paths[0]
    if path.get("stroke-linecap") != "round" or path.get("stroke-linejoin") != "round":
        raise MasterError("the path must use round caps and joins")
    tokens = re.findall(r"[A-Za-z]|-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?", path.get("d", ""))
    stray = sorted({t for t in tokens if t.isalpha() and t not in ("M", "C")})
    if stray:
        raise MasterError(f"the path may use only M and C, not {', '.join(stray)}")
    if not tokens or tokens[0] != "M":
        raise MasterError("the path must start with an absolute M")
    nums: list[float] = []
    start: Point | None = None
    cubics: list[tuple[Point, Point, Point]] = []
    command = None
    for tok in tokens + ["END"]:
        if tok in ("M", "C", "END"):
            if command == "M":
                if len(nums) != 2:
                    raise MasterError("M takes one point")
                start = (nums[0], nums[1])
            elif command == "C":
                if not nums or len(nums) % 6:
                    raise MasterError("C takes points in groups of three")
                for i in range(0, len(nums), 6):
                    a = nums[i:i + 6]
                    cubics.append(((a[0], a[1]), (a[2], a[3]), (a[4], a[5])))
            command, nums = tok, []
        else:
            nums.append(float(tok))
    if start is None or not cubics:
        raise MasterError("the path needs an M and at least one C")
    c = circles[0]
    try:
        width = float(path.get("stroke-width"))
        sun = (float(c.get("cx")), float(c.get("cy")), float(c.get("r")))
    except (TypeError, ValueError):
        raise MasterError("the path needs a stroke-width and the circle cx, cy and r") from None
    return Master(start, tuple(cubics), width, sun)


def load_master(name: str) -> Master:
    return parse_master((SOURCE / name).read_text(encoding="utf-8"))


# A contour is a start point and a list of segments; a segment is ("L", p) or ("C", c1, c2, p).
Contour = tuple[Point, list[tuple]]


def outline(master: Master) -> list[Contour]:
    """The mark as filled contours: the stroke turned into its outline, joined with the sun."""
    import pathops

    stroke = pathops.Path()
    stroke.moveTo(*master.start)
    for c1, c2, p in master.cubics:
        stroke.cubicTo(*c1, *c2, *p)
    stroke.stroke(master.stroke_width, pathops.LineCap.ROUND_CAP, pathops.LineJoin.ROUND_JOIN, 4)
    stroke.convertConicsToQuads()  # round caps and joins come out as conics, which the union cannot take

    cx, cy, r = master.sun
    k = 0.5522847498 * r  # cubic approximation of a quarter circle
    sun = pathops.Path()
    sun.moveTo(cx + r, cy)
    sun.cubicTo(cx + r, cy + k, cx + k, cy + r, cx, cy + r)
    sun.cubicTo(cx - k, cy + r, cx - r, cy + k, cx - r, cy)
    sun.cubicTo(cx - r, cy - k, cx - k, cy - r, cx, cy - r)
    sun.cubicTo(cx + k, cy - r, cx + r, cy - k, cx + r, cy)
    sun.close()

    stroke.addPath(sun)
    stroke.simplify()  # union, nonzero winding, no overlaps left

    contours: list[Contour] = []
    current: list[tuple] | None = None
    last: Point = (0.0, 0.0)
    for verb, pts in stroke.segments:
        if verb == "moveTo":
            current = []
            last = pts[0]
            contours.append((pts[0], current))
        elif verb == "lineTo":
            current.append(("L", pts[0]))
            last = pts[0]
        elif verb == "curveTo":
            current.append(("C", pts[0], pts[1], pts[2]))
            last = pts[2]
        elif verb == "qCurveTo":
            # TrueType-style run: off-curve points with implied on-curve midpoints. Raise each to a cubic.
            *offs, end = pts
            for i, q in enumerate(offs):
                p = end if i == len(offs) - 1 else ((q[0] + offs[i + 1][0]) / 2, (q[1] + offs[i + 1][1]) / 2)
                c1 = (last[0] + 2 / 3 * (q[0] - last[0]), last[1] + 2 / 3 * (q[1] - last[1]))
                c2 = (p[0] + 2 / 3 * (q[0] - p[0]), p[1] + 2 / 3 * (q[1] - p[1]))
                current.append(("C", c1, c2, p))
                last = p
        elif verb in ("closePath", "endPath"):
            current = None
        else:
            raise MasterError(f"unexpected outline segment {verb}")
    return contours


def _flatten(contour: Contour, scale: float, ox: float = 0.0, oy: float = 0.0, steps: int = 24) -> list[Point]:
    start, segs = contour
    pts = [start]
    for seg in segs:
        if seg[0] == "L":
            pts.append(seg[1])
        else:
            p0, (c1, c2, p3) = pts[-1], seg[1:]
            for i in range(1, steps + 1):
                t = i / steps
                u = 1 - t
                pts.append((u ** 3 * p0[0] + 3 * u * u * t * c1[0] + 3 * u * t * t * c2[0] + t ** 3 * p3[0],
                            u ** 3 * p0[1] + 3 * u * u * t * c1[1] + 3 * u * t * t * c2[1] + t ** 3 * p3[1]))
    return [(ox + x * scale, oy + y * scale) for x, y in pts]


def _area(poly: list[Point]) -> float:
    return sum(poly[i][0] * poly[i - 1][1] - poly[i - 1][0] * poly[i][1] for i in range(len(poly))) / 2


def mark_mask(contours: list[Contour], size: int, box: float | None = None, ox: float = 0.0, oy: float = 0.0):
    """An 'L' mask of the mark drawn into a box of `box` px (default: the whole image) at (ox, oy)."""
    from PIL import Image, ImageDraw

    big = size * SUPERSAMPLE
    box = size if box is None else box
    mask = Image.new("L", (big, big), 0)
    draw = ImageDraw.Draw(mask)
    polys = [_flatten(c, box * SUPERSAMPLE / CANVAS, ox * SUPERSAMPLE, oy * SUPERSAMPLE) for c in contours]
    polys.sort(key=lambda p: abs(_area(p)), reverse=True)
    outer_sign = _area(polys[0]) > 0
    for p in polys:  # largest first; a contour wound against the outer one is a hole
        draw.polygon(p, fill=255 if (_area(p) > 0) == outer_sign else 0)
    return mask.resize((size, size), Image.LANCZOS)


def _mix(t: float) -> tuple[int, int, int]:
    for (t0, c0), (t1, c1) in zip(PALETTE, PALETTE[1:]):
        if t <= t1:
            f = (t - t0) / (t1 - t0) if t1 > t0 else 0.0
            return tuple(round(a + (b - a) * f) for a, b in zip(c0, c1))
    return PALETTE[-1][1]


def gradient(size: int):
    from PIL import Image

    gx, gy = GRADIENT_VECTOR
    norm = gx * gx + gy * gy
    img = Image.new("RGB", (size, size))
    img.putdata([_mix(min(1.0, ((x + .5) / size * gx + (y + .5) / size * gy) / norm))
                 for y in range(size) for x in range(size)])
    return img


def squircle_mask(size: int):
    from PIL import Image, ImageDraw

    big = size * SUPERSAMPLE
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, big - 1, big - 1), radius=big * SQUIRCLE_RADIUS / CANVAS, fill=255)
    return mask.resize((size, size), Image.LANCZOS)


def app_icon(size: int, regular: list[Contour], small: list[Contour]):
    from PIL import Image

    icon = gradient(size).convert("RGBA")
    icon.putalpha(squircle_mask(size))
    white = Image.new("RGBA", (size, size), WHITE + (255,))
    icon.paste(white, (0, 0), mark_mask(small if size <= SMALL_MAX else regular, size))
    return icon


def mono(size: int, contours: list[Contour], color: tuple[int, int, int]):
    from PIL import Image

    img = Image.new("RGBA", (size, size), color + (0,))
    img.putalpha(mark_mask(contours, size))
    return img


def logo(height: int, text_color: tuple[int, int, int], regular: list[Contour], small: list[Contour]):
    """The app icon followed by the word Aerie, in Chromium's 140 x 32 logo slot (or 2x)."""
    from PIL import Image, ImageDraw, ImageFont

    scale = height / 32
    width = round(140 * scale)
    out = Image.new("RGBA", (width, height), text_color + (0,))
    out.alpha_composite(app_icon(height, regular, small))

    big = SUPERSAMPLE
    text = Image.new("L", (width * big, height * big), 0)
    font = ImageFont.truetype(str(FONT), round(23 * scale * big))
    font.set_variation_by_name(FONT_WEIGHT)
    d = ImageDraw.Draw(text)
    left, top, right, bottom = d.textbbox((0, 0), "Aerie", font=font)
    x = (height + 9 * scale) * big - left
    y = (height * big - (bottom - top)) / 2 - top
    d.text((x, y), "Aerie", font=font, fill=255)
    text = text.resize((width, height), Image.LANCZOS)
    layer = Image.new("RGBA", (width, height), text_color + (0,))
    layer.putalpha(text)
    out.alpha_composite(layer)
    return out


def _fmt(v: float) -> str:
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s == "-0" else s


def svg_path(contours: list[Contour], scale: float = 1.0) -> str:
    parts = []
    for (sx, sy), segs in contours:
        parts.append(f"M{_fmt(sx * scale)} {_fmt(sy * scale)}")
        for seg in segs:
            pts = seg[1:]
            parts.append(seg[0] + " ".join(f"{_fmt(x * scale)} {_fmt(y * scale)}" for x, y in pts))
        parts.append("Z")
    return "".join(parts)


def svg_logo(contours: list[Contour]) -> str:
    stops = "".join(f'<stop offset="{t:g}" stop-color="#{r:02X}{g:02X}{b:02X}"/>' for t, (r, g, b) in PALETTE)
    gx, gy = GRADIENT_VECTOR
    return ('<svg width="256" height="256" viewBox="0 0 256 256" fill="none" xmlns="http://www.w3.org/2000/svg">'
            f'<defs><linearGradient id="a" x1="0" y1="0" x2="{gx:g}" y2="{gy:g}">{stops}</linearGradient></defs>'
            f'<rect width="256" height="256" rx="{SQUIRCLE_RADIUS:g}" fill="url(#a)"/>'
            f'<path d="{svg_path(contours)}" fill="#FFFFFF"/></svg>\n')


def _icon_commands(contours: list[Contour], scale: float) -> list[str]:
    lines = []
    for (sx, sy), segs in contours:
        lines.append(f"MOVE_TO, {sx * scale:.2f}f, {sy * scale:.2f}f,")
        for seg in segs:
            name = "LINE_TO" if seg[0] == "L" else "CUBIC_TO"
            lines.append(f"{name}, " + ", ".join(f"{x * scale:.2f}f, {y * scale:.2f}f" for x, y in seg[1:]) + ",")
        lines.append("CLOSE,")
    return lines


def icon_mono(contours: list[Contour]) -> str:
    """Chromium vector icon of the mark alone, on the 56 px canvas Helium's file uses."""
    return (HEADER + "CANVAS_DIMENSIONS, 56,\nFILL_RULE_NONZERO,\n"
            + "\n".join(_icon_commands(contours, 56 / CANVAS)) + "\n")


def icon_color(contours: list[Contour]) -> str:
    r, g, b = SOLID
    return (HEADER + "CANVAS_DIMENSIONS, 256,\nNEW_PATH,\n"
            f"PATH_COLOR_ARGB, 0xFF, 0x{r:02X}, 0x{g:02X}, 0x{b:02X},\n"
            f"ROUND_RECT, 0, 0, 256, 256, {SQUIRCLE_RADIUS:g},\nNEW_PATH,\nFILL_RULE_NONZERO,\n"
            "PATH_COLOR_ARGB, 0xFF, 0xFF, 0xFF, 0xFF,\n" + "\n".join(_icon_commands(contours, 1.0)) + "\n")


def export(out: Path) -> list[str]:
    regular = outline(load_master("soar-mark.svg"))
    small = outline(load_master("soar-mark-small.svg"))
    images = {f"app_icon/{s}.png": app_icon(s, regular, small) for s in APP_ICON_SIZES}
    images.update({
        "favicon_ntp_16.png": mono(16, small, GLYPH_GRAY),
        "favicon_ntp_32.png": mono(32, small, GLYPH_GRAY),
        "product_logo_22_mono.png": mono(22, small, GLYPH_GRAY),
        "product_logo.png": logo(32, TEXT_DARK, regular, small),
        "product_logo_200.png": logo(64, TEXT_DARK, regular, small),
        "product_logo_white.png": logo(32, WHITE, regular, small),
        "product_logo_white_200.png": logo(64, WHITE, regular, small),
    })
    texts = {
        "product_logo.svg": svg_logo(regular),
        "product_logo.icon": icon_mono(small),
        "product_logo_color.icon": icon_color(regular),
    }
    assert set(images) | set(texts) == set(ARTWORK), "export and ARTWORK disagree"
    made = []
    for name, image in images.items():
        target = out / name
        target.parent.mkdir(parents=True, exist_ok=True)
        image.save(target, optimize=True)
        made.append(name)
    for name, text in texts.items():
        (out / name).write_text(text, encoding="utf-8")
        made.append(name)
    return sorted(made)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--out", type=Path, default=REPO / "branding" / "art")
    args = parser.parse_args()
    for name in export(args.out):
        print(name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
