"""The adblock_score check: the judge (score maths, baseline, leaks), the baseline file, the fixture page and
the check through the runner with a fake browser.

Host names below are synthetic. The outcomes have the shape that ``ad_blocking.collect_outcomes`` produces
from DevTools Network events.
"""

import json
import re
import tempfile
import unittest
from pathlib import Path

from aerie_bench import fixtures
from aerie_bench.checks import REGISTRY, adblock_score as sc

PORT = 4242
CATS = {"Ads": ["a1.example.net", "a2.example.net", "a3.example.net", "a4.example.net"],
        "Analytics": ["t1.example.org", "t2.example.org"]}


def url(host):
    return sc.probe_url(host, PORT)


def blocked(host):
    return {"url": url(host), "type": "Fetch", "state": "blocked", "error": sc.BLOCKED_ERROR, "status": None}


def loaded(host):
    return {"url": url(host), "type": "Fetch", "state": "loaded", "error": None, "status": 200}


def failed(host, error="net::ERR_CONNECTION_REFUSED"):
    return {"url": url(host), "type": "Fetch", "state": "failed", "error": error, "status": None}


def outcomes(**kw):
    """host -> outcome from keyword lists: outcomes(blocked=[...], loaded=[...])."""
    out = {}
    for kind, make in (("blocked", blocked), ("loaded", loaded), ("failed", failed)):
        for host in kw.get(kind, []):
            out[url(host)] = make(host)
    return out


ALL = [h for hosts in CATS.values() for h in hosts]


def judge(oc, netlog_hosts=(), fixture_hosts=None, baseline=None, **kw):
    if fixture_hosts is None:  # a request that loaded reached the fixture server
        fixture_hosts = {u.split("//")[1].split(":")[0] for u, o in oc.items() if o["state"] == "loaded"}
    return sc.judge(CATS, oc, PORT, set(netlog_hosts), set(fixture_hosts), baseline, hostlist_sha256="f" * 64, **kw)


class ScoreTest(unittest.TestCase):
    def test_score_is_blocked_over_total_per_category_and_overall(self):
        status, detail, data = judge(outcomes(blocked=ALL[:4], loaded=ALL[4:]))
        self.assertEqual(status, "pass", detail)
        self.assertEqual(data["score"]["overall"], {"blocked": 4, "total": 6, "percent": 66.7})
        self.assertEqual(data["score"]["categories"]["Ads"], {"blocked": 4, "total": 4, "percent": 100.0})
        self.assertEqual(data["score"]["categories"]["Analytics"], {"blocked": 0, "total": 2, "percent": 0.0})
        self.assertIn("4/6", detail)
        self.assertIn("Ads 4/4", detail)
        self.assertIn("Analytics 0/2", detail)

    def test_hosts_that_got_through_are_listed_in_the_message_and_the_data(self):
        _, detail, data = judge(outcomes(blocked=ALL[:4], loaded=ALL[4:]))
        self.assertEqual(data["score"]["through_hosts"], ["t1.example.org", "t2.example.org"])
        for host in ALL[4:]:
            self.assertIn(host, detail)

    def test_everything_blocked_is_a_full_score(self):
        status, detail, data = judge(outcomes(blocked=ALL))
        self.assertEqual((status, data["score"]["overall"]["percent"]), ("pass", 100.0))
        self.assertIn("none got through", detail)

    def test_a_blocked_request_that_the_fixture_saw_is_a_leak_and_fails(self):
        status, detail, _ = judge(outcomes(blocked=ALL), fixture_hosts={"a1.example.net"})
        self.assertEqual(status, "fail")
        self.assertIn("a1.example.net", detail)
        self.assertIn("reached", detail)

    def test_a_blocked_request_in_the_netlog_is_a_leak_and_fails(self):
        status, detail, _ = judge(outcomes(blocked=ALL), netlog_hosts={"t2.example.org"})
        self.assertEqual(status, "fail")
        self.assertIn("t2.example.org", detail)
        self.assertIn("NetLog", detail)

    def test_a_host_the_browser_never_requested_or_failed_for_another_reason_cannot_be_judged(self):
        oc = outcomes(blocked=ALL[:4], failed=["t1.example.org"])  # t2 never requested
        status, detail, data = judge(oc)
        self.assertEqual(status, "fail")
        self.assertEqual(sorted(data["score"]["unjudged"]), ["t1.example.org", "t2.example.org"])
        self.assertIn("could not be judged", detail)
        self.assertIn("ERR_CONNECTION_REFUSED", detail)

    def test_the_data_records_which_list_was_used(self):
        _, _, data = judge(outcomes(blocked=ALL))
        self.assertEqual(data["hostlist_sha256"], "f" * 64)
        self.assertEqual(data["score"]["blocked_hosts"], sorted(ALL))


