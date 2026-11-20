"""Tidy tab titles and rename rules (issue #146): source checks for what the C++ tests cannot see (the Settings patch
and its series entry, the pref names across the header, the patch and the page, the strings, the page's roles and keys,
no regular expression and no network in the new code, nothing that animates) and a run of the page's rule model with
node. The C++ is built and run by `./aerie test-cpp` (aerie_tab_model_unittests, aerie_browser_tests)."""

import json
import re
import shutil
import subprocess
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TAB_MODEL = REPO / "overlay" / "components" / "aerie_tab_model"
VIEWS = REPO / "overlay" / "chrome" / "browser" / "ui" / "views" / "aerie_sidebar"
PAGE = REPO / "overlay" / "chrome" / "browser" / "resources" / "settings" / "appearance_page"
HANDLER = REPO / "overlay" / "chrome" / "browser" / "ui" / "webui" / "aerie_title_rules"
PATCH = REPO / "patches" / "sidebar" / "0050-add-the-title-tidy-and-rename-rules-to-the-sidebar-section-of-settings.patch"
GRDP = (VIEWS / "aerie_sidebar_strings.grdp").read_text()
SERIES = [l.strip() for l in (REPO / "patches" / "series").read_text().splitlines() if l.strip() and not l.startswith("#")]

PREFS = {
    "kTidyTitles": "aerie.tabs.tidy_titles",
    "kTidyOffSites": "aerie.tabs.tidy_off_sites",
    "kRenameRulesEnabled": "aerie.tabs.rename_rules_enabled",
    "kRenameRules": "aerie.tabs.rename_rules",
}
NEW_SOURCES = [TAB_MODEL / n for n in ("title_tidier.cc", "title_tidier.h", "rename_rules.cc", "rename_rules.h",
                                       "title_settings.cc", "title_settings.h")] + \
    list(HANDLER.glob("*.cc")) + list(HANDLER.glob("*.h"))


class PatchTest(unittest.TestCase):
    def test_the_patch_is_in_the_series_after_the_theme_and_mode_rows(self):
        name = PATCH.relative_to(REPO / "patches").as_posix()
        self.assertIn(name, SERIES)
        self.assertGreater(
            SERIES.index(name),
            SERIES.index("sidebar/0048-add-the-theme-and-mode-rows-to-the-sidebar-section-of-settings.patch"))

    def test_the_patch_has_the_three_aerie_headers(self):
        text = PATCH.read_text()
        for header in ("Aerie-Purpose:", "Aerie-Upstream-Area:", "Aerie-Kind:"):
            self.assertIn(header, text)

    def test_the_patch_allowlists_the_four_prefs_with_the_right_types(self):
        text = PATCH.read_text()
        self.assertRegex(text, r'\["aerie.tabs.tidy_titles"\] = settings_api::PrefType::kBoolean')
        self.assertRegex(text, r'\["aerie.tabs.tidy_off_sites"\] = settings_api::PrefType::kList')
        self.assertRegex(text, r'\["aerie.tabs.rename_rules_enabled"\]\s*=\s*\+?\s*settings_api::PrefType::kBoolean')
        self.assertRegex(text, r'\["aerie.tabs.rename_rules"\] = settings_api::PrefType::kList')

    def test_the_patch_adds_the_handler_and_the_page_files(self):
        text = PATCH.read_text()
        self.assertIn("AerieTitleRulesHandler", text)
        self.assertIn("//chrome/browser/ui/webui/aerie_title_rules:handler", text)
        for name in ("aerie_title_rules_row.ts", "aerie_title_rules_row.html.ts", "aerie_title_rules_model.ts",
                     "aerie_title_rules_browser_proxy.ts"):
            self.assertIn(f"appearance_page/{name}", text)
            self.assertTrue((PAGE / name).exists(), name)
        self.assertIn("settings-aerie-title-rules-row", text)


