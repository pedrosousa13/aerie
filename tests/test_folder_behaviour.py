"""Folder behaviour (issue #141): source checks for what the C++ tests cannot see: the patches and their series
entries, the Settings row, the allowlist literal, the strings in a grd, no hard-coded text in the sheet and no network
request. The C++ is built and run by `./aerie test-cpp`."""

import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TAB_MODEL = REPO / "overlay" / "components" / "aerie_tab_model"
SERVICE = REPO / "overlay" / "chrome" / "browser" / "aerie_tab_model"
VIEWS = REPO / "overlay" / "chrome" / "browser" / "ui" / "views" / "aerie_sidebar"
RESOURCES = REPO / "overlay" / "chrome" / "browser" / "resources" / "settings" / "appearance_page"
ROW_PATCH = REPO / "patches" / "sidebar" / "0051-add-the-links-open-row-to-the-sidebar-section-of-settings.patch"
WINDOW_PATCH = REPO / "patches" / "sidebar" / "0053-leave-the-referrer-and-the-form-flag-of-a-new-window-on-its-contents.patch"
NAV_PATCH = REPO / "patches" / "sidebar" / "0052-let-a-folder-open-links-in-place-from-chrome-navigate.patch"
GRDP = (VIEWS / "aerie_sidebar_strings.grdp").read_text()
SERIES = [l.strip() for l in (REPO / "patches" / "series").read_text().splitlines() if l.strip() and not l.startswith("#")]


class PatchesTest(unittest.TestCase):
    def test_both_patches_are_in_the_series_in_order(self):
        names = [p.relative_to(REPO / "patches").as_posix() for p in (ROW_PATCH, NAV_PATCH)]
        for name in names:
            self.assertIn(name, SERIES)
        self.assertLess(SERIES.index(names[0]), SERIES.index(names[1]))

    def test_the_window_patch_sets_the_user_data_before_the_delegate_is_called(self):
        name = WINDOW_PATCH.relative_to(REPO / "patches").as_posix()
        self.assertIn(name, SERIES)
        self.assertLess(SERIES.index("sidebar/0052-let-a-folder-open-links-in-place-from-chrome-navigate.patch"), SERIES.index(name))
        patch = WINDOW_PATCH.read_text()
        self.assertIn("+            params.form_submission_post_data != nullptr));", patch)
        self.assertLess(patch.index("kAerieCreatedWindowInfoKey"), patch.index("delegate_->WebContentsCreated"))

    def test_the_navigate_patch_is_one_hook_call_and_one_dep(self):
        patch = NAV_PATCH.read_text()
        self.assertIn("+  if (aerie_tab_model::HandleLinkOpen(params)) {", patch)
        self.assertIn("//chrome/browser/aerie_tab_model:links_open_hook", patch)


class SettingsRowTest(unittest.TestCase):
    def test_the_row_is_a_dropdown_on_the_pref_with_the_two_values(self):
        html = (RESOURCES / "aerie_links_open_row.html.ts").read_text()
        ts = (RESOURCES / "aerie_links_open_row.ts").read_text()
        self.assertIn('pref-key="aerie.tabs.links_open"', html)
        self.assertEqual(re.findall(r"\{value: '([\w-]+)',", ts), ["new-tab", "in-place"])
        self.assertIn('kLinksOpen[] = "aerie.tabs.links_open"', (TAB_MODEL / "tab_model_prefs.h").read_text())

    def test_the_allowlist_literal_is_the_constant(self):
        self.assertIn('+  (*s_allowlist)["aerie.tabs.links_open"] = settings_api::PrefType::kString;', ROW_PATCH.read_text())

    def test_the_words_of_the_row_are_grd_strings(self):
        for name in ("IDS_SETTINGS_AERIE_LINKS_OPEN", "IDS_SETTINGS_AERIE_LINKS_OPEN_DESC",
                     "IDS_AERIE_SIDEBAR_LINKS_NEW_TAB", "IDS_AERIE_SIDEBAR_LINKS_IN_PLACE"):
            self.assertIn(f'name="{name}"', GRDP)
            self.assertIn(name, ROW_PATCH.read_text())


class SheetTest(unittest.TestCase):
    def test_the_behaviour_strings_are_in_the_grd(self):
        for name in ("HEADING", "ARCHIVE", "SLEEP", "LINKS", "SWITCH", "INHERITS_GLOBAL", "INHERITS_FROM",
                     "NAME_INHERITED", "NAME_OWN", "PINNED_NOTE"):
            self.assertIn(f'name="IDS_AERIE_SIDEBAR_BEHAVIOUR_{name}"', GRDP)

    def test_the_sheet_has_no_hard_coded_text(self):
        src = re.sub(r"//[^\n]*", "", (VIEWS / "aerie_sidebar_folder_sheet.cc").read_text())
        body = src[src.index("BuildBehaviourRow(views::BoxLayoutView"):]
        self.assertNotRegex(body, r'u"[A-Za-z]')
        self.assertNotRegex(body, r'(?<!#include )"[A-Z][a-z]+ [a-z]')

    def test_no_new_network_code(self):
        for path in (TAB_MODEL / "folder_behaviour.cc", SERVICE / "links_open_hook.cc"):
            text = path.read_text()
            for word in ("URLLoader", "SimpleURLLoader", "net::", "http://", "https://"):
                self.assertNotIn(word, text, path.name)


if __name__ == "__main__":
    unittest.main()
