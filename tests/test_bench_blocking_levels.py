"""The blocking_levels check (issue #109): its decision logic on hand-written observations, its seeded profile and its
fixture page. The first real run in the build train proves the browser side.

The observations below are written by hand to exercise ``judge``; they are not recordings of a browser."""

import hashlib
import json
import re
import tempfile
import unittest
from pathlib import Path

from aerie_bench import fixtures
from aerie_bench.checks import REGISTRY, blocking_levels as bl

REPO = Path(__file__).resolve().parents[1]
EASYLIST = (REPO / "overlay/third_party/filter_lists/easylist.txt").read_text().splitlines()

BLOCKED = {"state": "blocked", "error": "net::ERR_BLOCKED_BY_CLIENT"}
NOT_FOUND = {"state": "loaded", "status": 404}
CONTENT = {"state": "loaded", "status": 200}


def standard_visit():
    return {"ad_first": NOT_FOUND, "ad_third": BLOCKED, "builtin_first": NOT_FOUND, "builtin_third": BLOCKED,
            "custom_first": BLOCKED, "custom_third": BLOCKED, "content_first": CONTENT}


def aggressive_visit():
    return {"ad_first": BLOCKED, "ad_third": BLOCKED, "builtin_first": BLOCKED, "builtin_third": BLOCKED,
            "custom_first": BLOCKED, "custom_third": BLOCKED, "content_first": CONTENT}


def good():
    return {"seeded": True, "standard": standard_visit(), "aggressive": aggressive_visit()}


class JudgeTest(unittest.TestCase):
    def test_standard_lets_the_own_site_through_and_aggressive_blocks_it_passes(self):
        status, detail, _ = bl.judge(good())
        self.assertEqual(status, "pass", detail)

    def test_blocked_first_party_in_standard_fails(self):
        # Hand-written: if an engine without levels blocked every matching request, the Standard step would fail.
        obs = good()
        obs["standard"] = aggressive_visit()
        status, detail, _ = bl.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("BLOCKED IN STANDARD", detail)
        self.assertIn("ad_first", detail)
        self.assertIn("builtin_first", detail)

    def test_a_standard_that_lets_a_custom_list_important_rule_through_first_party_fails(self):
        # brave-core's second engine still judges custom lists first party, so Standard must block this one.
        obs = good()
        obs["standard"]["custom_first"] = NOT_FOUND
        status, detail, _ = bl.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("custom_first was NOT BLOCKED in standard", detail)

    def test_a_standard_that_also_lets_third_party_through_fails(self):
        obs = good()
        obs["standard"]["ad_third"] = NOT_FOUND
        status, detail, _ = bl.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("ad_third was NOT BLOCKED in standard", detail)

    def test_an_important_rule_that_is_not_blocked_third_party_fails(self):
        obs = good()
        obs["standard"]["builtin_third"] = NOT_FOUND
        self.assertEqual(bl.judge(obs)[0], "fail")

    def test_aggressive_must_block_everything_that_matches_the_control(self):
        # If the rules did not match on this page, an allowed first-party request in Standard would prove nothing.
        for name in ("ad_first", "builtin_first", "custom_first"):
            obs = good()
            obs["aggressive"][name] = NOT_FOUND
            status, detail, _ = bl.judge(obs)
            self.assertEqual(status, "fail", name)
            self.assertIn("NOT BLOCKED in aggressive", detail)

    def test_content_must_load_in_both_levels(self):
        for key in ("standard", "aggressive"):
            obs = good()
            obs[key]["content_first"] = BLOCKED
            self.assertEqual(bl.judge(obs)[0], "fail", key)
            obs = good()
            obs[key]["content_first"] = {"state": "loaded", "status": 404}
            self.assertEqual(bl.judge(obs)[0], "fail", key)

    def test_a_request_that_never_happened_or_a_missing_visit_fails(self):
        obs = good()
        obs["standard"]["ad_first"] = {}
        self.assertEqual(bl.judge(obs)[0], "fail")
        obs = good()
        obs["aggressive"] = None
        self.assertEqual(bl.judge(obs)[0], "fail")
        self.assertEqual(bl.judge(None)[0], "fail")

    def test_an_unseeded_profile_fails(self):
        obs = good()
        obs["seeded"] = False
        self.assertEqual(bl.judge(obs)[0], "fail")


