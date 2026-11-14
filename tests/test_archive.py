"""Archiving unused Today tabs (issue #183): source checks for what the C++ tests cannot see: the patch and its series
entry, the Settings rows, the strings in a grd, the single timer, the lazy service, the named motion tokens and the
absence of a network request. The C++ is built and run by `./aerie test-cpp` (aerie_tab_model_unittests,
aerie_browser_tests and aerie_interactive_ui_tests)."""

import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TAB_MODEL = REPO / "overlay" / "components" / "aerie_tab_model"
SIDEBAR_COMPONENT = REPO / "overlay" / "components" / "aerie_sidebar"
SERVICE = REPO / "overlay" / "chrome" / "browser" / "aerie_tab_model"
VIEWS = REPO / "overlay" / "chrome" / "browser" / "ui" / "views" / "aerie_sidebar"
RESOURCES = REPO / "overlay" / "chrome" / "browser" / "resources" / "settings" / "appearance_page"
PATCH = REPO / "patches" / "sidebar" / "0042-add-the-archive-rows-to-the-sidebar-section-of-settings.patch"
GRDP = (VIEWS / "aerie_sidebar_strings.grdp").read_text()
SERIES = [l.strip() for l in (REPO / "patches" / "series").read_text().splitlines() if l.strip() and not l.startswith("#")]


def code(path: Path) -> str:
    """The file without its comments."""
    return re.sub(r"//[^\n]*", "", path.read_text())


class SettingsRowsTest(unittest.TestCase):
    def test_the_patch_is_in_the_series_after_the_sidebar_section_and_the_folder_rules_row(self):
        name = PATCH.relative_to(REPO / "patches").as_posix()
        self.assertIn(name, SERIES)
        self.assertGreater(SERIES.index(name), SERIES.index("sidebar/0038-add-the-folder-rules-row-to-the-sidebar-section-of-settings.patch"))

    def test_the_after_row_is_a_dropdown_on_the_same_pref_as_the_panel_with_the_four_values(self):
        html = (RESOURCES / "aerie_archive_after_row.html.ts").read_text()
        ts = (RESOURCES / "aerie_archive_after_row.ts").read_text()
        self.assertIn('pref-key="aerie.tabs.archive_after"', html)
        self.assertEqual(re.findall(r"\{value: '(\w+)',", ts), ["12h", "24h", "7d", "never"])
        self.assertIn('kArchiveAfter[] = "aerie.tabs.archive_after"', (TAB_MODEL / "tab_model_prefs.h").read_text())

    def test_the_keep_row_is_a_dropdown_on_the_retention_pref_with_the_four_values(self):
        html = (RESOURCES / "aerie_archive_keep_row.html.ts").read_text()
        ts = (RESOURCES / "aerie_archive_keep_row.ts").read_text()
        self.assertIn('pref-key="aerie.tabs.archive_keep"', html)
        self.assertEqual(re.findall(r"\{value: '(\w+)',", ts), ["7d", "30d", "90d", "forever"])
        self.assertIn('kArchiveKeep[] = "aerie.tabs.archive_keep"', (TAB_MODEL / "tab_model_prefs.h").read_text())

    def test_the_allowlist_literal_is_the_constant_and_the_after_pref_was_allowed_already(self):
        patch = PATCH.read_text()
        self.assertIn('+  (*s_allowlist)["aerie.tabs.archive_keep"] = settings_api::PrefType::kString;', patch)
        series_0010 = (REPO / "patches/sidebar/0010-register-tab-model-prefs.patch").read_text()
        self.assertIn('(*s_allowlist)["aerie.tabs.archive_after"]', series_0010)

    def test_every_word_of_the_rows_is_a_grd_string_that_the_patch_hands_to_the_page(self):
        names = {
            "aerieArchiveAfter": "IDS_SETTINGS_AERIE_ARCHIVE_AFTER",
            "aerieArchiveAfterDesc": "IDS_SETTINGS_AERIE_ARCHIVE_AFTER_DESC",
            "aerieArchiveAfter12Hours": "IDS_AERIE_SIDEBAR_ARCHIVE_AFTER_12H",
            "aerieArchiveAfter24Hours": "IDS_AERIE_SIDEBAR_ARCHIVE_AFTER_24H",
            "aerieArchiveAfter7Days": "IDS_AERIE_SIDEBAR_ARCHIVE_AFTER_7D",
            "aerieArchiveAfterNever": "IDS_AERIE_SIDEBAR_ARCHIVE_AFTER_NEVER",
            "aerieArchiveKeep": "IDS_SETTINGS_AERIE_ARCHIVE_KEEP",
            "aerieArchiveKeepDesc": "IDS_SETTINGS_AERIE_ARCHIVE_KEEP_DESC",
            "aerieArchiveKeep7Days": "IDS_SETTINGS_AERIE_ARCHIVE_KEEP_7D",
            "aerieArchiveKeep30Days": "IDS_SETTINGS_AERIE_ARCHIVE_KEEP_30D",
            "aerieArchiveKeep90Days": "IDS_SETTINGS_AERIE_ARCHIVE_KEEP_90D",
            "aerieArchiveKeepForever": "IDS_SETTINGS_AERIE_ARCHIVE_KEEP_FOREVER",
        }
        patch = PATCH.read_text()
        ts = "".join((RESOURCES / f"aerie_archive_{k}_row{e}").read_text() for k in ("after", "keep") for e in (".ts", ".html.ts"))
        for key, ids in names.items():
            self.assertIn(f'name="{ids}"', GRDP)
            self.assertIn(f'+      {{"{key}", {ids}}},', patch)
            self.assertIn(key, ts)

    def test_the_rows_are_listed_in_the_settings_build_and_the_layout_page(self):
        patch = PATCH.read_text()
        for tag in ("after", "keep"):
            self.assertIn(f'+    "appearance_page/aerie_archive_{tag}_row.ts",', patch)
            self.assertIn(f"+import './aerie_archive_{tag}_row.js';", patch)
            self.assertIn(f"+  <settings-aerie-archive-{tag}-row></settings-aerie-archive-{tag}-row>", patch)


