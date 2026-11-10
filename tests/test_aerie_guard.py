"""The Aerie Guard icon in the address bar (issue #99): the toolbar button is gone, the page action
is wired by hook patches, and the overlay reads only browser-side state and speaks only through the grd."""

import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
VIEWS = REPO / "overlay" / "chrome" / "browser" / "ui" / "views" / "aerie_adblock"
BROWSER = REPO / "overlay" / "components" / "aerie_adblock" / "browser"
GRDP = VIEWS / "aerie_guard_strings.grdp"
PATCHES = REPO / "patches" / "adblock"
SERIES = [l.strip() for l in (REPO / "patches" / "series").read_text().splitlines()
          if l.strip() and not l.startswith("#")]

ACTION_PATCH = "adblock/0014-register-aerie-guard-page-action.patch"
ITEM_PATCH = "adblock/0015-add-aerie-guard-action-item.patch"
TAB_PATCH = "adblock/0016-attach-aerie-guard-controller.patch"
STRINGS_PATCH = "adblock/0017-add-aerie-guard-strings.patch"


def patch(name: str) -> str:
    return (REPO / "patches" / name).read_text(encoding="utf-8")


def added(name: str) -> str:
    return "\n".join(l[1:] for l in patch(name).splitlines() if l.startswith("+") and not l.startswith("+++"))


def messages():
    return {m.get("name"): m for m in ET.parse(GRDP).getroot().iter("message")}


WEBUI = REPO / "overlay" / "chrome" / "browser" / "ui" / "webui" / "aerie_guard"


def panel_code() -> str:
    return "".join(p.read_text(encoding="utf-8") for p in sorted(WEBUI.glob("*.cc")))


def views_code() -> str:
    return "".join(p.read_text(encoding="utf-8") for p in sorted(VIEWS.glob("*.cc")) + sorted(VIEWS.glob("*.h")))


class ToolbarButtonGoneTest(unittest.TestCase):
    def test_the_overlay_files_and_the_patch_are_gone(self):
        self.assertFalse((VIEWS / "aerie_filter_toolbar_button.h").exists())
        self.assertFalse((VIEWS / "aerie_filter_toolbar_button.cc").exists())
        self.assertFalse((PATCHES / "0010-add-filter-toolbar-button.patch").exists())
        self.assertNotIn("adblock/0010-add-filter-toolbar-button.patch", SERIES)

    def test_nothing_still_names_the_button(self):
        for path in list(REPO.glob("overlay/**/*")) + list(REPO.glob("patches/**/*.patch")):
            if path.is_file() and path.suffix in {".cc", ".h", ".gn", ".patch"}:
                self.assertNotIn("AerieFilterToolbarButton", path.read_text(encoding="utf-8"), path)
                self.assertNotIn("aerie_filter_toolbar_button", path.read_text(encoding="utf-8"), path)

    def test_patch_number_0010_is_not_reused(self):
        self.assertEqual([p.name for p in PATCHES.glob("0010-*")], [])


class SeriesTest(unittest.TestCase):
    def test_the_hook_patches_follow_0013_in_order(self):
        tail = SERIES[SERIES.index("adblock/0013-bind-cosmetic-host.patch") + 1:]
        self.assertEqual(tail[:3], [ACTION_PATCH, ITEM_PATCH, TAB_PATCH])

    def test_the_strings_patch_follows_the_patches_that_add_the_lines_it_sits_between(self):
        for earlier in ("onboarding/0001-serve-aerie-setup-page.patch", "services/0006-remove-the-helium-services-ui.patch"):
            self.assertLess(SERIES.index(earlier), SERIES.index(STRINGS_PATCH))

    def test_the_tab_patch_comes_after_the_one_that_edits_the_same_file(self):
        self.assertLess(SERIES.index("adblock/0007-attach-cosmetic-tab-helper.patch"), SERIES.index(TAB_PATCH))


