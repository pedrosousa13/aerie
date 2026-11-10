#!/usr/bin/env python3
"""Generate aerie_tokens.h (namespace aerie::tokens) from tokens.json.

Run: python3 gen_tokens_cc.py --out aerie_tokens.h [--tokens tokens.json]
The build runs it as the GN action tokens_header. Everything in the header is constexpr: no JSON parse, no
allocation and no animation object is built at startup (ADR 0007).

What the header holds
  * a constant for every size, radius, space, type, tint and motion-* token: kSizeSidebarWidth,
    kMotionDurationFast, ... A token with a `reduced` value also gets kMotionDurationFastReduced.
  * colour and shadow tokens per mode (kColorTextLight, kColorTextDark; a token with one value has no suffix).
  * easing: cubic-bezier tokens as four floats, the spring tokens as stiffness, damping, mass plus the curve sampled
    to the same points as the CSS linear() curve.
  * one ThemeTokens table per theme and colour mode, and the Workspace presets.

Theme override keys. A theme key written "<group>.<token>" (color.text, shadow.card, size.focus-ring-width)
REPLACES that token while the theme is active: the table field color_text holds the theme's value, and holds the base
token's value for a theme that has no such key. The CSS output means the same (the theme block wins over :root), and
tests/test_design_tokens.py compares every table field with the CSS cascade. A plain key (tint-strength, shade, ...)
becomes the field theme_<key> and is not an override. Fields with a CSS-only shape are split: card-border into a
width and a colour, strip-fill into an enum and an angle, switcher-fill into an enum.
"""
import argparse
import json
import pathlib
import re
import sys

here = pathlib.Path(__file__).parent
ap = argparse.ArgumentParser()
ap.add_argument("--tokens", type=pathlib.Path, default=here / "tokens.json")
ap.add_argument("--out", type=pathlib.Path, required=True)
args = ap.parse_args()
D = json.loads(args.tokens.read_text())

# Groups that are not flat constants.
SPECIAL = {"color", "shadow", "theme", "workspace", "motion-easing"}


def camel(name):
    return "".join(p[:1].upper() + p[1:] for p in name.split("-"))


def field(var):  # "color.frame-base" / "theme-text" -> color_frame_base
    return var.replace(".", "_").replace("-", "_")


def num(v, as_float):
    if as_float:
        return f"{float(v)!r}f"
    return str(int(v))


def is_int(v):
    return isinstance(v, int) and not isinstance(v, bool)


def sk_color(text):
    text = text.strip()
    m = re.fullmatch(r"#([0-9A-Fa-f]{2})([0-9A-Fa-f]{2})([0-9A-Fa-f]{2})", text)
    if m:
        r, g, b = (int(x, 16) for x in m.groups())
        return f"SkColorSetRGB(0x{r:02X}, 0x{g:02X}, 0x{b:02X})"
    m = re.fullmatch(r"rgba\((\d+),\s*(\d+),\s*(\d+),\s*([\d.]+)\)", text)
    if m:
        r, g, b = (int(x) for x in m.groups()[:3])
        a = int(float(m.group(4)) * 255 + 0.5)
        return f"SkColorSetARGB({a}, {r}, {g}, {b})"
    if text == "transparent":
        return "SkColorSetARGB(0, 0, 0, 0)"
    raise SystemExit(f"gen_tokens_cc.py: unsupported colour '{text}'")


def shadow(text):
    text = text.strip()
    if text == "none":
        return "{0, {}}"
    layers = []
    for part in re.findall(r"(?:[^,(]|\([^)]*\))+", text):
        part = part.strip()
        m = re.search(r"(#[0-9A-Fa-f]{6}|rgba\([^)]*\))$", part)
        if not m:
            raise SystemExit(f"gen_tokens_cc.py: unsupported shadow '{text}'")
        lengths = re.findall(r"-?[\d.]+(?:px)?", part[:m.start()])
        if not 3 <= len(lengths) <= 4:
            raise SystemExit(f"gen_tokens_cc.py: unsupported shadow '{text}'")
        vals = [int(float(x.rstrip("px"))) for x in lengths] + [0] * (4 - len(lengths))
        layers.append("{" + ", ".join(str(v) for v in vals) + ", " + sk_color(m.group(1)) + "}")
    if len(layers) > 2:
        raise SystemExit(f"gen_tokens_cc.py: more than two shadow layers in '{text}'")
    return "{" + str(len(layers)) + ", {" + ", ".join(layers) + "}}"


