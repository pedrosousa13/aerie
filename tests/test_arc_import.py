"""Import from Arc (issue #171): the fixtures, the format document, the strings, the one patch, and the
security properties that a source check can pin (what the handler may read, where JSON is parsed, the limits).

These are source checks. The C++ and the TypeScript are built and run by `./aerie test-cpp`."""

import json
import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OVERLAY = REPO / "overlay"
COMPONENT = OVERLAY / "components/aerie_tab_model"
FIXTURES = COMPONENT / "testdata/arc"
HANDLER_DIR = OVERLAY / "chrome/browser/ui/webui/aerie_arc_import"
PAGE_DIR = OVERLAY / "chrome/browser/resources/settings/people_page"
PATCH = "sidebar/0052-add-the-import-from-arc-row-to-settings.patch"
SERIES = [l.strip() for l in (REPO / "patches/series").read_text().splitlines()
          if l.strip() and not l.startswith("#")]
DOC = (REPO / "docs/arc-import.md").read_text(encoding="utf-8")


def code(path: Path) -> str:
    return re.sub(r"//[^\n]*", "", path.read_text(encoding="utf-8"))


class FixturesTest(unittest.TestCase):
    VALID = ["one_space", "three_spaces", "nested_folders", "favorites", "custom_titles", "bad_schemes",
             "markup_titles", "wrong_types", "missing_keys", "not_an_object", "no_containers"]

    def test_the_valid_json_fixtures_parse(self):
        for name in self.VALID:
            json.loads((FIXTURES / f"{name}.json").read_text(encoding="utf-8"))

    def test_the_malformed_fixture_is_not_json(self):
        with self.assertRaises(ValueError):
            json.loads((FIXTURES / "bad_json.json").read_text(encoding="utf-8"))

    def test_three_spaces_has_three_spaces_and_a_little_arc_sidebar(self):
        data = json.loads((FIXTURES / "three_spaces.json").read_text(encoding="utf-8"))
        containers = data["sidebar"]["containers"]
        self.assertEqual(len(containers[1]["spaces"]) // 2, 3)
        self.assertIn("littleBrowser", containers[2])

    def test_the_hostile_fixture_holds_every_refused_scheme(self):
        text = (FIXTURES / "bad_schemes.json").read_text(encoding="utf-8")
        for scheme in ("javascript:", "file:", "data:", "chrome:", "aerie:"):
            self.assertIn(scheme, text)

    def test_every_fixture_is_listed_in_the_document(self):
        for path in FIXTURES.glob("*.json"):
            self.assertIn(f"`{path.stem}`", DOC)


class DocumentTest(unittest.TestCase):
    def test_it_cites_both_readers_by_url_and_commit(self):
        self.assertRegex(DOC, r"https://github\.com/zen-browser/desktop/blob/[0-9a-f]{40}/")
        self.assertRegex(DOC, r"https://github\.com/pauljoda/Crest/blob/[0-9a-f]{40}/")

    def test_it_names_the_limits_of_the_issue(self):
        for text in ("32 MiB", "| Spaces | 100", "10 000", "| Depth of the item tree | 32", "1024"):
            self.assertIn(text, DOC)

    def test_it_says_where_json_is_parsed_and_why(self):
        self.assertIn("ParseUntrustedJson", DOC)
        self.assertIn("data_decoder", DOC)


class LimitsTest(unittest.TestCase):
    def test_the_header_holds_the_limits_of_the_issue(self):
        header = (COMPONENT / "arc_import.h").read_text(encoding="utf-8")
        self.assertIn("kArcMaxFileBytes = 32u * 1024 * 1024", header)
        self.assertIn("kArcMaxSpaces = 100", header)
        self.assertIn("kArcMaxItems = 10'000", header)
        self.assertIn("kArcMaxDepth = 32", header)
        self.assertIn("kArcMaxTitleLength = 1024", header)

    def test_only_http_and_https_pass(self):
        source = code(COMPONENT / "arc_import.cc")
        self.assertIn('name != "http" && name != "https"', source)
        self.assertIn("SchemeIsHTTPOrHTTPS", source)


class HandlerTest(unittest.TestCase):
    CC = code(HANDLER_DIR / "aerie_arc_import_handler.cc")

    def test_it_registers_exactly_the_three_messages_and_none_takes_a_path(self):
        names = re.findall(r'"(aerieArcImport\.[a-zA-Z]+)"', self.CC)
        self.assertEqual(sorted(names), ["aerieArcImport.choose", "aerieArcImport.import", "aerieArcImport.reset"])
        self.assertEqual(self.CC.count("RegisterMessageCallback"), 3)

    def test_the_only_file_it_reads_is_the_one_the_chooser_returns(self):
        self.assertIn("FileSelected", self.CC)
        self.assertEqual(self.CC.count("ReadFileToStringWithMaxSize"), 1)
        # The import message carries an index, not a path.
        self.assertIn("args[1].is_int()", self.CC)

    def test_the_size_is_checked_before_the_read_and_the_read_is_capped_too(self):
        self.assertLess(self.CC.index("GetFileInfo"), self.CC.index("ReadFileToStringWithMaxSize"))
        self.assertIn("kArcMaxFileBytes", self.CC)

    def test_the_file_is_read_and_parsed_on_the_thread_pool(self):
        self.assertIn("base::ThreadPool::PostTaskAndReplyWithResult", self.CC)
        self.assertIn("base::MayBlock()", self.CC)
        self.assertIn("ParseUntrustedJson", self.CC)

    def test_the_chooser_is_for_json_files_only(self):
        self.assertIn('FILE_PATH_LITERAL("json")', self.CC)
        self.assertIn("include_all_files = false", self.CC)

    def test_the_tabs_are_made_unloaded(self):
        self.assertIn("RestoreMissingTabs", self.CC)
        pinned = code(REPO / "overlay/chrome/browser/aerie_tab_model/pinned_tabs_controller.cc")
        self.assertIn("kDeferUntilActivation", pinned)

    def test_json_is_read_in_one_place_of_the_import(self):
        reader_files = [p.name for p in COMPONENT.glob("arc_import*.cc") if "JSONReader" in code(p)]
        self.assertEqual(reader_files, ["arc_import_json.cc"])
        self.assertIn("ParseUntrustedJson", (COMPONENT / "arc_import_json.h").read_text())
        self.assertNotIn("ParseUntrustedJson", (COMPONENT / "arc_import.h").read_text())
        for path in HANDLER_DIR.glob("*.cc"):
            self.assertNotIn("JSONReader", code(path), path.name)

    def test_a_second_press_is_rejected_and_a_dropped_dialog_is_told_first(self):
        self.assertIn('base::Value("busy")', self.CC)
        drop = self.CC[self.CC.index("void AerieArcImportHandler::DropDialog"):]
        drop = drop[:drop.index("}\n}")]
        self.assertLess(drop.index("ListenerDestroyed"), drop.index("reset()"))
        # No place lets go of the dialog by other means.
        self.assertEqual(self.CC.count("select_file_dialog_.reset()"), 1)

    def test_a_call_with_an_id_and_bad_arguments_is_rejected(self):
        self.assertGreaterEqual(self.CC.count('base::Value("bad_argument")'), 2)

    def test_the_type_of_the_file_is_checked_before_it_is_opened(self):
        self.assertLess(self.CC.index("KindOf(path)"), self.CC.index("ReadFileToStringWithMaxSize"))
        self.assertIn("S_ISREG", self.CC)
        self.assertIn('"not_a_file"', self.CC)

    def test_errors_are_an_enum_with_one_mapping_to_the_names(self):
        self.assertIn("enum class LoadError", self.CC)
        self.assertNotIn("outcome.error = \"", self.CC)
        self.assertEqual(self.CC.count("const char* LoadErrorName"), 1)

    def test_the_import_says_it_is_rolled_back_not_atomic(self):
        self.assertNotIn("All or nothing", DOC)
        self.assertIn("Rolled back on failure", DOC)
        self.assertIn("no batch or transaction API", DOC)

    def test_the_apply_step_writes_only_through_the_tab_model(self):
        source = code(COMPONENT / "arc_import_apply.cc")
        self.assertIn("model_.AddTab", source)
        self.assertIn("Rollback", source)
        self.assertNotIn("Navigate", source)


class PageTest(unittest.TestCase):
    TS = (PAGE_DIR / "aerie_arc_import_row.ts").read_text(encoding="utf-8")
    HTML = (PAGE_DIR / "aerie_arc_import_row.html.ts").read_text(encoding="utf-8")

    def test_nothing_from_the_file_becomes_markup(self):
        for text in (self.TS, self.HTML):
            self.assertNotIn("innerHTML", text)
            self.assertNotIn("unsafeHTML", text)
            self.assertNotIn("unsafeCSS", text)
            self.assertNotIn("sanitizedHtml", text)
        # The Space's name and icon are text bindings.
        self.assertIn("${this.spaceName_(space)}", self.HTML)
        self.assertIn("space.icon", self.HTML)

    def test_the_proxy_sends_only_the_three_messages(self):
        proxy = (PAGE_DIR / "aerie_arc_import_browser_proxy.ts").read_text(encoding="utf-8")
        self.assertEqual(sorted(re.findall(r"'(aerieArcImport\.[a-zA-Z]+)'", proxy)),
                         ["aerieArcImport.choose", "aerieArcImport.import", "aerieArcImport.reset"])


class StringsTest(unittest.TestCase):
    GRDP = HANDLER_DIR / "aerie_arc_import_strings.grdp"

    def ids(self):
        root = ET.parse(self.GRDP).getroot()
        return [m.attrib["name"] for m in root.iter("message")]

    def test_every_message_has_a_description(self):
        for message in ET.parse(self.GRDP).getroot().iter("message"):
            self.assertTrue(message.attrib.get("desc"), message.attrib["name"])

    def test_every_string_is_handed_to_the_page_and_the_page_asks_for_no_other(self):
        patch = (REPO / "patches" / PATCH).read_text(encoding="utf-8")
        pairs = dict(re.findall(r'^\+\s+\{"(aerieArcImport\w*)", (IDS_SETTINGS_AERIE_ARC_IMPORT\w*)\},', patch, re.M))
        self.assertEqual(sorted(pairs.values()), sorted(self.ids()))
        asked = set(re.findall(r"\$i18n\{(aerieArcImport\w*)\}", (PAGE_DIR / "aerie_arc_import_row.html.ts").read_text()))
        asked |= set(re.findall(r"'(aerieArcImport\w+)'", (PAGE_DIR / "aerie_arc_import_row.ts").read_text()))
        self.assertLessEqual(asked, set(pairs))

    def test_the_result_names_what_is_not_imported(self):
        text = self.GRDP.read_text(encoding="utf-8")
        for word in ("Boosts", "Easels", "Little Arc", "Peek", "Logins are not imported"):
            self.assertIn(word, text)

    def test_the_handler_uses_strings_that_exist(self):
        cc = (HANDLER_DIR / "aerie_arc_import_handler.cc").read_text(encoding="utf-8")
        for name in re.findall(r"IDS_SETTINGS_AERIE_ARC_IMPORT\w*", cc):
            self.assertIn(name, self.ids())

    def test_every_skip_reason_has_a_line(self):
        header = (COMPONENT / "arc_import.cc").read_text(encoding="utf-8")
        reasons = re.findall(r'return "([a-z_]+)";', header.split("ArcSkipReasonToString")[1].split("namespace {")[0])
        reasons = [r for r in reasons if r != "unknown"]
        ts = (PAGE_DIR / "aerie_arc_import_row.ts").read_text(encoding="utf-8")
        for reason in reasons:
            self.assertIn(f"{reason}:", ts, reason)


class PatchTest(unittest.TestCase):
    def test_it_is_listed_and_has_its_header(self):
        self.assertIn(PATCH, SERIES)
        text = (REPO / "patches" / PATCH).read_text(encoding="utf-8")
        self.assertTrue(text.startswith("Aerie-Purpose:"))
        self.assertIn("Aerie-Upstream-Area:", text)

    def test_it_puts_the_row_next_to_the_import_entry(self):
        text = (REPO / "patches" / PATCH).read_text(encoding="utf-8")
        self.assertIn("+        <settings-aerie-arc-import-row></settings-aerie-arc-import-row>", text)
        self.assertIn("onImportDataClick_", text)

    def test_the_handler_is_added_to_the_settings_page_only(self):
        text = (REPO / "patches" / PATCH).read_text(encoding="utf-8")
        self.assertIn("aerie_arc_import::AerieArcImportHandler", text)
        self.assertIn("chrome/browser/ui/webui/settings/settings_ui.cc", text)


if __name__ == "__main__":
    unittest.main()
