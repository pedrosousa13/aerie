"""Pinned tabs and Favorites (issue #180): source checks for what the C++ tests cannot see: the two patches and their
series entries, the strings in a grd, the pref and its allowlist entry, the new motion token, the build wiring, the
absence of a network request and the docs. The C++ is built and run by `./aerie test-cpp` (aerie_tab_model_unittests,
aerie_sidebar_unittests, aerie_browser_tests and aerie_interactive_ui_tests)."""

import json
import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TAB_MODEL = REPO / "overlay" / "components" / "aerie_tab_model"
SERVICE = REPO / "overlay" / "chrome" / "browser" / "aerie_tab_model"
VIEWS = REPO / "overlay" / "chrome" / "browser" / "ui" / "views" / "aerie_sidebar"
RESOURCES = REPO / "overlay" / "chrome" / "browser" / "resources" / "settings" / "appearance_page"
CLOSE_PATCH = REPO / "patches" / "sidebar" / "0036-close-pinned-tabs-by-the-pinned-close-setting.patch"
ROW_PATCH = REPO / "patches" / "sidebar" / "0037-add-the-pinned-close-row-to-the-sidebar-section-of-settings.patch"
GRDP = (VIEWS / "aerie_sidebar_strings.grdp").read_text()
SERIES = [l.strip() for l in (REPO / "patches" / "series").read_text().splitlines() if l.strip() and not l.startswith("#")]


def code(path: Path) -> str:
    return re.sub(r"//[^\n]*", "", path.read_text())


class PatchesTest(unittest.TestCase):
    def test_both_patches_are_in_the_series_after_the_sleep_row(self):
        sleep = SERIES.index("sidebar/0035-add-the-sleep-row-to-the-sidebar-section-of-settings.patch")
        for patch in (CLOSE_PATCH, ROW_PATCH):
            name = patch.relative_to(REPO / "patches").as_posix()
            self.assertIn(name, SERIES)
            self.assertGreater(SERIES.index(name), sleep)

    def test_each_patch_has_the_three_aerie_headers(self):
        for patch in (CLOSE_PATCH, ROW_PATCH):
            text = patch.read_text()
            for header in ("Aerie-Purpose:", "Aerie-Upstream-Area:", "Aerie-Kind:"):
                self.assertIn(header, text, patch.name)

    def test_ctrl_w_goes_through_the_one_hook_after_the_active_tab_check(self):
        text = CLOSE_PATCH.read_text()
        self.assertEqual(text.count("aerie_tab_model::HandlePinnedClose(active_tab)"), 1)
        self.assertIn("aerie_tab_model/pinned_close_hook.h", text)
        self.assertIn("//chrome/browser/aerie_tab_model:pinned_close_hook", text)


class PrefTest(unittest.TestCase):
    PREFS = (TAB_MODEL / "tab_model_prefs.h").read_text()

    def constant(self, name):
        return re.search(rf'{name}\[\] = "([^"]+)"', self.PREFS).group(1)

    def test_the_allowlist_literal_is_the_pref_constant_and_a_string(self):
        name = self.constant("kPinnedClose")
        self.assertEqual(name, "aerie.tabs.pinned_close")
        self.assertIn(f'(*s_allowlist)["{name}"] = settings_api::PrefType::kString;', ROW_PATCH.read_text())

    def test_the_default_is_reset_and_sleep_and_the_three_values_are_the_options(self):
        self.assertEqual(self.constant("kDefaultPinnedClose"), "reset-sleep-switch")
        values = [self.constant(n) for n in ("kPinnedCloseResetSleepSwitch", "kPinnedCloseSleep", "kPinnedCloseClose")]
        self.assertEqual(values, ["reset-sleep-switch", "sleep", "close"])
        ts = (RESOURCES / "aerie_pinned_close_row.ts").read_text()
        for value in values:
            self.assertIn(f"'{value}'", ts)
        html = (RESOURCES / "aerie_pinned_close_row.html.ts").read_text()
        self.assertIn('pref-key="aerie.tabs.pinned_close"', html)

    def test_every_word_of_the_row_is_a_grd_string_that_the_patch_hands_to_the_page(self):
        names = {"aeriePinnedClose": "IDS_SETTINGS_AERIE_PINNED_CLOSE", "aeriePinnedCloseDesc": "IDS_SETTINGS_AERIE_PINNED_CLOSE_DESC",
                 "aeriePinnedCloseResetSleepSwitch": "IDS_SETTINGS_AERIE_PINNED_CLOSE_RESET_SLEEP_SWITCH",
                 "aeriePinnedCloseSleep": "IDS_SETTINGS_AERIE_PINNED_CLOSE_SLEEP", "aeriePinnedCloseClose": "IDS_SETTINGS_AERIE_PINNED_CLOSE_CLOSE"}
        patch = ROW_PATCH.read_text()
        for key, ids in names.items():
            self.assertIn(f'name="{ids}"', GRDP)
            self.assertRegex(patch, r'\+\s+\{"' + key + r'",[\s+]+' + ids + r'\},')


