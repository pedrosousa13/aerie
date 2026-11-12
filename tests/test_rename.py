"""Tab rename (issue #145): source checks for what the C++ tests cannot see: the strings in a grd, the motion that
may only change opacity, the build wiring, the token, the precedence list, the absence of a network request and the docs.
The C++ is built and run by `./aerie test-cpp` (aerie_tab_model_unittests, aerie_sidebar_unittests,
aerie_browser_tests and aerie_interactive_ui_tests)."""

import json
import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TAB_MODEL = REPO / "overlay" / "components" / "aerie_tab_model"
SIDEBAR = REPO / "overlay" / "components" / "aerie_sidebar"
VIEWS = REPO / "overlay" / "chrome" / "browser" / "ui" / "views" / "aerie_sidebar"
GRDP = (VIEWS / "aerie_sidebar_strings.grdp").read_text()
DOC = (REPO / "docs" / "sidebar-design.md").read_text()

RENAME_VIEW_FILES = ("aerie_sidebar_rename_field.cc", "aerie_sidebar_rename_popover.cc")


def code(path: Path) -> str:
    return re.sub(r"//[^\n]*", "", path.read_text())


class ResolverTest(unittest.TestCase):
    HEADER = (TAB_MODEL / "title_resolver.h").read_text()

    def test_the_sources_are_in_the_designs_order(self):
        sources = re.search(r"enum class TitleSource \{([^}]*)\}", self.HEADER).group(1)
        self.assertEqual([s.strip() for s in sources.split(",")], ["kManual", "kRule", "kTidy", "kAi", "kPage"])

    def test_the_default_list_is_manual_then_page_and_a_list_of_providers(self):
        cc = code(TAB_MODEL / "title_resolver.cc")
        body = cc.split("TitleResolver::CreateDefault()")[1].split("}")[0]
        self.assertLess(body.index("ManualNameProvider"), body.index("PageTitleProvider"))
        self.assertIn("std::vector<std::unique_ptr<TitleProvider>>", self.HEADER)

    def test_the_limit_is_two_hundred(self):
        self.assertRegex(self.HEADER, r"kMaxManualNameLength = 200;")

    def test_the_files_are_in_the_build(self):
        gn = (TAB_MODEL / "BUILD.gn").read_text()
        for name in ("title_resolver.cc", "title_resolver.h", "title_resolver_unittest.cc"):
            self.assertIn(f'"{name}"', gn)

    def test_a_navigation_never_writes_the_manual_name(self):
        adapter = code(REPO / "overlay" / "chrome" / "browser" / "aerie_tab_model" / "tab_strip_adapter.cc")
        self.assertNotIn("manual_name", adapter)
        controller = code(REPO / "overlay" / "chrome" / "browser" / "aerie_tab_model" / "pinned_tabs_controller.cc")
        self.assertNotIn("manual_name", controller)

    def test_the_rename_code_does_not_touch_the_page_or_the_history(self):
        for name in ("aerie_sidebar_view.cc", "aerie_sidebar_row_view.cc", "aerie_sidebar_tile_view.cc",
                     "aerie_sidebar_rename_popover.cc", "aerie_sidebar_rename_field.cc"):
            text = code(VIEWS / name)
            for forbidden in ("SetTitle(base", "UpdateTitle", "history_service", "HistoryService", "SetWindowTitle"):
                self.assertNotIn(forbidden, text, name)


