"""Design Tokens (issue #174, ADR 0007): one tokens.json, two generated outputs, a contrast gate.

These tests run in the PR gate's unittest step. They stand in for the CI "generated file differs" check, the
contrast check, and the comparison of the C++ theme tables with the CSS blocks.
"""

import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
COMPONENT = REPO / "overlay" / "components" / "aerie_design_tokens"
PROTOTYPE = REPO / "prototypes" / "sidebar"
TOKENS = COMPONENT / "tokens.json"
GEN_CSS = COMPONENT / "gen_tokens_css.py"
GEN_CC = COMPONENT / "gen_tokens_cc.py"
CHECK = COMPONENT / "check_contrast.py"
HEADER_CSS = "/* Generated from tokens.json. Do not edit. */"
HEADER_CC = "// Generated from tokens.json. Do not edit."


def run(*argv, check=True):
    proc = subprocess.run([sys.executable, *map(str, argv)], capture_output=True, text=True)
    if check and proc.returncode != 0:
        raise AssertionError(f"{argv} exited {proc.returncode}:\n{proc.stdout}\n{proc.stderr}")
    return proc


def generate(script, tokens=TOKENS, extra=()):
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "out"
        run(script, "--tokens", tokens, "--out", out, *extra)
        return out.read_text()


def camel(name):
    return "".join(part[:1].upper() + part[1:] for part in name.split("-"))


class SourceLayoutTest(unittest.TestCase):
    def test_the_source_and_both_generators_live_in_the_component(self):
        for path in (TOKENS, GEN_CSS, GEN_CC, CHECK):
            self.assertTrue(path.is_file(), path)

    def test_the_prototype_reads_the_component_copy(self):
        link = PROTOTYPE / "tokens.json"
        self.assertEqual(link.resolve(), TOKENS.resolve())
        for gone in ("gen_tokens_css.py", "check_contrast.py"):
            self.assertFalse((PROTOTYPE / gone).exists(), gone)


class ContrastGateTest(unittest.TestCase):
    def test_every_theme_mode_and_preset_passes(self):
        proc = run(CHECK, check=False)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)

    def test_a_token_changed_to_a_failing_value_trips_the_gate(self):
        data = json.loads(TOKENS.read_text())
        data["theme"]["vivid"]["text"]["light"] = "#FFFFFF"  # white text on the light frame
        with tempfile.TemporaryDirectory() as tmp:
            bad = Path(tmp) / "tokens.json"
            bad.write_text(json.dumps(data))
            proc = run(CHECK, "--tokens", bad, check=False)
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("FAIL", proc.stdout)


class GeneratedOutputTest(unittest.TestCase):
    def test_both_generators_are_deterministic(self):
        for script in (GEN_CSS, GEN_CC):
            self.assertEqual(generate(script), generate(script), script.name)

    def test_generated_files_start_with_the_do_not_edit_header(self):
        self.assertEqual(generate(GEN_CSS).splitlines()[0], HEADER_CSS)
        self.assertEqual(generate(GEN_CC).splitlines()[0], HEADER_CC)

    def test_the_checked_in_prototype_css_is_what_the_generator_makes(self):
        fresh = generate(GEN_CSS, extra=("--check-horizontal", PROTOTYPE / "horizontal.css"))
        self.assertEqual((PROTOTYPE / "tokens.css").read_text(), fresh,
                         "prototypes/sidebar/tokens.css is stale: run gen_tokens_css.py (see the README there)")

    def test_a_stale_prototype_css_would_be_caught(self):
        data = json.loads(TOKENS.read_text())
        data["radius"]["xs"]["value"] = 5
        with tempfile.TemporaryDirectory() as tmp:
            changed = Path(tmp) / "tokens.json"
            changed.write_text(json.dumps(data))
            self.assertNotEqual((PROTOTYPE / "tokens.css").read_text(), generate(GEN_CSS, changed))

    def test_the_generator_fails_when_horizontal_css_drifts_from_the_icon_only_size(self):
        with tempfile.TemporaryDirectory() as tmp:
            css = Path(tmp) / "horizontal.css"
            css.write_text("/* no container queries */")
            proc = run(GEN_CSS, "--tokens", TOKENS, "--out", Path(tmp) / "o", "--check-horizontal", css, check=False)
        self.assertNotEqual(proc.returncode, 0)

    def test_the_header_needs_no_parser_and_no_allocation(self):
        text = generate(GEN_CC)
        for banned in ("<string>", "<vector>", "<map>", "base/json", "std::string", "std::vector"):
            self.assertNotIn(banned, text)
        self.assertNotIn("static const", text)
        self.assertIn("namespace aerie::tokens", text)

    def test_the_generators_run_in_under_a_second(self):
        import time
        for script in (GEN_CSS, GEN_CC):
            start = time.monotonic()
            generate(script)
            self.assertLess(time.monotonic() - start, 1.0, script.name)


