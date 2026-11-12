"""Auto-filing folders (issue #139): source checks for what the C++ tests cannot see: the Settings patch and its
series entry, the pref and its allowlist entry, the strings in a grd, the two new tokens, the build wiring, the absence
of a network request and the docs. The C++ is built and run by `./aerie test-cpp` (aerie_tab_model_unittests,
aerie_sidebar_unittests, aerie_browser_tests and aerie_interactive_ui_tests)."""

import json
import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TAB_MODEL = REPO / "overlay" / "components" / "aerie_tab_model"
VIEWS = REPO / "overlay" / "chrome" / "browser" / "ui" / "views" / "aerie_sidebar"
PATCH = REPO / "patches" / "sidebar" / "0038-add-the-folder-rules-row-to-the-sidebar-section-of-settings.patch"
GRDP = (VIEWS / "aerie_sidebar_strings.grdp").read_text()
SERIES = [l.strip() for l in (REPO / "patches" / "series").read_text().splitlines() if l.strip() and not l.startswith("#")]
TOKENS = json.loads((REPO / "overlay" / "components" / "aerie_design_tokens" / "tokens.json").read_text())


class PatchTest(unittest.TestCase):
    def test_the_patch_is_in_the_series_after_the_pinned_close_row(self):
        name = PATCH.relative_to(REPO / "patches").as_posix()
        self.assertIn(name, SERIES)
        self.assertGreater(
            SERIES.index(name),
            SERIES.index("sidebar/0037-add-the-pinned-close-row-to-the-sidebar-section-of-settings.patch"))

    def test_the_patch_has_the_three_aerie_headers_and_allowlists_the_pref(self):
        text = PATCH.read_text()
        for header in ("Aerie-Purpose:", "Aerie-Upstream-Area:", "Aerie-Kind:"):
            self.assertIn(header, text)
        self.assertIn("aerie.folders.rules_enabled", text)


class PrefTest(unittest.TestCase):
    def test_the_pref_name_is_the_same_in_the_header_the_patch_and_the_row(self):
        header = (TAB_MODEL / "tab_model_prefs.h").read_text()
        self.assertIn('kFolderRulesEnabled[] = "aerie.folders.rules_enabled"', header)
        row = (REPO / "overlay" / "chrome" / "browser" / "resources" / "settings" / "appearance_page"
               / "aerie_folder_rules_row.html.ts").read_text()
        self.assertIn("aerie.folders.rules_enabled", row)

    def test_the_pref_defaults_to_on(self):
        self.assertRegex((TAB_MODEL / "tab_model_prefs.cc").read_text(),
                         r"kFolderRulesEnabled,\s*true")


class StringsTest(unittest.TestCase):
    def test_every_string_the_views_use_is_in_the_grd(self):
        used = set()
        for path in VIEWS.glob("*.cc"):
            if path.name.endswith(("test.cc", "uitest.cc")):
                continue
            used |= set(re.findall(r"\bIDS_AERIE_SIDEBAR_\w+", path.read_text()))
        declared = set(re.findall(r'name="(IDS_AERIE_SIDEBAR_\w+)"', GRDP))
        self.assertEqual(sorted(used - declared), [])

    def test_the_new_strings_exist(self):
        for name in ("EDIT_FOLDER", "STOP_RULE_MATCHING", "FILED_INTO", "FILED_UNDO", "FILED_DONT_FILE_SITE",
                     "SHEET_TITLE", "RULE_REMOVE", "AUTO_FOLDER_NAME_EXPANDED", "FOLDER_RULES_BUTTON"):
            self.assertIn(f'name="IDS_AERIE_SIDEBAR_{name}"', GRDP)

    def test_the_folder_name_says_auto_filing_folder(self):
        self.assertIn("auto-filing folder", GRDP)


class TokensTest(unittest.TestCase):
    def test_the_pulse_and_sheet_tokens_exist(self):
        self.assertEqual(TOKENS["motion-opacity"]["autofile-pulse"]["value"], 0.3)
        self.assertEqual(TOKENS["motion-distance"]["sheet-slide"]["value"], 10)
        self.assertEqual(TOKENS["motion-distance"]["sheet-slide"]["reduced"], 0)


class NoNetworkTest(unittest.TestCase):
    def test_no_new_endpoint_and_no_network_code(self):
        sources = list(TAB_MODEL.glob("*rule*")) + list(TAB_MODEL.glob("auto_file*")) + list(VIEWS.glob("*sheet*")) + \
            list(VIEWS.glob("*switch*"))
        for path in sources:
            text = path.read_text()
            for banned in ("URLLoader", "SimpleURLLoader", "http://", "https://", "NetworkContext"):
                if path.name.endswith("unittest.cc"):
                    continue
                self.assertNotIn(banned, re.sub(r"//[^\n]*", "", text), path.name)

    def test_the_endpoint_list_is_unchanged(self):
        import subprocess
        out = subprocess.run(["git", "diff", "--name-only", "main", "--", "docs/network-endpoints.md"],
                             cwd=REPO, capture_output=True, text=True).stdout
        self.assertEqual(out.strip(), "")


class BuildWiringTest(unittest.TestCase):
    def test_new_sources_are_in_the_build_files(self):
        views_build = (VIEWS / "BUILD.gn").read_text()
        for name in ("aerie_sidebar_folder_sheet.cc", "aerie_sidebar_switch.cc",
                     "aerie_sidebar_autofile_browsertest.cc", "aerie_sidebar_autofile_uitest.cc"):
            self.assertIn(name, views_build)
        model_build = (TAB_MODEL / "BUILD.gn").read_text()
        for name in ("rule_matcher.cc", "folder_rules.cc", "auto_file_controller.cc"):
            self.assertIn(name, model_build)
        self.assertIn("aerie_folder_rules.icon", (VIEWS / "vector_icons" / "BUILD.gn").read_text())


class DocsTest(unittest.TestCase):
    def test_the_design_doc_describes_auto_filing(self):
        text = (REPO / "docs" / "sidebar-design.md").read_text()
        self.assertIn("Folders as built (#139)", text)
        self.assertIn("10 s", text)


if __name__ == "__main__":
    unittest.main()
