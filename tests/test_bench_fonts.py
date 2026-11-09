"""The fonts check (issue #117): judge() against observations, the data the check shares with the C++ code and the
patches (the allowlist, the pref name), and the Bench test fonts. The observations are built here; none of them is
a recording of a real browser."""

import copy
import json
import re
import unittest
from pathlib import Path

from aerie_bench import runner
from aerie_bench.checks.hardening import fingerprint_fonts as ff
from aerie_bench.checks.hardening.fingerprint_fonts import FingerprintingFonts
from aerie_hardening import model

REPO = Path(__file__).resolve().parent.parent
FONTS_DIR = REPO / "overlay" / "components" / "aerie_fonts"
INC = (FONTS_DIR / "allowed_font_families.inc").read_text()
BENCH_FIXTURES = REPO / "tools" / "aerie_bench" / "fixtures"

# The list the maintainer approved in issue #117, written out again here on purpose.
APPROVED = {
    "DejaVu Sans", "DejaVu Serif", "DejaVu Sans Mono", "Liberation Sans", "Liberation Serif", "Liberation Mono",
    "Noto Sans", "Noto Serif", "Noto Sans Mono",
    "Noto Sans CJK SC", "Noto Sans CJK TC", "Noto Sans CJK JP", "Noto Sans CJK KR",
    "Noto Serif CJK SC", "Noto Serif CJK TC", "Noto Serif CJK JP", "Noto Serif CJK KR",
    "Noto Color Emoji", "Cantarell", "Ubuntu", "Ubuntu Mono", "FreeSans", "FreeSerif", "FreeMono",
    "Droid Sans", "Droid Serif", "Droid Sans Mono",
}
ALIASES = {"Arial", "Helvetica", "Times New Roman", "Times", "Courier New", "Courier"}
GENERICS = {"serif", "sans-serif", "monospace", "cursive", "fantasy", "system-ui", "ui-serif", "ui-sans-serif",
            "ui-monospace", "ui-rounded", "emoji", "math", "fangsong"}


def listed():
    body = "\n".join(l for l in INC.splitlines() if not l.lstrip().startswith("//"))
    return re.findall(r'"([^"]+)",', body)


def reading(**over):
    r = {"probe": False, "probe_local": False, "probe_worker": False, "allowed": True, "allowed_local": True,
         "allowed_worker": True, "generic": 1200, "nonlatin": 2000, "nonlatin_base": 2000}
    r.update(over)
    return r


def found(**over):
    """A browser that finds the test font that is not on the list."""
    return reading(probe=True, probe_local=True, probe_worker=True, **over)


def good():
    return {"default": {"a": found()},
            "on": {"a": reading(), "b": reading()},
            "feature_off": {"a": found()}, "global_off": {"a": found()},
            "site_off": {"a": found(), "b": reading()}}


class AllowlistDataTest(unittest.TestCase):
    def test_the_data_file_is_the_list_the_issue_approved(self):
        names = listed()
        self.assertEqual(set(names), APPROVED | ALIASES | GENERICS)
        self.assertEqual(len(names), len(set(names)), "a name is listed twice")

    def test_the_cpp_includes_the_data_file(self):
        cc = (FONTS_DIR / "font_allowlist.cc").read_text()
        self.assertIn('#include "components/aerie_fonts/allowed_font_families.inc"', cc)

    def test_the_unit_test_names_every_approved_font(self):
        text = (FONTS_DIR / "font_allowlist_unittest.cc").read_text()
        for name in APPROVED | ALIASES | GENERICS:
            self.assertIn(f'"{name}"', text, name)