class FlatTokenNamesTest(unittest.TestCase):
    """Names are persisted later: they must not drift."""

    def setUp(self):
        self.data = json.loads(TOKENS.read_text())
        self.cc = generate(GEN_CC)

    def test_constants_exist_for_the_documented_examples(self):
        self.assertRegex(self.cc, r"inline constexpr int kSizeSidebarWidth = 260;")
        self.assertRegex(self.cc, r"inline constexpr int kMotionDurationFast = 140;")
        self.assertRegex(self.cc, r"inline constexpr int kMotionDurationFastReduced = 90;")

    def test_every_numeric_flat_token_has_a_constant(self):
        for group in ("size", "radius", "space", "type", "motion-duration", "motion-distance", "motion-scale",
                      "motion-opacity", "motion-timing", "motion-ratio", "motion-spring", "tint"):
            for name, tok in self.data[group].items():
                if isinstance(tok.get("value"), str):
                    continue
                const = "k" + camel(group) + camel(name)
                self.assertRegex(self.cc, rf"\bk{re.escape(const[1:])} = ", const)
                if "reduced" in tok:
                    self.assertRegex(self.cc, rf"\b{re.escape(const)}Reduced = ", const + "Reduced")

    def test_every_reduced_value_is_in_the_css_too(self):
        css = generate(GEN_CSS)
        reduced = re.findall(r"@media \(prefers-reduced-motion: reduce\) \{\n(.*?)\n\}\n", css, re.S)[0]
        attr = css.split(":root[data-reduced-motion] {")[1]
        for group, toks in self.data.items():
            if group.startswith("_") or group == "theme":
                continue
            for name, tok in toks.items():
                if "reduced" in tok:
                    var = f"--aerie-{group}-{name}:"
                    self.assertIn(var, reduced, var)
                    self.assertIn(var, attr, var)

    def test_easings_are_four_floats_and_spring_samples_match_the_css_curve(self):
        std = self.data["motion-easing"]["standard"]["value"]
        nums = [float(x) for x in re.findall(r"-?\d*\.?\d+", std.split("(")[1])]
        self.assertRegex(self.cc, r"kMotionEasingStandard\{" + r",\s*".join(
            re.escape(f"{n!r}f" if n != int(n) else f"{int(n)}.0f") for n in nums) + r"\}")
        spring = [float(x) for x in re.findall(r"-?\d*\.?\d+", self.data["motion-easing"]["spring"]["value"][len("linear("):])]
        block = re.search(r"kMotionEasingSpringSamples\[\] = \{(.*?)\};", self.cc, re.S).group(1)
        got = [float(x.rstrip("f")) for x in re.findall(r"[-\d.]+f", block)]
        self.assertEqual(got, spring)
        self.assertEqual(len(got), 23)
        for name in ("Stiffness", "Damping", "Mass"):
            self.assertIn(f"kMotionSpring{name} = ", self.cc)


# ---- the C++ theme tables against the CSS blocks -------------------------------------------------------------------

def parse_css_blocks(css):
    return [(sel.strip(), dict(re.findall(r"(--[\w-]+): (.*?);", body)))
            for sel, body in re.findall(r"^(:[^{]*)\{\n(.*?)\n\}", css, re.M | re.S)]


def css_values(css, theme, mode):
    """The cascade a page sees: :root, then the colour-mode block, then the active theme's block."""
    resolved = {}
    for sel, values in parse_css_blocks(css):
        if sel == ":root":
            resolved.update(values)
    for sel, values in parse_css_blocks(css):
        if sel.startswith(":root[data-mode=") and f'data-mode="{mode}"' in sel.split(",")[0] or \
                (mode == "light" and sel == ':root[data-mode="light"], :root:not([data-mode])'):
            resolved.update(values)
    for sel, values in parse_css_blocks(css):
        first = sel.split(",")[0]
        if first.startswith(f':root[data-theme="{theme}"]') and (f'[data-mode="{mode}"]' in first or
                                                                  (mode == "light" and ":is(" in first)):
            resolved.update(values)
    return resolved


