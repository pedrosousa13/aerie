"""Blocking levels (issue #109): Aggressive and Standard, globally and per site.

These are source checks. The C++ and the TypeScript are built, and the browser behaviour is shown by the Bench check
``blocking_levels``, in the combined build."""

import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BROWSER = REPO / "overlay/components/aerie_adblock/browser"
WEBUI = REPO / "overlay/chrome/browser/ui/webui/aerie_guard"
SETTINGS = REPO / "overlay/chrome/browser/ui/webui/aerie_guard_settings"
PANEL_TS = REPO / "overlay/chrome/browser/resources/aerie_guard"
SERIES = [l.strip() for l in (REPO / "patches/series").read_text().splitlines()
          if l.strip() and not l.startswith("#")]
PATCH = "adblock/0026-add-aerie-guard-level-settings-strings.patch"


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def code(path: Path) -> str:
    return re.sub(r"//[^\n]*", "", text(path))


class EngineTest(unittest.TestCase):
    def test_standard_skips_first_party_before_any_rule_is_asked(self):
        body = code(BROWSER / "adblock_engine.cc")
        body = body[body.index("bool AdblockEngine::ShouldBlock"):]
        standard = body.index("BlockingLevel::kStandard")
        self.assertIn("!request.third_party", body[standard:standard + 80])
        # Standard first party asks the custom-lists engine, never the all-lists one, so no built-in rule
        # (`$important` too) can override it.
        self.assertIn("first_party_only ? *first_party_engine_ : *engine_", body)
        self.assertLess(standard, body.index("engine.should_block("))

    def test_the_custom_lists_are_compiled_alone_for_the_first_party_engine(self):
        engine = code(BROWSER / "adblock_engine.cc")
        self.assertIn("custom_builder->add_list", engine)
        self.assertIn("first_party_engine_ = std::move(custom_only)", engine)
        self.assertIn("input.custom = !entry.builtin", code(BROWSER / "filter_list_manager.cc"))
        self.assertIn("custom.push_back(input.custom)", code(BROWSER / "filter_engine_service.cc"))

    def test_the_default_is_aggressive_so_the_bench_still_sees_first_party_blocks(self):
        header = text(BROWSER / "blocking_level.h")
        self.assertRegex(header, r"kDefaultBlockingLevel\s*=\s*BlockingLevel::kAggressive")
        self.assertIn("BlockingLevel level = kDefaultBlockingLevel", text(BROWSER / "adblock_engine.h"))

    def test_the_level_is_not_a_field_of_the_request_and_comes_from_the_browsers_policy(self):
        self.assertNotIn("level", code(BROWSER / "classified_request.h").lower())
        service = code(BROWSER / "filter_engine_service.cc")
        self.assertIn("BlockingLevelFor(request.source_hostname, request.session)", service)
        # The host is the top-level site the browser classified; nothing here parses a URL from a message.
        self.assertNotIn("mojom", service)

    def test_the_manager_installs_the_policy_in_init(self):
        manager = code(BROWSER / "filter_list_manager.cc")
        init = manager[manager.index("void FilterListManager::Init()"):manager.index("void FilterListManager::OnStateLoaded")]
        self.assertIn("SetBlockingLevelPolicy", init)
        self.assertIn("EffectiveBlockingLevel", init)

    def test_the_header_records_what_brave_core_does(self):
        header = text(BROWSER / "blocking_level.h")
        self.assertIn("AdBlockEngineWrapper::ShouldStartRequest", header)
        self.assertIn("$important", header)

    def test_the_new_sources_are_in_the_gn_targets(self):
        gn = text(BROWSER / "BUILD.gn")
        self.assertIn('"blocking_level.h"', gn)


class StorageTest(unittest.TestCase):
    def test_the_saved_state_is_capped_normalized_and_validated_on_load(self):
        state = code(BROWSER / "filter_list_state.cc")
        self.assertIn("kMaxLevelOverrides", state)
        self.assertIn("NormalizeExceptionHost(host)", state[state.index("::SetLevelOverride"):])
        from_value = state[state.index("FilterListState::FromValue"):]
        self.assertIn("BlockingLevelFromName", from_value)

    def test_an_incognito_level_is_memory_only(self):
        manager = code(BROWSER / "filter_list_manager.cc")
        body = manager[manager.index("ExceptionResult FilterListManager::SetBlockingLevelForSession"):]
        body = body[:body.index("ExceptionResult FilterListManager::SetBlockingLevel(")]
        off_the_record = body[body.index("is_off_the_record()"):body.index("state_.SetLevelOverride")]
        self.assertNotIn("Save()", off_the_record)
        self.assertIn("session_levels_", off_the_record)
        self.assertIn("session_levels_.erase(session)", manager[manager.index("::ClearSessionExceptions"):])