class PrefTest(unittest.TestCase):
    def test_the_pref_names_are_the_same_in_the_header_the_patch_and_the_page(self):
        header = (TAB_MODEL / "tab_model_prefs.h").read_text()
        patch = PATCH.read_text()
        page = (PAGE / "aerie_title_rules_row.ts").read_text()
        for constant, name in PREFS.items():
            self.assertIn(f'{constant}[] = "{name}"', header)
            self.assertIn(f'"{name}"', patch)
            self.assertIn(f"'{name}'", page)

    def test_the_defaults_are_on_and_the_lists_are_empty(self):
        text = (TAB_MODEL / "tab_model_prefs.cc").read_text()
        self.assertRegex(text, r"RegisterBooleanPref\(kTidyTitles,\s*true\)")
        self.assertRegex(text, r"RegisterBooleanPref\(kRenameRulesEnabled,\s*true\)")
        self.assertIn("RegisterListPref(kTidyOffSites)", text)
        self.assertIn("RegisterListPref(kRenameRules)", text)

    def test_the_rules_are_validated_when_the_pref_is_read(self):
        text = (TAB_MODEL / "tab_model_prefs.cc").read_text()
        self.assertIn("GetTitleSettings", text)
        self.assertIn("MakeTitleSettings", text)


class MatcherTest(unittest.TestCase):
    def test_there_is_one_matcher_and_the_rules_use_it(self):
        text = (TAB_MODEL / "rename_rules.h").read_text()
        self.assertIn('#include "components/aerie_tab_model/rule_matcher.h"', text)
        # No second matcher: the rename rules do not define a match function of their own.
        code = (TAB_MODEL / "rename_rules.cc").read_text()
        self.assertIn("Matches(rule.match, input)", code)
        for name in ("HostMatchesDomain", "UrlStartsWith", "UrlContains", "TitleContains"):
            self.assertNotRegex(code, rf"\bbool\s+{name}\s*\(")

    def test_one_test_file_covers_the_shared_matcher(self):
        self.assertEqual(len(list(TAB_MODEL.glob("rule_matcher*unittest.cc"))), 1)


class NoRegexNoNetworkTest(unittest.TestCase):
    def test_no_regex_engine_and_no_network_in_the_new_code(self):
        for path in NEW_SOURCES:
            text = path.read_text()
            for banned in ("<regex>", "re2/", "RE2", "std::regex", "icu::RegexMatcher", "regexp",
                           "URLLoader", "SimpleURLLoader", "NetworkContext", "http://", "https://"):
                self.assertNotIn(banned, text, f"{path.name}: {banned}")

    def test_the_tidy_reads_only_a_title_and_a_host(self):
        text = (TAB_MODEL / "title_tidier.h").read_text()
        self.assertIn("std::string TidyTitle(std::string_view title, std::string_view host);", text)

    def test_a_regular_expression_is_not_a_rule_type(self):
        text = (TAB_MODEL / "rule_matcher.cc").read_text()
        self.assertNotIn("regex", text.lower())

    def test_the_endpoint_list_is_untouched(self):
        # docs/network-endpoints.md is unchanged by this issue; the diff is empty against the branch point.
        # Other issues edit the page (the request policy tables, #257). What this feature must not add is
        # an endpoint row: a line that starts a table row with an http(s) address.
        out = subprocess.run(["git", "diff", "-U0", "origin/main", "--", "docs/network-endpoints.md"],
                             cwd=REPO, capture_output=True, text=True).stdout
        added = [l for l in out.splitlines() if l.startswith("+|") and ("`http://" in l or "`https://" in l)]
        self.assertEqual(added, [])