def color(text):
    text = text.strip()
    m = re.fullmatch(r"#([0-9A-Fa-f]{2})([0-9A-Fa-f]{2})([0-9A-Fa-f]{2})", text)
    if m:
        return (255, *(int(g, 16) for g in m.groups()))
    m = re.fullmatch(r"rgba\((\d+),(\d+),(\d+),([\d.]+)\)", text)
    if m:
        r, g, b, a = m.groups()
        return (int(float(a) * 255 + 0.5), int(r), int(g), int(b))
    if text == "transparent":
        return (0, 0, 0, 0)
    raise AssertionError(f"not a colour: {text}")


def cpp_color(text):
    m = re.fullmatch(r"SkColorSetRGB\((.*)\)", text.strip())
    if m:
        return (255, *(int(x, 0) for x in m.group(1).split(",")))
    m = re.fullmatch(r"SkColorSetARGB\((.*)\)", text.strip())
    if m:
        return tuple(int(x, 0) for x in m.group(1).split(","))
    raise AssertionError(f"not a C++ colour: {text}")


def css_shadow(text):
    if text.strip() == "none":
        return []
    layers = []
    for part in re.findall(r"(?:[^,(]|\([^)]*\))+", text):
        part = part.strip()
        col = re.search(r"(#[0-9A-Fa-f]{6}|rgba\([^)]*\))$", part).group(1)
        nums = [int(float(n.rstrip("px"))) for n in re.findall(r"-?[\d.]+(?:px)?", part[:-len(col)])]
        nums += [0] * (4 - len(nums))
        layers.append((*nums, color(col)))
    return layers


def cpp_shadow(text):
    count = int(re.match(r"\{(\d+),", text).group(1))
    layers = [(*(int(x) for x in m[:4]), cpp_color(m[4])) for m in re.findall(
        r"\{(-?\d+), (-?\d+), (-?\d+), (-?\d+), (SkColorSetA?RGB\([^)]*\))\}", text)]
    assert len(layers) == count, text
    return layers


def theme_header_entries(cc):
    entries = {}
    for theme, mode, body in re.findall(r"\{ThemeId::(\w+), ColorMode::(\w+), \{\n(.*?)\n    \}\},", cc, re.S):
        entries[(theme[1:], mode[1:])] = dict(re.findall(r"\.(\w+) = (.*),$", body, re.M))
    return entries


