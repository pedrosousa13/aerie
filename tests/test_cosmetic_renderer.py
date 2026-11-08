"""Issue #68: the renderer-side piece of cosmetic filtering, and the limits ADR 0005 puts on it.

ADR 0002 kept cosmetic filtering in the browser process. ADR 0005 allows one small renderer piece:
a user-origin hide sheet and a late-element watch, like Brave Shields. These tests read the sources
and fail when the piece grows past what the ADR allows. They need no compiler. The C++ unit tests
(`*_unittest.cc`) and the Bench check `cosmetic_filtering` prove the behavior on a real build.
"""

import re
import unittest
from pathlib import Path

from aerie_build import patches

REPO = Path(__file__).resolve().parents[1]
ADBLOCK = REPO / "overlay/components/aerie_adblock"
COMMON = ADBLOCK / "common"
RENDERER = ADBLOCK / "renderer"
ADR2 = REPO / "docs/adr/0002-cosmetic-filtering-in-the-browser-process.md"
ADR5 = REPO / "docs/adr/0005-renderer-side-cosmetic-piece.md"
MOJOM = COMMON / "cosmetic.mojom"
PATCH = REPO / "patches/adblock/0012-attach-cosmetic-renderer-agent.patch"
PATCH_BINDER = REPO / "patches/adblock/0013-bind-cosmetic-host.patch"
TAB_HELPER = REPO / "overlay/chrome/browser/aerie_adblock/aerie_cosmetic_tab_helper.cc"
SCRIPTS_CC = ADBLOCK / "browser/cosmetic_scripts.cc"


def code_of(path: Path) -> str:
    """The file without comments, so a rule about code is not tripped by prose."""
    text = path.read_text()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    lines = (re.sub(r"//.*", "", line) for line in text.splitlines())
    return "\n".join(line for line in lines if not line.lstrip().startswith("#include"))


class AdrTest(unittest.TestCase):
    def test_adr_5_exists_and_amends_adr_2(self):
        text = ADR5.read_text()
        self.assertTrue(text.startswith("# 5. "))
        self.assertRegex(text, r"Status: accepted \(issue #68\)")
        self.assertIn("0002", text)
        self.assertRegex(text, r"(?i)amends")

    def test_adr_5_names_the_limits(self):
        text = ADR5.read_text().lower()
        for phrase in ("only applies", "selectors the browser process sends", "no network", "parses no filter list",
                       "user-origin", "bounded", "brave"):
            self.assertIn(phrase, text)

    def test_adr_5_keeps_procedural_filters_out(self):
        self.assertRegex(ADR5.read_text().lower(), r"procedural")

    def test_adr_5_records_both_hook_patches(self):
        text = ADR5.read_text()
        self.assertIn("patches/adblock/0012", text)
        self.assertIn("patches/adblock/0013", text)

    def test_adr_5_names_the_blink_call_exactly(self):
        text = ADR5.read_text()
        self.assertIn("WebCssOrigin::kUser", text)
        self.assertNotIn("kUserOrigin", text)

    def test_adr_5_says_the_generic_sheet_is_appended_to(self):
        text = ADR5.read_text()
        self.assertNotIn("a new one replaces the old", text)
        self.assertRegex(text, r"(?i)append")
        self.assertRegex(text, r"(?i)not added again|dedup")

    def test_adr_5_says_sheets_stay_until_the_next_navigation(self):
        self.assertRegex(ADR5.read_text(), r"(?is)exception.{0,200}next navigation|next navigation.{0,200}exception")

    def test_adr_5_records_the_reinsert_bound_and_both_gates(self):
        text = ADR5.read_text()
        for phrase in ("250 ms", "64", "one generic sheet", "per frame", "per tab", "engine round trip"):
            self.assertIn(phrase, text)

    def test_adr_2_points_at_the_amendment(self):
        head = ADR2.read_text().split("\n## ", 1)[0]
        self.assertIn("0005", head)
        self.assertRegex(head, r"(?i)amended")

    def test_adr_2_stale_limits_are_marked_as_changed(self):
        self.assertRegex(ADR2.read_text(), r"(?s)Consequences.*0005")


