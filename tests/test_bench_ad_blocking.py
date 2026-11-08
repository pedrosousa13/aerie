"""The ad_blocking check: its decision logic, run on recorded DevTools events, and its fixture page.

The recordings in tests/data/ad_blocking/ have the format that the check writes
to `ad_blocking-recording.json` in a Bench results directory. They were built
from the DevTools protocol's event shapes before a browser with the Filter
Engine existed. The first real run in the build train writes the real
recording; copy it over tests/data/ad_blocking/pass.json then.
"""

import json
import re
import tempfile
import unittest
from pathlib import Path

from aerie_bench import fixtures, netlog, procmem
from aerie_bench.checks import REGISTRY, ad_blocking as ab

REPO = Path(__file__).resolve().parents[1]
DATA = Path(__file__).resolve().parent / "data" / "ad_blocking"
EXPECTED = ab.load_expected()


def recording(name):
    return json.loads((DATA / f"{name}.json").read_text())


def netlog_urls(rec):
    """URLs the browser's network stack saw. A recording may hold raw NetLog text, so the parser is in the path."""
    if "netlog_text" in rec:
        return netlog.all_urls(netlog.parse(rec["netlog_text"]))
    return set(rec["netlog_urls"])


def judge(name):
    rec = recording(name)
    outcomes = ab.collect_outcomes(rec["events"])
    return ab.judge(EXPECTED, outcomes, netlog_urls(rec), rec["fixture_paths"], rec["first_party_script_ran"],
                    rec["engine"], rec.get("fixture_redirects"))


class JudgeTest(unittest.TestCase):
    def test_a_blocking_browser_passes(self):
        status, detail, data = judge("pass")
        self.assertEqual(status, "pass", detail)
        self.assertIn("7/7 ad and tracker requests blocked", detail)
        self.assertIn("2/2 first-party requests loaded", detail)
        self.assertTrue(all(r["state"] == "blocked" and not r["in_netlog"] for r in data["blocked"]))

    def test_a_browser_that_blocks_nothing_fails_and_names_every_url(self):
        status, detail, _ = judge("nothing_blocked")
        self.assertEqual(status, "fail")
        for item in EXPECTED["blocked"]:
            self.assertIn(item["url"], detail)
        self.assertIn("NOT BLOCKED", detail)
        self.assertIn("ERR_NAME_NOT_RESOLVED", detail)

    def test_one_missed_url_fails(self):
        status, detail, data = judge("one_missed")
        self.assertEqual(status, "fail")
        self.assertIn("1 problem(s)", detail)
        self.assertIn(EXPECTED["blocked"][0]["url"], detail)
        self.assertEqual(sum(r["state"] == "failed" for r in data["blocked"]), 1)

    def test_broken_first_party_content_fails(self):
        status, detail, _ = judge("first_party_broken")
        self.assertEqual(status, "fail")
        self.assertIn("first-party content did not load", detail)
        self.assertIn("first-party script did not run", detail)

    def test_a_url_the_page_never_requested_fails(self):
        status, detail, _ = judge("one_never_requested")
        self.assertEqual(status, "fail")
        self.assertIn("never requested by the page", detail)

    def test_blocked_in_devtools_but_seen_in_the_netlog_fails(self):
        status, detail, _ = judge("blocked_but_reached_network")
        self.assertEqual(status, "fail")
        self.assertIn("reached the network stack", detail)

    def test_first_party_must_also_have_reached_the_fixture_server(self):
        rec = recording("pass")
        outcomes = ab.collect_outcomes(rec["events"])
        status, detail, _ = ab.judge(EXPECTED, outcomes, set(rec["netlog_urls"]), ["/blocking/index.html"], True, rec["engine"])
        self.assertEqual(status, "fail")
        self.assertIn("fixture server never saw it", detail)

    # The engine, not something else, must be what blocks. Helium still ships
    # uBlock Origin, which blocks the same URLs with the same error.

    def test_blocking_that_the_engine_did_not_do_does_not_pass(self):
        status, detail, _ = judge("blocked_by_someone_else")
        self.assertEqual(status, "fail")
        self.assertIn("Aerie.Adblock.RequestBlocked", detail)
        self.assertIn("uBlock Origin", detail)

    def test_an_engine_that_blocked_only_some_does_not_pass(self):
        status, detail, _ = judge("engine_blocked_only_some")
        self.assertEqual(status, "fail")
        self.assertIn("blocked 3 request(s), the page needs 8", detail)

    def test_missing_engine_histograms_fail(self):
        status, detail, _ = judge("engine_absent")
        self.assertEqual(status, "fail")
        self.assertIn("Aerie.Adblock.EngineLoadSuccess", detail)

    def test_a_failed_engine_load_sample_fails(self):
        status, detail, _ = judge("engine_load_failed")
        self.assertEqual(status, "fail")
        self.assertIn("load", detail)

    def test_the_passing_recording_credits_the_engine(self):
        _, detail, data = judge("pass")
        self.assertEqual(data["engine"]["requests"]["true"], 8)

    def test_buckets_to_counts(self):
        hist = {"count": 3, "sum": 2, "buckets": [{"low": 0, "high": 1, "count": 1}, {"low": 1, "high": 2, "count": 2}]}
        self.assertEqual(ab.boolean_counts(hist), {"true": 2, "false": 1})
        self.assertEqual(ab.boolean_counts({"count": 0, "buckets": []}), {"true": 0, "false": 0})

    def test_no_events_at_all_fails(self):
        status, detail, _ = ab.judge(EXPECTED, {}, set(), [], None, None)
        self.assertEqual(status, "fail")
        self.assertIn("never requested", detail)
        self.assertIn("first-party request never made", detail)


