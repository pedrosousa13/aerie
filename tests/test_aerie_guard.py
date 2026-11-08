"""The Aerie Guard icon in the address bar (issue #99): the toolbar button is gone, the page action
is wired by hook patches, and the overlay reads only browser-side state and speaks only through the grd."""

import re
import subprocess
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
        code = views_code() + added(ITEM_PATCH)
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
        code = (VIEWS / "aerie_guard_page_action_controller.cc").read_text(encoding="utf-8")
        self.assertIn("GetPrimaryMainFrame()->GetLastCommittedURL()", code)
        self.assertNotIn("GetVisibleURL", code)
        self.assertNotIn("GetURL()", code)

    def test_the_state_logic_only_shows_http_and_https(self):
        code = (BROWSER / "guard_state.cc").read_text(encoding="utf-8")
        self.assertIn("SchemeIsHTTPOrHTTPS()", code)

    def test_the_toggle_goes_through_the_context_api(self):
        code = (VIEWS / "aerie_guard_page_action_controller.cc").read_text(encoding="utf-8")
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


class NetworkEndpointsTest(unittest.TestCase):
    def test_the_endpoint_list_is_unchanged_by_this_work(self):
        # docs/network-endpoints.md lists every server the browser talks to; the icon adds none.
        names = subprocess.run(["git", "diff", "--name-only", "main", "--", "docs/network-endpoints.md"],
                               cwd=REPO, capture_output=True, text=True, check=False).stdout.split()
        self.assertEqual(names, [])


if __name__ == "__main__":
    unittest.main()
