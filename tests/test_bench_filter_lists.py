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


def lst(list_id, **over):
    item = {"id": list_id, "url": "https://x/" + list_id, "title": list_id, "builtin": list_id.startswith("easy"),
            "rules": 900, "ignoredExceptions": 0, "enabled": True, "required": list_id == "easylist",
            "status": "idle", "errorCode": "", "updated": None, "error": ""}
    item.update(over)
    return item


def lists_state(easyprivacy_on=True, easylist_on=True, listError="", custom_status="idle", easylist_status="idle"):
    return {"ready": True, "busy": False, "listError": listError,
            "lists": [lst("easylist", enabled=easylist_on, status=easylist_status,
                          errorCode="download" if easylist_status == "failed" else "",
                          error="x" if easylist_status == "failed" else ""),
                      lst("easyprivacy", enabled=easyprivacy_on),
                      lst("ccustom", builtin=False, rules=1, status=custom_status)]}


def good_lists():
    """What observe_lists returns on a browser whose handler guards EasyList."""
    return {
        "custom_id": "ccustom",
        "counts": lists_state(),
        "probe_on": "blocked",
        "off_state": lists_state(easyprivacy_on=False),
        "probe_off": "failed",
        "on_state": lists_state(),
        "easylist_answer": lists_state(listError="required"),
        "probe_back": "blocked",
        "easylist_probe": "blocked",
        "updating_answer": lists_state(custom_status="updating"),
        "updated_state": lists_state(custom_status="updated"),
        "failed_state": lists_state(easylist_status="failed"),
    }


def own_visit(blocked, host="a.localhost", **over):
    v = {"visit": "x", "host": host, "blocked_loaded": not blocked, "open_loaded": True,
         "scriptlet_marker": "undefined", "done": True, "fixture_blocked_requests": 0 if blocked else 1}
    v.update(over)
    return v


def own_state(with_custom=True, text=fl.OWN_RULES, error=""):
    issues = [{"line": line, "reason": reason, "ignored": reason == "matches_every_address", "rule": "r"}
              for line, reason in fl.OWN_ISSUES] if with_custom else []
    lists = [BUILTIN, {"id": "custom", "url": "", "title": "", "builtin": False, "rules": 1, "enabled": True,
                       "updated": 1, "error": ""}] if with_custom else [BUILTIN]
    return {"lists": lists, "customError": error,
            "customRules": {"text": text if with_custom else "", "networkRules": 1, "issues": issues}}


