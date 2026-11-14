"""Sidebar density, side and auto-hide (issue #184): source checks for what the C++ tests cannot see: the patches and
their series entries, the pref names in every place that spells them, the Settings rows and strings, the one timer and
the one repeating poll, and that no endpoint was added. The C++ is built and run by `./aerie test-cpp`
(aerie_sidebar_unittests, aerie_browser_tests and aerie_interactive_ui_tests)."""

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

STATE_PATCH = PATCHES / "0042-follow-the-sidebar-side-and-density-in-the-vertical-tab-strip-state.patch"
REGION_PATCH = PATCHES / "0043-float-the-auto-hidden-sidebar-and-fix-the-rail-width-in-the-layout.patch"
SETTINGS_PATCH = PATCHES / "0044-add-the-density-and-side-rows-to-the-sidebar-section-of-settings.patch"


class PatchTest(unittest.TestCase):
    def test_the_patches_are_in_the_series_in_order_with_the_three_headers(self):
        names = [p.relative_to(REPO / "patches").as_posix() for p in (STATE_PATCH, REGION_PATCH, SETTINGS_PATCH)]
        indexes = [SERIES.index(n) for n in names]
        self.assertEqual(indexes, sorted(indexes))
        self.assertGreater(indexes[0], SERIES.index(
            "sidebar/0041-keep-the-tab-model-record-id-when-a-tab-is-closed.patch"))
        for patch in (STATE_PATCH, REGION_PATCH, SETTINGS_PATCH):
            text = patch.read_text()
            for header in ("Aerie-Purpose:", "Aerie-Upstream-Area:", "Aerie-Kind:"):
                self.assertIn(header, text, patch.name)

    def test_the_side_pref_drives_helium_right_aligned_pref(self):
        text = STATE_PATCH.read_text()
        self.assertIn("SyncAerieSide", text)
        self.assertIn("kHeliumVerticalRightAligned", text)
        self.assertIn("aerie_sidebar::kSidePref", text)

    def test_the_layout_reads_the_floating_width_and_the_region_never_hides_in_auto_hide(self):
        text = REGION_PATCH.read_text()
        self.assertIn("kFloatingWidth", text)
        self.assertIn("AerieAutoHides", text)
        # The rail has no resize area.
        self.assertIn("fixed_width", text)


class PrefTest(unittest.TestCase):
    def test_the_pref_names_are_the_same_in_the_header_the_allowlist_and_the_rows(self):
        header = (COMPONENT / "aerie_sidebar_prefs.h").read_text()
        self.assertIn('kDensityPref[] = "aerie.sidebar.density"', header)
        self.assertIn('kSidePref[] = "aerie.sidebar.side"', header)
        allow = SETTINGS_PATCH.read_text()
        self.assertIn('(*s_allowlist)["aerie.sidebar.density"]', allow)
        self.assertIn('(*s_allowlist)["aerie.sidebar.side"]', allow)
        self.assertIn("aerie.sidebar.density", (SETTINGS / "aerie_density_row.html.ts").read_text())
        self.assertIn("aerie.sidebar.side", (SETTINGS / "aerie_side_row.html.ts").read_text())

    def test_the_defaults_are_comfortable_and_left_and_both_are_registered(self):
        cc = (COMPONENT / "aerie_sidebar_prefs.cc").read_text()
        self.assertRegex(cc, r"kDensityPref,\s*kDensityComfortable")
        self.assertRegex(cc, r"kSidePref,\s*kSideLeft")

    def test_the_four_densities_are_the_four_radio_buttons(self):
        row = (SETTINGS / "aerie_density_row.html.ts").read_text()
        for value in ("comfortable", "compact", "rail", "auto-hide"):
            self.assertIn(f'name="{value}"', row)
        side = (SETTINGS / "aerie_side_row.html.ts").read_text()
        for value in ("left", "right"):
            self.assertIn(f'name="{value}"', side)