class MojomTest(unittest.TestCase):
    def setUp(self):
        self.text = code_of(MOJOM)

    def interfaces(self):
        return dict(re.findall(r"interface (\w+) \{(.*?)\n\};", self.text, re.S))

    def test_exactly_two_interfaces(self):
        self.assertEqual(sorted(self.interfaces()), ["CosmeticAgent", "CosmeticHost"])

    def test_the_browser_talks_to_the_renderer_with_one_call(self):
        body = self.interfaces()["CosmeticAgent"]
        self.assertEqual(re.findall(r"(\w+)\(", body), ["SetRules"])

    def test_the_renderer_can_send_the_browser_only_lookup_keys(self):
        body = self.interfaces()["CosmeticHost"]
        self.assertEqual(re.findall(r"(\w+)\(", body), ["LateTokens"])
        self.assertEqual(re.findall(r"\((.*?)\)", body, re.S)[0].replace("\n", " ").split(), ["string", "classes,", "string", "ids"])

    def test_no_urls_no_handles_no_replies(self):
        for word in ("url.mojom", "Url", "=>", "handle<", "pending_", "mojo_base", "network"):
            self.assertNotIn(word, self.text)


class LimitsHeaderTest(unittest.TestCase):
    def test_the_late_watch_is_bounded(self):
        text = (COMMON / "cosmetic_limits.h").read_text()
        for name in ("kMaxSheetBytes", "kMaxLateTokenListBytes", "kMaxLateBatchesPerDocument", "kLateTabBurst",
                     "kLateMinIntervalMs", "kLateFrameGateIntervalMs", "kLateTabRefillIntervalMs",
                     "kMaxMutationRecordsPerBatch", "kMaxElementsPerMutationBatch", "kMaxLateBacklogItems",
                     "kLateFlushDelayMs", "kLateRetryDelayMs", "kMaxLateFlushRetries",
                     "kGenericReinsertMinIntervalMs", "kMaxGenericInsertsPerDocument"):
            self.assertRegex(text, rf"inline constexpr \w+ {name} = ", name)

    def test_counts_are_size_t_and_durations_are_int(self):
        text = (COMMON / "cosmetic_limits.h").read_text()
        for name in re.findall(r"inline constexpr \w+ (kMax\w+) = ", text):
            if name.startswith("kMaxLate") or name.startswith("kMaxGeneric") or name.startswith("kMaxMutation"):
                if name == "kMaxLateFlushRetries":
                    continue  # a retry counter, compared with the script's own counter
                self.assertRegex(text, rf"inline constexpr size_t {name} = ", name)
        for name in re.findall(r"inline constexpr \w+ (k\w+Ms) = ", text):
            self.assertRegex(text, rf"inline constexpr int {name} = ", name)