class PrefNameTest(unittest.TestCase):
    """The pref name is written in the C++ header, the settings_private allowlist line and the control table."""

    def test_the_name_is_one_string_everywhere(self):
        header = (FONTS_DIR / "fonts_prefs.h").read_text()
        name = re.search(r'kRestrictFonts\[\] = "([^"]+)"', header).group(1)
        patch = (REPO / "patches" / "hardening" / "0020-restrict-fonts-to-an-allowlist.patch").read_text()
        self.assertIn(f'(*s_allowlist)["{name}"] = settings_api::PrefType::kBoolean;', patch)
        control = next(c for c in model.load()["controls"] if c["id"] == "font_restriction")
        self.assertEqual(control["pref"], {"name": name, "default": False})
        self.assertEqual(control["ui"], {"kind": "toggle", "pref": name})

    def test_the_default_is_off(self):
        self.assertIn("RegisterBooleanPref(kRestrictFonts, false)", (FONTS_DIR / "fonts_prefs.cc").read_text())

    def test_the_bench_seeds_the_same_pref(self):
        self.assertEqual(ff.PREF_ON, {"aerie": {"fonts": {"restrict": True}}})


class PatchTest(unittest.TestCase):
    PATCH = (REPO / "patches" / "hardening" / "0020-restrict-fonts-to-an-allowlist.patch").read_text()

    def test_it_is_in_the_series_after_the_other_hardening_patches(self):
        series = (REPO / "patches" / "series").read_text().splitlines()
        self.assertIn("hardening/0020-restrict-fonts-to-an-allowlist.patch", series)

    def test_the_three_places_that_look_up_a_font_ask_the_restriction(self):
        for needle in ("IsInstalledFamilyHidden(font_family)", "IsLocalFontNameHidden(font_selector_->GetExecutionContext()"):
            self.assertIn(needle, self.PATCH)
        # CSSFontSelector, OffscreenFontSelector (workers) and the shared base for IsPlatformFamilyMatchAvailable.
        self.assertEqual(self.PATCH.count("IsInstalledFamilyHidden(font_family)"), 2)
        self.assertIn("IsInstalledFamilyHidden(passed_family)", self.PATCH)

    def test_the_system_fallback_for_glyphs_and_the_font_cache_are_not_touched(self):
        # FontFallbackIterator and FontCache decide which system font draws a character the listed fonts lack; text in
        # any script must keep rendering. FontFallbackList is touched in one place only: it must not look up a family
        # in the system fonts that the selector hides (the Bench found the probe font through that lookup).
        body = self.PATCH.split("\n--- ", 1)[1]
        for path in ("font_fallback_iterator.cc", "platform/fonts/font_cache.cc", "font_fallback_map.cc"):
            self.assertNotIn(path, body)
        self.assertEqual(body.count("a/third_party/blink/renderer/platform/fonts/font_fallback_list.cc"), 1)
        self.assertIn("!font_selector_->IsInstalledFamilyHidden(*curr_family)", self.PATCH)

    def test_every_new_include_has_its_dep(self):
        # core/BUILD.gn gets the allowlist, chrome/browser and permissions get the pref.
        self.assertIn('+    "//components/aerie_fonts",', self.PATCH)
        self.assertEqual(self.PATCH.count('"//components/aerie_fonts:prefs"'), 2)

    def test_the_restriction_follows_the_token_and_the_pref_only_decides_whether_a_token_is_sent(self):
        self.assertIn("HeliumFontRestrictionEnabled(context)", self.PATCH)
        self.assertIn("feature == blink::HeliumNoiseFeature::kFonts && !restrict_fonts", self.PATCH)

    def test_local_font_access_is_blocked_only_when_there_is_no_saved_choice(self):
        self.assertIn("if (setting != CONTENT_SETTING_ASK) {", self.PATCH)