def per_mode(tok, mode):
    return tok["value"] if "value" in tok else tok[mode]


out = ["// Generated from tokens.json. Do not edit.",
       "",
       "#ifndef COMPONENTS_AERIE_DESIGN_TOKENS_AERIE_TOKENS_H_",
       "#define COMPONENTS_AERIE_DESIGN_TOKENS_AERIE_TOKENS_H_",
       "",
       "#include \"third_party/skia/include/core/SkColor.h\"",
       "",
       "namespace aerie::tokens {",
       "",
       "struct CubicBezier {",
       "  float x1;",
       "  float y1;",
       "  float x2;",
       "  float y2;",
       "};",
       "",
       "// One layer of a box shadow: offset, blur, spread, colour.",
       "struct ShadowLayer {",
       "  int x;",
       "  int y;",
       "  int blur;",
       "  int spread;",
       "  SkColor color;",
       "};",
       "",
       "// At most two layers; count says how many are used (0 is \"none\").",
       "struct Shadow {",
       "  int count;",
       "  ShadowLayer layers[2];",
       "};",
       "",
       "// A motion curve: a cubic bezier, or (sampled) `sample_count` evenly spaced points of the progress curve,",
       "// the same points as the CSS linear() form.",
       "struct Easing {",
       "  bool sampled;",
       "  CubicBezier bezier;",
       "  const float* samples;",
       "  int sample_count;",
       "};",
       ""]

# ---- flat constants --------------------------------------------------------------------------------------------
for group, toks in D.items():
    if group.startswith("_") or group in SPECIAL:
        continue
    out.append(f"// {group}")
    for name, t in toks.items():
        const = "k" + camel(group) + camel(name)
        unit = t.get("unit", "")
        note = f"  // {unit}" if unit else ""
        if "value" not in t:
            raise SystemExit(f"gen_tokens_cc.py: {group}-{name} has no `value`")
        if isinstance(t["value"], str):
            if "reduced" in t:
                raise SystemExit(f"gen_tokens_cc.py: string token {group}-{name} has a reduced value")
            out.append(f"inline constexpr char {const}[] = {json.dumps(t['value'])};")
            continue
        vals = [t["value"]] + ([t["reduced"]] if "reduced" in t else [])
        as_float = not all(is_int(v) for v in vals)
        out.append(f"inline constexpr {'float' if as_float else 'int'} {const} = {num(t['value'], as_float)};{note}")
        if "reduced" in t:
            out.append(f"inline constexpr {'float' if as_float else 'int'} {const}Reduced = {num(t['reduced'], as_float)};{note}")
    out.append("")

# ---- durations as an enum, so the motion helper takes a token and not a number -------------------------------
durs = list(D["motion-duration"])
out.append("enum class MotionDuration {")
out += [f"  k{camel(n)}," for n in durs]
out += ["};", "",
        "// The duration of a token in milliseconds; under reduced motion, its reduced value.",
        "constexpr int GetDurationMs(MotionDuration token, bool reduced) {",
        "  switch (token) {"]
for n in durs:
    c = "kMotionDuration" + camel(n)
    body = f"reduced ? {c}Reduced : {c}" if "reduced" in D["motion-duration"][n] else c
    out.append(f"    case MotionDuration::k{camel(n)}:\n      return {body};")
out += ["  }", "  return 0;", "}", ""]

# ---- easing ---------------------------------------------------------------------------------------------------
BEZIER = re.compile(r"cubic-bezier\(([^)]*)\)")


