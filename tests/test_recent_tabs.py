"""Ctrl+Tab and the recent tabs overlay (issue #269): source checks for what the C++ tests cannot see: the Settings
patch and its series entry, the pref, its allowlist entry and default, the strings, the three tokens and the docs.
The C++ is built and run by `./aerie test-cpp` (aerie_sidebar_unittests for the order, aerie_interactive_ui_tests for
the keys and the overlay)."""

import json
import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SIDEBAR = REPO / "overlay" / "components" / "aerie_sidebar"
VIEWS = REPO / "overlay" / "chrome" / "browser" / "ui" / "views" / "aerie_sidebar"
ROW = REPO / "overlay" / "chrome" / "browser" / "resources" / "settings" / "appearance_page"
PATCH = REPO / "patches" / "sidebar" / "0057-add-the-recent-tabs-row-to-the-sidebar-section-of-settings.patch"
SERIES = [l.strip() for l in (REPO / "patches" / "series").read_text().splitlines() if l.strip() and not l.startswith("#")]
TOKENS = json.loads((REPO / "overlay" / "components" / "aerie_design_tokens" / "tokens.json").read_text())
PREF = "aerie.sidebar.recent_tab_switcher"


class SettingsSwitchTest(unittest.TestCase):
    def test_the_patch_is_in_the_series_after_the_links_open_row(self):
        name = PATCH.relative_to(REPO / "patches").as_posix()
        self.assertIn(name, SERIES)
        self.assertGreater(SERIES.index(name), SERIES.index("sidebar/0056-turn-on-automatic-picture-in-picture-by-default.patch"))

    def test_the_pref_name_is_the_same_in_the_header_the_patch_and_the_row(self):
        self.assertIn(f'kRecentTabSwitcherPref[] =\n    "{PREF}"', (SIDEBAR / "aerie_sidebar_prefs.h").read_text())
        self.assertIn(f'+  (*s_allowlist)["{PREF}"] =\n+      settings_api::PrefType::kBoolean;', PATCH.read_text())
        self.assertIn(f'pref-key="{PREF}"', (ROW / "aerie_recent_tabs_row.html.ts").read_text())

    def test_the_pref_defaults_to_on(self):
        self.assertRegex((SIDEBAR / "aerie_sidebar_prefs.cc").read_text(), r"RegisterBooleanPref\(kRecentTabSwitcherPref,\s*true\)")

    def test_the_row_says_ctrl_tab_cycles_recent_tabs(self):
        self.assertRegex((VIEWS / "aerie_sidebar_strings.grdp").read_text(),
                         r'IDS_SETTINGS_AERIE_RECENT_TABS".*?>\s*Ctrl\+Tab cycles recent tabs\s*</message>')


class TokensTest(unittest.TestCase):
    def test_the_switcher_sizes_are_tokens(self):
        for name in ("switcher-card-width", "switcher-snapshot-height", "switcher-max-cards"):
            self.assertIn(name, TOKENS["size"])

    def test_the_overlay_has_no_pixel_literals_or_colours(self):
        source = (VIEWS / "aerie_sidebar_recent_tabs_overlay.cc").read_text()
        self.assertNotRegex(source, r"0x[0-9a-fA-F]{6}|SK_Color[A-Z]|SkColorSetARGB|SkColorSetRGB")


class PrivacyTest(unittest.TestCase):
    def test_a_snapshot_is_only_asked_for_behind_the_private_tab_gate(self):
        source = (VIEWS / "aerie_sidebar_recent_tabs.cc").read_text()
        self.assertIn("IsPrivateTab(tab)", source)
        # Nothing writes a snapshot: no file API in the controller or the overlay.
        for name in ("aerie_sidebar_recent_tabs.cc", "aerie_sidebar_recent_tabs_overlay.cc"):
            text = (VIEWS / name).read_text()
            self.assertNotRegex(text, r"WriteFile|base::File\b|ImportantFileWriter|SavePNG|EncodePNG")
        self.assertIn("#259", (VIEWS / "aerie_sidebar_tab_snapshot.cc").read_text())
        self.assertIn("aerie_sidebar::WatchTabSnapshot", source)


class DocsTest(unittest.TestCase):
    def test_the_design_doc_has_the_section_the_shortcut_and_the_motion(self):
        doc = (REPO / "docs" / "sidebar-design.md").read_text()
        self.assertIn("## Recent tabs, as built (#269)", doc)
        self.assertIn("| Ctrl+Tab, Ctrl+Shift+Tab |", doc)
        self.assertIn("| Recent tabs overlay |", doc)


if __name__ == "__main__":
    unittest.main()
