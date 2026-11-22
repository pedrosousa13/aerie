import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from aerie_bench import cli, denylist, netlog, runner, scenario
from aerie_bench.checks import zero_traffic as zt

FIXTURES = Path(__file__).parent / "fixtures"
FAKE = Path(__file__).parent / "bench_fake_browser.py"


def records(name):
    return netlog.parse((FIXTURES / f"netlog-{name}.json").read_text())


class EvaluateTest(unittest.TestCase):
    def setUp(self):
        self.dl = denylist.load_default()

    def test_recorded_google_launch_fails_with_named_hosts(self):
        v = zt.evaluate(records("chrome-for-testing-first-launch"), self.dl)
        self.assertEqual(v.status, "fail")
        hosts = {h["host"] for h in v.hits}
        self.assertTrue({"update.googleapis.com", "redirector.gvt1.com", "accounts.google.com",
                         "www.google.com", "clients2.google.com"} <= hosts, hosts)
        self.assertEqual({h["group"] for h in v.hits}, {"google"})
        self.assertNotIn("127.0.0.1", hosts)
        self.assertIn("update.googleapis.com", v.reason)

    def test_helium_baseline_fails_on_imput_hosts(self):
        v = zt.evaluate(records("helium-real"), self.dl)
        self.assertEqual(v.status, "fail")
        self.assertEqual({h["group"] for h in v.hits}, {"helium"})
        self.assertEqual({h["host"] for h in v.hits}, {"services.helium.imput.net"})
        self.assertEqual(len(v.hits), 10)  # the real train-1 baseline run
        self.assertEqual(len(v.blocked), 0)

    def test_clean_run_passes(self):
        v = zt.evaluate(records("aerie-real"), self.dl)  # real Aerie run: 9 fixture requests, 1 refused
        self.assertEqual((v.status, v.hits), ("pass", []))
        self.assertEqual(v.fixture_requests, 9)
        self.assertEqual(len(v.blocked), 1)

    def test_blocked_dummy_host_attempt_is_reported_but_passes_unless_strict(self):
        recs = records("aerie-real")
        v = zt.evaluate(recs, self.dl)
        self.assertEqual(v.status, "pass")
        self.assertEqual([b["url"] for b in v.blocked], ["trk:https://helium-services-are-disabled.qjz9zk/"])
        self.assertEqual([b["net_error"] for b in v.blocked], [-20])
        strict = zt.evaluate(recs, self.dl, strict=True)
        self.assertEqual(strict.status, "fail")
        self.assertIn("blocked", strict.reason)

    def test_empty_capture_is_not_a_pass(self):
        v = zt.evaluate([], self.dl)
        self.assertEqual(v.status, "fail")
        self.assertIn("no fixture page request", v.reason)

    def test_other_external_hosts_are_listed_but_do_not_fail(self):
        recs = records("aerie-real") + [
            {"source_id": 90, "time": "9", "url": "https://duckduckgo.com/?q=x", "method": "GET", "scheme": "https",
             "host": "duckduckgo.com", "initiator": None, "net_error": -105, "external": True}]
        v = zt.evaluate(recs, self.dl)
        self.assertEqual(v.status, "pass")
        self.assertEqual(v.other_hosts, ["duckduckgo.com"])

    def test_phases_split_by_cumulative_request_counts(self):
        recs = records("helium-real")  # 19 requests; the real run's phase ends were 10, 13 and 20
        v = zt.evaluate(recs, self.dl, phase_ends={"first_launch": 10, "idle": 13, "browse": 20})
        by_phase = [h["phase"] for h in v.hits]
        self.assertEqual(by_phase, ["first_launch"] * 7 + ["idle"] * 3)  # as in the train-1 report

    def test_url_rules_apply_to_full_urls(self):
        rec = {"source_id": 1, "time": "1", "method": "GET", "scheme": "https", "host": "raw.githubusercontent.com",
               "url": "https://raw.githubusercontent.com/imputnet/uBlock/main/assets.json", "initiator": None,
               "net_error": None, "external": True}
        v = zt.evaluate([rec] + records("aerie-real"), self.dl)
        self.assertEqual(v.status, "fail")
        self.assertEqual(v.hits[0]["group"], "helium")


    def test_a_denylisted_redirect_hop_fails_even_when_the_first_url_is_clean(self):
        rec = {"source_id": 1, "time": "1", "method": "GET", "scheme": "https", "host": "example.org",
               "url": "https://example.org/go",
               "urls": ["https://example.org/go", "https://raw.githubusercontent.com/imputnet/uBlock/main/assets.json"],
               "initiator": None, "net_error": None, "external": True}
        v = zt.evaluate([rec] + records("aerie-real"), self.dl)
        self.assertEqual(v.status, "fail")
        self.assertEqual(v.hits[0]["group"], "helium")
        self.assertIn("raw.githubusercontent.com", v.hits[0]["url"])


