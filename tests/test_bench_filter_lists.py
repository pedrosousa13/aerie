"""The filter_lists check: its decision logic on recorded observations, and its fixtures.

The observations have the shape that observe_first_launch / observe_second_launch return (and that
the check writes to `filter_lists-recording.json`). The browser side is exercised in the combined build.
"""

import copy
import unittest
from pathlib import Path

from aerie_bench import fixtures
from aerie_bench.checks import REGISTRY, filter_lists as fl

LIST_URL = "http://127.0.0.1:5000/filters/custom-list.txt"


def visit(blocked, host="news.localhost", **over):
    v = {"visit": "x", "host": host, "ads": [False] * 3 if blocked else [True] * 3,
         "first_party": True, "trap": True, "done": True, "fixture_ad_requests": 0 if blocked else 3}
    v.update(over)
    return v


def entry(url, error="", builtin=False, rules=1, updated="2026-01-01"):
    return {"id": url[-6:], "url": url, "title": "t", "builtin": builtin, "rules": rules,
            "updated": updated, "error": error}


BUILTIN = entry("https://easylist.to/easylist/easylist.txt", error="net error", builtin=True, rules=900, updated=None)
STATE = {"ready": True, "autoUpdate": True, "exceptions": ["news.localhost"],
         "lists": [BUILTIN, entry(LIST_URL)]}


def record(*urls):
    return {"url": urls[0], "urls": list(urls), "host": "h", "scheme": "https", "external": True}


def good():
    state = copy.deepcopy(STATE)
    return {
        "list_url": LIST_URL,
        "insecure_entry": entry(fl.INSECURE_LIST_URL, error="http not allowed", rules=0, updated=None),
        "html_entry": entry("http://127.0.0.1:5000/filters/not-a-list.html", error="not a filter list", rules=0, updated=None),
        "insecure_entry_removed": True, "html_entry_removed": True,
        "added_entry": entry(LIST_URL),
        "blocked_visit": visit(True),
        "page_count_samples": {0: 1, 3: 1},
        "request_blocked": {"true": 3, "false": 20},
        "update": {"list_url": LIST_URL, "hits_before": 1, "hits_after": 2, "state": state,
                   "netlog_records": [record("http://127.0.0.1:5000/filters/custom-list.txt"),
                                      record("https://easylist.to/easylist/easylist.txt"),
                                      record("http://news.localhost:5000/filters/page.html")]},
        "exception": {"state": state, "excepted_visit": visit(False), "other_visit": visit(True, "other.localhost")},
        "restart": {"state": copy.deepcopy(STATE), "excepted_visit": visit(False), "other_visit": visit(True, "other.localhost")},
    }


class JudgeTest(unittest.TestCase):
    def fails(self, obs, *needles):
        status, detail, _ = fl.judge(obs)
        self.assertEqual(status, "fail", detail)
        for n in needles:
            self.assertIn(n, detail)
        return detail

    def test_good_run_passes(self):
        status, detail, data = fl.judge(good())
        self.assertEqual(status, "pass", detail)
        self.assertEqual(data["update"]["foreign_requests"], [])
        self.assertEqual(data["update"]["builtin_update_errors"], [BUILTIN["url"]])

    def test_accepted_negative_controls_fail(self):
        obs = good()
        obs["insecure_entry"]["error"] = ""
        self.fails(obs, "insecure http list: accepted without an error")
        obs = good()
        obs["html_entry"] = None
        self.fails(obs, "html page as list: the list is not in the state")

    def test_html_list_that_blocks_the_trap_fails(self):
        obs = good()
        obs["blocked_visit"]["trap"] = False
        self.fails(obs, "trap script was blocked")

    def test_list_that_does_not_take_effect_fails(self):
        obs = good()
        obs["blocked_visit"] = visit(False)
        self.fails(obs, "expected 3 blocked ads")

    def test_blocked_ad_that_reached_the_server_fails(self):
        obs = good()
        obs["blocked_visit"]["fixture_ad_requests"] = 1
        self.fails(obs, "still reached the fixture server")

    def test_added_list_with_error_fails(self):
        obs = good()
        obs["added_entry"]["error"] = "boom"
        self.fails(obs, "did not give a loaded list")

    def test_count_must_be_one_sample_of_the_blocked_requests(self):
        for samples in ({3: 2}, {2: 1}, {}, {3: 1, 1: 1}):
            obs = good()
            obs["page_count_samples"] = samples
            self.fails(obs, "PageBlockedCount samples")
        obs = good()
        obs["page_count_samples"] = None
        self.fails(obs, "no Aerie.Adblock.PageBlockedCount histogram")

    def test_engine_counter_below_blocked_ads_fails(self):
        obs = good()
        obs["request_blocked"] = {"true": 1, "false": 0}
        self.fails(obs, "RequestBlocked")

    def test_update_that_fetches_nothing_fails(self):
        obs = good()
        obs["update"]["hits_after"] = 1
        self.fails(obs, "did not fetch the custom list again")

    def test_update_traffic_to_other_hosts_fails(self):
        for url in ("https://tracker.example/x", "http://easylist.to/easylist.txt", "https://lists.bench.example/a"):
            obs = good()
            obs["update"]["netlog_records"].append(record("https://easylist.to/ok", url))
            self.fails(obs, "no list publisher owns", url)

    def test_request_to_a_user_list_url_is_allowed_only_over_https(self):
        state = copy.deepcopy(STATE)
        state["lists"].append(entry("https://lists.example/mine.txt"))
        ok, bad = fl.update_requests([record("https://lists.example/mine.txt"), record("http://lists.example/mine.txt")],
                                     state["lists"])
        self.assertEqual(ok, ["https://lists.example/mine.txt"])
        self.assertEqual(bad, ["http://lists.example/mine.txt"])

    def test_redirect_hop_to_a_foreign_host_is_caught(self):
        _, bad = fl.update_requests([record("https://easylist.to/a", "https://evil.example/b")], STATE["lists"])
        self.assertEqual(bad, ["https://evil.example/b"])

    def test_user_list_update_error_fails_but_builtin_error_does_not(self):
        obs = good()
        obs["update"]["state"]["lists"][1]["error"] = "failed"
        self.fails(obs, "custom list has an error after the update")

    def test_exception_must_not_leak_to_the_other_host(self):
        obs = good()
        obs["exception"]["other_visit"] = visit(False, "other.localhost")
        self.fails(obs, f"exception, {fl.OTHER_HOST}: expected 3 blocked")

    def test_exception_must_let_ads_load(self):
        obs = good()
        obs["exception"]["excepted_visit"] = visit(True)
        self.fails(obs, f"exception, {fl.EXCEPTED_HOST}: expected 3 ads to load")