class ActionIdPatchTest(unittest.TestCase):
    def setUp(self):
        self.text = added(ACTION_PATCH)

    def test_the_id_is_registered_in_every_table(self):
        for needle in ("E(kActionAerieGuard)", "    kActionAerieGuard,", "kAerieGuard = 50,",
                       "kMaxValue = kAerieGuard,", ".type = PageActionIconType::kAerieGuard",
                       '<int value="50" label="Aerie Guard"/>', '<variant name="AerieGuard"/>',
                       "case MojomPageActionId::kActionAerieGuard:", "case kActionAerieGuard:"):
            with self.subTest(needle=needle):
                self.assertIn(needle, self.text)

    def test_guard_is_not_hidden_by_the_hidden_ids_list(self):
        self.assertNotIn("kHiddenPageActionIds", patch(ACTION_PATCH).split("---", 1)[1])

    def test_the_bookmark_star_stays_last(self):
        self.assertNotIn("kActionBookmarkThisTab", self.text)
        self.assertNotIn("static_assert", self.text)

    def test_the_icon_is_not_ephemeral_and_has_no_hard_coded_text(self):
        self.assertIn(".is_ephemeral = false", self.text)


class ActionItemPatchTest(unittest.TestCase):
    def test_the_action_item_uses_the_controller_and_a_grd_tooltip(self):
        text = added(ITEM_PATCH)
        self.assertIn("SetActionId(kActionAerieGuard)", text)
        self.assertIn("aerie_guard_page_action_controller()", text)
        self.assertIn("IDS_AERIE_GUARD_TOOLTIP_NONE", text)
        self.assertNotIn('u"', text)

    def test_it_does_not_reuse_upstreams_shield(self):
        self.assertNotIn("kShieldIcon", added(ITEM_PATCH))
        self.assertNotIn("kShieldIcon", views_code())


class TabPatchTest(unittest.TestCase):
    def test_the_controller_is_created_only_where_the_action_exists(self):
        text = added(TAB_PATCH)
        self.assertIn("ActionExists(kActionAerieGuard)", text)
        self.assertIn("std::make_unique<AerieGuardPageActionController>", text)
        self.assertIn("//chrome/browser/ui/views/aerie_adblock:guard", text)


class StringsTest(unittest.TestCase):
    def test_every_message_has_a_description_and_text(self):
        for name, msg in messages().items():
            with self.subTest(name=name):
                self.assertTrue((msg.get("desc") or "").strip())
                self.assertTrue("".join(msg.itertext()).strip())

    def test_no_upstream_product_name(self):
        for word in ("Chrome", "Chromium", "Helium", "Brave"):
            self.assertNotRegex(GRDP.read_text(encoding="utf-8"), rf"\b{word}\b")

    def test_every_message_is_used_and_every_id_asked_for_exists(self):
        code = views_code() + panel_code() + added(ITEM_PATCH)
        used = set(re.findall(r"\bIDS_AERIE_GUARD_\w+", code))
        self.assertEqual(used, set(messages()))

    def test_the_part_is_included_by_the_grd_and_listed_in_the_gritdeps(self):
        text = patch(STRINGS_PATCH)
        self.assertIn('+      <part file="../browser/ui/views/aerie_adblock/aerie_guard_strings.grdp" />', text)
        self.assertIn("+chrome/browser/ui/views/aerie_adblock/aerie_guard_strings.grdp", text)

    def test_the_controller_has_no_user_facing_literal(self):
        code = (VIEWS / "aerie_guard_page_action_controller.cc").read_text(encoding="utf-8")
        literals = re.findall(r'u"([^"]*)"', code)
        self.assertEqual(literals, [])