def refused(url, host, error=-22, **extra):
    """A NetLog record of a request that the request policy (issue #257) refused."""
    rec = {"source_id": 70, "time": "7", "url": url, "method": "GET", "scheme": url.split(":")[0], "host": host,
           "initiator": None, "net_error": error, "external": True}
    rec.update(extra)
    return rec


class RefusedByThePolicyTest(unittest.TestCase):
    """The policy's refusals are reported by name: the host, never a path or query (issue #257)."""

    def setUp(self):
        self.dl = denylist.load_default()

    def test_a_refused_attempt_is_named_and_does_not_fail_by_default(self):
        recs = records("aerie-real") + [refused("https://telemetry.example/collect?id=7", "telemetry.example")]
        v = zt.evaluate(recs, self.dl)
        self.assertEqual(v.status, "pass")
        self.assertEqual([r["host"] for r in v.refused], ["telemetry.example"])
        self.assertEqual(v.hits, [])
        self.assertIn("1 attempt(s) refused by the request policy: telemetry.example", v.reason)

    def test_the_report_holds_the_host_only(self):
        recs = records("aerie-real") + [refused("https://user:pw@telemetry.example:8443/secret?token=abc", "telemetry.example")]
        v = zt.evaluate(recs, self.dl)
        self.assertEqual(set(v.refused[0]), {"host", "phase", "net_error", "group"})
        self.assertNotIn("secret", repr(v.refused) + v.reason)
        self.assertNotIn("token", repr(v.refused) + v.reason)

    def test_strict_fails_on_a_refused_attempt(self):
        recs = records("aerie-real") + [refused("https://telemetry.example/", "telemetry.example")]
        v = zt.evaluate(recs, self.dl, strict=True)
        self.assertEqual(v.status, "fail")
        self.assertIn("telemetry.example", v.reason)

    def test_a_refused_google_request_is_named_with_its_group_and_never_left_the_machine(self):
        recs = records("aerie-real") + [refused("https://update.googleapis.com/service/update2/json", "update.googleapis.com")]
        v = zt.evaluate(recs, self.dl)
        self.assertEqual((v.status, v.hits), ("pass", []))
        self.assertEqual((v.refused[0]["host"], v.refused[0]["group"]), ("update.googleapis.com", "google"))

    def test_a_hop_before_the_refused_one_still_counts_as_traffic(self):
        # The first hops of a redirect chain were made; only the last hop was refused.
        recs = records("aerie-real") + [refused(
            "https://telemetry.example/", "telemetry.example",
            urls=["https://www.google.com/go", "https://telemetry.example/"])]
        v = zt.evaluate(recs, self.dl)
        self.assertEqual(v.status, "fail")
        self.assertEqual(v.hits[0]["host"], "www.google.com")

    def test_other_net_errors_are_not_policy_refusals(self):
        recs = records("aerie-real") + [refused("https://ads.example/x.js", "ads.example", error=-20)]
        self.assertEqual(zt.evaluate(recs, self.dl).refused, [])

    def test_phases_are_kept(self):
        recs = records("aerie-real")
        n = len(recs)
        v = zt.evaluate(recs + [refused("https://a.example/", "a.example")], self.dl,
                        phase_ends={"first_launch": n, "idle": n + 1, "browse": n + 1})
        self.assertEqual(v.refused[0]["phase"], "idle")

    def test_the_counter_reading_is_reported_next_to_the_names(self):
        recs = records("aerie-real") + [refused("https://a.example/", "a.example")]
        v = zt.evaluate(recs, self.dl, refusal_count=1)
        self.assertEqual((v.refusal_count, v.status), (1, "pass"))
        self.assertNotIn("not named", v.reason)

    def test_a_counted_refusal_the_netlog_does_not_name_is_flagged(self):
        v = zt.evaluate(records("aerie-real"), self.dl, refusal_count=3)
        self.assertEqual(v.status, "pass")
        self.assertIn("3 refusal(s) counted, 0 named", v.reason)
        self.assertEqual(v.refused, [])
        self.assertEqual(zt.evaluate(records("aerie-real"), self.dl, strict=True, refusal_count=3).status, "fail")

    def test_a_zero_counter_adds_nothing(self):
        v = zt.evaluate(records("aerie-real"), self.dl, refusal_count=0)
        self.assertNotIn("refus", v.reason)


