"""Smart folders (issue #140): source checks for what the C++ tests cannot see: the strings in a grd, the icon and its
build entry, the build wiring of the new files, the absence of a network request and of startup work, and the docs. The
C++ is built and run by `./aerie test-cpp` (aerie_tab_model_unittests, aerie_sidebar_unittests, aerie_browser_tests and
aerie_interactive_ui_tests)."""

import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TAB_MODEL = REPO / "overlay" / "components" / "aerie_tab_model"
VIEWS = REPO / "overlay" / "chrome" / "browser" / "ui" / "views" / "aerie_sidebar"
GRDP = (VIEWS / "aerie_sidebar_strings.grdp").read_text()


class StringsTest(unittest.TestCase):
    def test_every_smart_string_the_views_use_is_in_the_grd(self):
        used = set()
        for path in VIEWS.glob("*.cc"):
            if path.name.endswith(("test.cc", "uitest.cc")):
                continue
            used |= set(re.findall(r"\bIDS_AERIE_SIDEBAR_SMART\w+|\bIDS_AERIE_SIDEBAR_SHEET_\w*SMART\w*", path.read_text()))
        declared = set(re.findall(r'name="(IDS_AERIE_SIDEBAR_\w+)"', GRDP))
        self.assertTrue(used)
        self.assertEqual(sorted(used - declared), [])

    def test_the_strings_the_issue_names_are_in_the_grd(self):
        text = GRDP.replace("&apos;", "'")
        for wanted in ("Smart folders show tabs. They don't hold them.", "smart folder", "Moved ", "shown from"):
            self.assertIn(wanted, text)

    def test_no_user_facing_smart_text_is_a_literal_in_the_views(self):
        for name in ("aerie_sidebar_view.cc", "aerie_sidebar_row_view.cc", "aerie_sidebar_folder_row_view.cc",
                     "aerie_sidebar_folder_sheet.cc", "aerie_sidebar_drag_host.cc"):
            text = (VIEWS / name).read_text()
            self.assertNotIn('u"Moved', text)
            self.assertNotIn('"Smart folders show', text)
            self.assertNotIn('shown from', re.sub(r"//.*", "", text))


class BuildTest(unittest.TestCase):
    def test_the_new_files_are_in_the_build(self):
        tab_model = (TAB_MODEL / "BUILD.gn").read_text()
        for name in ("smart_folder.cc", "smart_folder.h", "smart_folder_unittest.cc"):
            self.assertIn(name, tab_model)
        self.assertIn("aerie_sidebar_smart_browsertest.cc", (VIEWS / "BUILD.gn").read_text())

    def test_the_icon_is_registered(self):
        self.assertTrue((VIEWS / "vector_icons" / "aerie_smart_folder.icon").is_file())
        self.assertIn("aerie_smart_folder.icon", (VIEWS / "vector_icons" / "BUILD.gn").read_text())


class CostTest(unittest.TestCase):
    def test_the_index_does_no_work_at_startup_and_makes_no_request(self):
        for name in ("smart_folder.h", "smart_folder.cc"):
            text = (TAB_MODEL / name).read_text()
            self.assertNotRegex(text, r"PostTask|PostDelayedTask|URLLoader|SimpleURLLoader|NetworkContext")
        view = (VIEWS / "aerie_sidebar_view.cc").read_text()
        self.assertIn("smart_all_dirty_", view)
        # A profile with no smart folder skips the per-tab update.
        self.assertIn("HasSmartFolders()", view)

    def test_the_endpoint_list_is_untouched_by_the_name_of_the_feature(self):
        self.assertNotIn("smart", (REPO / "docs" / "network-endpoints.md").read_text().lower())

    def test_the_design_doc_describes_the_slice(self):
        self.assertIn("### Folders as built (#140)", (REPO / "docs" / "sidebar-design.md").read_text())


if __name__ == "__main__":
    unittest.main()