class StringsTest(unittest.TestCase):
    def test_every_string_the_views_use_is_in_the_grd(self):
        used = set()
        for path in VIEWS.glob("*.cc"):
            if path.name.endswith(("test.cc", "uitest.cc")):
                continue
            used |= set(re.findall(r"\bIDS_AERIE_SIDEBAR_\w+", path.read_text()))
        declared = set(re.findall(r'name="(IDS_AERIE_SIDEBAR_\w+)"', GRDP))
        self.assertEqual(sorted(used - declared), [])

    def test_the_new_sidebar_strings_exist(self):
        for name in ("TIDIED_TOOLTIP", "RULE_RENAMED_TOOLTIP", "MAKE_RULE"):
            self.assertIn(f'name="IDS_AERIE_SIDEBAR_{name}"', GRDP)

    def test_every_page_string_is_handed_over_by_the_patch_and_is_in_the_grd(self):
        patch = PATCH.read_text()
        provided = dict(re.findall(r'\+\s*\{"(\w+)",\s*(IDS_\w+)\}', patch))
        declared = set(re.findall(r'name="(IDS_\w+)"', GRDP))
        for page in ("aerie_title_rules_row.html.ts", "aerie_title_rules_row.ts"):
            text = (PAGE / page).read_text()
            keys = set(re.findall(r"\$i18n\{(\w+)\}", text)) | set(re.findall(r"get\w*String\w*\('(\w+)'", text))
            for key in keys:
                self.assertIn(key, provided, f"{page}: {key} is not handed to the page by the patch")
                self.assertIn(provided[key], declared, f"{provided[key]} is not in the grd")

    def test_the_template_has_no_hard_coded_text(self):
        text = (PAGE / "aerie_title_rules_row.html.ts").read_text()
        for node in re.findall(r">([^<>$`]*[A-Za-z]{3,}[^<>$`]*)<", text):
            self.assertFalse(node.strip(), f"hard-coded text {node.strip()!r}")

    def test_the_handler_holds_no_user_facing_text(self):
        text = (HANDLER / "aerie_title_rules_handler.cc").read_text()
        self.assertNotIn("l10n_util", text)


class PageTest(unittest.TestCase):
    def setUp(self):
        self.html = (PAGE / "aerie_title_rules_row.html.ts").read_text()
        self.ts = (PAGE / "aerie_title_rules_row.ts").read_text()

    def test_the_rule_list_is_a_listbox_of_options(self):
        self.assertRegex(self.html, r'<ul[^>]*role="listbox"')
        self.assertRegex(self.html, r'<li[^>]*role="option"')
        self.assertIn("aria-selected", self.html)

    def test_the_list_can_be_reordered_with_the_mouse_and_says_so(self):
        # #146: drag and drop beside Alt+Up/Down, same listbox, same live region.
        html = (PAGE / "aerie_title_rules_row.html.ts").read_text()
        ts = (PAGE / "aerie_title_rules_row.ts").read_text()
        self.assertIn('role="listbox"', html)
        self.assertIn('draggable="true"', html)
        for event in ("dragstart", "dragover", "drop", "dragend"):
            self.assertIn(f"@{event}=", html)
        # Keyboard and mouse share one move, which sets the live-region note.
        self.assertIn("private moveTo_(", ts)
        self.assertEqual(ts.count("this.moveTo_("), 2)  # moveSelected_ (keys, buttons) and the drop
        body = ts.split("private moveTo_(")[1].split("\n  }\n")[0]
        self.assertIn("this.announcement_ =", body)
        # A drag from elsewhere is not a target, and nothing but the index is set on it.
        self.assertIn("if (this.dragFrom_ < 0)", ts)
        self.assertIn("application/x-aerie-rule-index", ts)
        self.assertNotIn("text/plain", ts)

    def test_the_switches_have_the_switch_role(self):
        self.assertGreaterEqual(len(re.findall(r'role="switch"', self.html)), 3)
        self.assertEqual(len(re.findall(r'role="switch"', self.html)), len(re.findall(r"aria-checked=", self.html)))

    def test_alt_up_and_alt_down_reorder(self):
        self.assertIn("e.altKey", self.ts)
        self.assertRegex(self.ts, r"case 'ArrowUp':\s*case 'ArrowDown':")
        self.assertIn("moveSelected_(index, delta)", self.ts)

    def test_the_try_result_and_the_move_note_are_live_regions(self):
        self.assertRegex(self.html, r'id="aerieTryResult"[^>]*role="status"[^>]*aria-live="polite"')
        self.assertRegex(self.html, r'id="aerieRulesAnnounce"[^>]*role="status"[^>]*aria-live="polite"')

    def test_the_form_error_is_an_alert(self):
        self.assertRegex(self.html, r'id="aerieRuleError"[^>]*role="alert"')

    def test_page_text_is_never_html(self):
        for text in (self.html, self.ts):
            self.assertNotIn("innerHTML", text)
            self.assertNotIn("unsafeHTML", text)
            self.assertNotIn("sanitizeInnerHtml", text)

    def test_make_a_rule_from_this_tab_fills_the_editor_from_the_address(self):
        self.assertIn("aerieRuleHost", self.ts)
        self.assertIn("aerieRuleTitle", self.ts)
        sidebar = (VIEWS / "aerie_sidebar_view.cc").read_text()
        self.assertIn('"aerieRuleHost"', sidebar)
        self.assertIn('"aerieRuleTitle"', sidebar)
        self.assertIn("net::AppendQueryParameter", sidebar)

    def test_nothing_here_animates(self):
        # Reduced motion: the page has no transition or animation, and the row code the issue adds has none.
        for text in (self.ts, self.html):
            self.assertNotRegex(text, r"\b(transition|animation|@keyframes)\b")
        row = (VIEWS / "aerie_sidebar_row_view.cc").read_text()
        self.assertIn("SetTitleNote", (VIEWS / "aerie_sidebar_row_view.h").read_text())
        body = row[row.index("GetRenderedTooltipText"):]
        body = body[:body.index("\n}\n")]
        self.assertNotRegex(body, r"(?i)animat|motion")


