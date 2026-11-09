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


PROBES = [{"name": name, "refused": True} for name, _, _ in fl.VALIDATION_PROBES]
STATE_FILE = '{"exceptions": ["news.localhost"], "lists": []}'


def good_validation():
    return {"probes": [dict(p) for p in PROBES], "state_before": STATE_FILE, "state_after": STATE_FILE}


INC = "incognito.localhost"


def panel_state(blocking_on, incognito=True, host=INC, locked=False):
    return {"host": host, "incognito": incognito, "blocking_on": blocking_on, "locked": locked}


def good_incognito():
    """What observe_incognito returns: the panel of an incognito window turned Guard off for INC."""
    saved = '{"exceptions": ["news.localhost"], "lists": []}'
    return {
        "before_visit": visit(True, INC),
        "panel_before": panel_state(True),
        "panel_after": panel_state(False),
        "incognito_visit": visit(False, INC),
        "regular_visit": visit(True, INC),
        "regular_state": copy.deepcopy(STATE),
        "state_file_open": saved,
        "state_file_closed": saved,
        "after_close_visit": visit(True, INC),
    }


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
        "validation": good_validation(),
        "incognito": good_incognito(),
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


class ValidationTest(unittest.TestCase):
    def problems(self, mutate=None):
        val = good_validation()
        if mutate:
            mutate(val)
        return fl.judge_validation(val)

    def test_good_validation_passes(self):
        self.assertEqual(self.problems(), [])

    def test_judge_includes_the_validation_step(self):
        obs = good()
        obs["validation"]["probes"][0]["refused"] = False
        status, detail, _ = fl.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("validation", detail)

    def test_missing_validation_fails(self):
        obs = good()
        del obs["validation"]
        status, detail, _ = fl.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("never sent a bad argument", detail)

    def test_the_probes_cover_a_non_string_and_a_5000_character_host(self):
        by_name = {name: (msg, args) for name, msg, args in fl.VALIDATION_PROBES}
        self.assertEqual(by_name["setException with a number as the host"], ("setException", [123, True]))
        msg, args = by_name["setException with a 5000-character host"]
        self.assertEqual(msg, "setException")
        self.assertEqual(len(args[0]), 5000)
        self.assertGreater(len(args[0]), 4096)

    def test_a_handler_without_the_length_check_fails(self):
        # Without the check the long host is not refused: the manager answers with a state and an error.
        def accept_the_long_host(val):
            for probe in val["probes"]:
                if "5000-character host" in probe["name"]:
                    probe["refused"] = False
        p = self.problems(accept_the_long_host)
        self.assertEqual(p, ["validation: setException with a 5000-character host was not refused"])

    def test_a_handler_without_the_type_check_fails(self):
        def accept_a_number(val):
            val["probes"][0]["refused"] = False
        self.assertEqual(len(self.problems(accept_a_number)), 1)

    def test_a_refused_message_that_still_changed_the_state_file_fails(self):
        p = self.problems(lambda v: v.update(state_after='{"exceptions": ["news.localhost", "%s"]}' % ("a" * 5000)))
        self.assertTrue(any("state.json changed" in x for x in p), p)

    def test_an_unreadable_state_file_fails(self):
        for key in ("state_before", "state_after"):
            p = self.problems(lambda v: v.update({key: None}))
            self.assertTrue(any("state file could not be read" in x for x in p), p)


