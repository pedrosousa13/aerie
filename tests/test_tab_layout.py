"""Tab layout setting (issue #186): source checks for what the C++ tests cannot see: the Settings row with its three
choices, the pref name in every place that spells it, the strings, the patches and their series entries, and that no
endpoint was added. The C++ is built and run by `./aerie test-cpp`."""

import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
COMPONENT = REPO / "overlay" / "components" / "aerie_sidebar"
VIEWS = REPO / "overlay" / "chrome" / "browser" / "ui" / "views" / "aerie_sidebar"
SETTINGS = REPO / "overlay" / "chrome" / "browser" / "resources" / "settings" / "appearance_page"
PATCHES = REPO / "patches" / "sidebar"
SERIES = [l.strip() for l in (REPO / "patches" / "series").read_text().splitlines()
          if l.strip() and not l.startswith("#")]
GRDP = (VIEWS / "aerie_sidebar_strings.grdp").read_text()
SETTINGS_PATCH = next(PATCHES.glob("0057-*.patch"))
ROW_HTML = (SETTINGS / "aerie_layout_row.html.ts").read_text()


class SettingsRowTest(unittest.TestCase):
    def test_the_three_layouts_are_the_three_radio_buttons_in_the_order_of_the_issue(self):
        names = re.findall(r'name="([^"]+)"', ROW_HTML)
        self.assertEqual(names, ["sidebar", "horizontal", "horizontal-one-row"])

    def test_the_radio_values_are_the_pref_values_of_the_header(self):
        header = (COMPONENT / "aerie_sidebar_prefs.h").read_text()
        self.assertIn('kLayoutSidebar[] = "sidebar"', header)
        self.assertIn('kLayoutHorizontal[] = "horizontal"', header)
        self.assertIn('kLayoutHorizontalOneRow[] = "horizontal-one-row"', header)

    def test_the_pref_name_is_the_same_in_the_header_the_allowlist_and_the_row(self):
        header = (COMPONENT / "aerie_sidebar_prefs.h").read_text()
        self.assertIn('kLayoutPref[] = "aerie.sidebar.layout"', header)
        self.assertIn('(*s_allowlist)["aerie.sidebar.layout"] = settings_api::PrefType::kString',
                      SETTINGS_PATCH.read_text())
        self.assertEqual(ROW_HTML.count('pref-key="aerie.sidebar.layout"'), 4)

    def test_the_group_has_a_label_for_screen_readers(self):
        self.assertIn('group-aria-label="$i18n{aerieSidebarLayout}"', ROW_HTML)
        self.assertIn("<settings-radio-group", ROW_HTML)

    def test_the_row_is_in_the_sidebar_section_and_in_the_build(self):
        text = SETTINGS_PATCH.read_text()
        self.assertIn("<settings-aerie-layout-row></settings-aerie-layout-row>", text)
        self.assertIn("import './aerie_layout_row.js';", text)
        self.assertIn('"appearance_page/aerie_layout_row.html.ts"', text)
        self.assertIn('"appearance_page/aerie_layout_row.ts"', text)
        self.assertIn("customElements.define(SettingsAerieLayoutRowElement.is",
                      (SETTINGS / "aerie_layout_row.ts").read_text())


class StringsTest(unittest.TestCase):
    NAMES = ["LAYOUT", "LAYOUT_DESC", "LAYOUT_SIDEBAR", "LAYOUT_TWO_ROWS", "LAYOUT_ONE_ROW"]

    def test_every_settings_string_is_in_the_grd_and_handed_to_the_page(self):
        provider = SETTINGS_PATCH.read_text()
        for name in self.NAMES:
            full = f"IDS_SETTINGS_AERIE_SIDEBAR_{name}"
            self.assertIn(f'name="{full}"', GRDP)
            self.assertIn(full, provider)

    def test_the_choices_are_worded_as_the_issue_says(self):
        for words in ("Tab layout", "Sidebar", "Horizontal (two rows)", "Horizontal (one row)"):
            self.assertRegex(GRDP, rf">\s*{re.escape(words)}\s*<")

    def test_every_i18n_key_of_the_row_is_handed_to_the_page(self):
        provider = SETTINGS_PATCH.read_text()
        for key in set(re.findall(r"\$i18n\{(\w+)\}", ROW_HTML)):
            self.assertIn(f'"{key}"', provider)