class ThemeTablesMatchCssTest(unittest.TestCase):
    def setUp(self):
        self.data = json.loads(TOKENS.read_text())
        self.css = generate(GEN_CSS)
        self.cc = generate(GEN_CC)
        self.entries = theme_header_entries(self.cc)

    def test_there_is_a_table_for_every_theme_and_supported_mode(self):
        want = {(camel(t), camel(m)) for t, th in self.data["theme"].items() for m in th["_modes"]}
        self.assertEqual(set(self.entries), want)
        self.assertEqual(len(want), 11)  # six themes, OLED dark only

    def test_every_table_value_equals_what_the_css_cascade_gives_a_page(self):
        checked = 0
        for (theme_c, mode_c), fields in self.entries.items():
            theme = next(t for t in self.data["theme"] if camel(t) == theme_c)
            mode = mode_c.lower()
            css = css_values(self.css, theme, mode)
            for field, text in fields.items():
                var = "--aerie-" + field.replace("_", "-")
                if field.startswith("theme_card_border_"):
                    continue  # split in two fields; checked below
                if field in ("theme_strip_fill", "theme_strip_fill_angle", "theme_switcher_fill"):
                    continue  # enums; checked below
                self.assertIn(var, css, f"{theme}/{mode}: {var}")
                want = css[var]
                if field.startswith("color_") or field in ("theme_accent_base", "theme_selected_ring_color",
                                                           "theme_text", "theme_text_muted"):
                    self.assertEqual(cpp_color(text), color(want), f"{theme}/{mode}: {field}")
                elif field.startswith("shadow_"):
                    self.assertEqual(cpp_shadow(text), css_shadow(want), f"{theme}/{mode}: {field}")
                elif want.endswith("%"):
                    self.assertAlmostEqual(float(text.rstrip("f")), float(want[:-1]) / 100, places=6, msg=field)
                else:
                    self.assertAlmostEqual(float(text.rstrip("f")), float(want.rstrip("px")), places=6,
                                           msg=f"{theme}/{mode}: {field}")
                checked += 1
        self.assertGreater(checked, 11 * 25)

    def test_borders_and_enums_match_the_css(self):
        for (theme_c, mode_c), fields in self.entries.items():
            theme = next(t for t in self.data["theme"] if camel(t) == theme_c)
            css = css_values(self.css, theme, mode_c.lower())
            width, _style, col = re.fullmatch(r"(\d+)px (solid) (.*)", css["--aerie-theme-card-border"]).groups()
            self.assertEqual(int(fields["theme_card_border_width"]), int(width))
            self.assertEqual(cpp_color(fields["theme_card_border_color"]), color(col))
            fill = css["--aerie-theme-strip-fill"]
            if fill.startswith("linear-gradient"):
                self.assertEqual(fields["theme_strip_fill"], "StripFill::kGradient")
                self.assertEqual(int(fields["theme_strip_fill_angle"]), int(re.search(r"(\d+)deg", fill).group(1)))
            else:
                self.assertEqual(fill, "var(--ws-hue)")
                self.assertEqual(fields["theme_strip_fill"], "StripFill::kHue")
            sw = css["--aerie-theme-switcher-fill"]
            self.assertEqual(fields["theme_switcher_fill"],
                             {"var(--ws-accent)": "SwitcherFill::kAccent", "var(--ws-hue)": "SwitcherFill::kHue"}[sw])

    def test_an_override_key_replaces_that_token_only_while_its_theme_is_active(self):
        oled = self.entries[("Oled", "Dark")]
        vivid = self.entries[("Vivid", "Dark")]
        self.assertEqual(cpp_color(oled["color_text"]), color(self.data["theme"]["oled"]["color.text"]["dark"]))
        self.assertEqual(cpp_color(vivid["color_text"]), color(self.data["color"]["text"]["dark"]))
        self.assertEqual(oled["shadow_card"], "{0, {}}")  # shadow.card is "none" in OLED
        self.assertEqual(self.entries[("HighContrast", "Light")]["size_focus_ring_width"],
                         str(self.data["theme"]["high-contrast"]["size.focus-ring-width"]["value"]))
        self.assertEqual(vivid["size_focus_ring_width"], str(self.data["size"]["focus-ring-width"]["value"]))


class WorkspacePresetTablesTest(unittest.TestCase):
    def test_every_preset_matches_the_css(self):
        data = json.loads(TOKENS.read_text())
        css = generate(GEN_CSS)
        cc = generate(GEN_CC)
        rows = re.findall(r"/\* (\w+) \*/ (\d), \{(.*?)\}, (SkColorSetRGB\([^)]*\)), (SkColorSetRGB\([^)]*\))\},?\n", cc)
        self.assertEqual([r[0] for r in rows], sorted({k.split("-")[0] for k in data["workspace"]}, key=list(
            dict.fromkeys(k.split("-")[0] for k in data["workspace"])).index))
        self.assertEqual(len(rows), 8)
        light, dark = css_values(css, "vivid", "light"), css_values(css, "vivid", "dark")
        for name, count, stops, acc_l, acc_d in rows:
            want = [color(light[f"--aerie-workspace-{name}-stop-{i}"]) for i in (1, 2, 3)
                    if f"--aerie-workspace-{name}-stop-{i}" in light]
            self.assertEqual(int(count), len(want), name)
            got = [cpp_color(s) for s in re.findall(r"SkColorSetRGB\([^)]*\)", stops)][:len(want)]
            self.assertEqual(got, want, name)
            self.assertEqual(cpp_color(acc_l), color(light[f"--aerie-workspace-{name}-accent"]), name)
            self.assertEqual(cpp_color(acc_d), color(dark[f"--aerie-workspace-{name}-accent"]), name)