def easing_const(const, text):
    """Emit the constants of one easing form; returns (kind, count)."""
    text = text.strip()
    m = BEZIER.fullmatch(text)
    if m:
        p = [float(x) for x in m.group(1).split(",")]
        assert len(p) == 4, text
        out.append(f"inline constexpr CubicBezier {const}{{" + ", ".join(num(x, True) for x in p) + "};")
        return "bezier", 0
    if text == "linear":
        out.append(f"inline constexpr CubicBezier {const}{{0.0f, 0.0f, 1.0f, 1.0f}};  // the identity curve")
        return "bezier", 0
    m = re.fullmatch(r"linear\(([^)]*)\)", text)
    if m:
        pts = [float(x) for x in m.group(1).split(",")]
        out.append(f"inline constexpr float {const}Samples[] = {{")
        for i in range(0, len(pts), 6):
            out.append("    " + ", ".join(num(x, True) for x in pts[i:i + 6]) + ",")
        out.append("};")
        out.append(f"inline constexpr int {const}SampleCount = {len(pts)};")
        return "sampled", len(pts)
    raise SystemExit(f"gen_tokens_cc.py: unsupported easing '{text}'")


out.append("// motion-easing")
kinds = {}
for name, t in D["motion-easing"].items():
    const = "kMotionEasing" + camel(name)
    kinds[name] = [easing_const(const, t["value"])]
    if "reduced" in t:
        kinds[name].append(easing_const(const + "Reduced", t["reduced"]))
out += ["", "enum class MotionEasing {"]
out += [f"  k{camel(n)}," for n in D["motion-easing"]]
out += ["};", "",
        "// The curve of a token; under reduced motion, its reduced curve if it has one.",
        "constexpr Easing GetEasing(MotionEasing token, bool reduced) {",
        "  switch (token) {"]
for name in D["motion-easing"]:
    const = "kMotionEasing" + camel(name)

    def form(c, kind):
        return f"Easing{{false, {c}, nullptr, 0}}" if kind[0] == "bezier" else \
            f"Easing{{true, {{}}, {c}Samples, {c}SampleCount}}"
    base = form(const, kinds[name][0])
    body = base if len(kinds[name]) == 1 else f"reduced ? {form(const + 'Reduced', kinds[name][1])} : {base}"
    out.append(f"    case MotionEasing::k{camel(name)}:\n      return {body};")
out += ["  }", "  return Easing{false, kMotionEasingLinear, nullptr, 0};", "}", ""]

# ---- colours and shadows --------------------------------------------------------------------------------------
out.append("// color")
for name, t in D["color"].items():
    const = "kColor" + camel(name)
    if "value" in t:
        out.append(f"inline constexpr SkColor {const} = {sk_color(t['value'])};")
    else:
        out.append(f"inline constexpr SkColor {const}Light = {sk_color(t['light'])};")
        out.append(f"inline constexpr SkColor {const}Dark = {sk_color(t['dark'])};")
out += ["", "// shadow"]
for name, t in D["shadow"].items():
    const = "kShadow" + camel(name)
    if "value" in t:
        out.append(f"inline constexpr Shadow {const} = {shadow(t['value'])};")
    else:
        out.append(f"inline constexpr Shadow {const}Light = {shadow(t['light'])};")
        out.append(f"inline constexpr Shadow {const}Dark = {shadow(t['dark'])};")
out.append("")

# ---- theme tables ---------------------------------------------------------------------------------------------
themes = D["theme"]
theme_names = list(themes)
plain_keys = [k for k in next(iter(themes.values())) if not k.startswith("_") and "." not in k]
override_keys = []
for th in themes.values():
    for k in th:
        if "." in k and k not in override_keys:
            override_keys.append(k)
for th_name, th in themes.items():
    mine = [k for k in th if not k.startswith("_") and "." not in k]
    if mine != plain_keys:
        raise SystemExit(f"gen_tokens_cc.py: theme {th_name} has different plain keys than the first theme")
for k in override_keys:
    g, n = k.split(".", 1)
    if g not in ("color", "shadow", "size") or n not in D[g]:
        raise SystemExit(f"gen_tokens_cc.py: override key {k} names no color, shadow or size token")

