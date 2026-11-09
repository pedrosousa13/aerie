"""The Aerie Guard section of Settings (issue #103): a top-level Lit route, a handler that wraps
FilterListManager, the pref the Guard icon reads, and the redirect of chrome://filters.

These are source checks. The C++ and the TypeScript are built in the combined build."""

import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OVERLAY = REPO / "overlay"
HANDLER_DIR = OVERLAY / "chrome/browser/ui/webui/aerie_guard_settings"
PAGE_DIR = OVERLAY / "chrome/browser/resources/settings/aerie_guard_page"
VIEWS = OVERLAY / "chrome/browser/ui/views/aerie_adblock"
BROWSER = OVERLAY / "components/aerie_adblock/browser"
SERIES = [l.strip() for l in (REPO / "patches/series").read_text().splitlines()
          if l.strip() and not l.startswith("#")]

BACKEND = "adblock/0022-add-aerie-guard-settings-backend.patch"
PAGE = "adblock/0023-add-aerie-guard-settings-page.patch"
REDIRECT = "adblock/0024-redirect-filters-to-aerie-guard-settings.patch"
PREF = "aerie.guard.show_blocked_count"


def patch(name: str) -> str:
    return (REPO / "patches" / name).read_text(encoding="utf-8")


def added(name: str) -> str:
    return "\n".join(l[1:] for l in patch(name).splitlines() if l.startswith("+") and not l.startswith("+++"))


def removed(name: str) -> str:
    return "\n".join(l[1:] for l in patch(name).splitlines() if l.startswith("-") and not l.startswith("---"))


def code(path: Path) -> str:
    return re.sub(r"//[^\n]*", "", path.read_text(encoding="utf-8"))


HANDLER_CC = HANDLER_DIR / "aerie_guard_settings_handler.cc"
HANDLER_H = HANDLER_DIR / "aerie_guard_settings_handler.h"
STRINGS = HANDLER_DIR / "aerie_guard_settings_strings.grdp"


class SeriesTest(unittest.TestCase):
    def test_the_three_patches_follow_the_panel_patches_in_order(self):
        last = SERIES.index("adblock/0021-build-aerie-guard-browser-tests.patch")
        self.assertEqual(SERIES[last + 1:last + 4], [BACKEND, PAGE, REDIRECT])

    def test_every_patch_file_is_listed(self):
        for name in (BACKEND, PAGE, REDIRECT):
            self.assertTrue((REPO / "patches" / name).is_file(), name)


class HandlerTest(unittest.TestCase):
    def test_every_argument_is_checked_for_type_and_length(self):
        text = code(HANDLER_CC)
        self.assertIn("constexpr size_t kMaxArgLength = 4096;", text)
        self.assertRegex(text, r"is_string\(\)")
        self.assertRegex(text, r"size\(\)\s*<=\s*kMaxArgLength")
        self.assertRegex(text, r"is_bool\(\)")

    def test_a_bad_message_is_refused_not_ignored(self):
        text = code(HANDLER_CC)
        self.assertIn("RejectJavascriptCallback", text)
        self.assertNotIn("CHECK_EQ", text)  # a message is input: it must not crash the browser

    def test_every_message_is_a_promise_message(self):
        text = code(HANDLER_CC)
        names = re.findall(r'"(aerieGuard\.\w+)"', text)
        self.assertEqual(sorted(names), sorted([
            "aerieGuard.getState", "aerieGuard.addList", "aerieGuard.removeList", "aerieGuard.updateList",
            "aerieGuard.updateAll", "aerieGuard.setException", "aerieGuard.setAutoUpdate"]))
        self.assertGreaterEqual(text.count("ResolveJavascriptCallback"), 1)

    def test_settings_acts_on_the_regular_profile_and_never_shows_a_session(self):
        text = code(HANDLER_CC)
        self.assertIn("SessionKey::Regular()", text)
        self.assertIn("GetOriginalProfile()", text)
        self.assertNotIn("SessionKeyFor(", text)
        self.assertNotIn("SessionExceptions", text)

    def test_the_manager_keeps_the_rules(self):
        text = code(HANDLER_CC)
        for call in ("AddList(", "RemoveList(", "UpdateList(", "UpdateAll()", "SetException(", "SetAutoUpdate("):
            self.assertIn(call, text)
        self.assertNotIn("WatchSession", text)
        self.assertNotIn("HasSavedException", text)

    def test_the_handler_is_a_web_ui_message_handler_with_no_user_facing_literal(self):
        self.assertIn("content::WebUIMessageHandler", code(HANDLER_H))
        # Errors go to the page as codes, never as sentences: the page owns the words (grd).
        for literal in re.findall(r'"([^"]*)"', code(HANDLER_CC)):
            if literal.startswith("aerieGuard.") or literal.startswith("aerie-guard"):
                continue
            self.assertNotRegex(literal, r"[A-Za-z]+ [A-Za-z]+", literal)

    def test_it_is_registered_only_by_the_settings_ui(self):
        text = added(BACKEND)
        self.assertIn("settings_ui.cc", patch(BACKEND))
        self.assertIn("std::make_unique<aerie_guard::AerieGuardSettingsHandler>", text)
        for other in (OVERLAY / "chrome/browser/ui/webui").rglob("*.cc"):
            if other.parent == HANDLER_DIR:
                continue
            self.assertNotIn("AerieGuardSettingsHandler", other.read_text(encoding="utf-8"), other)
        for p in (PAGE, REDIRECT):
            self.assertNotIn("AerieGuardSettingsHandler", patch(p))

    def test_the_target_is_visible_to_settings_only(self):
        gn = (HANDLER_DIR / "BUILD.gn").read_text()
        self.assertIn('visibility = [ "//chrome/browser/ui/webui/settings:*" ]', gn)
        self.assertIn("aerie_guard_settings_handler.cc", gn)