class StringsTest(unittest.TestCase):
    def test_every_archive_string_the_views_use_is_in_the_grd(self):
        used = set()
        for path in VIEWS.glob("aerie_sidebar_archive_*.cc"):
            used |= set(re.findall(r"IDS_AERIE_SIDEBAR_ARCHIVE_\w+", path.read_text()))
        self.assertTrue(used)
        for name in used:
            self.assertIn(f'name="{name}"', GRDP)

    def test_the_panel_is_named_archive_and_the_restored_line_has_the_title_placeholder(self):
        self.assertRegex(GRDP, r'name="IDS_AERIE_SIDEBAR_ARCHIVE_PANEL"[^>]*>\s*<ph|name="IDS_AERIE_SIDEBAR_ARCHIVE_PANEL"')
        self.assertRegex(GRDP, r"<message name=\"IDS_AERIE_SIDEBAR_ARCHIVE_PANEL\"[^>]*>\s*Archive\s*</message>")
        self.assertRegex(GRDP, r"<message name=\"IDS_AERIE_SIDEBAR_ARCHIVE_RESTORED\"[^>]*>\s*Restored <ph name=\"TITLE\">\$1")

    def test_no_user_facing_text_is_typed_in_the_archive_views(self):
        for path in VIEWS.glob("aerie_sidebar_archive_*.cc"):
            if "test" in path.name:
                continue
            text = code(path)
            for literal in re.findall(r'(?:Label|SetText|SetName|SetTooltipText|SetPlaceholderText)\(\s*u"([^"]+)"', text):
                self.fail(f"{path.name}: typed text {literal!r}")