class StringsTest(unittest.TestCase):
    def test_every_new_string_is_a_grd_message(self):
        used = set()
        for name in ("aerie_sidebar_view.cc", "aerie_sidebar_row_view.cc", "aerie_sidebar_tile_view.cc",
                     "aerie_sidebar_context_menu.cc", *RENAME_VIEW_FILES):
            used |= set(re.findall(r"IDS_AERIE_SIDEBAR_\w+", (VIEWS / name).read_text()))
        for needed in ("RENAME_TAB", "RESET_NAME", "RENAME_FIELD_NAME", "RENAME_CAPTION", "ROW_RENAMED_NAME",
                       "RENAMED_TOOLTIP", "RENAMED_PRECEDENCE", "RENAME_POPOVER"):
            self.assertIn(f"IDS_AERIE_SIDEBAR_{needed}", used)
        for name in used:
            self.assertIn(f'name="{name}"', GRDP)

    def test_the_menu_words_are_rename_and_reset_name(self):
        self.assertRegex(GRDP, r'name="IDS_AERIE_SIDEBAR_RENAME_TAB"[^>]*>\s*Rename\s*</message>')
        self.assertRegex(GRDP, r'name="IDS_AERIE_SIDEBAR_RESET_NAME"[^>]*>\s*Reset name\s*</message>')
        self.assertRegex(GRDP, r'name="IDS_AERIE_SIDEBAR_RENAME_FIELD_NAME"[^>]*>\s*Tab name\s*</message>')
        self.assertRegex(GRDP, r'name="IDS_AERIE_SIDEBAR_ROW_RENAMED_NAME"[^>]*>\s*<ph name="TITLE">\$1<ex>[^<]*</ex></ph>, renamed\s*</message>')

    def test_the_tooltip_says_the_real_title_and_that_your_name_is_shown(self):
        self.assertRegex(GRDP, r'name="IDS_AERIE_SIDEBAR_RENAMED_TOOLTIP"[^>]*>\s*Page title: <ph name="TITLE">\$1<ex>[^<]*</ex></ph>\. Shown: your name\.\s*</message>')

    def test_no_user_facing_text_is_typed_in_the_rename_views(self):
        for name in RENAME_VIEW_FILES:
            for text in re.findall(r'u?"([^"\n]*)"', code(VIEWS / name)):
                self.assertFalse(re.search(r"[A-Za-z]{4,} [A-Za-z]{3,}", text), f"{name}: {text}")

    def test_the_accessible_name_order_is_title_asleep_renamed(self):
        for name in ("aerie_sidebar_row_view.cc", "aerie_sidebar_tile_view.cc"):
            text = code(VIEWS / name)
            body = text.split("::UpdateName()")[1].split("\n}\n")[0]
            self.assertLess(body.index("ROW_ASLEEP_NAME"), body.index("ROW_RENAMED_NAME"), name)


class MotionTest(unittest.TestCase):
    def test_the_rename_fade_is_xfast_and_the_views_animate_nothing_else(self):
        cc = code(SIDEBAR / "aerie_sidebar_motion.cc")
        body = cc.split("RenameFade(bool reduced)")[1].split("\n}\n")[0]
        self.assertIn("MotionDuration::kXfast", body)
        self.assertNotIn("transform", body.lower())
        # The views start the fade with an opacity target only.
        for name in ("aerie_sidebar_row_view.cc", "aerie_sidebar_rename_popover.cc"):
            text = code(VIEWS / name)
            for part in text.split("RenameFade(")[1:]:
                chunk = part.split("}\n\n")[0] if "}\n\n" in part else part
                chunk = chunk[:900]
                self.assertNotIn("target.transform", chunk, name)

    def test_the_motion_struct_names_no_transform(self):
        header = (SIDEBAR / "aerie_sidebar_motion.h").read_text()
        struct = header.split("struct RenameFadeMotion {")[1].split("};")[0]
        self.assertNotIn("transform", struct)
        self.assertNotIn("scale", struct)
        fields = set(re.findall(r"bool (animate_\w+);", header))
        self.assertNotIn("animate_rename", fields)