def good_custom():
    """What observe_custom_rules returns for a build where the step works."""
    return {
        "saved": own_state(),
        "state_after_save": own_state(),
        "blocked_visit": own_visit(True),
        "other_visit": own_visit(False, "b.localhost"),
        "file_text": fl.OWN_RULES,
        "render": {"pages": 1, "injected": 0, "xss": "undefined", "text": 'Line 4 a.localhost##<img onerror="x">'},
        "oversize": own_state(error="too_large"),
        "bad_argument": {"ok": False, "error": "bad_argument"},
        "refused_visit": own_visit(True),
        "cleared": own_state(with_custom=False),
        "cleared_visit": own_visit(False),
        "file_after_clear": None,
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
        "lists": good_lists(),
        "custom_rules": good_custom(),
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

    def test_the_checks_own_list_step_probes_are_not_foreign_requests(self):
        # Real build, train 14: with EasyPrivacy off the hotjar probe passes the filter and the NetLog holds it.
        probe = fl.TRACKER_URL + "?probe=off"
        obs = good()
        obs["update"]["netlog_records"].append(record(probe))
        status, detail, _ = fl.judge(obs)
        self.assertEqual(status, "pass", detail)
        _, bad = fl.update_requests([record(probe, fl.TRACKER_URL + "?other=1")], STATE["lists"])
        self.assertEqual(bad, [fl.TRACKER_URL + "?other=1"])

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


class VisitPageTest(unittest.TestCase):
    def test_only_the_visits_own_ad_requests_count(self):
        # Turning Guard off reloads the tab, so the earlier visit's page asks for its ads again; its marker
        # "visit=incognito-before" holds "visit=incognito", and those requests are not the later visit's.
        class FakeFixtures:
            port = 1

            @staticmethod
            def snapshot():
                return ([{"path": f"/filters/ads/ad{i}.js?visit=incognito-before", "status": 200} for i in range(3)]
                        + [{"path": f"/filters/ads/ad{i}.js?visit=incognito", "status": 200} for i in range(3)])

        class FakeSession:
            fixtures = FakeFixtures()

            def navigate_url(self, url):
                pass

            def wait_for(self, expr, timeout):
                pass

            def evaluate(self, expr):
                return {"done": True}

        self.assertEqual(fl.visit_page(FakeSession(), fl.INCOGNITO_HOST, "incognito")["fixture_ad_requests"], 3)
        self.assertEqual(fl.visit_page(FakeSession(), fl.INCOGNITO_HOST, "incognito-before")["fixture_ad_requests"], 3)


if __name__ == "__main__":
    unittest.main()


class CustomRulesStepTest(unittest.TestCase):
    """Issue #107: the user's own rules, saved through the handler."""

    def problems(self, mutate):
        obs = good_custom()
        mutate(obs)
        return fl.judge_custom_rules(obs)

    def has(self, problems, needle):
        self.assertTrue(any(needle in p for p in problems), problems)

    def test_a_good_run_has_no_problem(self):
        self.assertEqual(fl.judge_custom_rules(good_custom()), [])
        self.assertEqual(fl.judge(good())[0], "pass", fl.judge(good())[1])

    def test_no_observations_is_a_problem(self):
        self.assertTrue(fl.judge_custom_rules(None))

    def test_the_scriptlet_step_fails_when_the_marker_is_set(self):
        """A hand-made (synthetic) observation where the scriptlet ran and was not reported must fail. No build is run here."""
        def skipping(o):
            o["blocked_visit"]["scriptlet_marker"] = "boolean"
            o["saved"]["customRules"]["issues"] = [i for i in o["saved"]["customRules"]["issues"] if i["reason"] != "scriptlet"]
        problems = self.problems(skipping)
        self.has(problems, "the scriptlet line ran")
        self.has(problems, "dropped and ignored lines")
        status, detail, _ = fl.judge({**good(), "custom_rules": {**good_custom(), "blocked_visit": own_visit(True, scriptlet_marker="boolean")}})
        self.assertEqual(status, "fail")
        self.assertIn("the scriptlet line ran", detail)

    def test_a_rule_that_does_not_block_fails(self):
        self.has(self.problems(lambda o: o.update(blocked_visit=own_visit(False))), "was not blocked")

    def test_a_blocked_script_that_reached_the_server_fails(self):
        self.has(self.problems(lambda o: o["blocked_visit"].update(fixture_blocked_requests=1)), "reached the fixture server")

    def test_a_rule_for_one_site_that_blocks_another_fails(self):
        self.has(self.problems(lambda o: o.update(other_visit=own_visit(True, "b.localhost"))), "also blocked the script on b.localhost")

    def test_the_reasons_must_be_named_per_line(self):
        def wrong(o):
            o["saved"]["customRules"]["issues"][1]["reason"] = "procedural"
        self.has(self.problems(wrong), "dropped and ignored lines")

    def test_only_the_broad_exception_is_ignored(self):
        def wrong(o):
            o["saved"]["customRules"]["issues"][0]["ignored"] = True
        self.has(self.problems(wrong), "marked ignored")

    def test_the_list_needs_no_address(self):
        def addressed(o):
            o["state_after_save"]["lists"][1]["url"] = "https://lists.example/x"
        self.has(self.problems(addressed), "has an address")

    def test_the_file_must_hold_the_text_as_saved(self):
        self.has(self.problems(lambda o: o.update(file_text="||x^\n")), "lists/custom.txt is not the text")
        self.has(self.problems(lambda o: o.update(file_text=None)), "lists/custom.txt is not the text")

    def test_html_in_a_rule_must_be_shown_as_text(self):
        self.has(self.problems(lambda o: o["render"].update(injected=1)), "must be text")
        self.has(self.problems(lambda o: o["render"].update(xss="number")), "ran as script")
        self.has(self.problems(lambda o: o["render"].update(text="")), "does not show the HTML line as text")
        self.has(self.problems(lambda o: o.update(render={})), "not found to inspect")

    def test_a_text_over_the_limit_must_be_refused_and_change_nothing(self):
        self.has(self.problems(lambda o: o.update(oversize=own_state())), "not refused as too_large")
        self.has(self.problems(lambda o: o.update(oversize=own_state(error="too_large", text="x"))), "changed the saved rules")
        self.has(self.problems(lambda o: o.update(refused_visit=own_visit(False))), "after the refused saves")

    def test_a_number_instead_of_a_text_must_be_rejected(self):
        self.has(self.problems(lambda o: o.update(bad_argument={"ok": True})), "number instead of a text")

    def test_a_blank_text_removes_the_rules_and_the_file(self):
        self.has(self.problems(lambda o: o.update(cleared=own_state())), "did not remove the rules")
        self.has(self.problems(lambda o: o.update(cleared_visit=own_visit(True))), "should load but did not")
        self.has(self.problems(lambda o: o.update(file_after_clear=fl.OWN_RULES)), "still on disk")

    def test_the_fixtures_exist_and_the_rule_blocks_one_script_on_one_site(self):
        root = fixtures.FIXTURE_DIR / "filters"
        self.assertTrue((root / "own" / "blocked.js").is_file())
        self.assertTrue((root / "own" / "open.js").is_file())
        page = (root / "own-rules.html").read_text()
        self.assertIn("aerieScriptletMarker", page)
        self.assertIn("blocked.js", page)
        rule = fl.OWN_RULES.splitlines()[1]
        self.assertEqual(rule, "/filters/own/blocked.js$domain=a.localhost")
        self.assertNotIn("open.js", fl.OWN_RULES)
        self.assertIn("aerieScriptletMarker", fl.OWN_RULES)

    def test_the_text_is_sent_as_a_message_argument_never_put_in_script(self):
        # observe_custom_rules sends the text through CALL_JS as a JSON argument.
        import inspect
        text = inspect.getsource(fl.observe_custom_rules)
        self.assertIn('send(session, "setCustomRules", [OWN_RULES])', text)
        self.assertNotIn("evaluate(OWN_RULES", text)

    def test_the_oversize_text_is_over_the_managers_limit_and_under_the_handlers(self):
        self.assertEqual(fl.OVER_LIMIT_RULES.count("\n"), 0)
        self.assertGreater(len(fl.OVER_LIMIT_RULES), 256 * 1024)
        self.assertLess(len(fl.OVER_LIMIT_RULES), 1024 * 1024)  # past the handler's own limit it is a different error
        names = [n for n, _, _ in fl.VALIDATION_PROBES]
        self.assertTrue(any(n.startswith("setCustomRules") for n in names))


class ListsStepTest(unittest.TestCase):
    """Issue #104: per-list on/off, rule counts, EasyList refused, update status."""

    def problems(self, mutate):
        obs = good_lists()
        mutate(obs)
        return fl.judge_lists(obs)

    def test_a_good_run_has_no_problem(self):
        self.assertEqual(fl.judge_lists(good_lists()), [])

    def test_no_observations_is_a_problem(self):
        self.assertTrue(fl.judge_lists(None))
        self.assertTrue(fl.judge_lists({}))

    def test_a_zero_rule_count_fails_for_either_built_in_list(self):
        for list_id in ("easylist", "easyprivacy"):
            def zero(o, list_id=list_id):
                for item in o["counts"]["lists"]:
                    if item["id"] == list_id:
                        item["rules"] = 0
            self.assertTrue(any(f"rule count of {list_id}" in p for p in self.problems(zero)), list_id)

    def test_easyprivacy_off_that_still_blocks_fails(self):
        problems = self.problems(lambda o: o.update(probe_off="blocked"))
        self.assertTrue(any("still blocked with EasyPrivacy off" in p for p in problems), problems)

    def test_easyprivacy_that_does_not_block_when_on_fails(self):
        self.assertTrue(any("not blocked with both lists on" in p
                            for p in self.problems(lambda o: o.update(probe_on="failed"))))
        self.assertTrue(any("not blocked again" in p
                            for p in self.problems(lambda o: o.update(probe_back="failed"))))

    def test_the_easylist_step_fails_against_a_handler_without_the_guard(self):
        """Such a handler turns EasyList off: it answers with no error and EasyList off, and its requests load."""
        def unguarded(o):
            o["easylist_answer"] = lists_state(easylist_on=False)
            o["easylist_probe"] = "failed"
        problems = self.problems(unguarded)
        self.assertTrue(any("turning EasyList off was not refused" in p for p in problems), problems)
        self.assertTrue(any("EasyList is off after a request" in p for p in problems), problems)
        self.assertTrue(any("EasyList request was not blocked" in p for p in problems), problems)
        status, detail, _ = fl.judge({**good(), "lists": {**good_lists(), "easylist_answer": lists_state(easylist_on=False)}})
        self.assertEqual(status, "fail")
        self.assertIn("turning EasyList off was not refused", detail)

    def test_a_refusal_that_leaves_easylist_off_fails(self):
        problems = self.problems(lambda o: o.update(easylist_answer=lists_state(easylist_on=False, listError="required")))
        self.assertTrue(any("EasyList is off" in p for p in problems), problems)

    def test_update_status_must_go_through_updating_to_updated(self):
        self.assertTrue(self.problems(lambda o: o.update(updating_answer=lists_state())))
        self.assertTrue(self.problems(lambda o: o.update(updated_state=lists_state(custom_status="idle"))))

    def test_an_unreachable_built_in_list_must_end_failed_with_a_code(self):
        self.assertTrue(self.problems(lambda o: o.update(failed_state=lists_state())))

    def test_the_probes_are_the_expected_lists(self):
        text = (Path(fl.__file__).resolve().parent.parent / "fixtures" / "blocking" / "expected.json").read_text()
        self.assertIn(fl.TRACKER_URL, text)
        self.assertIn(fl.AD_URL, text)
        easyprivacy = (Path(__file__).resolve().parent.parent / "overlay/third_party/filter_lists/easyprivacy.txt").read_text()
        easylist = (Path(__file__).resolve().parent.parent / "overlay/third_party/filter_lists/easylist.txt").read_text()
        self.assertIn("||hotjar.com^$third-party", easyprivacy)
        self.assertNotIn("hotjar.com", easylist)
        self.assertIn("googlesyndication.com", easylist)

    def test_the_validation_probes_cover_the_new_message(self):
        names = [n for n, _, _ in fl.VALIDATION_PROBES]
        self.assertTrue(any(n.startswith("setListEnabled") for n in names))