class RestartTest(unittest.TestCase):
    def test_restart_half_passes_on_its_own(self):
        self.assertEqual(fl.judge_restart(good()["restart"], LIST_URL), [])

    def test_no_restart_is_a_problem(self):
        self.assertEqual(len(fl.judge_restart(None)), 1)

    def test_exception_lost_after_restart(self):
        r = good()["restart"]
        r["state"] = dict(STATE, exceptions=[])
        r["excepted_visit"] = visit(True)
        problems = fl.judge_restart(r, LIST_URL)
        self.assertTrue(any("exception for news.localhost is gone" in p for p in problems))
        self.assertTrue(any("expected 3 ads to load" in p for p in problems))

    def test_list_lost_after_restart(self):
        r = good()["restart"]
        r["state"] = dict(STATE, lists=[BUILTIN])
        self.assertTrue(any("custom list is gone" in p for p in fl.judge_restart(r, LIST_URL)))

    def test_exception_applying_to_every_host_after_restart(self):
        r = good()["restart"]
        r["other_visit"] = visit(False, "other.localhost")
        self.assertTrue(any("other.localhost" in p for p in fl.judge_restart(r, LIST_URL)))


class HelpersTest(unittest.TestCase):
    def test_histogram_samples(self):
        hist = {"buckets": [{"low": 0, "high": 1, "count": 1}, {"low": 3, "high": 4, "count": 2}, {"low": 5, "high": 6, "count": 0}]}
        self.assertEqual(fl.histogram_samples(hist), {0: 1, 3: 2})

    def test_registered_and_flagged(self):
        self.assertIn("filter_lists", REGISTRY)
        self.assertEqual(fl.FLAG, "--aerie-test-allow-loopback-filter-lists")


class FixtureTest(unittest.TestCase):
    def test_fixtures_exist_and_the_list_blocks_the_ads_not_the_trap(self):
        root = fixtures.FIXTURE_DIR / "filters"
        rules = [l for l in (root / "custom-list.txt").read_text().splitlines() if l and l[0] not in "![ "]
        self.assertEqual(rules, ["/filters/ads/"])
        for n in (1, 2, 3):
            self.assertTrue((root / "ads" / f"banner-{n}.js").is_file())
        self.assertTrue((root / "trap" / "probe.js").is_file())
        self.assertIn("/filters/trap/", (root / "not-a-list.html").read_text())
        self.assertNotIn("/filters/ads/", (root / "not-a-list.html").read_text())
        self.assertIn("<script>", (root / "page.html").read_text())

    def test_fixture_server_serves_list_and_page(self):
        import urllib.request
        with fixtures.FixtureServer() as server:
            for path, ctype in (("/filters/custom-list.txt", "text/plain"), ("/filters/not-a-list.html", "text/html"),
                                ("/filters/page.html", "text/html"), ("/filters/ads/banner-1.js", "text/javascript")):
                resp = urllib.request.urlopen(server.base_url + path)
                self.assertEqual(resp.status, 200)
                self.assertTrue(resp.headers["Content-Type"].startswith(ctype), path)


if __name__ == "__main__":
    unittest.main()