class PrefTest(unittest.TestCase):
    def test_the_pref_is_registered_on_by_default_and_not_synced(self):
        text = code(BROWSER / "aerie_guard_prefs.cc")
        self.assertIn(f'kShowBlockedCount[] = "{PREF}"', code(BROWSER / "aerie_guard_prefs.h"))
        self.assertRegex(text, r"RegisterBooleanPref\(kShowBlockedCount,\s*true\)")
        self.assertIn("RegisterProfilePrefs", added(BACKEND))

    def test_settings_may_write_the_same_name(self):
        self.assertIn(f'(*s_allowlist)["{PREF}"] = settings_api::PrefType::kBoolean;',
                      re.sub(r"\s+", " ", added(BACKEND)))

    def test_the_icon_reads_the_pref_and_redraws_when_it_changes(self):
        text = code(VIEWS / "aerie_guard_page_action_controller.cc")
        self.assertIn("aerie_guard::kShowBlockedCount", text)
        self.assertIn("PrefChangeRegistrar", code(VIEWS / "aerie_guard_page_action_controller.h"))
        self.assertIn("GuardBadgeLabel(state.count, ShowsBlockedCount())", text)

    def test_the_tooltip_still_states_the_count_when_the_badge_is_hidden(self):
        text = code(VIEWS / "aerie_guard_page_action_controller.cc")
        self.assertIn("IDS_AERIE_GUARD_TOOLTIP_BLOCKED, state.count", re.sub(r"\s+", " ", text))
        label = (BROWSER / "guard_state.cc").read_text()
        self.assertRegex(label, r"if \(!show_count\)")

    def test_the_label_logic_has_unit_tests(self):
        text = (BROWSER / "guard_state_unittest.cc").read_text()
        self.assertIn("GuardBadgeLabel(7, false)", text)
        self.assertIn("aerie_guard_prefs_unittest.cc", (BROWSER / "BUILD.gn").read_text())


class StringsTest(unittest.TestCase):
    def messages(self):
        return {m.get("name"): m for m in ET.parse(STRINGS).getroot().iter("message")}

    def test_every_message_has_a_description_and_text(self):
        for name, msg in self.messages().items():
            with self.subTest(name=name):
                self.assertTrue((msg.get("desc") or "").strip())
                self.assertTrue("".join(msg.itertext()).strip())

    def test_the_count_switch_has_the_words_of_the_brief(self):
        text = "".join(self.messages()["IDS_SETTINGS_AERIE_GUARD_SHOW_COUNT"].itertext())
        self.assertEqual(text.strip(), "Show the number of blocked items on the Guard icon")

    def test_the_menu_entry_is_aerie_guard(self):
        text = "".join(self.messages()["IDS_SETTINGS_AERIE_GUARD_TITLE"].itertext())
        self.assertEqual(text.strip(), "Aerie Guard")

    def test_every_message_is_given_to_the_page_and_every_name_exists(self):
        used = set(re.findall(r"\bIDS_SETTINGS_AERIE_GUARD_\w+", added(BACKEND)))
        self.assertEqual(used, set(self.messages()))

    def test_no_upstream_product_name(self):
        for word in ("Chrome", "Chromium", "Helium", "Brave"):
            self.assertNotRegex(STRINGS.read_text(encoding="utf-8"), rf"\b{word}\b")

    def test_the_part_is_included_by_the_grd_and_listed_in_the_gritdeps(self):
        text = patch(BACKEND)
        self.assertIn("aerie_guard_settings_strings.grdp", added(BACKEND))
        self.assertIn("generated_resources.grd.gritdeps", text)
        self.assertIn("chrome/browser/ui/webui/aerie_guard_settings/aerie_guard_settings_strings.grdp", added(BACKEND))

    def test_the_page_has_no_hard_coded_sentence(self):
        for path in PAGE_DIR.glob("*.html.ts"):
            text = path.read_text(encoding="utf-8")
            text = text[text.index("_html_template_start_"):]
            text = re.sub(r"\$\{[^{}]*\}", "", text)  # an expression is not text
            # Text nodes of the template (between > and <) must be an $i18n{}.
            for node in re.findall(r">([^<>$`]*[A-Za-z]{3,}[^<>$`]*)<", text):
                self.assertFalse(node.strip(), f"{path.name}: hard-coded text {node.strip()!r}")
            self.assertNotRegex(text, r'(label|sub-label|placeholder|aria-label)="[A-Za-z]')