class StubbedCounter:
    """A DevTools client that answers Browser.getHistogram from a canned table."""

    def __init__(self, table):
        self.table, self.calls = table, []

    def send(self, method, params=None, **kw):
        self.calls.append((method, params))
        if method != "Browser.getHistogram" or params["name"] not in self.table:
            from aerie_bench.cdp import CdpError
            raise CdpError("Histogram not found")
        return {"histogram": {"name": params["name"], "count": self.table[params["name"]], "sum": 0, "buckets": []}}


class RefusalCounterTest(unittest.TestCase):
    def check(self):
        return zt.ZeroTraffic(idle_seconds=0, settle_seconds=0)

    def session(self, table):
        return mock.Mock(client=StubbedCounter(table), timeout=5)

    def test_reads_the_policy_histogram(self):
        s = self.session({"Aerie.Network.RequestPolicy.Refused": 4})
        self.assertEqual(self.check().read_refusal_counter(s), 4)
        self.assertEqual(s.client.calls[0][1]["name"], zt.REFUSAL_HISTOGRAM)

    def test_a_histogram_that_was_never_recorded_is_zero(self):
        self.assertEqual(self.check().read_refusal_counter(self.session({})), 0)

    def test_the_real_browsers_wording_for_a_missing_histogram_is_zero(self):
        from aerie_bench.cdp import CdpError

        class Real:
            def send(self, *a, **k):
                raise CdpError("Browser.getHistogram: Cannot find histogram: x (code -32602)")
        self.assertEqual(self.check().read_refusal_counter(mock.Mock(client=Real(), timeout=5)), 0)

    def test_a_failed_reading_is_none_not_zero(self):
        from aerie_bench.cdp import CdpError

        class Broken:
            def send(self, *a, **k):
                raise CdpError("Browser.getHistogram: timeout")
        c = self.check()
        self.assertIsNone(c.read_refusal_counter(mock.Mock(client=Broken(), timeout=5)))
        self.assertIn("timeout", c.counter_error)

    def test_finalize_puts_the_reading_and_the_names_in_the_result(self):
        c = self.check()
        c.refusal_count = 2
        session = mock.Mock(netlog=records("aerie-real") + [refused("https://a.example/x?y=1", "a.example")],
                            netlog_lookups=[], netlog_complete=True, exit={"code": 0})
        result = c.finalize(session, zt.CheckResult("zero_traffic", "pending"))
        self.assertEqual(result.status, "pass")
        self.assertEqual(result.data["refusal_counter"], 2)
        self.assertEqual([r["host"] for r in result.data["refused"]], ["a.example"])
        self.assertIn("a.example", result.detail)
        self.assertIn("2 refusal(s) counted, 1 named", result.detail)


class LookupTest(unittest.TestCase):
    def setUp(self):
        self.dl = denylist.load_default()
        self.parsed = netlog.load((FIXTURES / "netlog-dns-prefetch.json").read_text())

    def test_url_requests_alone_pass_this_capture(self):
        self.assertEqual(zt.evaluate(self.parsed.records, self.dl).status, "pass")

    def test_dns_prefetch_doh_and_preconnect_names_fail_it(self):
        v = zt.evaluate(self.parsed.records, self.dl, lookups=self.parsed.lookups)
        self.assertEqual(v.status, "fail")
        self.assertEqual([h["host"] for h in v.hits], ["www.google.com", "dns.google", "fonts.gstatic.com"])
        self.assertEqual({h["via"] for h in v.hits}, {"dns"})
        self.assertNotIn("duckduckgo.com", v.reason)
        self.assertEqual(v.other_hosts, ["duckduckgo.com"])

    def test_a_host_seen_as_request_and_lookup_is_one_hit(self):
        recs = records("helium-real")
        v = zt.evaluate(recs, self.dl, lookups=[{"host": "services.helium.imput.net", "event": "x", "source_type": "y"}])
        self.assertEqual(len(v.hits), 10)

    def test_ip_and_loopback_lookups_are_not_other_hosts(self):
        v = zt.evaluate(records("aerie-real"), self.dl, lookups=[
            {"host": "127.0.0.1", "event": "x", "source_type": "y"}, {"host": "localhost", "event": "x", "source_type": "y"}])
        self.assertEqual(v.other_hosts, [])

    def test_dummy_host_lookups_are_not_hits(self):
        v = zt.evaluate(records("aerie-real"), self.dl,
                        lookups=[{"host": "helium-services-are-disabled.qjz9zk", "event": "x", "source_type": "y"}])
        self.assertEqual(v.status, "pass")


class RunWithFakeBrowserTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.root, ignore_errors=True))

    def run_bench(self, env=None, **opts):
        options = {"idle_seconds": 0, "settle_seconds": 0, **opts}
        return runner.run_bench(FAKE, scenario.Scenario(checks=("zero_traffic",)), self.root, timeout=10,
                                extra_env=env or {}, check_options={"zero_traffic": options})

    def test_quiet_browser_passes_and_records_phases(self):
        res = self.run_bench()
        check = res.data["checks"][0]
        self.assertEqual((res.status, check["status"]), ("pass", "pass"), check)
        self.assertEqual(list(check["data"]["phases"]), ["first_launch", "idle", "browse"])
        self.assertEqual(check["data"]["hits"], [])
        self.assertGreaterEqual(check["data"]["fixture_requests"], 3)
        self.assertEqual(check["data"]["denylist_entries"], len(denylist.load_default().entries))

    def test_google_request_in_the_netlog_fails_the_run(self):
        res = self.run_bench(env={"FAKE_BROWSER_EXTERNAL_URL": "https://update.googleapis.com/service/update2/json"})
        check = res.data["checks"][0]
        self.assertEqual((res.status, check["status"]), ("fail", "fail"))
        self.assertEqual([h["host"] for h in check["data"]["hits"]], ["update.googleapis.com"])
        self.assertIn("update.googleapis.com", check["detail"])

    def test_helium_imput_request_fails_the_run(self):
        res = self.run_bench(env={"FAKE_BROWSER_EXTERNAL_URL": "https://services.helium.imput.net/com"})
        self.assertEqual(res.data["checks"][0]["data"]["hits"][0]["group"], "helium")

    def test_seeds_the_worst_case_services_profile_without_an_origin_override(self):
        dump = self.root / "prefs.json"
        # The crash step starts more browsers on other profiles, and each would overwrite the dump.
        self.run_bench(env={"FAKE_BROWSER_DUMP_PREFS": str(dump)}, crash=False)
        helium = json.loads(dump.read_text())["helium"]["services"]
        self.assertIs(helium["enabled"], True)
        self.assertIs(helium["user_consented"], True)
        self.assertEqual(helium["schema_version"], 1)
        self.assertNotIn("origin_override", helium)

    def test_strict_fails_on_a_blocked_dummy_host_attempt(self):
        env = {"FAKE_BROWSER_EXTERNAL_URL": "https://helium-services-are-disabled.qjz9zk/com"}
        self.assertEqual(self.run_bench(env=env).status, "pass")
        self.assertEqual(self.run_bench(env=env, strict=True).status, "fail")

    def test_truncated_netlog_is_inconclusive_never_a_pass(self):
        res = self.run_bench(env={"FAKE_BROWSER_TRUNCATE_NETLOG": "1"})
        check = res.data["checks"][0]
        self.assertEqual((res.status, check["status"]), ("fail", "fail"))
        self.assertIn("INCONCLUSIVE", check["detail"])
        self.assertIn("NetLog", check["detail"])
        self.assertIs(check["data"]["inconclusive"], True)

    def test_crash_or_unclean_exit_is_inconclusive_never_a_pass(self):
        res = self.run_bench(env={"FAKE_BROWSER_EXIT": "5"})
        check = res.data["checks"][0]
        self.assertEqual(check["status"], "fail")
        self.assertIn("INCONCLUSIVE", check["detail"])
        self.assertIn("exit", check["detail"])

    def test_hits_are_still_reported_when_the_log_is_truncated(self):
        res = self.run_bench(env={"FAKE_BROWSER_TRUNCATE_NETLOG": "1",
                                  "FAKE_BROWSER_EXTERNAL_URL": "https://www.google.com/"})
        check = res.data["checks"][0]
        self.assertIn("www.google.com", check["detail"])
        self.assertIs(check["data"]["inconclusive"], True)

    def test_a_complete_log_and_clean_exit_is_not_inconclusive(self):
        check = self.run_bench().data["checks"][0]
        self.assertEqual(check["status"], "pass")
        self.assertIs(check["data"]["inconclusive"], False)

    def test_waits_are_taken_from_the_options(self):
        sleeps = []
        with mock.patch.object(zt.time, "sleep", side_effect=sleeps.append):
            self.run_bench(idle_seconds=7, settle_seconds=3)
        self.assertGreaterEqual(sum(sleeps), 10)