class BaselineTest(unittest.TestCase):
    def baseline(self, blocked_hosts, total=None):
        n = total or len(ALL)
        return {"schema": 1, "overall": {"blocked": len(blocked_hosts), "total": n},
                "categories": {}, "blocked_hosts": sorted(blocked_hosts), "through_hosts": []}

    def test_no_baseline_passes_and_says_the_score_was_recorded(self):
        status, detail, data = judge(outcomes(blocked=ALL[:3], loaded=ALL[3:]))
        self.assertEqual(status, "pass")
        self.assertIsNone(data["baseline"])

    def test_the_same_score_passes(self):
        status, detail, data = judge(outcomes(blocked=ALL[:4], loaded=ALL[4:]), baseline=self.baseline(ALL[:4]))
        self.assertEqual(status, "pass", detail)
        self.assertEqual(data["baseline"]["problems"], [])

    def test_a_better_score_passes(self):
        status, _, _ = judge(outcomes(blocked=ALL), baseline=self.baseline(ALL[:4]))
        self.assertEqual(status, "pass")

    def test_a_drop_of_more_than_the_threshold_fails(self):
        # baseline 6/6 -> now 5/6: but a host that got through was blocked before, so give both reasons
        status, detail, _ = judge(outcomes(blocked=ALL[:5], loaded=ALL[5:]), baseline=self.baseline(ALL))
        self.assertEqual(status, "fail")
        self.assertIn("dropped from 100.0% (6/6) to 83.3% (5/6)", detail)

    def test_a_drop_inside_the_threshold_alone_passes(self):
        # 200 hosts: one fewer blocked is a 0.5 point drop. The host is not one the baseline blocked.
        cats = {"Ads": [f"h{i}.example.net" for i in range(200)]}
        hosts = cats["Ads"]
        oc = {sc.probe_url(h, PORT): (loaded(h) if h == "h0.example.net" else blocked(h)) | {"url": sc.probe_url(h, PORT)}
              for h in hosts}
        base = {"overall": {"blocked": 200, "total": 200}, "blocked_hosts": []}
        status, detail, _ = sc.judge(cats, oc, PORT, set(), {"h0.example.net"}, base, hostlist_sha256="f" * 64)
        self.assertEqual(status, "pass", detail)

    def test_a_host_blocked_in_the_baseline_that_gets_through_now_fails_even_if_the_score_rose(self):
        # now 5/6 blocked, but a2 got through while the baseline had blocked it; the baseline only had 4 blocked
        base = self.baseline(ALL[:4])
        oc = outcomes(blocked=[ALL[0], ALL[2], ALL[3], ALL[4], ALL[5]], loaded=[ALL[1]])
        status, detail, data = judge(oc, baseline=base)
        self.assertEqual(status, "fail")
        self.assertIn("newly unblocked", detail)
        self.assertIn(ALL[1], detail)
        self.assertEqual(data["baseline"]["newly_unblocked"], [ALL[1]])

    def test_a_baseline_host_that_is_no_longer_on_the_list_is_ignored(self):
        base = self.baseline(ALL[:4] + ["gone.example.net"], total=7)
        status, detail, _ = judge(outcomes(blocked=ALL), baseline=base)
        self.assertEqual(status, "pass", detail)

    def test_a_different_list_is_noted(self):
        base = {**self.baseline(ALL[:4]), "hostlist_sha256": "0" * 64}
        _, _, data = judge(outcomes(blocked=ALL), baseline=base)
        self.assertIn("different host list", data["baseline"]["note"])

    def test_a_filter_engine_that_is_off_fails_against_a_baseline_of_a_working_one(self):
        # the negative control: nothing blocked now, the baseline blocked everything
        status, detail, _ = judge(outcomes(loaded=ALL), baseline=self.baseline(ALL))
        self.assertEqual(status, "fail")
        self.assertIn("dropped", detail)


class BaselineFileTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.dir, ignore_errors=True))

    def test_the_artifact_the_check_writes_is_a_valid_baseline(self):
        _, _, data = judge(outcomes(blocked=ALL[:4], loaded=ALL[4:]))
        path = self.dir / "adblock_score.json"
        path.write_text(json.dumps(sc.score_document(data)))
        base = sc.load_baseline(path)
        self.assertEqual(base["overall"]["blocked"], 4)
        status, _, _ = judge(outcomes(blocked=ALL[:4], loaded=ALL[4:]), baseline=base)
        self.assertEqual(status, "pass")

    def test_a_missing_or_broken_baseline_is_an_error(self):
        with self.assertRaisesRegex(sc.BaselineError, "cannot read"):
            sc.load_baseline(self.dir / "nope.json")
        bad = self.dir / "bad.json"
        bad.write_text("{}")
        with self.assertRaisesRegex(sc.BaselineError, "not an adblock_score"):
            sc.load_baseline(bad)


class FixtureTest(unittest.TestCase):
    def test_the_check_is_registered(self):
        self.assertIn("adblock_score", REGISTRY)

    def test_the_fixture_page_asks_like_the_source_test(self):
        page = (fixtures.FIXTURE_DIR / "adblock" / "index.html").read_text()
        self.assertIn("method: 'HEAD'", page)
        self.assertIn("mode: 'no-cors'", page)
        self.assertIn("runAdblockTest", page)
        self.assertEqual(re.findall(r"https?://[a-z]", page), [], "the fixture names no external host")

    def test_the_probe_url_goes_to_the_fixture_path(self):
        self.assertEqual(sc.probe_url("a.example.net", 80), "http://a.example.net:80/adblock/fakepage.html")


# ---- the check itself, against a fake session and through the runner ----

LIST = """#====== Ads =====
0.0.0.0 a1.example.net
0.0.0.0 a2.example.net
#====== Analytics =====
0.0.0.0 t1.example.org
"""
LIST_BYTES = LIST.encode()


def make_pin():
    import hashlib
    from aerie_bench import adblock_hosts as ah
    return ah.Pin("https://lists.example/x.txt", hashlib.sha256(LIST_BYTES).hexdigest(), len(LIST_BYTES), "2026-11-09")


class FakeResponse:
    def __init__(self, data):
        self.data, self.status, self.headers = data, 200, {}

    def read(self, n=-1):
        return self.data

    def geturl(self):
        return "https://lists.example/x.txt"

    def close(self):
        pass


def good_opener(request):
    return FakeResponse(LIST_BYTES)


def offline_opener(request):
    import urllib.error
    raise urllib.error.URLError("no route")


class FakeServer:
    port = PORT


class FakeFixtures:
    def __init__(self, hosts):
        self.hosts = hosts

    def snapshot(self):
        return [{"path": sc.PROBE_PATH, "host": f"{h}:{PORT}", "status": 200} for h in self.hosts]


class FakeSession:
    """Plays the DevTools events of a browser that blocks `blocked_hosts` and lets the others through."""

    def __init__(self, blocked_hosts, out_dir, netlog_urls=(), online=None):
        self.blocked_hosts = set(blocked_hosts)
        self.out_dir = Path(out_dir)
        self.artifacts, self.sent = [], []
        self.netlog = [{"url": u, "urls": [u], "host": _host(u), "external": True} for u in netlog_urls]
        self.netlog_lookups = []
        self.variant_launcher = None if online is None else (lambda *a, **k: None)
        self.online = online
        self.fixtures = FakeFixtures([])

    def cdp(self, method, params=None):
        self.sent.append(method)

    def navigate(self, path):
        self.sent.append(("navigate", path))

    def evaluate(self, expression, **kw):
        self.sent.append(expression)
        if "runAdblockTest" in expression:
            hosts = json.loads(expression[expression.index("(") + 1:expression.rindex(",")])
            self.hosts = hosts
            self.fixtures = FakeFixtures([h for h in hosts if h not in self.blocked_hosts])
            return len(hosts)
        return True

    def take_events(self, method):
        events = []
        for n, host in enumerate(getattr(self, "hosts", [])):
            u, rid = url(host), f"r{n}"
            if method == "Network.requestWillBeSent":
                events.append({"method": method, "params": {"requestId": rid, "type": "Fetch", "request": {"url": u}}})
            elif host in self.blocked_hosts and method == "Network.loadingFailed":
                events.append({"method": method, "params": {"requestId": rid, "errorText": sc.BLOCKED_ERROR}})
            elif host not in self.blocked_hosts and method == "Network.responseReceived":
                events.append({"method": method, "params": {"requestId": rid, "response": {"status": 200}}})
        return events

    def variant(self, flags, prefs, profile=None):
        import contextlib

        @contextlib.contextmanager
        def ctx():
            self.variant_flags = flags
            yield self.online
        return ctx()


def _host(u):
    return u.split("//")[1].split(":")[0].split("/")[0]


class CheckTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.dir, ignore_errors=True))
        import aerie_bench.adblock_hosts as ah
        self.lock = self.dir / "lock.json"
        pin = make_pin()
        self.lock.write_text(json.dumps({"schema": 1, "url": pin.url, "sha256": pin.sha256, "size": pin.size,
                                         "date": pin.date}))

    def check(self, **kw):
        return sc.AdblockScore(build_root=self.dir, pin_path=self.lock, opener=kw.pop("opener", good_opener),
                               settle_seconds=2.0, **kw)

    def run_check(self, check, session):
        flags = check.browser_flags(FakeServer())
        result = check.run(session)
        return flags, check.finalize(session, result)

    def test_flags_map_the_listed_hosts_to_loopback(self):
        check = self.check()
        flags = check.browser_flags(FakeServer())
        self.assertEqual(len(flags), 1)
        for host in ("a1.example.net", "a2.example.net", "t1.example.org"):
            self.assertIn(f"MAP {host} 127.0.0.1", flags[0])

    def test_a_blocking_browser_scores_and_writes_the_baseline_artifact(self):
        session = FakeSession(["a1.example.net", "a2.example.net"], self.dir)
        _, result = self.run_check(self.check(), session)
        self.assertEqual(result.status, "pass", result.detail)
        self.assertEqual(result.data["score"]["overall"], {"blocked": 2, "total": 3, "percent": 66.7})
        self.assertIn("t1.example.org", result.detail)
        self.assertEqual(result.data["hostlist"]["source"], "download")
        self.assertIn(sc.ARTIFACT, session.artifacts)
        self.assertEqual(sc.load_baseline(self.dir / sc.ARTIFACT)["overall"]["blocked"], 2)
        self.assertIn(("navigate", sc.PAGE), session.sent)

    def test_a_browser_that_blocks_nothing_fails_against_a_baseline(self):
        good = FakeSession(["a1.example.net", "a2.example.net", "t1.example.org"], self.dir)
        self.run_check(self.check(), good)
        off = FakeSession([], self.dir)
        _, result = self.run_check(self.check(baseline=str(self.dir / sc.ARTIFACT)), off)
        self.assertEqual(result.status, "fail")
        self.assertIn("dropped", result.detail)
        self.assertIn("newly unblocked", result.detail)

    def test_a_leak_to_the_netlog_fails(self):
        session = FakeSession(["a1.example.net", "a2.example.net", "t1.example.org"], self.dir,
                              netlog_urls=[url("a1.example.net")])
        _, result = self.run_check(self.check(), session)
        self.assertEqual(result.status, "fail")
        self.assertIn("a1.example.net", result.detail)

    def test_traffic_to_unlisted_hosts_is_recorded_not_failed(self):
        session = FakeSession(["a1.example.net", "a2.example.net", "t1.example.org"], self.dir,
                              netlog_urls=["https://other.example.com/x"])
        _, result = self.run_check(self.check(), session)
        self.assertEqual(result.status, "pass", result.detail)
        self.assertEqual(result.data["other_external_hosts"], ["other.example.com"])

    def test_no_network_and_no_cache_skips_with_the_reason_and_touches_no_page(self):
        session = FakeSession([], self.dir)
        flags, result = self.run_check(self.check(opener=offline_opener), session)
        self.assertEqual((flags, result.status), ((), "skip"))
        self.assertIn("no usable cache", result.detail)
        self.assertEqual(session.sent, [])

    def test_no_network_with_a_cache_from_an_earlier_run_still_scores(self):
        self.run_check(self.check(), FakeSession([], self.dir))
        _, result = self.run_check(self.check(opener=offline_opener), FakeSession(["a1.example.net"], self.dir))
        self.assertEqual(result.data["hostlist"]["source"], "cache")

    def test_a_pin_that_does_not_match_the_download_is_an_error(self):
        lock = json.loads(self.lock.read_text())
        lock["sha256"] = "0" * 64
        self.lock.write_text(json.dumps(lock))
        _, result = self.run_check(self.check(), FakeSession([], self.dir))
        self.assertEqual(result.status, "error")
        self.assertIn("sha256", result.detail)

    def test_an_unreadable_baseline_is_an_error(self):
        check = self.check(baseline=str(self.dir / "missing.json"))
        check.browser_flags(FakeServer())
        with self.assertRaises(sc.BaselineError):
            check.run(FakeSession([], self.dir))

    def test_online_is_off_by_default(self):
        session = FakeSession([], self.dir, online={"state": "finished"})
        _, result = self.run_check(self.check(), session)
        self.assertNotIn("online", result.data)
        self.assertFalse(hasattr(session, "variant_flags"))

    def test_the_online_result_lands_next_to_the_offline_score_and_never_fails(self):
        online = {"url": "https://adblock.turtlecute.org/", "state": "finished", "blocked": 90, "total": 128, "percent": 70.3}
        session = FakeSession(["a1.example.net", "a2.example.net", "t1.example.org"], self.dir, online=object())
        import aerie_bench.adblock_online as ao
        original = ao.read_live
        ao.read_live = lambda live, wait_seconds: online
        self.addCleanup(lambda: setattr(ao, "read_live", original))
        _, result = self.run_check(self.check(online_seconds=5), session)
        self.assertEqual(result.status, "pass")
        self.assertEqual(result.data["online"], online)
        self.assertEqual(result.data["score"]["overall"]["blocked"], 3)
        self.assertIn("90/128", result.detail)
        self.assertEqual(session.variant_flags, sc.ONLINE_FLAGS)

    def test_an_online_failure_is_reported_in_the_results_and_the_offline_verdict_stands(self):
        session = FakeSession(["a1.example.net", "a2.example.net", "t1.example.org"], self.dir)  # no variant launcher
        _, result = self.run_check(self.check(online_seconds=5), session)
        self.assertEqual(result.status, "pass")
        self.assertEqual(result.data["online"]["state"], "error")

    def test_online_still_runs_when_the_offline_part_is_skipped(self):
        session = FakeSession([], self.dir)
        _, result = self.run_check(self.check(opener=offline_opener, online_seconds=5), session)
        self.assertEqual(result.status, "skip")
        self.assertEqual(result.data["online"]["state"], "error")


