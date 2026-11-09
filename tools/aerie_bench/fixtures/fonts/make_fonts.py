#!/usr/bin/env python3
"""Make the two Bench test fonts (issue #117). Needs fontTools; the output is committed, so the Bench does not.

    python3 tools/aerie_bench/fixtures/fonts/make_fonts.py

The fonts are written by this script, not copied from anywhere, so they have no licence of their own beyond
the repository's. Every glyph is a solid square, and every glyph has the same advance width, so a text
measured in one of them has a width that no other font on a machine gives:

* ``aerie-bench-probe.ttf``, family "Aerie Bench Probe": installed on the Bench machine but not on the
  allowlist. With the restriction on, a page must not find it.
* ``aerie-bench-allowed.ttf``, family "Droid Sans Mono": a name that is on the allowlist, and a family that
  few machines have, so the Bench can show that an allowlisted font is found. If a machine has the real font
  of that name too, the check still sees a font with a width that is no generic font's.

The fonts have no glyphs outside basic Latin, so other scripts must come from system fallback.
"""

from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

HERE = Path(__file__).resolve().parent
EM = 1000
CHARS = [chr(c) for c in range(0x20, 0x7F)]


def square(size: int):
    pen = TTGlyphPen(None)
    pen.moveTo((50, 0))
    pen.lineTo((50, size))
    pen.lineTo((size, size))
    pen.lineTo((size, 0))
    pen.closePath()
    return pen.glyph()


def make(path: Path, family: str, advance: int) -> None:
    names = [".notdef", "space"] + [f"uni{ord(c):04X}" for c in CHARS if c != " "]
    fb = FontBuilder(EM, isTTF=True)
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap({ord(c): ("space" if c == " " else f"uni{ord(c):04X}") for c in CHARS})
    glyphs = {n: (TTGlyphPen(None).glyph() if n == "space" else square(advance - 100)) for n in names}
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics({n: (advance, 50 if n != "space" else 0) for n in names})
    fb.setupHorizontalHeader(ascent=800, descent=-200)
    fb.setupNameTable({"familyName": family, "styleName": "Regular", "uniqueFontIdentifier": f"{family} Regular",
                       "fullName": family, "psName": family.replace(" ", "") + "-Regular"})
    fb.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200)
    fb.setupPost()
    fb.font["head"].created = fb.font["head"].modified = 3786825600  # fixed, so the output is reproducible
    fb.save(str(path))


if __name__ == "__main__":
    make(HERE / "aerie-bench-probe.ttf", "Aerie Bench Probe", 777)
    make(HERE / "aerie-bench-allowed.ttf", "Droid Sans Mono", 888)