class CollectOutcomesTest(unittest.TestCase):
    def test_states(self):
        outcomes = ab.collect_outcomes(recording("one_missed")["events"])
        by_url = {u: o["state"] for u, o in outcomes.items()}
        self.assertEqual(by_url[EXPECTED["blocked"][0]["url"]], "failed")
        self.assertEqual(by_url[EXPECTED["blocked"][1]["url"]], "blocked")
        self.assertEqual(by_url["http://127.0.0.1:41233/blocking/content.js"], "loaded")

    def test_request_without_a_final_event_is_pending(self):
        events = [{"method": "Network.requestWillBeSent", "params": {"requestId": "1", "request": {"url": "https://a/"}, "type": "Script"}}]
        self.assertEqual(ab.collect_outcomes(events)["https://a/"]["state"], "pending")

    def test_events_for_unknown_requests_and_other_methods_are_ignored(self):
        events = [{"method": "Network.loadingFailed", "params": {"requestId": "9", "errorText": "x"}},
                  {"method": "Page.loadEventFired", "params": {}}]
        self.assertEqual(ab.collect_outcomes(events), {})

    def test_settled(self):
        events = recording("pass")["events"]
        self.assertTrue(ab.is_settled(EXPECTED, ab.collect_outcomes(events)))
        self.assertFalse(ab.is_settled(EXPECTED, ab.collect_outcomes(events[:4])))
        self.assertFalse(ab.is_settled(EXPECTED, {}))

    def test_event_order_does_not_matter_after_ranking(self):
        # The check reads each method's events separately, so only the order
        # BETWEEN methods is lost; within one method (the hops of a redirect)
        # it is kept.
        events = sorted(recording("pass")["events"], key=lambda e: -ab._event_rank(e))
        outcomes = ab.collect_outcomes(sorted(events, key=ab._event_rank))
        self.assertTrue(ab.is_settled(EXPECTED, outcomes))


