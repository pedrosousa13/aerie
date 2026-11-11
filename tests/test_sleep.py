"""Putting inactive tabs to sleep (issue #179): source checks for what the C++ tests cannot see: the patch and its
series entry, the strings in a grd, the settings row, the single timer, the lazy service and the absence of a network
request. The C++ is built and run by `./aerie test-cpp` (aerie_tab_model_unittests, aerie_browser_tests and
aerie_interactive_ui_tests)."""

import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TAB_MODEL = REPO / "overlay" / "components" / "aerie_tab_model"
SERVICE = REPO / "overlay" / "chrome" / "browser" / "aerie_tab_model"
VIEWS = REPO / "overlay" / "chrome" / "browser" / "ui" / "views" / "aerie_sidebar"
RESOURCES = REPO / "overlay" / "chrome" / "browser" / "resources" / "settings" / "appearance_page"
PATCH = REPO / "patches" / "sidebar" / "0035-add-the-sleep-row-to-the-sidebar-section-of-settings.patch"
GRDP = (VIEWS / "aerie_sidebar_strings.grdp").read_text()
SERIES = [l.strip() for l in (REPO / "patches" / "series").read_text().splitlines() if l.strip() and not l.startswith("#")]


def code(path: Path) -> str:
    """The file without its comments."""
    text = path.read_text()
    return re.sub(r"//[^\n]*", "", text)


class SettingsRowTest(unittest.TestCase):
    def test_the_patch_is_in_the_series_after_the_sidebar_section_exists(self):
        name = PATCH.relative_to(REPO / "patches").as_posix()
        self.assertIn(name, SERIES)
        self.assertGreater(SERIES.index(name), SERIES.index("sidebar/0022-show-an-empty-sidebar-section-in-settings.patch"))

    def test_the_row_is_a_dropdown_on_the_sleep_pref_with_the_seven_options(self):
        html = (RESOURCES / "aerie_sleep_row.html.ts").read_text()
        ts = (RESOURCES / "aerie_sleep_row.ts").read_text()
        self.assertIn('pref-key="aerie.tabs.sleep_after_minutes"', html)
        values = [int(v) for v in re.findall(r"\{value: (\d+),", ts)]
        self.assertEqual(values, [15, 30, 60, 120, 240, 480, 0])
        prefs = (TAB_MODEL / "tab_model_prefs.h").read_text()
        self.assertIn("aerie.tabs.sleep_after_minutes", prefs)

    def test_every_word_of_the_row_is_a_grd_string_that_the_patch_hands_to_the_page(self):
        names = {"aerieSleepAfter": "IDS_SETTINGS_AERIE_SLEEP_AFTER", "aerieSleepAfterDesc": "IDS_SETTINGS_AERIE_SLEEP_AFTER_DESC",
                 "aerieSleepAfter15Min": "IDS_SETTINGS_AERIE_SLEEP_15_MIN", "aerieSleepAfter30Min": "IDS_SETTINGS_AERIE_SLEEP_30_MIN",
                 "aerieSleepAfter1Hour": "IDS_SETTINGS_AERIE_SLEEP_1_HOUR", "aerieSleepAfter2Hours": "IDS_SETTINGS_AERIE_SLEEP_2_HOURS",
                 "aerieSleepAfter4Hours": "IDS_SETTINGS_AERIE_SLEEP_4_HOURS", "aerieSleepAfter8Hours": "IDS_SETTINGS_AERIE_SLEEP_8_HOURS",
                 "aerieSleepAfterNever": "IDS_SETTINGS_AERIE_SLEEP_NEVER"}
        patch = PATCH.read_text()
        ts = (RESOURCES / "aerie_sleep_row.ts").read_text() + (RESOURCES / "aerie_sleep_row.html.ts").read_text()
        for key, ids in names.items():
            self.assertIn(f'name="{ids}"', GRDP)
            self.assertIn(f'+      {{"{key}", {ids}}},', patch)
            self.assertIn(key, ts)

    def test_the_default_is_one_hour_and_the_options_span_the_pref_range(self):
        prefs = (TAB_MODEL / "tab_model_prefs.h").read_text()
        self.assertIn("kDefaultSleepAfterMinutes = 60", prefs)
        self.assertIn("kMinSleepAfterMinutes = 15", prefs)
        self.assertIn("kMaxSleepAfterMinutes = 8 * 60", prefs)


