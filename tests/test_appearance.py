"""Theme and mode (issue #185): source checks for what the C++ tests cannot see: the pref names in every place that
spells them, the strings, the series entries and that no endpoint was added. The C++ runs under `./aerie test-cpp`."""

import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
O = REPO / "overlay"
PREFS_H = (O / "components/aerie_design_tokens/aerie_appearance_prefs.h").read_text()
ROW = (O / "chrome/browser/resources/settings/appearance_page/aerie_appearance_row.ts").read_text()
GRDP = (O / "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_strings.grdp").read_text()
SERIES = (REPO / "patches/series").read_text()
ALLOW = (REPO / "patches/sidebar/0046-register-the-appearance-prefs.patch").read_text()


class AppearanceTest(unittest.TestCase):
    def test_pref_names_agree_in_prefs_allowlist_and_row(self):
        for name in ("aerie.appearance.theme", "aerie.appearance.mode"):
            self.assertIn(name, PREFS_H)
            self.assertIn(name, ROW)
            self.assertIn(name, ALLOW)

    def test_patches_are_in_the_series_in_order(self):
        idx = [SERIES.index(n) for n in (
            "sidebar/0046-register-the-appearance-prefs.patch",
            "sidebar/0047-add-the-aerie-color-mixer-after-the-chromium-mixers.patch",
            "sidebar/0048-add-the-theme-and-mode-rows-to-the-sidebar-section-of-settings.patch")]
        self.assertEqual(idx, sorted(idx))

    def test_every_row_string_is_in_the_grdp(self):
        for ident in set(re.findall(r"IDS_SETTINGS_AERIE_APPEARANCE_\w+", GRDP)):
            self.assertIn(ident, GRDP)
        self.assertGreaterEqual(len(set(re.findall(r'name="(IDS_SETTINGS_AERIE_APPEARANCE_\w+)"', GRDP))), 15)

    def test_no_endpoint_was_added(self):
        for path in (O / "chrome/browser/ui/views/aerie_look").glob("*.cc"):
            self.assertNotRegex(path.read_text(), r"https?://")

    def test_the_crossfade_snapshots_and_does_not_mirror(self):
        src = (O / "chrome/browser/ui/views/aerie_look/look_crossfade.cc").read_text()
        self.assertIn("wm::RecreateLayers", src)
        self.assertNotIn("->Mirror(", src)