class TimerTest(unittest.TestCase):
    def test_the_schedule_owns_one_timer_and_no_poll_loop(self):
        header = (TAB_MODEL / "archive_schedule.h").read_text()
        source = code(TAB_MODEL / "archive_schedule.cc")
        self.assertEqual(len(re.findall(r"base::OneShotTimer\s+\w+_;", header)), 1)
        self.assertNotIn("RepeatingTimer", header + source)
        self.assertNotIn("PostDelayedTask", source)

    def test_the_archiver_has_no_timer_of_its_own(self):
        text = (SERVICE / "tab_archiver.h").read_text() + code(SERVICE / "tab_archiver.cc")
        for forbidden in ("OneShotTimer", "RepeatingTimer", "WallClockTimer"):
            self.assertNotIn(forbidden, text)

    def test_the_archiver_is_created_lazily_and_the_sidebar_arms_it_after_the_window_is_interactive(self):
        factory = code(SERVICE / "tab_archiver_factory.cc")
        self.assertRegex(factory, r"ServiceIsCreatedWithBrowserContext\(\) const \{\s*(?:\n)?\s*return false;")
        view = code(VIEWS / "aerie_sidebar_view.cc")
        self.assertRegex(view, re.compile(r"PostDelayedTask\(.*?self->archive_->Attach\(\);", re.S))


class SharedTableTest(unittest.TestCase):
    def test_the_parser_and_the_panel_share_one_table_of_timeouts(self):
        prefs = code(TAB_MODEL / "tab_model_prefs.cc")
        self.assertIn("kArchiveAfterOptions", prefs)
        panel = code(VIEWS / "aerie_sidebar_archive_panel.cc")
        self.assertIn("kArchiveAfterOptions", panel)
        self.assertNotIn('"12h"', panel)

    def test_the_untokenised_panel_values_are_listed_in_the_design_doc(self):
        doc = (REPO / "docs" / "sidebar-design.md").read_text()
        for name in ("kListMaxHeight", "kLandingSlackMs"):
            self.assertIn(name, doc)


class MotionTest(unittest.TestCase):
    def test_the_archive_motion_uses_the_named_tokens(self):
        text = (SIDEBAR_COMPONENT / "aerie_sidebar_motion.cc").read_text()
        for token in ("kArchiveCollapse", "kArchivePulse", "kArchiveRestore", "kMotionScaleArchiveCollapse", "kMotionScaleArchivePulse"):
            self.assertIn(token, text)

    def test_the_views_type_no_duration_or_scale_of_their_own(self):
        for name in ("aerie_sidebar_archive_controller.cc", "aerie_sidebar_archive_button.cc", "aerie_sidebar_archive_panel.cc"):
            text = code(VIEWS / name)
            self.assertNotRegex(text, r"base::Milliseconds\(\s*\d{2,}\s*\)", name)
            self.assertNotRegex(text, r"\bScale\(\s*\d", name)


class NoNetworkTest(unittest.TestCase):
    def test_the_archive_code_touches_no_network(self):
        paths = list(TAB_MODEL.glob("archive*")) + list(SERVICE.glob("tab_archiver*")) + list(VIEWS.glob("aerie_sidebar_archive_*"))
        self.assertTrue(paths)
        for path in paths:
            if "test" in path.name:
                continue
            text = code(path)
            for forbidden in ("net/", "http://", "https://", "URLLoader", "SimpleURLLoader"):
                self.assertNotIn(forbidden, text, path.name)

    def test_the_network_endpoint_list_is_unchanged_by_this_issue(self):
        self.assertNotIn("archive", (REPO / "docs" / "network-endpoints.md").read_text().lower())


class BuildWiringTest(unittest.TestCase):
    def test_the_new_tests_are_in_their_targets(self):
        views = (VIEWS / "BUILD.gn").read_text()
        for name in ("aerie_sidebar_archive_browsertest.cc", "aerie_sidebar_archive_uitest.cc", "aerie_sidebar_archive_panel.cc"):
            self.assertIn(f'"{name}"', views)
        self.assertIn('"tab_archiver_browsertest.cc"', (SERVICE / "BUILD.gn").read_text())
        gn = (TAB_MODEL / "BUILD.gn").read_text()
        for name in ("archive_unittest.cc", "archive_schedule_unittest.cc"):
            self.assertIn(f'"{name}"', gn)


if __name__ == "__main__":
    unittest.main()