class CliTest(unittest.TestCase):
    def options(self, argv):
        import argparse
        p = argparse.ArgumentParser()
        cli.add_arguments(p, binary_required=False)
        with mock.patch.object(cli, "run_bench") as rb, contextlib.redirect_stdout(io.StringIO()):
            rb.side_effect = RuntimeError("stop")
            try:
                cli.run(p.parse_args(argv), Path("/tmp/x"), FAKE)
            except RuntimeError:
                pass
        return rb.call_args.kwargs["check_options"]["zero_traffic"]

    def test_defaults_are_the_issue_numbers(self):
        o = self.options(["--check", "zero_traffic"])
        self.assertEqual((o["idle_seconds"], o["settle_seconds"], o["strict"]), (300.0, 60.0, False))

    def test_flags_reach_the_check(self):
        o = self.options(["--zero-traffic-idle", "5", "--zero-traffic-settle", "2", "--zero-traffic-strict"])
        self.assertEqual((o["idle_seconds"], o["settle_seconds"], o["strict"]), (5.0, 2.0, True))


class BaselineDemoTest(unittest.TestCase):
    """--baseline runs the check on a second browser that must fail: the proof the check is real."""

    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.addCleanup(lambda: __import__("shutil").rmtree(self.root, ignore_errors=True))
        # The fake reads its own file name: "fake-google" contacts Google, anything else is quiet.
        self.quiet = Path(self.root) / "fake-quiet"
        self.google = Path(self.root) / "fake-google"
        for link in (self.quiet, self.google):
            link.symlink_to(FAKE)

    def invoke(self, binary, baseline):
        out, err = io.StringIO(), io.StringIO()
        argv = ["--build-root", self.root + "/br", "--timeout", "10", "--binary", str(binary),
                "--check", "zero_traffic", "--zero-traffic-idle", "0", "--zero-traffic-settle", "0",
                "--baseline", str(baseline)]
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = cli.main(argv)
        return code, out.getvalue(), err.getvalue()

    def test_clean_browser_plus_failing_baseline_is_a_pass(self):
        code, out, _ = self.invoke(self.quiet, self.google)
        self.assertEqual(code, 0, out)
        self.assertIn("baseline  FAILED as expected", out)
        self.assertIn("www.google.com", out)

    def test_baseline_that_passes_means_the_check_is_vacuous(self):
        code, out, err = self.invoke(self.quiet, self.quiet)
        self.assertEqual(code, 1)
        self.assertIn("vacuous", out + err)

    def test_failing_main_run_fails_even_if_baseline_fails(self):
        code, out, _ = self.invoke(self.google, self.google)
        self.assertEqual(code, 1)
        self.assertIn("result    FAIL", out)


class MeasureModeTest(unittest.TestCase):
    """Another browser measured with the same check (issue #254): counts, never a verdict on the browser."""

    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.root, ignore_errors=True))

    def run_bench(self, env=None, **opts):
        options = {"idle_seconds": 0, "settle_seconds": 0, **opts}
        return runner.run_bench(FAKE, scenario.Scenario(checks=("zero_traffic",)), self.root, timeout=10,
                                extra_env=env or {}, check_options={"zero_traffic": options})

    def run_measure(self, env=None, **opts):
        return self.run_bench(env=env, measure=True, **opts)

    def test_traffic_to_google_is_recorded_and_the_run_still_passes(self):
        res = self.run_measure(env={"FAKE_BROWSER_EXTERNAL_URL": "https://update.googleapis.com/service/update2/json"})
        check = res.data["checks"][0]
        self.assertEqual((res.status, check["status"]), ("pass", "pass"), check)
        self.assertEqual([h["host"] for h in check["data"]["hits"]], ["update.googleapis.com"])
        self.assertTrue(check["data"]["measure"])
        self.assertIn("measured only", check["detail"])

    def test_the_profile_stays_at_defaults(self):
        dump = self.root / "prefs.json"
        self.run_measure(env={"FAKE_BROWSER_DUMP_PREFS": str(dump)})
        seeded = dump.read_text() if dump.exists() else ""   # empty: the browser found no Preferences file
        self.assertNotIn("helium", seeded)

    def test_no_crash_step_runs(self):
        check = self.run_measure().data["checks"][0]
        self.assertEqual(check["data"]["crash_step"], {"status": "skipped"})

    def test_a_truncated_log_is_still_inconclusive(self):
        res = self.run_measure(env={"FAKE_BROWSER_TRUNCATE_NETLOG": "1"})
        self.assertEqual(res.data["checks"][0]["status"], "fail")


if __name__ == "__main__":
    unittest.main()
