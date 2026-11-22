"""The tab preview card (issue #270): source checks for what the C++ tests cannot see: the Settings patch and its series
entry, the pref, its allowlist entry and default, the strings, the two tokens, the one privacy gate and the docs.
The C++ is built and run by `./aerie test-cpp` (aerie_sidebar_unittests for the placement and the text,
aerie_interactive_ui_tests for the delay, the places, the keyboard, the snapshot and the switch)."""

import json
import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SIDEBAR = REPO / "overlay" / "components" / "aerie_sidebar"
VIEWS = REPO / "overlay" / "chrome" / "browser" / "ui" / "views" / "aerie_sidebar"
ROW = REPO / "overlay" / "chrome" / "browser" / "resources" / "settings" / "appearance_page"
PATCH = REPO / "patches" / "sidebar" / "0058-add-the-tab-preview-card-row-to-the-sidebar-section-of-settings.patch"
SERIES = [l.strip() for l in (REPO / "patches" / "series").read_text().splitlines() if l.strip() and not l.startswith("#")]
TOKENS = json.loads((REPO / "overlay" / "components" / "aerie_design_tokens" / "tokens.json").read_text())
PREF = "aerie.sidebar.hover_card_snapshots"


class SettingsSwitchTest(unittest.TestCase):
    def test_the_patch_is_in_the_series_after_the_recent_tabs_row(self):
        name = PATCH.relative_to(REPO / "patches").as_posix()
        self.assertIn(name, SERIES)
        self.assertGreater(SERIES.index(name), SERIES.index("sidebar/0057-add-the-recent-tabs-row-to-the-sidebar-section-of-settings.patch"))

    def test_the_pref_name_is_the_same_in_the_header_the_patch_and_the_row(self):
        self.assertIn(f'kHoverCardSnapshotsPref[] =\n    "{PREF}"', (SIDEBAR / "aerie_sidebar_prefs.h").read_text())
        self.assertIn(f'+  (*s_allowlist)["{PREF}"] =\n+      settings_api::PrefType::kBoolean;', PATCH.read_text())
        self.assertIn(f'pref-key="{PREF}"', (ROW / "aerie_preview_card_row.html.ts").read_text())

    def test_the_pref_defaults_to_on(self):
        self.assertRegex((SIDEBAR / "aerie_sidebar_prefs.cc").read_text(), r"RegisterBooleanPref\(kHoverCardSnapshotsPref,\s*true\)")

    def test_the_row_says_show_page_previews_on_tab_cards(self):
        self.assertRegex((VIEWS / "aerie_sidebar_strings.grdp").read_text(),
                         r'IDS_SETTINGS_AERIE_PREVIEW_CARD".*?>\s*Show page previews on tab cards\s*</message>')

    def test_the_patch_hands_the_strings_to_the_page(self):
        text = PATCH.read_text()
        for name in ("IDS_SETTINGS_AERIE_PREVIEW_CARD", "IDS_SETTINGS_AERIE_PREVIEW_CARD_DESC"):
            self.assertIn(name, text)
            self.assertIn(name, (VIEWS / "aerie_sidebar_strings.grdp").read_text())


class TokensTest(unittest.TestCase):
    def test_the_card_sizes_are_tokens(self):
        for name in ("hover-card-width", "hover-card-snapshot-height"):
            self.assertIn(name, TOKENS["size"])

    def test_the_card_has_no_pixel_literals_or_colours(self):
        for name in ("aerie_sidebar_hover_card_view.cc", "aerie_sidebar_hover_card.cc"):
            source = (VIEWS / name).read_text()
            self.assertNotRegex(source, r"0x[0-9a-fA-F]{6}|SK_Color[A-Z]|SkColorSetARGB|SkColorSetRGB")

    def test_the_delay_is_the_tooltip_token(self):
        self.assertIn("kMotionTimingTooltipDelay", (VIEWS / "aerie_sidebar_hover_card.cc").read_text())


class PrivacyTest(unittest.TestCase):
    def test_both_cards_take_snapshots_through_the_one_gate(self):
        snapshot = (VIEWS / "aerie_sidebar_tab_snapshot.cc").read_text()
        self.assertIn("IsPrivateTab(tab)", snapshot)
        self.assertIn("#259", snapshot)
        self.assertIn("WatchTabSnapshot", (VIEWS / "aerie_sidebar_hover_card.cc").read_text())
        self.assertIn("WatchTabSnapshot", (VIEWS / "aerie_sidebar_recent_tabs.cc").read_text())

    def test_nothing_writes_a_snapshot(self):
        for name in ("aerie_sidebar_hover_card.cc", "aerie_sidebar_hover_card_view.cc", "aerie_sidebar_tab_snapshot.cc"):
            self.assertNotRegex((VIEWS / name).read_text(), r"WriteFile|base::File\b|ImportantFileWriter|SavePNG|EncodePNG")

    def test_the_card_checks_the_gate_before_asking_and_before_showing(self):
        source = (VIEWS / "aerie_sidebar_hover_card.cc").read_text()
        self.assertGreaterEqual(len(re.findall(r"IsPrivateTab\(tab\)", source)), 2)


class DocsTest(unittest.TestCase):
    def test_the_design_doc_has_the_section_and_the_motion_row(self):
        doc = (REPO / "docs" / "sidebar-design.md").read_text()
        self.assertIn("## Tab preview card, as built (#270)", doc)
        self.assertIn("| Tab preview card |", doc)

    def test_the_glossary_names_the_card(self):
        self.assertIn("**Tab preview card**", (REPO / "GLOSSARY.md").read_text())


if __name__ == "__main__":
    unittest.main()
