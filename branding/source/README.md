# Aerie art sources

The final Aerie mark is **Soar**: the wings sweep up into a peak, so the bird is also an A. The sun sits where the crossbar would be. The palette is **Alpenglow**: peach `#FFA463` to pink `#F0466F` to violet `#7B3FE4`. Both were chosen in issue #21.

| File | What it is |
| --- | --- |
| `soar-mark.svg` | Master mark. One stroked path and one circle on a 256 px canvas. |
| `soar-mark-small.svg` | The same mark tuned for 16 to 24 px: heavier wings and a smaller, lower sun. It is used for small icons, the toolbar glyph and the new tab page favicon. |
| `fonts/Sora-Variable.ttf` | Sora, SemiBold weight, for the word "Aerie" in the logo images. SIL Open Font License 1.1, see `fonts/OFL.txt`. |

`tools/aerie_branding/export.py` turns these sources into every file in `branding/art/`. It keeps the names and sizes that `branding/resources.txt` maps into the Chromium tree. The build does not run the exporter, because the exported files are committed. After you change a source, run:

```sh
uv run --no-project --with pillow==11.3.0 --with skia-pathops \
    python3 tools/aerie_branding/export.py
```

The masters use a small SVG subset: one path made of absolute `M` and `C` commands, with round caps and joins, plus one circle. The exporter refuses anything else and does not guess.