class ResolverWiringTest(unittest.TestCase):
    def test_the_default_list_is_manual_rule_tidy_page(self):
        text = (TAB_MODEL / "title_resolver.cc").read_text()
        i = text.index("TitleResolver::CreateDefault")
        body = text[i:text.index("\n}\n", i)]
        self.assertLess(body.index("ManualNameProvider"), body.index("RuleTitleProvider"))
        self.assertLess(body.index("RuleTitleProvider"), body.index("TidyTitleProvider"))
        self.assertLess(body.index("TidyTitleProvider"), body.index("PageTitleProvider"))

    def test_the_new_code_cannot_reach_the_page(self):
        # document.title is never changed: the tidy, the rules and the handler have no WebContents, no script and no
        # navigation entry to write a title to.
        for path in NEW_SOURCES:
            text = path.read_text()
            for banned in ("WebContents", "ExecuteJavaScript", "document.title", "NavigationEntry", "SetTitle"):
                self.assertNotIn(banned, text, f"{path.name}: {banned}")

    def test_the_menu_item_exists(self):
        self.assertIn("kMakeRule = 18", (VIEWS / "aerie_sidebar_context_menu.h").read_text())
        self.assertIn("IDS_AERIE_SIDEBAR_MAKE_RULE", (VIEWS / "aerie_sidebar_context_menu.cc").read_text())