# (field, C++ type) in declaration order, and a resolver (theme, mode) -> expression.
fields = []


def add(fname, ctype, resolver):
    fields.append((fname, ctype, resolver))


for name in D["color"]:
    add(field("color." + name), "SkColor",
        lambda th, mode, name=name: sk_color(per_mode(th.get("color." + name, D["color"][name]), mode)))
for name in D["shadow"]:
    add(field("shadow." + name), "Shadow",
        lambda th, mode, name=name: shadow(per_mode(th.get("shadow." + name, D["shadow"][name]), mode)))
for k in override_keys:
    g, n = k.split(".", 1)
    if g == "size":
        add(field(k), "int", lambda th, mode, n=n: str(int(per_mode(th.get("size." + n, D["size"][n]), mode))))


def plain(key, th, mode):
    t = th[key]
    return per_mode(t, mode), t.get("unit", "")


def float_kind(key):
    for th in themes.values():
        for mode in th["_modes"]:
            v, unit = plain(key, th, mode)
            if unit == "%" or not is_int(v):
                return True
    return False


GRADIENT = re.compile(r"linear-gradient\((\d+)deg,var\(--ws-g1\),var\(--ws-g2\),var\(--ws-g3,var\(--ws-g2\)\)\)")
ENUMS = {"StripFill": ["Hue", "Gradient"], "SwitcherFill": ["Accent", "Hue"]}
for key in plain_keys:
    f = field("theme-" + key)
    sample = plain(key, next(iter(themes.values())), next(iter(themes.values()))["_modes"][0])
    if key == "strip-fill":
        def fill(th, mode):
            v, _ = plain("strip-fill", th, mode)
            m = GRADIENT.fullmatch(v)
            if m:
                return "StripFill::kGradient", m.group(1)
            if v == "var(--ws-hue)":
                return "StripFill::kHue", "0"
            raise SystemExit(f"gen_tokens_cc.py: unsupported strip-fill '{v}'")
        add(f, "StripFill", lambda th, mode, fill=fill: fill(th, mode)[0])
        add(f + "_angle", "int", lambda th, mode, fill=fill: fill(th, mode)[1])
    elif key == "switcher-fill":
        def sw(th, mode):
            v, _ = plain("switcher-fill", th, mode)
            if v not in ("var(--ws-accent)", "var(--ws-hue)"):
                raise SystemExit(f"gen_tokens_cc.py: unsupported switcher-fill '{v}'")
            return "SwitcherFill::kAccent" if v == "var(--ws-accent)" else "SwitcherFill::kHue"
        add(f, "SwitcherFill", lambda th, mode, sw=sw: sw(th, mode))
    elif key == "card-border":
        def border(th, mode):
            v, _ = plain("card-border", th, mode)
            m = re.fullmatch(r"(\d+)px solid (.*)", v)
            if not m:
                raise SystemExit(f"gen_tokens_cc.py: unsupported card-border '{v}'")
            return m.group(1), sk_color(m.group(2))
        add(f + "_width", "int", lambda th, mode, border=border: border(th, mode)[0])
        add(f + "_color", "SkColor", lambda th, mode, border=border: border(th, mode)[1])
    elif isinstance(sample[0], str):
        add(f, "SkColor", lambda th, mode, key=key: sk_color(plain(key, th, mode)[0]))
    elif sample[1] == "%" or float_kind(key):
        def frac(th, mode, key=key):
            v, unit = plain(key, th, mode)
            return num(round(v / 100, 6) if unit == "%" else v, True)
        add(f, "float", frac)
    else:
        add(f, "int", lambda th, mode, key=key: str(plain(key, th, mode)[0]))

out.append("enum class StripFill { " + ", ".join("k" + n for n in ENUMS["StripFill"]) + " };")
out.append("enum class SwitcherFill { " + ", ".join("k" + n for n in ENUMS["SwitcherFill"]) + " };")
out.append("")
out.append("enum class ThemeId {")
out += [f"  k{camel(n)}," for n in theme_names]
out += ["};", "", "enum class ColorMode { kLight, kDark };", "",
        f"inline constexpr int kThemeCount = {len(theme_names)};",
        "// The persisted ids, indexed by ThemeId.",
        "inline constexpr const char* kThemeIds[kThemeCount] = {"]