class SettingsRowTest(unittest.TestCase):
    """The reduced-motion pref is one name in four places; the row's strings are in the grdp."""

    def setUp(self):
        self.prefs_h = (COMPONENT / "aerie_motion_prefs.h").read_text()
        self.patches = {p.name: p.read_text() for p in (REPO / "patches" / "sidebar").glob("*.patch")}
        self.row_html = (REPO / "overlay/chrome/browser/resources/settings/appearance_page/aerie_motion_row.html.ts").read_text()

    def test_the_pref_name_is_the_same_in_the_header_the_allowlist_and_the_row(self):
        name = re.search(r'kReduceMotionPref\[\] = "([^"]+)"', self.prefs_h).group(1)
        self.assertEqual(name, "aerie.motion.reduce")
        self.assertIn(f'(*s_allowlist)["{name}"] = settings_api::PrefType::kString;',
                      self.patches["0001-add-the-reduced-motion-pref.patch"])
        self.assertIn(f'pref-key="{name}"', self.row_html)

    def test_the_two_values_are_the_row_options(self):
        ts = (REPO / "overlay/chrome/browser/resources/settings/appearance_page/aerie_motion_row.ts").read_text()
        for const in ("kReduceMotionSystem", "kReduceMotionOn"):
            value = re.search(const + r'\[\] = "([^"]+)"', self.prefs_h).group(1)
            self.assertIn(f"value: '{value}'", ts)

    def test_every_string_the_row_uses_is_in_the_grdp_and_handed_to_the_page(self):
        grdp = (COMPONENT / "aerie_motion_strings.grdp").read_text()
        provider = self.patches["0002-add-the-reduced-motion-strings.patch"]
        ts = (REPO / "overlay/chrome/browser/resources/settings/appearance_page/aerie_motion_row.ts").read_text()
        used = set(re.findall(r"\$i18n\{(\w+)\}", self.row_html)) | set(re.findall(r"getString\('(\w+)'\)", ts))
        self.assertEqual(len(used), 4)
        for key in used:
            ids = re.findall(r'\{"%s", (IDS_\w+)\}' % key, provider)
            self.assertEqual(len(ids), 1, key)
            self.assertIn(f'<message name="{ids[0]}"', grdp)

    def test_the_row_is_on_the_appearance_page_and_in_the_build(self):
        html = self.patches["0003-add-the-reduced-motion-row.patch"]
        self.assertIn("+    <settings-aerie-motion-row></settings-aerie-motion-row>", html)
        self.assertIn('+    "appearance_page/aerie_motion_row.ts",', html)
        self.assertIn("+import './aerie_motion_row.js';", html)
        series = (REPO / "patches" / "series").read_text()
        for name in self.patches:
            self.assertIn(f"sidebar/{name}", series)


class GuardPanelTokensTest(unittest.TestCase):
    def setUp(self):
        self.dir = REPO / "overlay/chrome/browser/resources/aerie_guard"
        self.css = (self.dir / "app.css").read_text()

    def test_the_panel_loads_the_generated_css_through_the_build(self):
        self.assertIn('<link rel="stylesheet" href="aerie_tokens.css">', (self.dir / "aerie_guard.html").read_text())
        build = (self.dir / "BUILD.gn").read_text()
        self.assertIn('"//components/aerie_design_tokens:webui_grdp"', build)
        self.assertIn("aerie_tokens_resources.grdp", build)
        self.assertIn('input_files = [ "aerie_tokens.css" ]', (COMPONENT / "BUILD.gn").read_text())

    def test_the_panel_has_no_colour_of_its_own(self):
        for text in (self.css, (self.dir / "aerie_guard.html").read_text()):
            self.assertNotRegex(text, r"#[0-9A-Fa-f]{3,8}\b")
            self.assertNotRegex(text, r"\brgba?\(")
            self.assertNotIn("color-sys", text)

    def test_every_token_the_panel_reads_exists(self):
        defined = set(re.findall(r"(--aerie-[\w-]+):", generate(GEN_CSS)))
        used = set(re.findall(r"var\((--aerie-[\w-]+)", self.css))
        self.assertTrue(used)
        self.assertEqual(sorted(used - defined), [])

    def test_the_panel_sets_the_colour_mode_the_tokens_select_on(self):
        self.assertIn("dataset['mode']", (self.dir / "app.ts").read_text())
        self.assertIn(':root[data-mode="dark"]', generate(GEN_CSS))


if __name__ == "__main__":
    unittest.main()