@unittest.skipUnless(shutil.which("node"), "node is not installed")
class RuleModelTest(unittest.TestCase):
    """Runs the page's rule model (it has no import) with node."""

    SCRIPT = r"""
import * as m from '%s';
const out = {};
const rule = (o) => ({id: 'a', name: 'n', type: 'domain', value: 'x.com', mode: 'template', text: '{title}', enabled: true, ...o});
out.ok = m.validateRule(rule({}), 0, true);
out.empty = m.validateRule(rule({value: '  '}), 0, true);
out.long_name = m.validateRule(rule({name: 'a'.repeat(101)}), 0, true);
out.long_text = m.validateRule(rule({text: 'a'.repeat(513)}), 0, true);
out.tpl = m.validateRule(rule({text: ' '}), 0, true);
out.rm = m.validateRule(rule({mode: 'remove', text: ''}), 0, true);
out.rm_title = m.validateRule(rule({mode: 'remove', text: '', type: 'title-contains'}), 0, true);
out.too_many = m.validateRule(rule({}), 50, true);
out.edit_full = m.validateRule(rule({}), 50, false);
out.multibyte = m.validateRule(rule({name: '€'.repeat(34)}), 0, true);
const list = [rule({id: 'a'}), rule({id: 'b'}), rule({id: 'c'})];
out.up = m.moveRule(list, 2, -1).map(r => r.id).join('');
out.down = m.moveRule(list, 0, 1).map(r => r.id).join('');
out.top = m.moveRule(list, 0, -1);
out.bottom = m.moveRule(list, 2, 1);
out.drag_down = m.moveRuleTo(list, 0, 2).map(r => r.id).join('');
out.drag_up = m.moveRuleTo(list, 2, 0).map(r => r.id).join('');
out.drag_same = m.moveRuleTo(list, 1, 1);
out.drag_out = [m.moveRuleTo(list, 0, 3), m.moveRuleTo(list, -1, 0), m.moveRuleTo(list, 0, 1.5), m.moveRuleTo(list, NaN, 0)];
out.orig = list.map(r => r.id).join('');
out.fromPref = m.rulesFromPref([{id: 'a', name: 'n', type: 'domain', value: 'x', mode: 'template', text: 't'}, 5, null,
  {id: 1}, {id: 'b', name: 'n', type: 'regex', value: 'x', mode: 'template', text: 't'},
  {id: 'c', name: 'n', type: 'domain', value: 'x', mode: 'template', text: 't', enabled: false}]).map(r => r.id + r.enabled).join(',');
out.notArray = m.rulesFromPref('x').length;
out.host = [m.hostFromInput(' HTTPS://www.Example.com:8080/a?b#c '), m.hostFromInput('*.news.example.com.'), m.hostFromInput('')];
out.id = m.newRuleId(1700000000000, 0.5);
out.idOk = /^[A-Za-z0-9_-]{1,64}$/.test(out.id);
console.log(JSON.stringify(out));
"""

    def test_the_model(self):
        model = (PAGE / "aerie_title_rules_model.ts").resolve()
        script = self.SCRIPT % model.as_posix()
        out = subprocess.run(["node", "--experimental-strip-types", "--no-warnings", "--input-type=module", "-e", script],
                             capture_output=True, text=True, cwd=str(REPO))
        self.assertEqual(out.returncode, 0, out.stderr)
        result = json.loads(out.stdout.strip().splitlines()[-1])
        self.assertEqual(result["ok"], "")
        self.assertEqual(result["empty"], "value-empty")
        self.assertEqual(result["long_name"], "name-long")
        self.assertEqual(result["long_text"], "text-long")
        self.assertEqual(result["tpl"], "template-empty")
        self.assertEqual(result["rm"], "remove-empty")
        self.assertEqual(result["rm_title"], "")
        self.assertEqual(result["too_many"], "too-many")
        self.assertEqual(result["edit_full"], "")
        # 34 euro signs are 102 bytes: the limit is in bytes, as the browser's is.
        self.assertEqual(result["multibyte"], "name-long")
        self.assertEqual(result["up"], "acb")
        self.assertEqual(result["down"], "bac")
        self.assertEqual(result["drag_down"], "bca")
        self.assertEqual(result["drag_up"], "cab")
        self.assertIsNone(result["drag_same"])
        self.assertEqual(result["drag_out"], [None, None, None, None])
        self.assertIsNone(result["top"])
        self.assertIsNone(result["bottom"])
        self.assertEqual(result["orig"], "abc")
        self.assertEqual(result["fromPref"], "atrue,cfalse")
        self.assertEqual(result["notArray"], 0)
        self.assertEqual(result["host"], ["example.com", "news.example.com", ""])
        self.assertTrue(result["idOk"])


class DocsTest(unittest.TestCase):
    def test_the_design_doc_describes_what_was_built(self):
        text = (REPO / "docs" / "sidebar-design.md").read_text()
        self.assertIn("What #146 built", text)
        self.assertIn("{title}", text)


if __name__ == "__main__":
    unittest.main()