class TokenTest(unittest.TestCase):
    def test_the_pencil_size_is_a_token_with_a_css_twin_and_the_view_uses_it(self):
        tokens = json.loads((REPO / "overlay" / "components" / "aerie_design_tokens" / "tokens.json").read_text())
        self.assertEqual(tokens["size"]["pencil-button"]["value"], 20)
        css = (REPO / "prototypes" / "sidebar" / "tokens.css").read_text()
        self.assertIn("--aerie-size-pencil-button: 20px;", css)
        row = code(VIEWS / "aerie_sidebar_row_view.cc")
        self.assertRegex(row, r"pencil_->SetPreferredSize\(gfx::Size\(aerie::tokens::kSizePencilButton")

    def test_the_xfast_duration_is_ninety_milliseconds_in_both_modes(self):
        tokens = json.loads((REPO / "overlay" / "components" / "aerie_design_tokens" / "tokens.json").read_text())
        xfast = tokens["motion-duration"]["xfast"]
        self.assertEqual((xfast["value"], xfast["reduced"]), (90, 90))


class WiringTest(unittest.TestCase):
    def test_the_sources_and_tests_are_listed(self):
        gn = (VIEWS / "BUILD.gn").read_text()
        for name in ("aerie_sidebar_rename_field.cc", "aerie_sidebar_rename_field.h", "aerie_sidebar_rename_popover.cc",
                     "aerie_sidebar_rename_popover.h", "aerie_sidebar_rename_browsertest.cc", "aerie_sidebar_rename_uitest.cc"):
            self.assertIn(f'"{name}"', gn)

    def test_the_test_suites_start_with_aerie_so_that_the_nightly_runs_them(self):
        for name in ("aerie_sidebar_rename_browsertest.cc", "aerie_sidebar_rename_uitest.cc"):
            text = (VIEWS / name).read_text()
            for suite in re.findall(r"IN_PROC_BROWSER_TEST_[FP]\(\s*(\w+),", text):
                self.assertTrue(suite.startswith("Aerie"), f"{name}: {suite}")
            for suite in re.findall(r"INSTANTIATE_TEST_SUITE_P\(\s*(\w+),", text):
                self.assertTrue(suite.startswith("Aerie"), f"{name}: {suite}")

    def test_there_is_one_context_menu_and_it_has_the_two_commands(self):
        menu = (VIEWS / "aerie_sidebar_context_menu.h").read_text()
        self.assertIn("kCommandRename = 8", menu)
        self.assertIn("kCommandResetName = 9", menu)
        self.assertEqual(len(list(VIEWS.glob("*context_menu*.cc"))), 1)
        cc = code(VIEWS / "aerie_sidebar_context_menu.cc")
        self.assertRegex(cc, r"if \(options_\.has_manual_name\) \{\s*root_->AppendMenuItem\(\s*kCommandResetName")

    def test_no_patch_is_needed_and_the_row_reads_the_resolver_not_the_raw_title(self):
        view = code(VIEWS / "aerie_sidebar_view.cc")
        self.assertEqual(len(re.findall(r"->SetTitle\(", view)), 2)
        self.assertEqual(len(re.findall(r"SetTitle\(shown\.text\)", view)), 2)
        self.assertIn("ResolveTitle", view)


class NoNetworkTest(unittest.TestCase):
    def test_the_new_files_make_no_request(self):
        for path in (*[TAB_MODEL / n for n in ("title_resolver.cc", "title_resolver.h")],
                     *[VIEWS / n for n in RENAME_VIEW_FILES]):
            text = code(path)
            for forbidden in ("services/network", "net/", "url_loader", "SimpleURLLoader", "GURL"):
                self.assertNotIn(forbidden, text, path.name)

    def test_the_endpoint_list_is_unchanged(self):
        # docs/network-endpoints.md lists every endpoint; this slice adds none, so it must not mention rename.
        self.assertNotIn("rename", (REPO / "docs" / "network-endpoints.md").read_text().lower())


class DocsTest(unittest.TestCase):
    def test_the_decisions_are_written_down(self):
        section = DOC.split("**What #145 built.**")[1].split("\n## ")[0]
        for phrase in ("focus loss keeps the name", "200 UTF-16", "case-sensitive", "popover", "Backspace", "xfast",
                       "opacity", ", asleep", ", renamed", "TitleResolver", "size-pencil-button"):
            self.assertIn(phrase.lower(), section.lower(), phrase)


if __name__ == "__main__":
    unittest.main()
