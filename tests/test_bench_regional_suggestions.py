"""The regional_suggestions check: its decision logic on recorded observations (issue #106).

The browser side runs in the combined build; here the judgement is fed what a good browser and what a browser that
turns the match on by itself would leave behind.
"""

import copy
import json
import unittest
from pathlib import Path

from aerie_bench.checks import REGISTRY, regional_suggestions as rs

ROOT = Path(__file__).resolve().parent.parent
CATALOGUE = json.loads((ROOT / "overlay/components/aerie_adblock/catalogue.json").read_text(encoding="utf-8"))


def state(on=(), suggested=("easylistgermany",), status="idle"):
    rows = [{"id": e["id"], "on": e["id"] in on, "status": "updating" if e["id"] in on else status}
            for e in CATALOGUE]
    return {"lists": [], "suggested": list(suggested), "languageCardDismissed": False,
            "catalogue": [{"category": "regional", "lists": rows}]}


def good():
    return {"state": state(), "catalogue": CATALOGUE, "netlog_records": [], "state_file": None,
            "fixture_requests_before": 7, "fixture_requests_after": 7}


class JudgeTest(unittest.TestCase):
    def test_a_browser_that_only_suggests_passes(self):
        self.assertEqual([], rs.judge(good()))

    def test_a_state_file_with_no_catalogue_entry_passes(self):
        obs = good()
        obs["state_file"] = json.dumps({"lists": [{"id": "custom", "catalogue": False}]})
        self.assertEqual([], rs.judge(obs))

    def test_no_observation_is_not_a_pass(self):
        self.assertTrue(rs.judge(None))
        self.assertTrue(rs.judge({}))

    def test_a_browser_that_suggests_nothing_for_german_fails(self):
        obs = good()
        obs["state"] = state(suggested=())
        self.assertTrue(any("did not suggest" in p for p in rs.judge(obs)))

    def test_a_suggestion_that_is_not_in_the_catalogue_fails(self):
        obs = good()
        obs["state"] = state(suggested=("easylistgermany", "nosuchlist"))
        self.assertTrue(any("nosuchlist" in p for p in rs.judge(obs)))

    def test_a_list_turned_on_by_the_match_fails_three_ways(self):
        obs = good()
        obs["state"] = state(on=("easylistgermany",))
        obs["state_file"] = json.dumps({"lists": [{"id": "easylistgermany", "catalogue": True}]})
        obs["netlog_records"] = [{"url": "https://easylist.to/easylistgermany/easylistgermany.txt",
                                  "urls": ["https://easylist.to/easylistgermany/easylistgermany.txt"],
                                  "host": "easylist.to", "scheme": "https", "external": True}]
        problems = rs.judge(obs)
        self.assertTrue(any("turned on" in p for p in problems), problems)
        self.assertTrue(any("not idle" in p for p in problems), problems)
        self.assertTrue(any("state.json holds" in p for p in problems), problems)
        self.assertTrue(any("contacted a list publisher" in p for p in problems), problems)

    def test_each_signal_fails_on_its_own(self):
        on = good()
        on["state"] = state(on=("easylistgermany",))
        self.assertTrue(rs.judge(on))
        file_only = good()
        file_only["state_file"] = json.dumps({"lists": [{"id": "easylistgermany", "catalogue": True}]})
        self.assertTrue(rs.judge(file_only))
        net_only = good()
        url = "https://easylist-downloads.adblockplus.org/easylistitaly.txt"
        net_only["netlog_records"] = [{"url": url, "urls": [url], "host": "x", "scheme": "https", "external": True}]
        self.assertTrue(rs.judge(net_only))

    def test_a_request_to_the_fixture_server_during_the_wait_fails(self):
        obs = good()
        obs["fixture_requests_after"] = 8
        self.assertTrue(rs.judge(obs))

    def test_what_was_not_measured_is_not_a_pass(self):
        for key in ("netlog_records", "fixture_requests_before", "catalogue"):
            obs = copy.deepcopy(good())
            obs[key] = None
            self.assertTrue(rs.judge(obs), key)

    def test_the_check_is_registered(self):
        self.assertIn("regional_suggestions", REGISTRY)
        self.assertEqual("de_DE.UTF-8", rs.LANG_ENV["LC_ALL"])


if __name__ == "__main__":
    unittest.main()