class RunnerAndCliTest(unittest.TestCase):
    FAKE = Path(__file__).parent / "bench_fake_browser.py"

    def test_through_the_runner_a_train_without_network_or_cache_skips_loudly(self):
        from aerie_bench import runner, scenario
        root = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(root, ignore_errors=True))
        res = runner.run_bench(self.FAKE, scenario.Scenario(checks=["adblock_score"]), root, timeout=10,
                               check_options={"adblock_score": {"build_root": str(root), "opener": offline_opener}})
        check = res.data["checks"][0]
        self.assertEqual(check["status"], "skip")
        self.assertIn("cannot download", check["detail"])
        self.assertIn("SKIP", (res.out_dir / "summary.txt").read_text())

    def parse(self, argv):
        import argparse
        from aerie_bench import cli
        p = argparse.ArgumentParser()
        cli.add_arguments(p, binary_required=False)
        return p.parse_args(argv)

    def test_the_online_run_is_off_and_there_is_no_baseline_by_default(self):
        args = self.parse([])
        self.assertEqual((args.adblock_online, args.adblock_baseline, args.adblock_update_pin), (0.0, None, None))

    def test_the_flags_reach_the_check(self):
        from unittest import mock
        from aerie_bench import cli
        args = self.parse(["--check", "adblock_score", "--adblock-online", "90", "--adblock-baseline", "/x/b.json"])
        with mock.patch.object(cli, "run_bench", side_effect=RuntimeError("stop")) as rb, \
                self.assertRaises(RuntimeError):
            cli.run(args, Path("/tmp/x"), self.FAKE)
        opts = rb.call_args.kwargs["check_options"]["adblock_score"]
        self.assertEqual((opts["online_seconds"], opts["baseline"], opts["build_root"]), (90.0, "/x/b.json", "/tmp/x"))

    def test_the_flags_need_the_check(self):
        import contextlib
        import io
        from aerie_bench import cli
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            code = cli.run(self.parse(["--adblock-online", "60"]), Path("/tmp/x"), self.FAKE)
        self.assertEqual(code, cli.EXIT_HARNESS)
        self.assertIn("--check adblock_score", err.getvalue())

    def test_update_pin_does_not_need_a_browser_and_writes_the_lock(self):
        from unittest import mock
        from aerie_bench import adblock_hosts, cli
        with mock.patch.object(adblock_hosts, "update_pin") as up:
            code = cli.run(self.parse(["--adblock-update-pin", "a" * 40]), Path("/tmp/x"), None)
        self.assertEqual(code, cli.EXIT_PASS)
        self.assertEqual(up.call_args.kwargs["commit"], "a" * 40)
        with mock.patch.object(adblock_hosts, "update_pin") as up:
            cli.run(self.parse(["--adblock-update-pin"]), Path("/tmp/x"), None)
        self.assertIsNone(up.call_args.kwargs["commit"])


if __name__ == "__main__":
    unittest.main()
