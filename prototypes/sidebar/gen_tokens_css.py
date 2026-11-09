#!/usr/bin/env python3
"""Generate tokens.css from tokens.json. Run: python3 gen_tokens_css.py

Plain groups: a token has `value` (shared) or `light` and `dark`.
Group `theme`: one entry per theme. Plain keys become --aerie-theme-<key> and
"<group>.<key>" keys override --aerie-<group>-<key>. Each theme emits one block
per colour mode it supports, selected by :root[data-theme][data-mode].
"""
import json, pathlib
here = pathlib.Path(__file__).parent
data = json.loads((here / "tokens.json").read_text())
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

out = ["/* Generated from tokens.json by gen_tokens_css.py. Do not edit by hand. */\n",
  block(":root", shared),
  block(':root[data-mode="light"], :root:not([data-mode])', light),
  block(':root[data-mode="dark"]', dark),
  *theme_blocks,
  "@media (prefers-reduced-motion: reduce) {\n" + block("  :root", ["  "+l for l in reduced]) + "}\n",
  block(":root[data-reduced-motion]", reduced)]
(here / "tokens.css").write_text("\n".join(out))

# Container queries cannot read var(), so horizontal.css repeats size-hz-tab-icon-only. Fail if they drift apart.
thr = data["size"]["hz-tab-icon-only"]["value"]
hcss = (here / "horizontal.css").read_text()
for want in (f"@container (max-width:{thr}px)", f"@container (min-width:{thr + 1}px)"):
    if want not in hcss:
        raise SystemExit(f"horizontal.css is missing '{want}' (size-hz-tab-icon-only = {thr})")