out += [f"    {json.dumps(n)}," for n in theme_names]
out += ["};", "", "// Every token a theme can change, resolved for one theme and colour mode: the theme's override when it has one,",
        "// the base token otherwise. Plain theme keys are the theme_* fields.", "struct ThemeTokens {"]
out += [f"  {ct} {fn};" for fn, ct, _ in fields]
out += ["};", "", "struct ThemeEntry {", "  ThemeId theme;", "  ColorMode mode;", "  ThemeTokens tokens;", "};", "",
        "// A theme without a light mode (OLED) has no light entry; GetThemeTokens falls back to Clean there.",
        "inline constexpr ThemeEntry kThemeTable[] = {"]
for th_name, th in themes.items():
    for mode in th["_modes"]:
        out.append(f"    {{ThemeId::k{camel(th_name)}, ColorMode::k{camel(mode)}, {{")
        for fn, _, resolver in fields:
            out.append(f"        .{fn} = {resolver(th, mode)},")
        out.append("    }},")
out += ["};", "",
        "constexpr bool ThemeSupportsMode(ThemeId theme, ColorMode mode) {",
        "  for (const ThemeEntry& e : kThemeTable) {",
        "    if (e.theme == theme && e.mode == mode) {",
        "      return true;",
        "    }",
        "  }",
        "  return false;",
        "}", "",
        "constexpr const ThemeTokens& GetThemeTokens(ThemeId theme, ColorMode mode) {",
        "  if (!ThemeSupportsMode(theme, mode)) {",
        "    theme = ThemeId::kClean;",
        "  }",
        "  for (const ThemeEntry& e : kThemeTable) {",
        "    if (e.theme == theme && e.mode == mode) {",
        "      return e.tokens;",
        "    }",
        "  }",
        "  return kThemeTable[0].tokens;",
        "}", ""]

# ---- Workspace presets ----------------------------------------------------------------------------------------
presets = list(dict.fromkeys(k.split("-")[0] for k in D["workspace"]))
for key in D["workspace"]:
    if not re.fullmatch(r"[a-z]+-(stop-[123]|accent)", key):
        raise SystemExit(f"gen_tokens_cc.py: unsupported workspace token {key}")
out += ["enum class WorkspacePresetId {"] + [f"  k{camel(p)}," for p in presets] + ["};", "",
        f"inline constexpr int kWorkspacePresetCount = {len(presets)};",
        "inline constexpr const char* kWorkspacePresetIds[kWorkspacePresetCount] = {"]
out += [f"    {json.dumps(p)}," for p in presets]
out += ["};", "", "// Gradient stops (stop_count of them are used) and the accent in each colour mode.",
        "struct WorkspacePreset {", "  int stop_count;", "  SkColor stops[3];", "  SkColor accent_light;",
        "  SkColor accent_dark;", "};", "",
        "inline constexpr WorkspacePreset kWorkspacePresets[kWorkspacePresetCount] = {"]
for p in presets:
    stops = [D["workspace"][f"{p}-stop-{i}"]["value"] for i in (1, 2, 3) if f"{p}-stop-{i}" in D["workspace"]]
    filler = ["#000000"] * (3 - len(stops))
    acc = D["workspace"][f"{p}-accent"]
    out.append(f"    {{/* {p} */ {len(stops)}, {{" + ", ".join(sk_color(s) for s in stops + filler) + "}, "
               + f"{sk_color(per_mode(acc, 'light'))}, {sk_color(per_mode(acc, 'dark'))}}},")
out += ["};", "", "}  // namespace aerie::tokens", "", "#endif  // COMPONENTS_AERIE_DESIGN_TOKENS_AERIE_TOKENS_H_", ""]
args.out.write_text("\n".join(out))
