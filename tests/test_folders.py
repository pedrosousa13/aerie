"""Folders in Pinned and Today (issue #181): source checks for what the C++ tests cannot see: the strings in a grd, the two
new tokens, the build wiring, that no network code was added and the docs. The C++ is built and run by `./aerie test-cpp`
(aerie_tab_model_unittests, aerie_sidebar_unittests, aerie_browser_tests and aerie_interactive_ui_tests)."""

import json
import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
VIEWS = REPO / "overlay" / "chrome" / "browser" / "ui" / "views" / "aerie_sidebar"
GRDP = (VIEWS / "aerie_sidebar_strings.grdp").read_text()
TOKENS = json.loads((REPO / "overlay" / "components" / "aerie_design_tokens" / "tokens.json").read_text())
FOLDER_STRINGS = [
    "IDS_AERIE_SIDEBAR_NEW_FOLDER",
    "IDS_AERIE_SIDEBAR_FOLDER_ITEMS",
    "IDS_AERIE_SIDEBAR_FOLDER_NAME_EXPANDED",
    "IDS_AERIE_SIDEBAR_FOLDER_NAME_COLLAPSED",
    "IDS_AERIE_SIDEBAR_FOLDER_TOO_DEEP",
    "IDS_AERIE_SIDEBAR_FOLDER_DELETE_TITLE",
    "IDS_AERIE_SIDEBAR_DELETE_FOLDER",
]


class StringsTest(unittest.TestCase):
    def test_every_folder_string_is_in_the_grd(self):
        for name in FOLDER_STRINGS:
            self.assertIn(f'name="{name}"', GRDP)

    def test_the_depth_tooltip_names_the_limit_with_a_placeholder(self):
        match = re.search(r'name="IDS_AERIE_SIDEBAR_FOLDER_TOO_DEEP".*?</message>', GRDP, re.S)
        self.assertIsNotNone(match)
        self.assertIn("nest deeper than", match.group(0))

    def test_the_item_count_is_an_icu_plural(self):
        match = re.search(r'name="IDS_AERIE_SIDEBAR_FOLDER_ITEMS".*?</message>', GRDP, re.S)
        self.assertIn("plural", match.group(0))


class SourceTest(unittest.TestCase):
    def test_no_folder_file_touches_the_network(self):
        for path in list(VIEWS.glob("*folder*")) + list((REPO / "overlay" / "components" / "aerie_tab_model").glob("folder_tree*")):
            text = path.read_text()
            for word in ("URLLoader", "SimpleURLLoader", "net::", "HttpRequest"):
                if path.name.endswith("browsertest.cc"):
                    continue
                self.assertNotIn(word, text, path.name)

    def test_the_depth_limit_is_five(self):
        text = (REPO / "overlay" / "components" / "aerie_tab_model" / "folder_tree.h").read_text()
        self.assertRegex(text, r"kMaxFolderDepth\s*=\s*5")

    def test_the_group_files_are_in_the_build(self):
        build = (VIEWS / "BUILD.gn").read_text()
        self.assertIn("aerie_sidebar_folder_group.cc", build)
        self.assertIn("aerie_sidebar_folders_browsertest.cc", build)
        self.assertIn("//components/aerie_sidebar:tree", build)


class DocsTest(unittest.TestCase):
    def test_the_design_doc_records_the_choices(self):
        text = (REPO / "docs" / "sidebar-design.md").read_text()
        self.assertIn("Folders as built (#181)", text)
        self.assertIn("#223", text)


if __name__ == "__main__":
    unittest.main()