class IncognitoTest(unittest.TestCase):
    """Guard turned off through the panel in an incognito window: it applies there, and nowhere else."""

    def problems(self, mutate=None):
        inc = good_incognito()
        if mutate:
            mutate(inc)
        return fl.judge_incognito(inc)

    def mentions(self, mutate, needle):
        p = self.problems(mutate)
        self.assertTrue(any(needle in x for x in p), p)

    def test_a_good_run_has_no_problem(self):
        self.assertEqual(self.problems(), [])

    def test_judge_includes_the_incognito_step(self):
        obs = good()
        obs["incognito"]["state_file_open"] = '{"exceptions": ["news.localhost", "%s"]}' % INC
        status, detail, _ = fl.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("state file", detail)

    def test_no_incognito_observations_fail(self):
        obs = good()
        del obs["incognito"]
        self.assertEqual(fl.judge(obs)[0], "fail")
        self.assertEqual(len(fl.judge_incognito(None)), 1)

    def test_a_run_that_stopped_names_why(self):
        p = fl.judge_incognito({"error": "CdpError: no Guard icon in the address bar"})
        self.assertTrue(any("no Guard icon" in x for x in p), p)

    # Negative controls: each leak or lost step must fail.
    def test_an_exception_that_leaks_to_state_json_while_the_window_is_open_fails(self):
        self.mentions(lambda i: i.update(state_file_open='{"exceptions": ["news.localhost", "%s"]}' % INC),
                      "written to the state file")

    def test_an_exception_that_leaks_to_state_json_by_the_time_the_window_closed_fails(self):
        self.mentions(lambda i: i.update(state_file_closed='{"exceptions": ["%s"]}' % INC),
                      "written to the state file")

    def test_an_unreadable_state_file_fails(self):
        for key in ("state_file_open", "state_file_closed"):
            self.mentions(lambda i: i.update({key: None}), "state file could not be read")

    def test_an_exception_that_does_not_apply_in_the_incognito_window_fails(self):
        self.mentions(lambda i: i.update(incognito_visit=visit(True, INC)), "incognito window")

    def test_a_list_that_did_not_block_in_incognito_before_the_switch_fails(self):
        # Otherwise the ads loading afterwards prove nothing.
        self.mentions(lambda i: i.update(before_visit=visit(False, INC)), "before the switch")

    def test_an_exception_that_reaches_the_regular_window_fails(self):
        self.mentions(lambda i: i.update(regular_visit=visit(False, INC)), "regular window")

    def test_an_exception_listed_in_the_settings_state_of_the_regular_window_fails(self):
        def leak(i):
            i["regular_state"]["exceptions"] = ["news.localhost", INC]
        self.mentions(leak, "regular window's exception list")

    def test_an_exception_that_survives_the_window_fails(self):
        self.mentions(lambda i: i.update(after_close_visit=visit(False, INC)), "after the first closed")

    def test_a_panel_of_the_regular_window_does_not_count(self):
        # The panel that was driven must be the incognito one, or nothing here is about incognito.
        self.mentions(lambda i: i.update(panel_before=panel_state(True, incognito=False)), "not the incognito window")

    def test_a_panel_for_another_site_does_not_count(self):
        self.mentions(lambda i: i.update(panel_before=panel_state(True, host="news.localhost")), "named")

    def test_a_panel_switch_that_did_not_take_effect_fails(self):
        self.mentions(lambda i: i.update(panel_after=panel_state(True)), "still shows Guard on")

    def test_the_step_never_opens_settings_in_an_incognito_context(self):
        # chrome://settings crashes in a CDP incognito context.
        import inspect
        src = inspect.getsource(fl.observe_incognito)
        self.assertNotIn("open_manager(inc", src)
        self.assertNotIn("MANAGER_URL", src)
        self.assertNotIn("chrome://settings", src)

    def test_the_step_runs_in_a_launch_with_ui_devtools_and_the_check_wires_it(self):
        import inspect
        src = inspect.getsource(fl.FilterLists.run)
        self.assertIn("ui_devtools.FLAGS", src)
        self.assertIn("observe_incognito", src)
        self.assertNotIn("ui_devtools", inspect.getsource(fl.observe_first_launch))

    def test_an_incognito_exception_that_is_in_the_state_after_a_restart_fails(self):
        r = good()["restart"]
        r["state"]["exceptions"] = ["news.localhost", INC]
        self.assertTrue(any(INC in p for p in fl.judge_restart(r, LIST_URL)))


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

    def test_the_check_drives_the_settings_section_through_its_handler(self):
        # The check reads chrome://settings/aerieGuard; the route and the message names must stay in step.
        self.assertEqual(fl.MANAGER_URL, "chrome://settings/aerieGuard")
        root = Path(__file__).resolve().parent.parent
        handler = (root / "overlay/chrome/browser/ui/webui/aerie_guard_settings/aerie_guard_settings_handler.cc").read_text()
        for message in ("getState", "addList", "removeList", "updateList", "updateAll", "setException", "setAutoUpdate"):
            self.assertIn(f'"aerieGuard.{message}"', handler)
        source = Path(fl.__file__).read_text()
        self.assertNotIn("aerieFilters", source.replace("``aerieFilters``", ""))
        self.assertNotIn("chrome://filters", source.split('"""', 2)[2])
        self.assertIn("'/aerieGuard', 'aerieGuard'", (root / "patches/adblock/0023-add-aerie-guard-settings-page.patch").read_text())

    def test_the_old_page_is_gone(self):
        self.assertFalse((Path(__file__).resolve().parent.parent / "overlay/chrome/browser/ui/webui/aerie_filters").exists())


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