class PanelTest(unittest.TestCase):
    MOJOM = text(WEBUI / "aerie_guard.mojom")

    def test_the_call_takes_an_enum_and_the_state_carries_an_enum(self):
        self.assertIn("SetBlockingLevel(BlockingLevel level);", self.MOJOM)
        self.assertIn("BlockingLevel blocking_level;", self.MOJOM)
        self.assertRegex(self.MOJOM, r"enum BlockingLevel \{\s*[^}]*kAggressive,[^}]*kStandard,")

    def test_no_new_interface_message_handler_or_binder(self):
        self.assertEqual(self.MOJOM.count("interface "), 3)
        ui = code(WEBUI / "aerie_guard_ui.cc")
        self.assertNotIn("AddMessageHandler", ui)
        self.assertNotIn("WebUIMessageHandler", code(WEBUI / "aerie_guard_handler.h"))
        for path in sorted((REPO / "patches/adblock").glob("00[2-9][0-9]-*")):
            if path.name >= "0026":
                self.assertNotIn(".Add<", path.read_text(), path.name)

    def test_the_handler_acts_on_its_captured_target_(self):
        handler = code(WEBUI / "aerie_guard_handler.cc")
        body = handler[handler.index("void AerieGuardHandler::SetBlockingLevel"):handler.index("void AerieGuardHandler::SetCosmeticHidingOn")]
        self.assertEqual(body.count("target_.SetBlockingLevel("), 2)
        self.assertIn("FinishToggle(", body)
        self.assertNotIn("ReportBadMessage", body)  # Mojo rejects an unknown enum value before the handler runs.
        self.assertNotRegex(body, r"GURL|host\(|SetException")

    def test_the_target_resolves_first_and_reloads(self):
        target = code(BROWSER / "guard_target.cc")
        body = target[target.index("GuardTarget::SetBlockingLevel"):]
        for needle in ("Resolve()", "state->locked", "SetBlockingLevel(host_", "Reload("):
            self.assertIn(needle, body)
        self.assertLess(body.index("Resolve()"), body.index("SetBlockingLevel(host_"))

    def test_the_page_sends_only_the_enum_or_the_existing_switch(self):
        ts = text(PANEL_TS / "app.ts")
        self.assertIn("setBlockingLevel(BlockingLevel.kAggressive)", ts)
        self.assertIn("setBlockingLevel(BlockingLevel.kStandard)", ts)
        self.assertIn("setGuardOn(false)", ts)
        self.assertNotIn("chrome.send", ts)
        self.assertNotRegex(ts, r"handler\.\w+\([^)]*(host|url|tab)")

    def test_the_row_is_a_radio_group_of_aggressive_standard_and_off(self):
        html = text(PANEL_TS / "app.html.ts")
        self.assertIn('role="radiogroup"', html)
        for key in ("levelAggressive", "levelStandard", "levelOff"):
            self.assertIn(f"this.i18n_('{key}')", html)
        self.assertEqual(html.count('role="radio"'), 3)

    def test_the_strings_are_in_the_grd_and_used(self):
        grdp = text(REPO / "overlay/chrome/browser/ui/views/aerie_adblock/aerie_guard_strings.grdp")
        ui = text(WEBUI / "aerie_guard_ui.cc")
        for name in ("LEVEL_LABEL", "LEVEL_AGGRESSIVE", "LEVEL_STANDARD", "LEVEL_OFF", "LEVEL_AGGRESSIVE_DESC",
                     "LEVEL_STANDARD_DESC", "LEVEL_OFF_DESC", "LEVEL_RELOAD"):
            self.assertIn(f'name="IDS_AERIE_GUARD_PANEL_{name}"', grdp)
            self.assertIn(f"IDS_AERIE_GUARD_PANEL_{name}", ui)


class SettingsTest(unittest.TestCase):
    HANDLER = code(SETTINGS / "aerie_guard_settings_handler.cc")

    def test_the_message_accepts_only_the_two_level_names(self):
        body = self.HANDLER[self.HANDLER.index("void AerieGuardSettingsHandler::HandleSetDefaultLevel"):]
        body = body[:body.index("void AerieGuardSettingsHandler::HandleSetAutoUpdate")]
        self.assertIn("StringArg(args, 1)", body)
        self.assertIn("BlockingLevelFromName(*name)", body)
        self.assertIn('Reject(args, "bad_argument")', body)
        self.assertIn("args.size() == 2", body)

    def test_the_state_reports_the_default_and_the_page_shows_it(self):
        self.assertIn('out.Set("defaultLevel"', self.HANDLER)
        page = text(REPO / "overlay/chrome/browser/resources/settings/aerie_guard_page/aerie_guard_page.html.ts")
        self.assertIn('<select id="defaultLevel"', page)
        for key in ("aerieGuardDefaultLevel", "aerieGuardDefaultLevelDesc", "aerieGuardLevelAggressive",
                    "aerieGuardLevelStandard"):
            self.assertIn("$i18n{%s}" % key, page)

    def test_the_four_strings_reach_the_page_through_patch_0026_after_0022(self):
        self.assertIn(PATCH, SERIES)
        self.assertGreater(SERIES.index(PATCH), SERIES.index("adblock/0022-add-aerie-guard-settings-backend.patch"))
        patch = (REPO / "patches" / PATCH).read_text()
        self.assertTrue(patch.startswith("Aerie-Purpose:"))
        for name in ("DEFAULT_LEVEL", "DEFAULT_LEVEL_DESC", "LEVEL_AGGRESSIVE", "LEVEL_STANDARD"):
            self.assertIn(f"IDS_SETTINGS_AERIE_GUARD_{name}", patch)
            self.assertIn(f'name="IDS_SETTINGS_AERIE_GUARD_{name}"',
                          text(SETTINGS / "aerie_guard_settings_strings.grdp"))

    def test_it_is_still_the_regular_profiles_manager_and_adds_no_endpoint(self):
        self.assertNotIn("SessionKeyFor(", self.HANDLER)
        endpoints = text(REPO / "docs/network-endpoints.md")
        self.assertNotIn("level", endpoints.lower().replace("levels of", ""))


if __name__ == "__main__":
    unittest.main()