class SeedTest(unittest.TestCase):
    def test_the_state_sets_the_default_and_one_site_level(self):
        state = bl.seeded_state(1_700_000_000_000)
        self.assertEqual(state["default_level"], "standard")
        self.assertEqual(state["levels"], {bl.AGGRESSIVE_HOST: "aggressive"})
        self.assertNotIn("exceptions", state)  # Blocking stays on for both sites.
        self.assertFalse(state["auto_update"])

    def test_every_seeded_list_has_an_address_or_the_browser_skips_it(self):
        # filter_list_state.cc skips a "lists" item without "id" or "url": the built-in's seeded text would be ignored.
        for item in bl.seeded_state(1_700_000_000_000)["lists"]:
            self.assertTrue(item.get("id") and item.get("url"), item)
        code = (REPO / "overlay/components/aerie_adblock/browser/filter_list_state.cc").read_text()
        self.assertIn("if (!id || !url || !IsValidListId(*id))", code)

    def test_the_list_is_cached_under_the_id_the_browser_derives_from_its_address(self):
        digest = hashlib.sha256(bl.LIST_URL.encode()).digest()[:8].hex()
        self.assertEqual(bl.list_id(), "c" + digest)
        # The same recipe as CustomListId() in filter_list_state.cc.
        code = (REPO / "overlay/components/aerie_adblock/browser/filter_list_state.cc").read_text()
        self.assertIn('"c" + base::ToLowerASCII(base::HexEncode(', code)
        self.assertIn("first<8u>()", code)

    def test_seeding_writes_the_state_and_the_list_where_the_browser_reads_them(self):
        with tempfile.TemporaryDirectory() as d:
            path = bl.seed_profile(Path(d), now_ms=1_700_000_000_000)
            self.assertEqual(path, Path(d) / "AerieFilterLists" / "state.json")
            state = json.loads(path.read_text())
            entry = state["lists"][0]
            cached = Path(d) / "AerieFilterLists" / "lists" / f"{entry['id']}.txt"
            self.assertEqual(cached.read_text(), bl.LIST_TEXT)
            self.assertNotEqual(entry["updated"], "0")  # A list with no download date is skipped by Rebuild().
            builtin = state["lists"][1]
            self.assertEqual((builtin["id"], builtin["builtin"]), (bl.BUILTIN_ID, True))
            cached = Path(d) / "AerieFilterLists" / "lists" / f"{bl.BUILTIN_ID}.txt"
            self.assertEqual(cached.read_text(), bl.BUILTIN_TEXT)
            self.assertNotEqual(builtin["updated"], "0")

    def test_the_fields_are_the_ones_the_browser_reads(self):
        code = (REPO / "overlay/components/aerie_adblock/browser/filter_list_state.cc").read_text()
        for field in ('"default_level"', '"levels"'):
            self.assertEqual(len(re.findall(field, code)), 2, field)  # ToValue and FromValue.

    def test_the_list_address_is_https_and_never_a_real_host_the_check_could_fetch(self):
        self.assertTrue(bl.LIST_URL.startswith("https://"))
        self.assertTrue(bl.LIST_URL.split("/")[2].endswith(".example"))


class FixtureTest(unittest.TestCase):
    page = (fixtures.FIXTURE_DIR / "blocking" / "levels.html").read_text()

    def test_the_page_asks_for_each_script_from_its_own_host_and_from_another_site(self):
        for name in (bl.AD, bl.IMPORTANT_BUILTIN, bl.IMPORTANT_CUSTOM, bl.CONTENT):
            self.assertIn(f'"{name}"', self.page)
        self.assertIn(bl.BASE, self.page)
        self.assertIn("127.0.0.1", self.page)
        self.assertIn("__levelsDone", self.page)
        self.assertEqual(bl.PAGE, "/blocking/levels.html")

    def test_easylist_blocks_the_ad_name_on_any_site_and_the_content_file_exists(self):
        self.assertIn("/ad_header.js$script", EASYLIST)  # No domain: it applies on localhost too.
        self.assertTrue((fixtures.FIXTURE_DIR / "blocking" / "levels" / bl.CONTENT).is_file())
        for name in (bl.AD, bl.IMPORTANT_BUILTIN, bl.IMPORTANT_CUSTOM):
            self.assertFalse((fixtures.FIXTURE_DIR / "blocking" / "levels" / name).exists())

    def test_each_seeded_list_blocks_its_own_important_name_only(self):
        self.assertIn(f"{bl.BASE}{bl.IMPORTANT_CUSTOM}$important", bl.LIST_TEXT)
        self.assertNotIn(bl.IMPORTANT_BUILTIN, bl.LIST_TEXT)
        self.assertIn(f"{bl.BASE}{bl.IMPORTANT_BUILTIN}$important", bl.BUILTIN_TEXT)
        self.assertNotIn(bl.IMPORTANT_CUSTOM, bl.BUILTIN_TEXT)
        for text in (bl.LIST_TEXT, bl.BUILTIN_TEXT):
            self.assertNotIn(bl.CONTENT, text)

    def test_the_sites_are_localhost_names_and_the_third_party_is_another_site(self):
        for host in (bl.STANDARD_HOST, bl.AGGRESSIVE_HOST):
            self.assertTrue(host.endswith(".localhost"))
        self.assertNotEqual(bl.STANDARD_HOST, bl.AGGRESSIVE_HOST)
        urls = bl.request_urls(bl.STANDARD_HOST, 1234)
        self.assertEqual(urls["ad_first"], "http://standard.localhost:1234/blocking/levels/ad_header.js")
        self.assertEqual(urls["ad_third"], "http://127.0.0.1:1234/blocking/levels/ad_header.js")


class RegistrationTest(unittest.TestCase):
    def test_registered_and_documented(self):
        self.assertIn("blocking_levels", REGISTRY)
        self.assertFalse(REGISTRY["blocking_levels"].seeds_helium_services)
        self.assertIn("blocking_levels", (REPO / "docs/bench.md").read_text())


if __name__ == "__main__":
    unittest.main()