class StringsTest(unittest.TestCase):
    def test_every_string_the_views_use_is_in_the_grd(self):
        used = set()
        for name in ("aerie_sidebar_view.cc", "aerie_sidebar_row_view.cc", "aerie_sidebar_tile_view.cc", "aerie_sidebar_context_menu.cc"):
            used |= set(re.findall(r"IDS_AERIE_SIDEBAR_\w+", (VIEWS / name).read_text()))
        for needed in ("IDS_AERIE_SIDEBAR_FAVORITES", "IDS_AERIE_SIDEBAR_PINNED", "IDS_AERIE_SIDEBAR_BACK_TO_PINNED",
                       "IDS_AERIE_SIDEBAR_TILE_ASLEEP", "IDS_AERIE_SIDEBAR_FAVORITES_FULL", "IDS_AERIE_SIDEBAR_PIN_TAB"):
            self.assertIn(needed, used)
        for name in used:
            self.assertIn(f'name="{name}"', GRDP)

    def test_no_user_facing_text_is_typed_in_the_views(self):
        for name in ("aerie_sidebar_tile_view.cc", "aerie_sidebar_context_menu.cc"):
            literals = re.findall(r'u?"([^"\n]*)"', code(VIEWS / name))
            for text in literals:
                self.assertFalse(re.search(r"[A-Za-z]{4,} [A-Za-z]{3,}", text), f"{name}: {text}")


class TokenTest(unittest.TestCase):
    def test_the_menu_scale_is_a_token_of_ninety_seven_hundredths_with_a_css_twin(self):
        tokens = json.loads((REPO / "overlay" / "components" / "aerie_design_tokens" / "tokens.json").read_text())
        self.assertEqual(tokens["motion-scale"]["menu-enter"]["value"], 0.97)
        css = (REPO / "prototypes" / "sidebar" / "tokens.css").read_text()
        self.assertIn("--aerie-motion-scale-menu-enter: 0.97;", css)
        self.assertIn("kMotionScaleMenuEnter", code(TAB_MODEL.parent / "aerie_sidebar" / "aerie_sidebar_motion.cc"))


class WiringTest(unittest.TestCase):
    def test_the_hook_header_is_a_leaf_target_so_that_no_cycle_forms(self):
        gn = (SERVICE / "BUILD.gn").read_text()
        self.assertIn('source_set("pinned_close_hook")', gn)
        header = (SERVICE / "pinned_close_hook.h").read_text()
        self.assertEqual(re.findall(r'#include "([^"]+)"', header), [])

    def test_the_sources_and_tests_are_listed(self):
        views = (VIEWS / "BUILD.gn").read_text()
        for name in ("aerie_sidebar_tile_view.cc", "aerie_sidebar_pinned_browsertest.cc", "aerie_sidebar_pinned_uitest.cc"):
            self.assertIn(f'"{name}"', views)
        model = (TAB_MODEL / "BUILD.gn").read_text()
        for name in ("pinned_tabs.cc", "pinned_tabs_unittest.cc"):
            self.assertIn(f'"{name}"', model)
        self.assertIn('"pinned_tabs_controller.cc"', (SERVICE / "BUILD.gn").read_text())

    def test_nothing_in_the_new_files_sets_the_chromium_pinned_bit_or_reaches_the_network(self):
        for path in (SERVICE / "pinned_tabs_controller.cc", SERVICE / "tab_strip_adapter.cc", TAB_MODEL / "pinned_tabs.cc"):
            text = code(path)
            self.assertNotIn("SetTabPinned", text, path.name)
            self.assertNotIn("services/network", text, path.name)

    def test_the_decisions_are_written_down(self):
        doc = (REPO / "docs" / "sidebar-design.md").read_text()
        self.assertIn("## Pinned tabs and Favorites", doc)
        section = doc.split("## Pinned tabs and Favorites")[1].split("\n## ")[0]
        for phrase in ("Back to pinned page", "12", "never auto-archived", "reset-sleep-switch"):
            self.assertIn(phrase.lower(), section.lower())


if __name__ == "__main__":
    unittest.main()