class RendererPieceTest(unittest.TestCase):
    def sources(self):
        return [p for p in RENDERER.iterdir() if p.suffix in (".cc", ".h") and "unittest" not in p.name]

    def test_the_renderer_piece_exists(self):
        names = {p.name for p in self.sources()}
        self.assertTrue({"aerie_cosmetic_agent.cc", "aerie_cosmetic_agent.h", "cosmetic_sheet_state.cc",
                         "cosmetic_sheet_state.h"} <= names, names)

    def test_it_includes_nothing_that_touches_the_network_or_the_engine(self):
        banned = ("net/", "services/network", "mojo/public/cpp/system", "adblock_engine", "filter_engine",
                  "filter_list", "rust/", "aerie_adblock/browser", "base/files", "base/threading/thread",
                  "base/task/thread_pool", "url/gurl.h", "third_party/blink/public/platform/web_url")
        for path in self.sources():
            for inc in re.findall(r'#include "([^"]+)"', path.read_text()):
                for b in banned:
                    self.assertNotIn(b, inc, f"{path.name} includes {inc}")

    def test_it_never_parses_a_filter_list(self):
        for path in self.sources():
            code = code_of(path)
            for word in ("sanitize_list", "AdblockEngine", "EasyList", '"##', '"#@#', "ParseFilter", "ParseList", "Engine"):
                self.assertNotIn(word, code, f"{path.name}: {word}")

    def test_the_gn_target_has_no_network_or_engine_dependency(self):
        gn = (RENDERER / "BUILD.gn").read_text()
        for bad in ("//net", "//services/network", "//components/aerie_adblock/browser", "rust", "service_manager"):
            self.assertNotIn(bad, gn)
        self.assertIn('"//chrome/renderer:renderer"', gn)  # the one target that may depend on it
        self.assertIn("//components/aerie_adblock/common", gn)

    def test_the_agent_applies_user_origin_sheets_only(self):
        code = code_of(RENDERER / "aerie_cosmetic_agent.cc")
        self.assertIn("WebCssOrigin::kUser", code)
        self.assertNotIn("WebCssOrigin::kAuthor", code)
        self.assertNotIn("adoptedStyleSheets", code)

    def test_the_agent_installs_its_native_function_only_in_the_aerie_world(self):
        code = code_of(RENDERER / "aerie_cosmetic_agent.cc")
        self.assertRegex(code, r"world_id\s*!=\s*aerie_adblock::kCosmeticWorldId")

    def test_the_agent_bounds_what_it_accepts_and_sends(self):
        agent = code_of(RENDERER / "aerie_cosmetic_agent.cc")
        state = code_of(RENDERER / "cosmetic_sheet_state.cc") + code_of(RENDERER / "cosmetic_sheet_state.h")
        gate = code_of(COMMON / "late_report_gate.cc") + code_of(COMMON / "late_report_gate.h")
        self.assertIn("kMaxLateTokenListBytes", agent)
        self.assertIn("gate_.Allow(", agent)
        self.assertIn("sheets_.Apply(", agent)
        self.assertIn("pacer_.OnChange(", agent)
        for name in ("kMaxSheetBytes", "kGenericReinsertMinIntervalMs", "kMaxGenericInsertsPerDocument"):
            self.assertIn(name, state)
        for name in ("kMaxLateTokenListBytes", "kMaxLateBatchesPerDocument", "kLateMinIntervalMs"):
            self.assertIn(name, gate)

    def test_the_report_gate_is_in_common_so_the_browser_does_not_depend_on_the_renderer_directory(self):
        self.assertTrue((COMMON / "late_report_gate.h").is_file())
        self.assertNotIn("LateReportGate", (RENDERER / "cosmetic_sheet_state.h").read_text())
        helper_gn = (REPO / "overlay/chrome/browser/aerie_adblock/BUILD.gn").read_text()
        helper_gn = helper_gn[helper_gn.index('source_set("tab_helper")'):]
        self.assertNotIn("aerie_adblock/renderer", helper_gn)
        self.assertIn("//components/aerie_adblock/common:gate", helper_gn)
        for path in (TAB_HELPER, TAB_HELPER.with_suffix(".h")):
            self.assertNotIn("aerie_adblock/renderer", path.read_text())

    def test_headers_that_include_a_target_get_it_as_a_public_dep(self):
        helper_gn = (REPO / "overlay/chrome/browser/aerie_adblock/BUILD.gn").read_text()
        block = helper_gn[helper_gn.index('source_set("tab_helper")'):]
        public = re.search(r"public_deps = \[(.*?)\]", block, re.S).group(1)
        for dep in ("common:mojom", "common:gate", "common:limits", "browser:cosmetic_scripts", "browser:service"):
            self.assertIn(dep, public)
        renderer_gn = (RENDERER / "BUILD.gn").read_text()
        public = re.search(r"public_deps = \[(.*?)\]", renderer_gn[renderer_gn.index('source_set("renderer")'):], re.S).group(1)
        for dep in (":sheet_state", "common:mojom", "common:gate"):
            self.assertIn(dep, public)

    def test_the_mojom_is_visible_to_browser_core_which_includes_the_helper_header(self):
        self.assertIn('"//chrome/browser:core"', (COMMON / "BUILD.gn").read_text())

    def test_the_sheet_state_includes_initializer_list(self):
        self.assertIn("#include <initializer_list>", (RENDERER / "cosmetic_sheet_state.cc").read_text())

    def test_the_agent_does_not_call_anything_but_the_two_mojo_methods(self):
        code = code_of(RENDERER / "aerie_cosmetic_agent.cc")
        self.assertEqual(sorted(set(re.findall(r"host_->(\w+)\(", code))), ["LateTokens"])

    def test_gn_visibility_of_the_common_target_is_the_renderer_and_the_browser_only(self):
        gn = (COMMON / "BUILD.gn").read_text()
        self.assertIn("//chrome/renderer:renderer", gn)
        self.assertIn("//components/aerie_adblock/*", gn)
        self.assertIn("//chrome/browser/aerie_adblock:*", gn)