class JudgeTest(unittest.TestCase):
    def test_pass(self):
        status, detail = FingerprintingFonts.judge(good())
        self.assertEqual(status, "pass", detail)
        self.assertIn("Aerie Bench Probe", detail)

    def test_each_way_to_find_the_unlisted_font_fails_under_the_restriction(self):
        for field in ("probe", "probe_local", "probe_worker"):
            obs = good()
            obs["on"]["a"][field] = True
            status, detail = FingerprintingFonts.judge(obs)
            self.assertEqual(status, "fail", field)
            self.assertIn("not on the list", detail)

    def test_the_other_site_must_stay_restricted_when_one_site_is_off(self):
        obs = good()
        obs["site_off"]["b"] = found()
        status, detail = FingerprintingFonts.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("only a.localhost", detail)

    def test_an_allowlisted_font_must_stay_findable(self):
        for field in ("allowed", "allowed_local", "allowed_worker"):
            obs = good()
            obs["on"]["b"][field] = False
            status, detail = FingerprintingFonts.judge(obs)
            self.assertEqual(status, "fail", field)
            self.assertIn("allowlisted", detail)

    def test_a_generic_family_must_still_have_a_width(self):
        obs = good()
        obs["on"]["a"]["generic"] = 0
        self.assertEqual(FingerprintingFonts.judge(obs)[0], "fail")

    def test_text_in_another_script_must_render_as_it_does_unrestricted(self):
        obs = good()
        obs["on"]["a"]["nonlatin"] = 0
        status, detail = FingerprintingFonts.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("another script", detail)
        obs = good()
        obs["on"]["a"]["nonlatin"] = 2400
        self.assertEqual(FingerprintingFonts.judge(obs)[0], "fail")

    def test_each_control_must_find_the_font_again(self):
        for key, where, text in (("feature_off", "a", "HeliumNoiseFonts"), ("global_off", "a", "whole browser"),
                                 ("site_off", "a", "site")):
            obs = good()
            obs[key][where] = reading()
            status, detail = FingerprintingFonts.judge(obs)
            self.assertEqual(status, "fail", key)
            self.assertIn("negative control failed", detail)
            self.assertIn(text, detail)

    def test_a_browser_without_the_test_fonts_cannot_pass(self):
        obs = good()
        obs["default"]["a"] = reading()
        status, detail = FingerprintingFonts.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("detector failed", detail)

    def test_the_restriction_off_by_default_is_part_of_the_check(self):
        obs = good()
        obs["default"]["a"] = reading()
        self.assertEqual(FingerprintingFonts.judge(obs)[0], "fail")

    def test_a_page_error_fails(self):
        obs = good()
        obs["global_off"]["a"] = {"error": "boom"}
        status, detail = FingerprintingFonts.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("boom", detail)

    def test_judge_does_not_change_its_input(self):
        obs = good()
        before = copy.deepcopy(obs)
        FingerprintingFonts.judge(obs)
        self.assertEqual(obs, before)


class BenchFontsTest(unittest.TestCase):
    def test_the_fixture_page_and_fonts_exist(self):
        self.assertTrue((BENCH_FIXTURES / "hardening" / "fonts.html").exists())
        for name in ("aerie-bench-probe.ttf", "aerie-bench-allowed.ttf", "make_fonts.py"):
            self.assertTrue((BENCH_FIXTURES / "fonts" / name).exists(), name)

    def test_the_probe_font_is_not_on_the_list_and_the_other_is(self):
        self.assertNotIn(ff.PROBE_FAMILY, listed())
        self.assertIn(ff.ALLOWED_FAMILY, listed())

    def test_the_names_in_the_page_are_the_names_of_the_check(self):
        page = (BENCH_FIXTURES / "hardening" / "fonts.html").read_text()
        self.assertIn(ff.PROBE_FAMILY, page)
        self.assertIn(ff.ALLOWED_FAMILY, page)

    def test_the_browser_environment_installs_the_test_fonts_in_the_profile(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            env = runner.browser_env({"PATH": "/bin"}, Path(tmp))
            fonts = Path(env["XDG_DATA_HOME"]) / "fonts"
            self.assertEqual(sorted(p.name for p in fonts.iterdir()), ["aerie-bench-allowed.ttf", "aerie-bench-probe.ttf"])
            self.assertTrue(str(fonts).startswith(tmp))


class RecordedCasesTest(unittest.TestCase):
    def test_the_recorded_file_says_where_its_data_came_from(self):
        doc = json.loads((REPO / "tests" / "fixtures" / "hardening" / "hardening_fingerprinting_fonts.json").read_text())
        self.assertIn("hand-written", doc["source"])
        for case in doc["cases"]:
            self.assertTrue(case["name"].startswith("synthetic_"), case["name"])


if __name__ == "__main__":
    unittest.main()
