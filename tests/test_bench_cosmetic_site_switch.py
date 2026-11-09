"""The cosmetic_site_switch check (issue #101): its decision logic on recorded observations, its seeded state
and its fixture page. The first real run in the build train proves the browser side."""

import json
import re
import tempfile
import unittest
from pathlib import Path

from aerie_bench import fixtures
from aerie_bench.checks import REGISTRY, cosmetic_site_switch as cs

REPO = Path(__file__).resolve().parents[1]
EASYLIST = (REPO / "overlay/third_party/filter_lists/easylist.txt").read_text().splitlines()


def shown(selector):
    return {"selector": selector, "found": True, "display": "block", "width": 300, "height": 60, "rects": 1}


def hidden(selector):
    return {"selector": selector, "found": True, "display": "none", "width": 0, "height": 0, "rects": 0}


BLOCKED = {"state": "blocked", "error": "net::ERR_BLOCKED_BY_CLIENT"}
LOADED = {"state": "loaded"}


def visit(box, ad=BLOCKED, content=None):
    return {"box": box, "content": content or shown(cs.CONTENT), "ad": ad}


def good():
    return {"seeded": True,
            "off": visit(shown(cs.BOX)),
            "on": visit(hidden(cs.BOX))}


class JudgeTest(unittest.TestCase):
    def test_box_visible_on_the_off_site_hidden_on_the_other_and_both_blocked_passes(self):
        status, detail, _ = cs.judge(good())
        self.assertEqual(status, "pass", detail)

    def test_the_policy_callback_missing_fails(self):
        # Negative control: nothing installs the policy, so the engine hides the box on a.localhost as well.
        obs = good()
        obs["off"] = visit(hidden(cs.BOX))
        status, detail, _ = cs.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn(cs.OFF_HOST, detail)
        self.assertIn("HIDDEN BUT MUST STAY", detail)

    def test_a_box_that_is_not_hidden_on_the_other_site_proves_nothing(self):
        obs = good()
        obs["on"] = visit(shown(cs.BOX))
        status, detail, _ = cs.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn(cs.ON_HOST, detail)
        self.assertIn("NOT HIDDEN", detail)

    def test_the_ad_request_must_stay_blocked_on_both_sites(self):
        for key in ("off", "on"):
            obs = good()
            obs[key]["ad"] = LOADED
            status, detail, _ = cs.judge(obs)
            self.assertEqual(status, "fail", key)
            self.assertIn("network blocking must stay on", detail)
        obs = good()
        obs["off"]["ad"] = {}
        self.assertEqual(cs.judge(obs)[0], "fail")

    def test_first_party_content_must_stay(self):
        obs = good()
        obs["off"]["content"] = hidden(cs.CONTENT)
        self.assertEqual(cs.judge(obs)[0], "fail")

    def test_a_missing_box_or_observation_fails(self):
        obs = good()
        obs["off"]["box"] = {"selector": cs.BOX, "found": False}
        self.assertEqual(cs.judge(obs)[0], "fail")
        obs = good()
        obs["on"] = None
        self.assertEqual(cs.judge(obs)[0], "fail")
        self.assertEqual(cs.judge(None)[0], "fail")

    def test_an_unseeded_state_fails(self):
        obs = good()
        obs["seeded"] = False
        self.assertEqual(cs.judge(obs)[0], "fail")


class SeedTest(unittest.TestCase):
    def test_the_seeded_state_turns_cosmetic_hiding_off_for_the_first_host_only(self):
        with tempfile.TemporaryDirectory() as d:
            path = cs.seed_state(Path(d))
            self.assertEqual(path, Path(d) / "AerieFilterLists" / "state.json")
            self.assertEqual(json.loads(path.read_text()), {"cosmetic_off": ["a.localhost"]})
        self.assertNotIn("exceptions", cs.SEEDED_STATE)  # Blocking stays on.

    def test_the_field_is_the_one_the_browser_reads_and_writes(self):
        code = (REPO / "overlay/components/aerie_adblock/browser/filter_list_state.cc").read_text()
        self.assertEqual(len(re.findall(r'"cosmetic_off"', code)), 2)  # ToValue and FromValue.


class FixtureTest(unittest.TestCase):
    page = (fixtures.FIXTURE_DIR / "cosmetic" / "site-switch.html").read_text()

    def test_the_page_has_the_box_the_content_and_the_ad_script(self):
        self.assertIn('class="ad-slot"', self.page)
        self.assertIn('id="content"', self.page)
        self.assertIn(f'src="{cs.AD_PATH}"', self.page)
        self.assertEqual(re.findall(r"(?:https?:)?//[^\s\"')]+", self.page), [])
        self.assertEqual(cs.PAGE, "/cosmetic/site-switch.html")

    def test_easylist_hides_the_box_and_blocks_the_script_on_any_site(self):
        self.assertIn("##.ad-slot", EASYLIST)
        self.assertIn("/ad_header.js$script", EASYLIST)  # No domain: it applies on localhost too.
        self.assertTrue(cs.AD_PATH.endswith("/ad_header.js"))

    def test_the_two_sites_are_localhost_names(self):
        for host in (cs.OFF_HOST, cs.ON_HOST):
            self.assertTrue(host.endswith(".localhost"))
        self.assertNotEqual(cs.OFF_HOST, cs.ON_HOST)


class RegistrationTest(unittest.TestCase):
    def test_registered_and_documented(self):
        self.assertIn("cosmetic_site_switch", REGISTRY)
        self.assertFalse(REGISTRY["cosmetic_site_switch"].seeds_helium_services)
        self.assertIn("cosmetic_site_switch", (REPO / "docs/bench.md").read_text())


if __name__ == "__main__":
    unittest.main()