class PanelTest(unittest.TestCase):
    """Issue #100: the panel is Mojo-only and acts only on what the browser captured."""

    def test_no_chrome_send_and_no_message_handler(self):
        code = panel_code() + "".join(p.read_text() for p in WEBUI.glob("*.h"))
        code = "\n".join(l for l in code.splitlines() if not l.strip().startswith("//") and "TheDirectPage" not in l)
        code = re.sub(r'"[^"\n]*"', '""', code)
        self.assertNotIn("WebUIMessageHandler", code)
        self.assertNotIn("AddMessageHandler", code)
        self.assertNotIn("enable_chrome_send", code)
        self.assertNotIn("chrome.send", (REPO / "overlay/chrome/browser/resources/aerie_guard/app.ts").read_text())

    def test_mojom_inputs_are_bools_and_enums_only(self):
        text = (WEBUI / "aerie_guard.mojom").read_text()
        handler = text[text.index("interface PageHandler "):text.index("interface Page {")]
        for line in handler.splitlines():
            if "(" in line and "GetState" not in line and "{" not in line:
                self.assertRegex(line, r"\((bool \w+|Link \w+|BlockingLevel \w+)?\)", line)

    def test_config_is_not_preloadable(self):
        code = (WEBUI / "aerie_guard_ui.cc").read_text()
        self.assertRegex(code, r"IsPreloadable\(\) \{\s*return false;")

    def test_binder_hook_names_only_the_panel(self):
        text = added("adblock/0019-bind-aerie-guard-handler-factory.patch")
        self.assertIn("ForWebUI<aerie_guard::AerieGuardUI>", text)
        self.assertEqual(text.count(".Add<"), 1)

    def test_page_never_sends_a_host(self):
        ts = (REPO / "overlay/chrome/browser/resources/aerie_guard/app.ts").read_text()
        self.assertIn("setGuardOn(e.detail)", ts)
        self.assertNotRegex(ts, r"handler\.\w+\([^)]*(host|url|tab)", )

    def test_menu_strings_gone_and_subdomain_wording_is_symmetric(self):
        self.assertNotIn("IDS_AERIE_GUARD_MENU_TURN_ON", GRDP.read_text())
        self.assertIn("subdomains", "".join(messages()["IDS_AERIE_GUARD_PANEL_SCOPE"].itertext()))

    def test_panel_strings_from_the_issue(self):
        text = GRDP.read_text()
        self.assertIn("Guard is on for this site", text)
        self.assertIn("Guard is off for this site", text)
        self.assertIn("Guard on this site", text)

    def test_panel_patches_are_in_the_series(self):
        for n in ("0018-register-aerie-guard-page", "0019-bind-aerie-guard-handler-factory",
                  "0020-build-aerie-guard-resources", "0021-build-aerie-guard-browser-tests"):
            self.assertIn(f"adblock/{n}.patch", SERIES)


class CosmeticRowTest(unittest.TestCase):
    """Issue #101: the "Hide leftover ad boxes" row in the Advanced section."""

    TS = (REPO / "overlay/chrome/browser/resources/aerie_guard/app.ts").read_text()
    HTML = (REPO / "overlay/chrome/browser/resources/aerie_guard/app.html.ts").read_text()

    def test_the_row_is_in_the_collapsed_advanced_section_that_remembers_its_state(self):
        self.assertIn('<details id="advanced" ?open="${this.advancedOpen_}"', self.HTML)
        self.assertIn("advancedOpen_: boolean = readAdvancedOpen()", self.TS)
        self.assertIn("cosmeticToggle", self.HTML)
        self.assertIn("setCosmeticHidingOn(e.detail)", self.TS)
        # Storage can be missing: every access is guarded.
        self.assertEqual(self.TS.count("localStorage"), 2)
        self.assertEqual(self.TS.count("try {"), 2)

    def test_the_mojo_call_takes_a_bool_and_the_state_carries_only_bools(self):
        text = (WEBUI / "aerie_guard.mojom").read_text()
        self.assertIn("SetCosmeticHidingOn(bool on);", text)
        self.assertIn("bool cosmetic_hiding_on;", text)
        self.assertIn("bool cosmetic_locked;", text)

    def test_the_handler_acts_on_its_captured_target_and_nothing_else(self):
        code = (WEBUI / "aerie_guard_handler.cc").read_text()
        self.assertIn("target_.SetCosmeticHidingOn(on)", code)
        start = code.index("void AerieGuardHandler::FinishToggle")
        body = code[start:code.index("void AerieGuardHandler::SetFingerprintingOn")]
        self.assertIn("ClosePanel()", body)  # A stale target closes the bubble.
        self.assertNotRegex(body, r"GURL|host\(")

    def test_the_target_reloads_and_refuses_what_incognito_may_not_undo(self):
        code = (BROWSER / "guard_target.cc").read_text()
        body = code[code.index("GuardTarget::SetCosmeticHidingOn"):]
        for needle in ("Resolve()", "cosmetic_locked", "SetCosmeticHidingOff", "Reload("):
            self.assertIn(needle, body)

    def test_the_manager_installs_the_policy_in_init(self):
        code = (BROWSER / "filter_list_manager.cc").read_text()
        init = code[code.index("void FilterListManager::Init()"):code.index("void FilterListManager::OnStateLoaded")]
        self.assertIn("SetCosmeticFilteringPolicy", init)
        self.assertIn("AllowsCosmeticHiding", init)

    def test_no_new_mojo_interface_is_bound_for_another_controller(self):
        text = added("adblock/0019-bind-aerie-guard-handler-factory.patch")
        self.assertEqual(text.count(".Add<"), 1)
        # Issue #103 added patches 0022 to 0024 (the Settings section); none of them binds an interface.
        for path in sorted(PATCHES.glob("00[2-9][0-9]-*")):
            if path.name >= "0022":
                self.assertNotIn(".Add<", added(f"adblock/{path.name}"), path.name)


