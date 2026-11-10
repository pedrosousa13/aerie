#!/usr/bin/env python3
"""Generate the CSS custom properties from tokens.json.

Run: python3 gen_tokens_css.py --out aerie_tokens.css [--tokens tokens.json] [--check-horizontal horizontal.css]
The build runs it as the GN action tokens_css; prototypes/sidebar/tokens.css is a checked-in copy (see its README).

Plain groups: a token has `value` (shared) or `light` and `dark`.
Group `theme`: one entry per theme. Plain keys become --aerie-theme-<key> and
"<group>.<key>" keys override --aerie-<group>-<key>. Each theme emits one block
per colour mode it supports, selected by :root[data-theme][data-mode].
"""
import argparse, json, pathlib
here = pathlib.Path(__file__).parent
ap = argparse.ArgumentParser()
ap.add_argument("--tokens", type=pathlib.Path, default=here / "tokens.json")
ap.add_argument("--out", type=pathlib.Path, required=True)
ap.add_argument("--check-horizontal", type=pathlib.Path, help="horizontal.css whose container queries must match size-hz-tab-icon-only")
args = ap.parse_args()
data = json.loads(args.tokens.read_text())
fmt = lambda v, u: f"{v}{u}" if u else str(v)
shared, light, dark, reduced = [], [], [], []
for group, toks in data.items():
    if group.startswith("_") or group == "theme": continue
    for name, t in toks.items():
        var = f"--aerie-{group}-{name}"
        u = t.get("unit", "")
        if "value" in t:
            shared.append(f"  {var}: {fmt(t['value'], u)};")
        else:
            light.append(f"  {var}: {fmt(t['light'], u)};")
            dark.append(f"  {var}: {fmt(t['dark'], u)};")
        if "reduced" in t:
            reduced.append(f"  {var}: {fmt(t['reduced'], u)};")
def block(sel, lines): return sel + " {\n" + "\n".join(lines) + "\n}\n"

LIGHT = ':is([data-mode="light"], :not([data-mode]))'
theme_blocks = []
DEFAULT_THEME = "vivid"
for tname, th in data["theme"].items():
    modes = th.get("_modes", ["light", "dark"])
    for mode in modes:
        lines = []
        for key, t in th.items():
            if key.startswith("_"): continue
            var = "--aerie-" + (key.replace(".", "-", 1) if "." in key else "theme-" + key)
            v = t["value"] if "value" in t else t[mode]
            lines.append(f"  {var}: {fmt(v, t.get('unit', ''))};")
        sel = f':root[data-theme="{tname}"]' + (LIGHT if mode == "light" else '[data-mode="dark"]')
        if tname == DEFAULT_THEME:
            sel += ", :root:not([data-theme])" + (LIGHT if mode == "light" else '[data-mode="dark"]')
        theme_blocks.append(block(sel, lines))

out = ["/* Generated from tokens.json. Do not edit. */\n",
  block(":root", shared),
  block(':root[data-mode="light"], :root:not([data-mode])', light),
  block(':root[data-mode="dark"]', dark),
  *theme_blocks,
  "@media (prefers-reduced-motion: reduce) {\n" + block("  :root", ["  "+l for l in reduced]) + "}\n",
  block(":root[data-reduced-motion]", reduced)]
args.out.write_text("\n".join(out))

# Container queries cannot read var(), so horizontal.css repeats size-hz-tab-icon-only. Fail if they drift apart.
if args.check_horizontal:
    thr = data["size"]["hz-tab-icon-only"]["value"]
    hcss = args.check_horizontal.read_text()
    for want in (f"@container (max-width:{thr}px)", f"@container (min-width:{thr + 1}px)"):
        if want not in hcss:
            raise SystemExit(f"{args.check_horizontal.name} is missing '{want}' (size-hz-tab-icon-only = {thr})")