class StringsTest(unittest.TestCase):
    def test_every_settings_string_is_in_the_grd_and_handed_to_the_page(self):
        names = ["DENSITY", "DENSITY_DESC", "DENSITY_COMFORTABLE", "DENSITY_COMPACT", "DENSITY_RAIL",
                 "DENSITY_AUTO_HIDE", "SIDE", "SIDE_DESC", "SIDE_LEFT", "SIDE_RIGHT"]
        provider = SETTINGS_PATCH.read_text()
        for name in names:
            full = f"IDS_SETTINGS_AERIE_SIDEBAR_{name}"
            self.assertIn(f'name="{full}"', GRDP)
            self.assertIn(full, provider)

    def test_the_toast_and_guard_button_strings_exist(self):
        for name in ("PEEK_KEPT_OPEN", "PEEK_RELEASED", "GUARD_BUTTON"):
            self.assertIn(f'name="IDS_AERIE_SIDEBAR_{name}"', GRDP)

    def test_every_string_the_views_use_is_in_the_grd(self):
        used = set()
        for path in VIEWS.glob("*.cc"):
            if path.name.endswith(("test.cc", "uitest.cc")):
                continue
            used |= set(re.findall(r"\bIDS_AERIE_SIDEBAR_\w+", path.read_text()))
        declared = set(re.findall(r'name="(IDS_AERIE_SIDEBAR_\w+)"', GRDP))
        self.assertEqual(sorted(used - declared), [])


class TimerTest(unittest.TestCase):
    def test_the_controller_owns_exactly_one_one_shot_timer_and_no_repeating_one(self):
        header = (COMPONENT / "aerie_sidebar_peek.h").read_text()
        source = (COMPONENT / "aerie_sidebar_peek.cc").read_text()
        self.assertEqual(len(re.findall(r"base::OneShotTimer\s+\w+", header)), 1)
        for text in (header, source):
            self.assertNotIn("RepeatingTimer", text)
            self.assertNotIn("PostDelayedTask", text)

    def test_the_only_repeating_timer_is_the_cursor_poll_and_it_is_stopped_when_not_wanted(self):
        found = sorted(p.name for p in list(VIEWS.glob("aerie_sidebar_peek*")) + list(COMPONENT.glob("aerie_sidebar_peek*"))
                       if not p.name.endswith(("test.cc", "uitest.cc")) and "RepeatingTimer" in p.read_text())
        self.assertEqual(found, ["aerie_sidebar_peek_driver.h"])
        driver = (VIEWS / "aerie_sidebar_peek_driver.cc").read_text()
        self.assertIn("WantsCursorPoll()", driver)
        self.assertIn("poll_timer_.Stop()", driver)
        self.assertIn("GetCursorScreenPoint", driver)

    def test_the_slide_moves_only_transform_and_opacity(self):
        driver = (VIEWS / "aerie_sidebar_peek_driver.cc").read_text()
        self.assertIn("PeekShowMotion", driver)
        self.assertIn("AnimateLayer", driver)
        self.assertNotIn("SetBounds", driver)


class NetworkTest(unittest.TestCase):
    def test_no_new_network_code_in_the_slice(self):
        for path in list(COMPONENT.glob("aerie_sidebar_density*")) + list(COMPONENT.glob("aerie_sidebar_peek*")) + \
                list(VIEWS.glob("aerie_sidebar_peek*")):
            text = path.read_text()
            for needle in ("net::", "URLLoader", "GURL(\"http", "SimpleURLLoader"):
                self.assertNotIn(needle, text, path.name)


class ZenTest(unittest.TestCase):
    def test_helium_zen_mode_stays_locked_off(self):
        # The lock is ADR 0008 and `AerieSidebarBrowserTest.TheHeliumLayoutPrefsAreLocked` tests it; nothing in this
        # slice writes the Zen pref.
        for path in list(COMPONENT.glob("aerie_sidebar_density*")) + list(COMPONENT.glob("aerie_sidebar_peek*")) + \
                list(VIEWS.glob("aerie_sidebar_peek*")):
            self.assertNotIn("kHeliumZenMode", path.read_text(), path.name)
        self.assertNotIn("kHeliumZenMode", STATE_PATCH.read_text() + REGION_PATCH.read_text())


class DocsTest(unittest.TestCase):
    def test_the_design_doc_has_the_section_and_the_archive_popover_note(self):
        text = (REPO / "docs" / "sidebar-design.md").read_text()
        self.assertIn("## Densities, side and auto-hide, as built (issue #184)", text)
        section = text.split("## Densities, side and auto-hide, as built (issue #184)")[1]
        self.assertIn("#183", section)
        self.assertIn("ScopedBusy", section)


if __name__ == "__main__":
    unittest.main()