class FingerprintingRowTest(unittest.TestCase):
    """Issue #102: the fingerprinting row goes through the helper and the captured target, and says only what
    the code does."""

    def setUp(self):
        self.handler = (WEBUI / "aerie_guard_handler.cc").read_text()
        self.mojom = (WEBUI / "aerie_guard.mojom").read_text()
        self.ts = (REPO / "overlay/chrome/browser/resources/aerie_guard/app.ts").read_text()

    def body(self, name):
        start = self.handler.index(f"AerieGuardHandler::{name}(")
        return self.handler[start:self.handler.index("\n}\n", start)]

    def test_the_setter_goes_through_the_helium_helper_and_the_target(self):
        body = self.body("SetFingerprintingOn")
        self.assertIn("helium_noise::SetHeliumNoiseEnabledForOrigin", body)
        self.assertIn("helium_noise::CanSetHeliumNoiseForOrigin", body)
        self.assertIn("target_.Resolve()", body)
        self.assertIn("ClosePanel()", body)

    def test_the_url_is_the_committed_url_of_the_captured_tab(self):
        self.assertIn("GetPrimaryMainFrame()->GetLastCommittedURL()", self.handler)
        self.assertNotIn("GetVisibleURL", self.handler)

    def test_only_a_state_with_a_switch_can_change(self):
        body = self.body("SetFingerprintingOn")
        self.assertIn("kEnabled", body)
        self.assertIn("kDisabledForSite", body)
        self.assertNotIn("kGloballyDisabled", body)

    def test_the_state_comes_from_the_helper(self):
        self.assertIn("helium_noise::GetHeliumNoiseState", self.handler)
        for mode in ("kOn", "kOffForSite", "kOffInSettings", "kUnavailable"):
            self.assertIn(mode, self.mojom)

    def test_the_surface_list_is_the_one_helper_and_the_enum_is_complete(self):
        self.assertIn("aerie_fingerprint::ProtectedSurfaces()", self.handler)
        header = (REPO / "overlay/components/aerie_fingerprint/protected_surfaces.h").read_text()
        cpp = re.findall(r"^  (k\w+),$", header, re.M)
        mojom = re.search(r"enum ProtectedSurface \{(.*?)\};", self.mojom, re.S).group(1)
        self.assertEqual(cpp, re.findall(r"(k\w+),", mojom))
        listing = (REPO / "overlay/components/aerie_fingerprint/protected_surfaces.cc").read_text()
        for name in cpp:
            self.assertIn(f"ProtectedSurface::{name}", listing)
            self.assertIn(f"mojom::ProtectedSurface::{name}", self.handler)
            self.assertIn(f"ProtectedSurface.{name}", self.ts)

    def test_the_new_calls_take_a_bool_and_nothing_else(self):
        self.assertIn("SetFingerprintingOn(bool on);", self.mojom)
        self.assertIn("Reload();", self.mojom)
        self.assertIn("setFingerprintingOn(e.detail)", self.ts)

    def test_the_page_has_no_switch_when_protection_is_off_in_settings(self):
        html = (REPO / "overlay/chrome/browser/resources/aerie_guard/app.html.ts").read_text()
        self.assertIn("fingerprintingHasSwitch_()", html)
        self.assertIn("kOffInSettings", html)
        self.assertIn("Link.kNoiseSettings", self.ts)

    def test_no_new_interface_is_bound_and_there_is_still_one_binder(self):
        self.assertEqual(self.mojom.count("PageHandlerFactory {"), 1)
        text = added("adblock/0019-bind-aerie-guard-handler-factory.patch")
        self.assertEqual(text.count(".Add<"), 1)

    def test_the_browser_tests_cover_the_acceptance_cases(self):
        text = (WEBUI / "aerie_guard_browsertest.cc").read_text()
        for needle in ("a.localhost", "b.localhost", "CreateIncognitoBrowser", "toDataURL", "SetFingerprintingOn(false)",
                       "IsOffTheRecord", "helium_noise"):
            self.assertIn(needle, text)

    def test_the_switch_gets_its_scope_and_reload_prompt_from_the_grd(self):
        for name in ("IDS_AERIE_GUARD_PANEL_FINGERPRINTING", "IDS_AERIE_GUARD_PANEL_RELOAD_TO_APPLY",
                     "IDS_AERIE_GUARD_PANEL_FINGERPRINTING_OFF_IN_SETTINGS"):
            self.assertIn(name, messages())
        self.assertIn("subdomains", "".join(messages()["IDS_AERIE_GUARD_PANEL_FINGERPRINTING_SCOPE"].itertext()))
        self.assertEqual(messages()["IDS_AERIE_GUARD_PANEL_FINGERPRINTING"].text.strip(), "Fingerprinting protection")

    def test_the_page_has_no_hard_coded_sentence(self):
        html = (REPO / "overlay/chrome/browser/resources/aerie_guard/app.html.ts").read_text()
        self.assertNotRegex(html, r">\s*[A-Z][a-z]+ [a-z]+")


