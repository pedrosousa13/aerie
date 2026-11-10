#!/usr/bin/env python3
"""Contrast check for every theme x mode x Workspace preset.

Rebuilds the frame the way sidebar.css paints it (frame base, workspace gradient at
the theme's tint strength, black shade, white glow), samples it where text sits
(the sidebar column on either side of a 1440x900 window), and asserts:
  text >= 4.5:1 (7:1 for high-contrast), muted >= 4.5:1 (7:1 for high-contrast),
  accent and focus ring >= 3:1.
Also checks the status colours (ok, warn, danger) at >= 4.5:1 on the popover surface of every theme and mode,
and on their own chip tints (13%, 15%, 10% over the popover), plus text-on-danger on danger-fill.
Prints a table; exits 1 on any failure. Run: python3 check_contrast.py [-v] [--tokens tokens.json]
CI runs it through tests/test_design_tokens.py.
"""
import json, math, pathlib, sys
here = pathlib.Path(__file__).parent
_path = pathlib.Path(sys.argv[sys.argv.index("--tokens") + 1]) if "--tokens" in sys.argv else here / "tokens.json"
D = json.loads(_path.read_text())
W, H, SIDEBAR, ANGLE = 1440, 900, 260, math.radians(D["tint"]["angle"]["value"])

def rgb(h):
    h = h.lstrip("#"); return tuple(int(h[i:i+2], 16) for i in (0, 2, 4))
def lum(c):
    f = lambda v: (v/255/12.92) if v/255 <= .03928 else ((v/255 + .055)/1.055) ** 2.4
    r, g, b = map(f, c); return .2126*r + .7152*g + .0722*b
def ratio(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True); return (la + .05)/(lb + .05)
def mix(a, b, t): return tuple(a[i]*t + b[i]*(1-t) for i in range(3))   # t share of a

def gradient_at(stops, x, y):
    L = abs(W*math.sin(ANGLE)) + abs(H*math.cos(ANGLE))
    t = ((x - W/2)*math.sin(ANGLE) - (y - H/2)*math.cos(ANGLE))/L + .5
    t = min(1, max(0, t))
    if len(stops) == 1: return stops[0]
    s = t*(len(stops)-1); i = min(int(s), len(stops)-2)
    return mix(stops[i+1], stops[i], s - i)

def glow_alpha(x, y):   # radial-gradient(120% 80% at 0 0, white, transparent 60%)
    d = math.hypot(x/(1.2*W), y/(.8*H)); return max(0, 1 - d/.6)

def tok(group, name, mode):
    t = D[group][name]; return t["value"] if "value" in t else t[mode]

presets = sorted({k.split("-")[0] for k in D["workspace"]})
samples = [(x, y) for y in range(0, H+1, 45) for x in list(range(0, SIDEBAR+1, 26)) + list(range(W-SIDEBAR, W+1, 26))]
verbose = "-v" in sys.argv
rows, fails = [], 0
print(f"{'theme':14}{'mode':6}{'min text':>9}{'min muted':>10}{'min accent':>11}  worst preset")
for tname, th in D["theme"].items():
    for mode in ("light", "dark"):
        if mode not in th["_modes"]: continue
        g = lambda k: th[k]["value"] if "value" in th[k] else th[k][mode]
        frame = rgb(th.get("color.frame-base", D["color"]["frame-base"])[mode])
        text, muted = rgb(g("text")), rgb(g("text-muted"))
        need = 7.0 if tname == "high-contrast" else 4.5
        strength, shade, glow = g("tint-strength"), g("shade"), g("glow")
        shade_colour = rgb(tok("color", "frame-shade", mode))   # the colours the shade and the glow are laid in
        glow_colour = rgb(tok("color", "frame-glow", mode))
        worst = {"text": 99, "muted": 99, "accent": 99}; worst_p = ""
        for p in presets:
            stops = [rgb(D["workspace"][f"{p}-stop-{i}"]["value"]) for i in (1, 2, 3) if f"{p}-stop-{i}" in D["workspace"]]
            hue = rgb(D["workspace"][f"{p}-accent"][mode])
            share = float(str(g("accent-hue-share")).rstrip("%"))/100
            acc = mix(hue, rgb(g("accent-base")), share)
            pm = {"text": 99, "muted": 99, "accent": 99}
            for x, y in samples:
                c = mix(gradient_at(stops, x, y), frame, strength)
                c = mix(shade_colour, c, shade)
                c = mix(glow_colour, c, glow*glow_alpha(x, y))
                pm["text"] = min(pm["text"], ratio(text, c)); pm["muted"] = min(pm["muted"], ratio(muted, c)); pm["accent"] = min(pm["accent"], ratio(acc, c))
            ok = pm["text"] >= need and pm["muted"] >= need and pm["accent"] >= 3
            if not ok:
                fails += 1; print(f"  FAIL {tname}/{mode}/{p}: text {pm['text']:.2f} muted {pm['muted']:.2f} accent {pm['accent']:.2f} (need {need}/{need}/3)")
            elif verbose: print(f"  ok   {tname}/{mode}/{p}: {pm['text']:.2f} {pm['muted']:.2f} {pm['accent']:.2f}")
            for k in worst:
                if pm[k] < worst[k]: worst[k] = pm[k]; 
            if pm["accent"] <= worst["accent"]: worst_p = p
        print(f"{tname:14}{mode:6}{worst['text']:9.2f}{worst['muted']:10.2f}{worst['accent']:11.2f}  {worst_p}  (need {need}/{need}/3)")

# status colours on popover surfaces (the editor sheet, menus and popovers)
print(f"\n{'status':14}{'mode':6}{'surface':>9}{'ok':>7}{'warn':>7}{'danger':>7}  (need 4.5, also on 13%/15%/10% chip tints)")
TINT = {"status-ok": .13, "status-warn": .15, "status-danger": .10}
for tname, th in D["theme"].items():
    for mode in ("light", "dark"):
        if mode not in th["_modes"]: continue
        pop = rgb(th.get("color.surface-popover", D["color"]["surface-popover"])[mode]); vals = {}
        for k, a in TINT.items():
            fg = rgb(D["color"][k][mode]); chip = mix(fg, pop, a)
            r_pop, r_chip = ratio(fg, pop), ratio(fg, chip); vals[k] = min(r_pop, r_chip)
            if min(r_pop, r_chip) < 4.5:
                fails += 1; print(f"  FAIL {k} on {tname}/{mode}: on popover {r_pop:.2f}, on chip tint {r_chip:.2f}")
        print(f"{tname:14}{mode:6}{'#%02X%02X%02X' % pop:>9}" + "".join(f"{vals[k]:7.2f}" for k in TINT))
fill, on = rgb(D["color"]["danger-fill"]["value"]), rgb(D["color"]["text-on-danger"]["value"])
rf = ratio(fill, on); print(f"text-on-danger on danger-fill: {rf:.2f}")
if rf < 4.5: fails += 1; print("  FAIL text-on-danger on danger-fill")
print("FAILED" if fails else "OK", f"- {fails} failing theme/mode/preset combinations")
sys.exit(1 if fails else 0)
