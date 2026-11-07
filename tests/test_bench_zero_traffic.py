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
        v = zt.evaluate(records("helium-baseline"), self.dl)
        self.assertEqual(v.status, "fail")
        self.assertEqual({h["group"] for h in v.hits}, {"helium"})
        self.assertEqual({h["host"] for h in v.hits}, {"services.helium.imput.net"})
        self.assertEqual(len(v.hits), 5)
        self.assertEqual(len(v.blocked), 1)

    def test_clean_run_passes(self):
        v = zt.evaluate(records("aerie-clean"), self.dl)
        self.assertEqual((v.status, v.hits, v.blocked), ("pass", [], []))
        self.assertEqual(v.fixture_requests, 2)

    def test_blocked_dummy_host_attempt_is_reported_but_passes_unless_strict(self):
        recs = records("aerie-blocked-attempt")
        v = zt.evaluate(recs, self.dl)
        self.assertEqual(v.status, "pass")
        self.assertEqual([b["host"] for b in v.blocked], ["helium-services-are-disabled.qjz9zk"])
        strict = zt.evaluate(recs, self.dl, strict=True)
        self.assertEqual(strict.status, "fail")
        self.assertIn("blocked", strict.reason)

    def test_empty_capture_is_not_a_pass(self):
        v = zt.evaluate([], self.dl)
        self.assertEqual(v.status, "fail")
        self.assertIn("no fixture page request", v.reason)

    def test_other_external_hosts_are_listed_but_do_not_fail(self):
        recs = records("aerie-clean") + [
            {"source_id": 90, "time": "9", "url": "https://duckduckgo.com/?q=x", "method": "GET", "scheme": "https",
             "host": "duckduckgo.com", "initiator": None, "net_error": -105, "external": True}]
        v = zt.evaluate(recs, self.dl)
        self.assertEqual(v.status, "pass")
        self.assertEqual(v.other_hosts, ["duckduckgo.com"])

    def test_phases_split_by_cumulative_request_counts(self):
        recs = records("helium-baseline")  # 2 loopback, then 5 hits, then 1 blocked
        v = zt.evaluate(recs, self.dl, phase_ends={"first_launch": 3, "idle": 6, "browse": 99})
        by_phase = [(h["host"], h["phase"]) for h in v.hits]
        self.assertEqual([p for _, p in by_phase], ["first_launch", "idle", "idle", "idle", "browse"])
        self.assertEqual(v.blocked[0]["phase"], "browse")

    def test_url_rules_apply_to_full_urls(self):
        rec = {"source_id": 1, "time": "1", "method": "GET", "scheme": "https", "host": "raw.githubusercontent.com",
               "url": "https://raw.githubusercontent.com/imputnet/uBlock/main/assets.json", "initiator": None,
               "net_error": None, "external": True}
        v = zt.evaluate([rec] + records("aerie-clean"), self.dl)
        self.assertEqual(v.status, "fail")
        self.assertEqual(v.hits[0]["group"], "helium")


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
        recs = records("helium-baseline")
        v = zt.evaluate(recs, self.dl, lookups=[{"host": "services.helium.imput.net", "event": "x", "source_type": "y"}])
        self.assertEqual(len(v.hits), 5)

    def test_ip_and_loopback_lookups_are_not_other_hosts(self):
        v = zt.evaluate(records("aerie-clean"), self.dl, lookups=[
            {"host": "127.0.0.1", "event": "x", "source_type": "y"}, {"host": "localhost", "event": "x", "source_type": "y"}])
        self.assertEqual(v.other_hosts, [])

    def test_dummy_host_lookups_are_not_hits(self):
        v = zt.evaluate(records("aerie-clean"), self.dl,
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
        self.run_bench(env={"FAKE_BROWSER_DUMP_PREFS": str(dump)})
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


if __name__ == "__main__":
    unittest.main()