class IconTest(unittest.TestCase):
    def test_the_two_icons_exist_and_are_aggregated(self):
        gn = (VIEWS / "vector_icons" / "BUILD.gn").read_text()
        for name in ("aerie_guard_on.icon", "aerie_guard_off.icon"):
            self.assertTrue((VIEWS / "vector_icons" / name).is_file())
            self.assertIn(f'"{name}"', gn)
        for template in ("vector_icons.cc.template", "vector_icons.h.template"):
            self.assertTrue((VIEWS / "vector_icons" / template).is_file())

    def test_the_code_names_both_generated_icons(self):
        code = views_code()
        self.assertIn("kAerieGuardOnIcon", code)
        self.assertIn("kAerieGuardOffIcon", code)


class BrowserSideStateTest(unittest.TestCase):
    def test_the_host_comes_from_the_primary_main_frames_committed_url(self):
        code = (BROWSER / "guard_target.cc").read_text(encoding="utf-8")
        self.assertIn("GetPrimaryMainFrame()->GetLastCommittedURL()", code)
        self.assertNotIn("GetVisibleURL", code)
        self.assertNotIn("GetURL()", code)

    def test_the_state_logic_only_shows_http_and_https(self):
        code = (BROWSER / "guard_state.cc").read_text(encoding="utf-8")
        self.assertIn("SchemeIsHTTPOrHTTPS()", code)

    def test_the_toggle_goes_through_the_context_api(self):
        code = (BROWSER / "guard_target.cc").read_text(encoding="utf-8")
        self.assertRegex(code, r"manager->SetException\([^;]*GetBrowserContext\(\)\)")
        self.assertNotIn("ForTesting", views_code())

    def test_the_icon_makes_no_network_request(self):
        for word in ("URLLoader", "SimpleURLLoader", "network::", "NetworkContext"):
            self.assertNotIn(word, views_code())

    def test_the_unit_tests_are_built_and_cover_the_cases(self):
        gn = (BROWSER / "BUILD.gn").read_text()
        self.assertIn('"guard_state_unittest.cc"', gn)
        self.assertIn('source_set("guard_state")', gn)
        test = (BROWSER / "guard_state_unittest.cc").read_text()
        for needle in ("GuardBadgeLabel(0)", "GuardBadgeLabel(1)", "GuardBadgeLabel(99)", "GuardBadgeLabel(100)",
                       "http://news.test", "https://news.test", "chrome://settings", "file:///", "OffTheRecord"):
            with self.subTest(needle=needle):
                self.assertIn(needle, test)