class PatchTest(unittest.TestCase):
    def test_the_patches_are_in_the_series_in_order_with_the_three_headers(self):
        ours = sorted(p for p in PATCHES.glob("00*.patch") if 51 <= int(p.name[:4]) <= 58)
        self.assertEqual(len(ours), 8)
        indexes = [SERIES.index(p.relative_to(REPO / "patches").as_posix()) for p in ours]
        self.assertEqual(indexes, sorted(indexes))
        for patch in ours:
            text = patch.read_text()
            for header in ("Aerie-Purpose:", "Aerie-Upstream-Area:", "Aerie-Kind:"):
                self.assertIn(header, text, patch.name)

    def test_the_helium_layout_follows_the_pref_through_the_lock(self):
        text = next(PATCHES.glob("0051-*.patch")).read_text()
        self.assertIn("RegisterLayoutLockStore", text)
        self.assertIn("FollowTabLayout", text)

    def test_horizontal_tab_sizes_come_from_the_tokens(self):
        text = next(PATCHES.glob("0054-*.patch")).read_text()
        for token in ("kSizeHzTabMax", "kSizeHzTabMin", "kSizeHzTabSelectedMin", "kSizeHzFlareRadius"):
            self.assertIn(token, text)
        tokens = (REPO / "overlay" / "components" / "aerie_design_tokens" / "aerie_tokens.h")
        if tokens.exists():
            for token in ("kSizeHzTabMax", "kSizeHzTabMin"):
                self.assertIn(token, tokens.read_text())

    def test_the_motion_hook_is_for_the_horizontal_orientation_only(self):
        text = next(PATCHES.glob("0055-*.patch")).read_text()
        self.assertIn("TabStripOrientation::kHorizontal", text)
        self.assertIn("AnimateOpen", text)
        self.assertIn("AnimateClose", text)

    def test_the_motion_code_animates_only_transform_and_opacity_through_the_helper(self):
        src = (REPO / "overlay" / "chrome" / "browser" / "ui" / "views" / "aerie_hz_tabs"
               / "aerie_hz_tab_motion.cc").read_text()
        self.assertIn("AnimateLayer", src)
        self.assertNotIn("PrefersReducedMotion", src)
        self.assertNotIn("ShouldRenderRichAnimation", src)


class StripImplementationTest(unittest.TestCase):
    def test_the_horizontal_layouts_use_the_tab_collection_strip(self):
        text = next(PATCHES.glob("0058-*.patch")).read_text()
        self.assertIn("+BASE_FEATURE(kTabStripUnification, base::FEATURE_ENABLED_BY_DEFAULT);", text)


class NetworkTest(unittest.TestCase):
    def test_the_new_code_makes_no_network_request(self):
        roots = [REPO / "overlay" / "chrome" / "browser" / "ui" / "views" / "aerie_hz_tabs",
                 COMPONENT / "aerie_sidebar_hz_policy.cc",
                 REPO / "overlay" / "components" / "aerie_tab_model" / "horizontal_order.cc",
                 REPO / "overlay" / "chrome" / "browser" / "aerie_tab_model" / "horizontal_order_mirror.cc"]
        files = []
        for r in roots:
            files += list(r.glob("*")) if r.is_dir() else [r]
        for f in files:
            text = f.read_text()
            for needle in ("https://", "http://", "URLLoader", "SimpleURLLoader"):
                self.assertNotIn(needle, text, f.name)


if __name__ == "__main__":
    unittest.main()