class PageTest(unittest.TestCase):
    TS = (PAGE_DIR / "aerie_guard_page.ts").read_text(encoding="utf-8")
    HTML = (PAGE_DIR / "aerie_guard_page.html.ts").read_text(encoding="utf-8")

    def test_it_is_a_lit_settings_view_and_a_plugin(self):
        self.assertIn("SettingsViewMixinLit(CrLitElement)", self.TS)
        self.assertIn("implements SettingsPlugin", self.TS)
        self.assertIn("searchContents(", self.TS)
        self.assertIn("'settings-aerie-guard-page'", self.TS)

    def test_the_route_is_top_level_and_wired_like_the_hardening_page(self):
        text = added(PAGE)
        self.assertIn("r.AERIE_GUARD = r.BASIC.createSection(", text)
        self.assertIn("'/aerieGuard', 'aerieGuard'", text)
        self.assertIn("AERIE_GUARD: Route;", text)
        self.assertIn("import './aerie_guard_page/aerie_guard_page.js';", text)
        self.assertIn("export {SettingsAerieGuardPageElement}", text)
        for file in ("route.ts", "router.ts", "lazy_load.ts", "settings_menu.html", "settings_main.html",
                     "resources/settings/BUILD.gn"):
            self.assertIn(file, patch(PAGE), file)

    def test_the_menu_entry_is_a_string_from_the_grd(self):
        text = added(PAGE)
        self.assertIn('href="/aerieGuard"', text)
        self.assertIn("$i18n{aerieGuardPageTitle}", text)

    def test_the_count_switch_is_on_the_pref(self):
        self.assertIn(f'pref-key="{PREF}"', self.HTML)
        self.assertIn("$i18n{aerieGuardShowCount}", self.HTML)

    def test_the_page_lists_exceptions_and_has_the_update_switch(self):
        for needle in ("exceptions_", "onAutoUpdateChange_", "onAddSiteClick_", "onRemoveSiteClick_"):
            self.assertIn(needle, self.HTML)

    def test_the_page_shows_no_dead_link(self):
        # Lists, the catalogue, custom filters and subscriptions (#104 to #108) add their own rows.
        self.assertNotIn("navigateTo", self.TS)
        self.assertNotIn("cr-link-row", self.HTML)

    def test_the_proxy_uses_the_promise_messages_of_the_handler(self):
        proxy = (PAGE_DIR / "aerie_guard_browser_proxy.ts").read_text(encoding="utf-8")
        handler = set(re.findall(r'"(aerieGuard\.\w+)"', code(HANDLER_CC)))
        used = set(re.findall(r"sendWithPromise\('(aerieGuard\.\w+)'", proxy))
        self.assertEqual(used, handler)


class RedirectTest(unittest.TestCase):
    def test_the_old_page_is_gone(self):
        self.assertFalse((OVERLAY / "chrome/browser/ui/webui/aerie_filters").exists())
        gone = removed(REDIRECT)
        for needle in ("aerie_filters_ui.h", "AerieFiltersUIConfig", "aerie_filters_resources.pak",
                       "aerie_filters/aerie_filters_resources.grd", ":resources"):
            self.assertIn(needle, gone)
        self.assertNotIn("aerieFilters", "".join(p.read_text(errors="ignore") for p in
                                                (OVERLAY / "chrome").rglob("*") if p.is_file()))

    def test_chrome_filters_is_rewritten_into_the_section(self):
        text = added(REDIRECT)
        self.assertIn("kLegacyAerieFiltersHost[] = \"filters\"", text)
        self.assertIn("aerieGuard", text)
        self.assertIn("chrome_content_browser_client.cc", patch(REDIRECT))

    def test_the_panels_footer_opens_the_section(self):
        handler = (OVERLAY / "chrome/browser/ui/webui/aerie_guard/aerie_guard_handler.cc").read_text()
        self.assertIn('kAerieGuardSubPage[] = "aerieGuard"', handler)
        self.assertNotIn("chrome://filters", handler)
        self.assertIn("kAerieGuardSettings", (OVERLAY / "chrome/browser/ui/webui/aerie_guard/aerie_guard.mojom").read_text())

    def test_the_ledger_names_the_helium_patch_the_menu_hunk_sits_next_to(self):
        ledger = (REPO / "docs/helium-dependency-ledger.md").read_text()
        row = next(l for l in ledger.splitlines() if "adblock/0023-add-aerie-guard-settings-page.patch" in l)
        self.assertIn("helium-chromium/patches/helium/settings/reorder-settings-menu.patch", row)


if __name__ == "__main__":
    unittest.main()