class FixtureTest(unittest.TestCase):
    PAGE = (fixtures.FIXTURE_DIR / "blocking" / "index.html").read_text()

    def test_every_expected_blocked_url_is_in_the_page_and_every_external_url_in_the_page_is_expected(self):
        expected = {b["url"] for b in EXPECTED["blocked"]}
        in_page = set(re.findall(r'(?:src|href)="(https?://[^"]+)"', self.PAGE))
        in_page |= set(re.findall(r'fetch\("(https?://[^"]+)"', self.PAGE))
        self.assertEqual(in_page, expected)

    def test_first_party_paths_are_served_by_the_fixture_server(self):
        with fixtures.FixtureServer() as server:
            import urllib.request
            for item in EXPECTED["allowed"]:
                with urllib.request.urlopen(server.base_url + item["path"]) as resp:
                    self.assertEqual(resp.status, 200)
            self.assertEqual(urllib.request.urlopen(server.base_url + EXPECTED["page"]).status, 200)

    def test_first_party_script_sets_the_flag_the_check_reads(self):
        self.assertIn("firstPartyScriptLoaded = true", (fixtures.FIXTURE_DIR / "blocking" / "content.js").read_text())

    def test_expected_blocked_urls_are_all_https_and_name_no_loopback_host(self):
        for item in EXPECTED["blocked"]:
            self.assertTrue(item["url"].startswith("https://"))
            self.assertNotIn("127.0.0.1", item["url"])

    def test_first_party_paths_are_not_filter_shaped(self):
        # The real lists have generic path rules (for example /ads/). The paths
        # were chosen so that no list blocks them; keep new ones plain.
        for item in EXPECTED["allowed"]:
            self.assertNotRegex(item["path"], r"(?i)ad[sv]?[/._-]|banner|track|analytics|pixel|beacon")

    def test_the_whole_fixture_dir_references_no_external_host_except_the_blocking_and_perf_pages(self):
        allowed_hosts = {re.match(r"https?://([^/]+)", b["url"]).group(1) for b in EXPECTED["blocked"]}
        for f in fixtures.FIXTURE_DIR.rglob("*"):
            if not f.is_file() or f.suffix not in (".html", ".css", ".js"):
                continue
            for url in re.findall(r"(?:https?:)?//[^\s\"')]+", f.read_text()):
                host = re.sub(r"^(?:https?:)?//", "", url).split("/")[0].split(":")[0]
                if host in ("127.0.0.1", "localhost"):
                    continue
                # The perf check's ad-heavy pages name the same hosts (issue #16).
                self.assertIn(f.parent.name, ("blocking", "perf"), f"{f}: {url}")
                self.assertIn(host, allowed_hosts, f"{f}: {url}")


FAKE = Path(__file__).parent / "bench_fake_browser.py"


class RunnerEndToEndTest(unittest.TestCase):
    """The check through the real runner, against the fake browser (no Chromium needed)."""

    def run_check(self, mode, more_env=None):
        from aerie_bench import runner, scenario
        root = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(root, ignore_errors=True))
        env = {"FAKE_BROWSER_BLOCKING": mode} if mode else {}
        env.update(more_env or {})
        res = runner.run_bench(FAKE, scenario.Scenario(checks=["ad_blocking"]), root, timeout=10, extra_env=env,
                               check_options={"ad_blocking": {"settle_seconds": 1.0}})
        check = next(c for c in res.data["checks"] if c["name"] == "ad_blocking")
        return res, check

    def test_a_blocking_browser_passes_and_records_metrics(self):
        res, check = self.run_check("block")
        self.assertEqual(check["status"], "pass", check["detail"])
        self.assertEqual(res.status, "pass")
        metrics = check["data"]["metrics"]
        self.assertEqual(metrics["note"], "recorded only; no threshold yet")
        for key in ("page_timing", "resource_entries", "memory", "engine_load"):
            self.assertIn(key, metrics)
        # The fake browser is a Python process, so /proc has a real tree to sum.
        self.assertGreater(metrics["memory"]["total_kb"], 0)
        self.assertIn("ad_blocking-recording.json", res.data["files"])
        rec = json.loads((res.out_dir / "ad_blocking-recording.json").read_text())
        self.assertEqual(set(rec), {"events", "netlog_urls", "fixture_paths", "fixture_redirects", "first_party_script_ran", "engine"})

    def test_blocking_by_something_else_fails_the_run(self):
        # Blocks every URL, but has no Aerie engine histograms: another blocker's work.
        res, check = self.run_check("block", {"FAKE_BROWSER_ENGINE": "none"})
        self.assertEqual(check["status"], "fail")
        self.assertIn("no Aerie Filter Engine", check["detail"])
        self.assertNotIn("NOT BLOCKED", check["detail"])

    def test_a_browser_without_a_blocker_fails_the_run(self):
        res, check = self.run_check("none")
        self.assertEqual(check["status"], "fail")
        self.assertIn("NOT BLOCKED", check["detail"])
        self.assertEqual(res.status, "fail")
        self.assertEqual(res.exit_code, 1)

    def test_a_browser_that_never_loads_the_page_content_fails(self):
        res, check = self.run_check(None)
        self.assertEqual(check["status"], "fail")
        self.assertIn("never requested", check["detail"])