class SidebarStringsTest(unittest.TestCase):
    def test_every_sleep_string_the_view_uses_is_in_the_grd(self):
        used = set()
        for path in (VIEWS / "aerie_sidebar_view.cc", VIEWS / "aerie_sidebar_row_view.cc", VIEWS / "aerie_sidebar_context_menu.cc"):
            used |= set(re.findall(r"IDS_AERIE_SIDEBAR_(?:SLEEP\w*|ROW_ASLEEP_NAME)", path.read_text()))
        self.assertTrue(used)
        for name in used:
            self.assertIn(f'name="{name}"', GRDP)

    def test_the_row_name_ends_with_asleep_and_the_header_tooltip_says_estimate(self):
        self.assertRegex(GRDP, r'IDS_AERIE_SIDEBAR_ROW_ASLEEP_NAME[^>]*>\s*<ph name="TITLE">\$1<ex>[^<]*</ex></ph>, asleep\s*</message>')
        self.assertRegex(GRDP, r'IDS_AERIE_SIDEBAR_SLEEPING_TOOLTIP[^>]*>\s*Estimate')

    def test_no_user_facing_text_is_typed_in_the_sleep_views(self):
        for name in ("aerie_sidebar_context_menu.cc", "aerie_sidebar_row_view.cc"):
            text = code(VIEWS / name)
            literals = " ".join(re.findall(r'u?"[^"\n]*"', text))
            self.assertNotIn("asleep", literals.lower(), name)
            self.assertNotIn("Sleep tab", literals)
        view_literals = " ".join(re.findall(r'u?"[^"\n]*"', code(VIEWS / "aerie_sidebar_view.cc")))
        self.assertNotIn("stimate", view_literals)


class SchedulerTest(unittest.TestCase):
    def test_the_schedule_owns_one_timer_and_no_poll_loop(self):
        header = (TAB_MODEL / "sleep_schedule.h").read_text()
        source = code(TAB_MODEL / "sleep_schedule.cc")
        self.assertEqual(len(re.findall(r"base::OneShotTimer\s+\w+_;", header)), 1)
        self.assertNotIn("RepeatingTimer", header + source)
        self.assertNotIn("PostDelayedTask", source)

    def test_the_controller_has_no_timer_of_its_own(self):
        text = (SERVICE / "sleep_controller.h").read_text() + code(SERVICE / "sleep_controller.cc")
        for forbidden in ("OneShotTimer", "RepeatingTimer", "PostDelayedTask", "WallClockTimer"):
            self.assertNotIn(forbidden, text)

    def test_the_controller_is_created_lazily_and_not_with_the_profile(self):
        factory = code(SERVICE / "sleep_controller_factory.cc")
        self.assertRegex(factory, r"ServiceIsCreatedWithBrowserContext\(\) const \{\s*(?:\n)?\s*return false;")
        # The sidebar asks for it a few seconds after its window has opened.
        view = code(VIEWS / "aerie_sidebar_view.cc")
        self.assertRegex(view, r"PostDelayedTask\([^;]*SleepControllerFactory::GetForProfile", )

    def test_sleeping_is_chromiums_discard_and_there_is_no_second_unloader(self):
        text = code(SERVICE / "sleep_controller.cc")
        self.assertIn("user_tuning::DiscardPage", text)
        self.assertNotIn("kMemorySaverModeTimeBeforeDiscardInMinutes", text)
        self.assertNotIn("->Discard(", text)

    def test_the_rounding_is_to_ten_megabytes(self):
        self.assertIn("10 * kMb", code(TAB_MODEL / "sleep_summary.cc"))


class NoNetworkTest(unittest.TestCase):
    def test_the_sleep_code_touches_no_network(self):
        for path in list(TAB_MODEL.glob("sleep_*")) + list(SERVICE.glob("sleep_*")) + [VIEWS / "aerie_sidebar_context_menu.cc"]:
            text = code(path)
            for forbidden in ("net/", "network", "http://", "https://", "URLLoader", "SimpleURLLoader"):
                if path.name.endswith("browsertest.cc") or path.name.endswith("unittest.cc"):
                    continue
                self.assertNotIn(forbidden, text, path.name)


class BuildWiringTest(unittest.TestCase):
    def test_the_new_tests_are_in_their_targets(self):
        self.assertIn('"sleep_controller_browsertest.cc"', (SERVICE / "BUILD.gn").read_text())
        self.assertIn('"//chrome/browser/aerie_tab_model:browser_tests"', (REPO / "overlay/chrome/test/aerie/BUILD.gn").read_text())
        views = (VIEWS / "BUILD.gn").read_text()
        for name in ("aerie_sidebar_sleep_browsertest.cc", "aerie_sidebar_sleep_uitest.cc", "aerie_sidebar_context_menu.cc"):
            self.assertIn(f'"{name}"', views)
        gn = (TAB_MODEL / "BUILD.gn").read_text()
        for name in ("sleep_schedule_unittest.cc", "sleep_summary_unittest.cc", "sleep_schedule.cc", "sleep_summary.cc"):
            self.assertIn(f'"{name}"', gn)

    def test_the_moon_badge_is_a_vector_icon_in_the_sidebar_folder(self):
        icon = VIEWS / "vector_icons" / "aerie_sleep_badge.icon"
        self.assertTrue(icon.is_file())
        self.assertIn('"aerie_sleep_badge.icon"', (VIEWS / "vector_icons" / "BUILD.gn").read_text())
        self.assertIn("//chrome/browser/ui/views/aerie_sidebar/vector_icons", (VIEWS / "BUILD.gn").read_text())

    def test_the_network_endpoint_list_is_unchanged_by_this_issue(self):
        # docs/network-endpoints.md lists every request the browser makes; sleeping makes none.
        text = (REPO / "docs" / "network-endpoints.md").read_text().lower()
        self.assertNotIn("sleep", text)


if __name__ == "__main__":
    unittest.main()