if __name__ == "__main__":
    unittest.main()


class BlockedListTest(unittest.TestCase):
    """Issue #110: the list of blocked addresses is bounded, memory only and shown as text."""

    LIST_H = (BROWSER / "blocked_url_list.h").read_text()
    LIST_CC = (BROWSER / "blocked_url_list.cc").read_text()
    COUNTER = (BROWSER / "page_block_counter.cc").read_text() + (BROWSER / "page_block_counter.h").read_text()
    HTML = (REPO / "overlay/chrome/browser/resources/aerie_guard/app.html.ts").read_text()
    TS = (REPO / "overlay/chrome/browser/resources/aerie_guard/app.ts").read_text()

    def test_the_bounds_are_200_entries_and_256_characters(self):
        self.assertRegex(self.LIST_H, r"kMaxEntries = 200;")
        self.assertRegex(self.LIST_H, r"kMaxUrlLength = 256;")

    def test_the_browser_test_has_a_negative_control_and_an_incognito_case(self):
        bt = (WEBUI / "aerie_guard_browsertest.cc").read_text()
        self.assertIn("ScopedLimitsForTesting", bt)
        self.assertIn("TheUnboundedStoreShowsWhatTheBoundCuts", bt)
        self.assertIn("IncognitoStoresTheSameWay", bt)
        self.assertIn("ScopedLimitsForTesting", self.LIST_H)
        self.assertNotRegex(bt, r"while \(aerie_adblock::PageBlockCounter::CountFor\([^)]*\) < 300\) \{")

    def test_nothing_logs_or_saves_the_urls(self):
        for text in (self.LIST_CC, self.COUNTER, panel_code()):
            code = "\n".join(l for l in text.splitlines() if not l.strip().startswith("//"))
            for banned in ("LOG(", "VLOG(", "DLOG(", "PrefService", "WriteFile", "SetString", "ScopedDictPrefUpdate"):
                self.assertNotIn(banned, code)

    def test_the_urls_are_cleared_with_the_count(self):
        self.assertIn("urls_.Clear();", self.COUNTER)
        self.assertEqual(self.COUNTER.count("urls_.Clear();"), 2)

    def test_the_mojom_sends_plain_strings(self):
        text = (WEBUI / "aerie_guard.mojom").read_text()
        self.assertIn("array<string> blocked_urls;", text)

    def test_the_panel_renders_the_addresses_as_text(self):
        self.assertIn("${url}</li>", self.HTML)
        for unsafe in ("unsafeHTML", "innerHTML", "href=\"${url", "src=\"${url"):
            self.assertNotIn(unsafe, self.HTML + self.TS)

    def test_the_strings_are_in_the_grd_and_used(self):
        names = messages()
        ui = (WEBUI / "aerie_guard_ui.cc").read_text()
        for name in ("IDS_AERIE_GUARD_PANEL_BLOCKED_LIST", "IDS_AERIE_GUARD_PANEL_BLOCKED_LIST_CUT"):
            self.assertIn(name, names)
            self.assertIn(name, ui)
        self.assertNotRegex(self.HTML, r">\s*Show what was blocked")

    def test_no_new_interface(self):
        text = (WEBUI / "aerie_guard.mojom").read_text()
        code = "\n".join(l for l in text.splitlines() if not l.strip().startswith("//"))
        declared = re.findall(r"^interface (\w+)", code, re.M)
        self.assertEqual(len(declared), 3, declared)