class RedirectFixtureTest(unittest.TestCase):
    def test_the_fixture_server_serves_the_redirects_the_expectations_name(self):
        import http.client
        redirects = EXPECTED["redirects"]
        with fixtures.FixtureServer() as server:
            for item in redirects["blocked"] + redirects["allowed"]:
                conn = http.client.HTTPConnection("127.0.0.1", server.port)
                conn.request("GET", item["path"])
                resp = conn.getresponse()
                self.assertEqual(resp.status, 302, item["path"])
                self.assertEqual(resp.getheader("Location"), item.get("target") or item["final"])
                conn.close()
            # the first-party final target is a real file
            import urllib.request
            self.assertEqual(urllib.request.urlopen(server.base_url + redirects["allowed"][0]["final"]).status, 200)

    def test_the_page_uses_every_redirect_entry(self):
        page = (fixtures.FIXTURE_DIR / "blocking" / "index.html").read_text()
        for item in EXPECTED["redirects"]["blocked"] + EXPECTED["redirects"]["allowed"]:
            self.assertIn(item["path"], page)

    def test_redirect_targets_in_expectations_are_not_in_the_plain_blocked_list(self):
        # The point is that nothing in the page asks for the tracker directly.
        page = (fixtures.FIXTURE_DIR / "blocking" / "index.html").read_text()
        for item in EXPECTED["redirects"]["blocked"]:
            self.assertNotIn(item["target"], page)

    def test_redirects_never_leave_the_listed_hosts(self):
        hosts = {re.match(r"https?://([^/]+)", b["url"]).group(1) for b in EXPECTED["blocked"]}
        for item in EXPECTED["redirects"]["blocked"]:
            self.assertIn(re.match(r"https?://([^/]+)", item["target"]).group(1), hosts)


class RedirectJudgeTest(unittest.TestCase):
    def test_a_blocking_browser_that_judges_redirects_passes(self):
        status, detail, data = judge("pass")
        self.assertEqual(status, "pass", detail)
        self.assertIn("redirect", detail)
        self.assertTrue(all(r["state"] == "blocked" for r in data["redirects"]["blocked"]))
        self.assertTrue(all(r["state"] == "loaded" for r in data["redirects"]["allowed"]))

    def test_a_redirect_that_reached_the_tracker_fails(self):
        status, detail, _ = judge("redirect_not_blocked")
        self.assertEqual(status, "fail")
        self.assertIn("redirect", detail)
        self.assertIn("NOT BLOCKED", detail)

    def test_a_blocked_redirect_target_seen_in_the_netlog_fails(self):
        # The only trace of the hop in the NetLog is the second START_JOB of the
        # redirecting source, found by the parser.
        status, detail, _ = judge("redirect_reached_network")
        self.assertEqual(status, "fail")
        self.assertIn("reached the network stack", detail)

    def test_a_good_redirect_that_was_blocked_fails(self):
        status, detail, _ = judge("redirect_allowed_but_blocked")
        self.assertEqual(status, "fail")
        self.assertIn("did not load", detail)

    def test_a_redirect_the_page_never_made_fails(self):
        status, detail, _ = judge("redirect_never_requested")
        self.assertEqual(status, "fail")
        self.assertIn("never requested", detail)

    def test_every_hop_has_an_outcome(self):
        outcomes = ab.collect_outcomes(recording("pass")["events"])
        src = [u for u in outcomes if u.endswith("/blocking/go-tracker")]
        self.assertEqual(outcomes[src[0]]["state"], "redirected")
        self.assertEqual(outcomes[src[0]]["status"], 302)

    def test_the_hop_needs_its_own_engine_counter(self):
        status, detail, _ = judge("engine_missed_the_hop")
        self.assertEqual(status, "fail")
        self.assertIn("Aerie.Adblock.RedirectHopBlocked", detail)

    def test_hop_counter_absent_fails(self):
        rec = recording("pass")
        engine = dict(rec["engine"], redirect_hops=None)
        status, detail, _ = ab.judge(EXPECTED, ab.collect_outcomes(rec["events"]), netlog_urls(rec),
                                     rec["fixture_paths"], True, engine, rec["fixture_redirects"])
        self.assertEqual(status, "fail")
        self.assertIn("Aerie.Adblock.RedirectHopBlocked", detail)

    def test_engine_must_have_judged_the_redirect_hop_too(self):
        rec = recording("pass")
        engine = dict(rec["engine"])
        engine["requests"] = {"true": len(EXPECTED["blocked"]), "false": 3}  # the plain ones only
        outcomes = ab.collect_outcomes(rec["events"])
        status, detail, _ = ab.judge(EXPECTED, outcomes, set(rec["netlog_urls"]), rec["fixture_paths"], True, engine,
                                     rec["fixture_redirects"])
        self.assertEqual(status, "fail")
        self.assertIn("the page needs", detail)