class EngineTokenTest(unittest.TestCase):
    ENGINE = ADBLOCK / "browser/adblock_engine.cc"

    def test_the_engine_skips_non_ascii_tokens_one_at_a_time(self):
        text = self.ENGINE.read_text()
        body = text[text.index("AdblockEngine::GenericStylesheet"):]
        self.assertIn("AsciiTokens(classes)", body)
        self.assertIn("AsciiTokens(ids)", body)
        # The raw lists never go to Rust.
        self.assertNotIn("ToStr(classes)", body)
        self.assertNotIn("ToStr(ids)", body)

    def test_the_watch_script_drops_non_ascii_tokens_too(self):
        self.assertIn("x7f", SCRIPTS_CC.read_text())


class BrowserSideTest(unittest.TestCase):
    def test_a_frame_must_be_active(self):
        text = TAB_HELPER.read_text()
        self.assertIn("rfh->IsActive()", text[text.index("::ShouldFilter"):text.index("::ShouldFilter") + 300])

    def test_callbacks_are_keyed_by_document_not_by_origin(self):
        text = TAB_HELPER.read_text()
        self.assertIn("WeakDocumentPtr", text)
        self.assertNotIn("StillAt", text)
        self.assertNotIn("IsSameOriginWith", text)

    def test_an_empty_page_result_clears_the_page_sheet(self):
        text = TAB_HELPER.read_text()
        body = text[text.index("::SendRules"):]
        self.assertIn("CosmeticSheetKind::kPage", body[:body.index("\n}\n")])

    def test_there_is_a_per_tab_gate_on_top_of_the_per_frame_one(self):
        text = TAB_HELPER.read_text()
        handler = text[text.index("::LateTokens"):]
        handler = handler[:handler.index("\n}\n")]
        self.assertIn("tab_bucket_.HasToken(", handler)
        self.assertIn("tab_bucket_.Take(", handler)
        self.assertIn("frame_gate", handler)
        # Checked before the frame gate spends its budget, taken from after it.
        self.assertLess(handler.index("tab_bucket_.HasToken("), handler.index("frame_gate.first->second.Allow("))
        self.assertLess(handler.index("frame_gate.first->second.Allow("), handler.index("tab_bucket_.Take("))
        self.assertNotIn("kMaxLateBatchesPerTab", handler)

    def test_one_helper_for_service_and_policy_and_one_for_generic_appends(self):
        text = TAB_HELPER.read_text()
        self.assertEqual(text.count("IsCosmeticFilteringEnabled("), 1)
        self.assertEqual(text.count("service->GetGenericStylesheet("), 1)
        self.assertIn("ServiceIfEnabled(rfh)", text)

    def test_the_duplicate_size_check_is_explained(self):
        text = TAB_HELPER.read_text()
        self.assertIn("bad message that ends the renderer", text)


    def test_the_late_handler_rechecks_the_policy_and_the_exceptions(self):
        text = TAB_HELPER.read_text()
        handler = text[text.index("::LateTokens"):]
        handler = handler[:handler.index("\n}\n")]
        self.assertIn("ServiceIfEnabled(rfh)", handler)

    def test_no_claim_that_junk_cannot_hide_an_ads_class(self):
        for path in (ADR5, ADBLOCK / "common/cosmetic_limits.h", SCRIPTS_CC, REPO / "docs/adblock.md"):
            text = path.read_text().replace("\n", " ")
            self.assertNotRegex(text, r"(?i)cannot hide it", path.name)
            self.assertNotRegex(text, r"(?i)delays? an ad's class and never", path.name)

    def test_the_prerendering_check_is_not_repeated(self):
        self.assertNotIn("kPrerendering", TAB_HELPER.read_text())

    def test_the_watch_budget_is_not_divided_for_utf8(self):
        self.assertNotIn("kMaxLateTokenListBytes / 3", SCRIPTS_CC.read_text())

    def test_the_agent_logs_once_when_the_insertion_cap_is_reached(self):
        text = (RENDERER / "aerie_cosmetic_agent.cc").read_text()
        self.assertIn("VLOG(", text)
        self.assertIn("logged_insert_cap_", text)

    def test_the_bfcache_page_sheet_edge_is_written_down(self):
        self.assertRegex(TAB_HELPER.read_text().replace("\n//", ""),
                         r"(?i)back-forward cache.{0,300}page sheet|page sheet.{0,300}back-forward cache")

    def test_the_late_handler_bounds_what_the_renderer_sends(self):
        text = TAB_HELPER.read_text()
        handler = text[text.index("::LateTokens"):]
        handler = handler[:handler.index("\n}\n")]
        self.assertIn("kMaxLateTokenListBytes", handler)

    def test_the_watch_is_only_injected_where_cosmetic_filtering_is_on(self):
        text = TAB_HELPER.read_text()
        start = text.index("::InstallLateWatch")
        body = text[start:text.index("\n}\n", start)]
        self.assertIn("ServiceIfEnabled(rfh)", body)

    def test_the_browser_hands_sheets_to_the_renderer_instead_of_adopting_them(self):
        self.assertNotIn("adoptedStyleSheets", SCRIPTS_CC.read_text())
        self.assertNotIn("replaceSync", SCRIPTS_CC.read_text())
        self.assertNotIn("BuildInjectionScript", TAB_HELPER.read_text())
        self.assertIn("SetRules(", TAB_HELPER.read_text())

    def test_the_watch_script_is_fixed_text_that_only_reads_the_dom(self):
        script = SCRIPTS_CC.read_text()
        start = script.index("LateWatchScript()")
        body = script[start:script.index("\n}\n", start)]
        self.assertIn("MutationObserver", body)
        self.assertIn("__aerieCosmeticNotify", body)
        for banned in ("fetch", "eval(", "XMLHttpRequest", "innerHTML", "appendChild", "Function(", "setAttribute"):
            self.assertNotIn(banned, body)


