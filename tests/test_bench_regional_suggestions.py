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
            "page_languages": "de-DE,de|de-DE",
            "fixture_requests_before": 7, "fixture_requests_after": 7}


class JudgeTest(unittest.TestCase):
    def test_a_browser_that_only_suggests_passes(self):
        self.assertEqual([], rs.judge(good()))

    def test_a_state_file_with_no_catalogue_entry_passes(self):
        obs = good()
        obs["state_file"] = json.dumps({"lists": [{"id": "custom", "catalogue": False}]})
        self.assertEqual([], rs.judge(obs))

    def test_no_state_file_means_nothing_was_turned_on_and_the_result_says_so(self):
        obs = good()
        self.assertIsNone(obs["state_file"])
        self.assertEqual([], rs.judge(obs))
        self.assertEqual("no state file", rs.state_file_note(None))

    def test_a_present_state_file_must_show_no_catalogue_entry(self):
        obs = good()
        obs["state_file"] = json.dumps({"lists": []})
        self.assertEqual([], rs.judge(obs))
        self.assertEqual("state.json has no catalogue entry", rs.state_file_note("{}"))
        obs["state_file"] = json.dumps({"lists": [{"id": "uboannoyances", "catalogue": True}]})
        self.assertTrue(any("state.json holds" in p for p in rs.judge(obs)))
        obs["state_file"] = "not json"
        self.assertTrue(any("not JSON" in p for p in rs.judge(obs)))
        obs["state_file"] = ""  # a file that exists and is empty is not "no file"
        self.assertTrue(rs.judge(obs))

    def test_a_built_in_list_update_from_the_same_host_is_not_a_catalogue_fetch(self):
        obs = good()
        for url in ("https://easylist.to/easylist/easylist.txt", "https://easylist.to/easylist/easyprivacy.txt?x=1"):
            obs["netlog_records"] = [{"url": url, "urls": [url], "host": "easylist.to", "scheme": "https",
                                      "external": True}]
            self.assertEqual([], rs.judge(obs), url)

    def test_a_fetch_of_a_catalogue_list_fails_even_with_a_query(self):
        obs = good()
        url = "https://easylist.to/easylistgermany/easylistgermany.txt?v=1"
        obs["netlog_records"] = [{"url": url, "urls": [url], "host": "easylist.to", "scheme": "https",
                                  "external": True}]
        self.assertTrue(any("fetched a catalogue list" in p for p in rs.judge(obs)))

    def test_a_browser_that_did_not_take_the_german_locale_fails(self):
        for languages in ("en-US,en|en-US", None, ""):
            obs = good()
            obs["page_languages"] = languages
            self.assertTrue(any("German locale" in p for p in rs.judge(obs)), languages)

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
        self.assertTrue(any("fetched a catalogue list" in p for p in problems), problems)

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