class RegistrationTest(unittest.TestCase):
    def test_check_is_registered_and_does_not_seed_services(self):
        self.assertIn("ad_blocking", REGISTRY)
        self.assertFalse(REGISTRY["ad_blocking"].seeds_helium_services)

    def test_bad_expected_file_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "e.json"
            path.write_text(json.dumps({"page": "/x", "blocked": [], "allowed": []}))
            with self.assertRaises(ValueError):
                ab.load_expected(path)


class ProcMemTest(unittest.TestCase):
    def make(self, root, pid, ppid, comm, rss):
        d = root / str(pid)
        d.mkdir()
        (d / "stat").write_text(f"{pid} ({comm}) S {ppid} 1 1 0 -1 0 0 0\n")
        (d / "status").write_text(f"Name:\t{comm}\nVmRSS:\t{rss} kB\n")

    def test_sums_the_tree_below_the_root_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.make(root, 100, 1, "chrome", 1000)
            self.make(root, 101, 100, "chrome (zygote)", 500)
            self.make(root, 102, 101, "chrome", 300)
            self.make(root, 200, 1, "other", 9999)
            (root / "self").mkdir()
            result = procmem.tree_rss_kb(100, root)
            self.assertEqual(result["total_kb"], 1800)
            self.assertEqual([p["pid"] for p in result["processes"]], [100, 101, 102])
            self.assertEqual(result["processes"][1]["comm"], "chrome (zygote)")

    def test_missing_proc_or_process_gives_none(self):
        self.assertIsNone(procmem.tree_rss_kb(1, Path("/nonexistent-proc")))
        with tempfile.TemporaryDirectory() as tmp:
            self.assertIsNone(procmem.tree_rss_kb(5, Path(tmp)))


class CdpEventsTest(unittest.TestCase):
    def test_take_events_consumes_only_matching_events(self):
        from aerie_bench.cdp import CdpClient
        import os
        r, w = os.pipe()
        r2, w2 = os.pipe()
        client = CdpClient(r, w2)
        try:
            with client._lock:
                client._events.extend([{"method": "A", "sessionId": "s"}, {"method": "B", "sessionId": "s"},
                                       {"method": "A", "sessionId": "other"}, {"method": "A", "sessionId": "s"}])
            self.assertEqual(len(client.take_events("A", "s")), 2)
            self.assertEqual(len(client.take_events("A", "s")), 0)
            self.assertEqual(len(client.take_events("B")), 1)
            self.assertEqual(len(client.take_events("A")), 1)
        finally:
            client.close()
            for fd in (w, r2):
                os.close(fd)


if __name__ == "__main__":
    unittest.main()