class PatchTest(unittest.TestCase):
    def test_the_hook_patches_are_in_the_series_after_the_other_adblock_hooks(self):
        series = patches.parse_series(REPO / "patches/series")
        last = series.index("adblock/0011-hook-protection-navigation-throttle.patch")
        self.assertGreater(series.index("adblock/0012-attach-cosmetic-renderer-agent.patch"), last)
        self.assertGreater(series.index("adblock/0013-bind-cosmetic-host.patch"),
                           series.index("adblock/0012-attach-cosmetic-renderer-agent.patch"))

    def check_small_hook(self, path, files, area):
        text = path.read_text()
        header = patches.parse_header(text)
        self.assertTrue(header["Aerie-Purpose"])
        self.assertEqual(header["Aerie-Kind"], "hook")
        for part in area:
            self.assertIn(part, header["Aerie-Upstream-Area"])
        added = [l for l in text.splitlines() if l.startswith("+") and not l.startswith("+++")]
        self.assertLessEqual(len([l for l in added if l.strip("+").strip()]), 6, added)
        self.assertEqual(sorted(re.findall(r"^\+\+\+ b/(\S+)", text, re.M)), files)

    def test_the_renderer_patch_is_a_small_hook(self):
        self.check_small_hook(PATCH, ["chrome/renderer/BUILD.gn", "chrome/renderer/chrome_content_renderer_client.cc"],
                              ["chrome/renderer/chrome_content_renderer_client.cc"])

    def test_the_binder_patch_is_a_small_hook(self):
        self.check_small_hook(PATCH_BINDER, ["chrome/browser/BUILD.gn",
                                             "chrome/browser/chrome_content_browser_client_receiver_bindings.cc"],
                              ["chrome_content_browser_client_receiver_bindings.cc"])

    def test_the_renderer_gets_the_agent_for_each_frame_it_creates(self):
        self.assertIn("aerie_adblock::AerieCosmeticAgent::Create(render_frame);", PATCH.read_text())

    def test_the_browser_registers_only_the_cosmetic_host(self):
        text = PATCH_BINDER.read_text()
        self.assertIn("AerieCosmeticTabHelper::RegisterBinder(associated_registry", text)
        added = [l for l in text.splitlines() if l.startswith("+") and not l.startswith("+++")]
        self.assertFalse([l for l in added if "AddInterface" in l], added)

    def test_the_renderer_target_depends_on_the_aerie_renderer_piece_only(self):
        text = PATCH.read_text()
        deps = [l for l in text.splitlines() if l.startswith("+") and "//components/" in l]
        self.assertEqual(deps, ['+    "//components/aerie_adblock/renderer",'])


if __name__ == "__main__":
    unittest.main()
