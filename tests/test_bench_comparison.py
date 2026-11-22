"""The request classifier and the comparison table of the other-browsers measurement (issue #254)."""
import argparse
import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from aerie_bench import cli, comparison, netlog, privacy_report
from aerie_bench.runner import RunResult

FIXTURES = Path(__file__).parent / "fixtures"
TRACKERS = {"tracker.adtech.example"}


def brave_records():
    return netlog.parse((FIXTURES / "netlog-comparison-brave.json").read_text())


def real_brave_records():
    return netlog.parse((FIXTURES / "netlog-brave-real.json").read_text())


class RecordedBraveTest(unittest.TestCase):
    """A trimmed real recording: Brave 155.1.97.59, fresh profile, 2026-11-22 (shortened run)."""

    def test_every_attempt_to_an_outside_host_is_the_makers_and_retries_count(self):
        c = comparison.classify(real_brave_records(), "brave", set())
        self.assertEqual(c["counts"], {"vendor": 15, "google": 0, "tracker": 0, "other": 0})
        self.assertEqual(c["hosts"]["vendor"], {"variations.brave.com": 1, "dict.brave.com": 1,
                                                "go-updater.brave.com": 12, "usage-ping.brave.com": 1})

    def test_the_makers_hosts_in_the_table_cover_everything_the_recording_shows(self):
        hosts = {r["host"] for r in real_brave_records() if r["external"]}
        for h in hosts:
            self.assertEqual(comparison.classify_host(h, "brave", set()), "vendor", h)

    def test_the_same_recording_is_other_for_a_browser_without_those_hosts(self):
        self.assertEqual(comparison.classify(real_brave_records(), "chrome", set())["counts"]["other"], 15)

    def test_the_recording_holds_no_query_cookie_or_local_path(self):
        text = (FIXTURES / "netlog-brave-real.json").read_text()
        for bad in ("?", "/home/", "/mnt/", "Set-Cookie", "osname"):
            self.assertNotIn(bad, text.replace("RECORDED, trimmed", ""))


class IdentifyTest(unittest.TestCase):
    def test_brave_is_found_from_its_version_line(self):
        b = comparison.identify("Brave Browser 131.1.73.101", "brave-browser")
        self.assertEqual((b["id"], b["name"], b["version"]), ("brave", "Brave", "131.1.73.101"))

    def test_vivaldi_is_not_a_known_browser(self):
        self.assertEqual(comparison.identify("Vivaldi 7.0.1", "vivaldi")["id"], "unknown")

    def test_chrome_and_helium_and_aerie(self):
        self.assertEqual(comparison.identify("Google Chrome 131.0.6778.85", "google-chrome")["id"], "chrome")
        self.assertEqual(comparison.identify("Helium 0.7.1", "helium")["id"], "helium")
        self.assertEqual(comparison.identify("Aerie 1.2.3.4", "aerie")["id"], "aerie")

    def test_an_unknown_browser_keeps_its_own_name_and_has_no_vendor(self):
        b = comparison.identify("Fake Browser 1.2.3.4", "fakebrowser")
        self.assertEqual((b["id"], b["version"]), ("unknown", "1.2.3.4"))

    def test_a_missing_version_is_unknown(self):
        self.assertEqual(comparison.identify(None, "x")["version"], "unknown")


class ClassifyTest(unittest.TestCase):
    def classify(self, browser="brave", trackers=TRACKERS):
        return comparison.classify(brave_records(), browser, trackers)

    def test_every_class_is_counted_and_loopback_is_left_out(self):
        c = self.classify()
        self.assertEqual(c["counts"], {"vendor": 3, "google": 3, "tracker": 1, "other": 2})
        self.assertEqual(c["total"], 9)

    def test_hosts_are_listed_by_class(self):
        h = self.classify()["hosts"]
        self.assertEqual(h["vendor"], {"go-updater.brave.com": 1, "variations.brave.com": 1, "laptop-updates.brave.com": 1})
        self.assertEqual(sorted(h["google"]), ["ads.doubleclick.net", "safebrowsing.googleapis.com", "www.google.com"])
        self.assertEqual(h["tracker"], {"tracker.adtech.example": 1})

    def test_a_lookalike_host_is_other(self):
        self.assertIn("www.google.com.evil.example", self.classify()["hosts"]["other"])

    def test_the_vendor_depends_on_the_browser(self):
        c = self.classify("chrome")
        self.assertEqual(c["counts"]["vendor"], 0)
        self.assertEqual(c["counts"]["other"], 5)

    def test_the_helium_denylist_group_is_the_vendor_of_helium_and_aerie(self):
        recs = [{"url": "https://services.helium.imput.net/com", "urls": ["https://services.helium.imput.net/com"],
                 "host": "services.helium.imput.net", "scheme": "https", "external": True, "net_error": None}]
        for b in ("helium", "aerie"):
            self.assertEqual(comparison.classify(recs, b, set())["counts"]["vendor"], 1)

    def test_a_subdomain_of_a_listed_tracker_is_a_tracker(self):
        recs = [{"url": "https://a.tracker.adtech.example/x", "urls": ["https://a.tracker.adtech.example/x"],
                 "host": "a.tracker.adtech.example", "scheme": "https", "external": True, "net_error": None}]
        self.assertEqual(comparison.classify(recs, "brave", TRACKERS)["counts"]["tracker"], 1)

    def test_every_hop_of_a_redirect_counts(self):
        recs = [{"url": "https://example.org/a", "urls": ["https://example.org/a", "https://www.google.com/b"],
                 "host": "example.org", "scheme": "https", "external": True, "net_error": None}]
        c = comparison.classify(recs, "chrome", set())
        self.assertEqual((c["counts"]["other"], c["counts"]["google"]), (1, 1))

    def test_a_request_the_policy_refused_never_left(self):
        recs = [{"url": "https://www.google.com/x", "urls": ["https://www.google.com/x"], "host": "www.google.com",
                 "scheme": "https", "external": True, "net_error": -22}]
        self.assertEqual(comparison.classify(recs, "aerie", set())["total"], 0)


FLAGS = ["--user-data-dir=/mnt/x/bench-tmp/profile-abc", "--remote-debugging-pipe", "--log-net-log=/tmp/n.json",
         "--no-first-run", "--host-resolver-rules=MAP * ~NOTFOUND , EXCLUDE localhost", "--no-proxy-server",
         "--headless=new", "about:blank"]


def measurement(bid="brave", **over):
    base = {"schema": 1, "browser": {"id": bid, "name": bid.title(), "version": "131.1.73.101"},
            "date": "2026-11-22",
            "settings": {"profile": "fresh", "settings": "defaults", "idle_seconds": 300.0, "settle_seconds": 60.0,
                         "defaults": True, "headless": True,
                         "launch_flags": ["--user-data-dir=<profile>", "--remote-debugging-pipe", "--headless=new"]},
            "vendor_known": True, "tracker_list": {"sha256": "ab" * 32, "date": "2026-11-09"},
            "counts": {"vendor": 3, "google": 3, "tracker": 1, "other": 2}, "total": 9,
            "hosts": {"vendor": {"a.brave.com": 2, "b.brave.com": 1}, "google": {"www.google.com": 3},
                      "tracker": {"t.adtech.example": 1}, "other": {"x.example": 1, "y.example": 1}},
            "adblock_offline": {"blocked": 60, "total": 100, "percent": 60.0},
            "command": "./aerie bench --measure --binary ./brave-browser"}
    return {**base, **over}


def aerie_measurement(**over):
    return measurement("aerie", counts={"vendor": 0, "google": 0, "tracker": 0, "other": 0}, total=0,
                       hosts={c: {} for c in comparison.CLASSES},
                       adblock_offline={"blocked": 90, "total": 100, "percent": 90.0}, **over)


class TableTest(unittest.TestCase):
    def render(self, others, aerie="default"):
        return comparison.render_html(aerie_measurement() if aerie == "default" else aerie, others)

    def test_headline_has_the_same_form_for_every_browser(self):
        html = self.render([measurement()])
        self.assertIn("Aerie: 0 requests to trackers or vendors.", html)
        self.assertIn("Brave: 7 requests to trackers or vendors.", html)

    def test_one_request_is_singular(self):
        m = measurement(counts={"vendor": 1, "google": 0, "tracker": 0, "other": 0})
        self.assertIn("Brave: 1 request to trackers or vendors.", self.render([m]))

    def test_each_row_has_version_date_and_settings(self):
        html = self.render([measurement()])
        for s in ("131.1.73.101", "2026-11-22", "fresh profile", "default settings, headless, offline (Bench launch flags)",
                  "300"):
            self.assertIn(s, html)

    def test_the_launch_flags_of_each_browser_are_shown(self):
        html = self.render([measurement()])
        self.assertIn("--headless=new", html)
        self.assertIn("--user-data-dir=&lt;profile&gt;", html)

    def test_a_headed_run_is_not_called_headless(self):
        m = measurement()
        m["settings"] = {**m["settings"], "headless": False}
        self.assertIn("default settings, headed, offline", self.render([m]))

    def test_other_browsers_blocking_is_not_scored_with_the_reason_in_the_row(self):
        html = self.render([measurement()])
        self.assertIn("not scored", html)
        self.assertIn("filter lists download at first run; the Bench runs offline", html)
        self.assertNotIn("60/100", html)
        self.assertNotIn("60.0%", html)

    def test_aerie_row_shows_its_offline_score(self):
        self.assertIn("90/100", self.render([]))

    def test_a_browser_with_unknown_maker_hosts_is_not_given_a_zero(self):
        m = measurement("zen", vendor_known=False, counts={"vendor": 0, "google": 2, "tracker": 1, "other": 4})
        html = self.render([m])
        self.assertIn("Zen: 3 requests to Google or trackers; maker hosts not known.", html)
        self.assertNotIn("Zen: 3 requests to trackers or vendors", html)
        self.assertIn("<td>unknown</td>", html)

    def test_a_missing_tracker_list_is_said_in_the_headline_and_the_cell(self):
        html = self.render([measurement(tracker_list=None)])
        self.assertIn("no tracker list", html)

    def test_distinct_hosts_are_shown_beside_the_attempts(self):
        html = self.render([measurement()])
        self.assertIn("3 (2 hosts)", html)       # vendor: 3 attempts to 2 hosts
        self.assertIn("3 (1 host)", html)        # google

    def test_tracker_column_says_it_is_the_pinned_list_only(self):
        self.assertIn("pinned test list only; a lower bound", self.render([measurement()]))

    def test_the_counting_rule_is_stated(self):
        html = self.render([measurement()])
        self.assertIn("attempts", html)
        self.assertIn("refused by the browser's own policy before it left", html)
        self.assertIn("Retries count as attempts", html)

    def test_aerie_not_measured_is_said_not_assumed(self):
        html = self.render([measurement()], aerie=None)
        self.assertIn("Aerie was not measured with the comparison method", html)
        self.assertNotIn("Aerie: 0 requests", html)

    def test_a_shortened_run_is_marked_in_the_row(self):
        short = measurement(settings={**measurement()["settings"], "idle_seconds": 30.0, "settle_seconds": 10.0,
                                      "defaults": False})
        self.assertIn("NOT the full run", self.render([short]))

    def test_not_measured_rows_say_why_and_zen_has_its_own_slot(self):
        html = self.render([])
        for s in ("Zen", "not measured yet", "Gecko-based; the Bench captures Chromium NetLogs only", "Dia", "Mac-only"):
            self.assertIn(s, html)

    def test_a_browser_that_was_measured_is_not_also_listed_as_not_measured(self):
        self.assertEqual(self.render([measurement("zen")]).count("not measured yet"), 0)

    def test_links_the_method(self):
        self.assertIn("browser-comparison.md", self.render([]))

    def test_html_escapes_browser_text(self):
        html = self.render([measurement("x", browser={"id": "x", "name": "<b>X</b>", "version": "1"})])
        self.assertNotIn("<b>X</b>", html)


class MeasureTest(unittest.TestCase):
    def run_data(self, version="Brave Browser 131.1.73.101", with_list=True, records=None):
        checks = [{"name": "zero_traffic", "status": "pass", "detail": "",
                   "data": {"idle_seconds": 300.0, "settle_seconds": 60.0}}]
        if with_list:
            checks.append({"name": "adblock_score", "status": "pass", "detail": "",
                           "data": {"score": {"overall": {"blocked": 1, "total": 2}, "categories": {},
                                              "blocked_hosts": ["tracker.adtech.example"],
                                              "through_hosts": ["other.adtech.example"], "unjudged": []},
                                    "hostlist": {"sha256": "ab" * 32, "date": "2026-11-09"}}})
        return {"binary": {"path": "/opt/brave/brave-browser", "version": version},
                "network": {"requests": records or brave_records()}, "checks": checks,
                "started": "2026-11-22T10:00:00+00:00", "flags": FLAGS, "headless": True}

    def test_a_run_becomes_a_measurement(self):
        m = comparison.measure(self.run_data(), "./aerie bench --measure --binary ./brave-browser")
        self.assertEqual(m["browser"]["id"], "brave")
        self.assertEqual(m["date"], "2026-11-22")
        self.assertEqual(m["counts"], {"vendor": 3, "google": 3, "tracker": 1, "other": 2})
        self.assertEqual(m["settings"]["idle_seconds"], 300.0)
        self.assertNotIn("/opt", json.dumps(m))

    def test_the_raw_offline_result_is_kept_under_a_name_that_is_not_a_score(self):
        m = comparison.measure(self.run_data(), "cmd")
        self.assertEqual(m["adblock_offline"], {"blocked": 1, "total": 2, "percent": 50.0})
        self.assertNotIn("blocking", m)

    def test_the_launch_flags_are_recorded_without_a_local_path(self):
        m = comparison.measure(self.run_data(), "cmd")
        flags = m["settings"]["launch_flags"]
        self.assertIn("--headless=new", flags)
        self.assertIn("--no-first-run", flags)
        self.assertIn("--user-data-dir=<profile>", flags)
        self.assertIn("--log-net-log=<netlog>", flags)
        self.assertTrue(any(f.startswith("--host-resolver-rules=") for f in flags))
        self.assertNotIn("/mnt", json.dumps(m))
        self.assertNotIn("/tmp", json.dumps(m))
        self.assertTrue(m["settings"]["headless"])

    def test_distinct_hosts_are_kept_per_class(self):
        m = comparison.measure(self.run_data(), "cmd")
        self.assertEqual(len(m["hosts"]["vendor"]), 3)

    def test_without_the_host_list_the_tracker_class_is_marked_unknown(self):
        m = comparison.measure(self.run_data(with_list=False), "cmd")
        self.assertIsNone(m["tracker_list"])
        self.assertIsNone(m["adblock_offline"])

    def test_a_short_idle_is_recorded_so_the_table_does_not_hide_it(self):
        d = self.run_data()
        d["checks"][0]["data"]["idle_seconds"] = 30.0
        m = comparison.measure(d, "cmd")
        self.assertEqual(m["settings"]["idle_seconds"], 30.0)
        self.assertFalse(m["settings"]["defaults"])

    def test_a_recorded_run_measured_end_to_end(self):
        m = comparison.measure(self.run_data(records=real_brave_records()), "cmd")
        self.assertEqual((m["counts"]["vendor"], m["total"], m["vendor_known"]), (15, 15, True))


FAKE = Path(__file__).parent / "bench_fake_browser.py"


class NonAerieBinaryCliTest(unittest.TestCase):
    """``--measure`` runs the two checks against any Chromium-family binary."""

    def parse(self, argv):
        p = argparse.ArgumentParser()
        cli.add_arguments(p, binary_required=False)
        return p.parse_args(argv)

    def run_cli(self, argv, run_data=None, default_binary=FAKE):
        out = Path(tempfile.mkdtemp())
        seen = {}

        def fake(binary, sc, root, **kw):
            seen.update(binary=binary, checks=sc.checks, options=kw["check_options"])
            d = out / "run"
            d.mkdir()
            (d / "summary.txt").write_text("summary\n")
            return RunResult("pass", d, run_data or MeasureTest().run_data())

        stdout = io.StringIO()
        with mock.patch.object(cli, "run_bench", fake), contextlib.redirect_stdout(stdout), \
                contextlib.redirect_stderr(io.StringIO()):
            code = cli.run(self.parse(argv), Path(tempfile.mkdtemp()), default_binary)
        return code, out / "run", seen, stdout.getvalue()

    def test_runs_zero_traffic_and_adblock_score_in_measure_mode_without_the_services_probe(self):
        code, _, seen, _ = self.run_cli(["--measure", "--binary", str(FAKE)])
        self.assertEqual(code, 0)
        self.assertEqual(seen["checks"], ("zero_traffic", "adblock_score"))
        self.assertIs(seen["options"]["zero_traffic"]["measure"], True)
        self.assertEqual(str(seen["binary"]), str(FAKE))

    def test_the_idle_and_settle_flags_still_apply(self):
        _, _, seen, _ = self.run_cli(["--measure", "--binary", str(FAKE), "--zero-traffic-idle", "30"])
        self.assertEqual(seen["options"]["zero_traffic"]["idle_seconds"], 30.0)

    def test_writes_the_measurement_and_prints_the_counts(self):
        code, run_dir, _, stdout = self.run_cli(["--measure", "--binary", str(FAKE)])
        m = json.loads((run_dir / "measurement.json").read_text())
        self.assertEqual(m["browser"]["id"], "brave")
        self.assertEqual(m["counts"]["google"], 3)
        self.assertIn("Brave: 7 requests", stdout)
        self.assertNotIn(str(FAKE), json.dumps(m))

    def test_the_command_in_the_measurement_is_the_one_that_repeats_the_run(self):
        _, run_dir, _, _ = self.run_cli(["--measure", "--binary", str(FAKE), "--zero-traffic-idle", "30"])
        m = json.loads((run_dir / "measurement.json").read_text())
        self.assertEqual(m["command"], "./aerie bench --measure --binary ./bench_fake_browser.py "
                                       "--zero-traffic-idle 30.0")

    def test_needs_an_explicit_binary(self):
        code, _, seen, _ = self.run_cli(["--measure"])
        self.assertEqual(code, 2)
        self.assertEqual(seen, {})

    def test_cannot_be_combined_with_report_or_check(self):
        self.assertEqual(self.run_cli(["--measure", "--binary", str(FAKE), "--report", "/x"])[0], 2)
        self.assertEqual(self.run_cli(["--measure", "--binary", str(FAKE), "--check", "smoke"])[0], 2)

    def test_compare_needs_report(self):
        self.assertEqual(self.run_cli(["--binary", str(FAKE), "--compare", "m.json"])[0], 2)

    def test_a_failed_run_is_not_a_measurement(self):
        d = MeasureTest().run_data()
        with mock.patch.object(cli, "run_bench") as rb, contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(io.StringIO()):
            out = Path(tempfile.mkdtemp())
            (out / "summary.txt").write_text("s\n")
            rb.return_value = RunResult("fail", out, d)
            code = cli.run(self.parse(["--measure", "--binary", str(FAKE)]), Path(tempfile.mkdtemp()), FAKE)
        self.assertEqual(code, 1)
        self.assertFalse((out / "measurement.json").exists())


class ScrubModuleTest(unittest.TestCase):
    def test_the_shared_scrubber_is_the_one_the_report_uses(self):
        from aerie_bench import scrub
        self.assertIs(privacy_report.scrub_text, scrub.scrub_text)
        self.assertEqual(scrub.scrub_text("see /home/alice/x and https://h/p?q=1"), "see <path> and https://h/p")


class LoadMeasurementTest(unittest.TestCase):
    def write(self, doc):
        f = Path(tempfile.mkdtemp()) / "measurement.json"
        f.write_text(doc if isinstance(doc, str) else json.dumps(doc))
        return f

    def test_a_good_file_loads(self):
        self.assertEqual(comparison.load_measurement(self.write(measurement()))["browser"]["id"], "brave")

    def test_junk_and_wrong_schema_are_refused(self):
        for bad in ("not json", {"schema": 99}, {**measurement(), "counts": {"vendor": "many"}}):
            with self.assertRaises(comparison.MeasurementError):
                comparison.load_measurement(self.write(bad))

    def test_a_local_path_in_the_flags_is_scrubbed_on_load(self):
        m = measurement()
        m["settings"]["launch_flags"] = ["--user-data-dir=/home/alice/p"]
        loaded = comparison.load_measurement(self.write(m))
        self.assertNotIn("/home/alice", json.dumps(loaded))


class ReportTableTest(unittest.TestCase):
    def build(self, measurements):
        from tests.test_bench_privacy_report import DOC, META, REQUIRED, clean_run
        return privacy_report.build(clean_run(), META, privacy_report.load_allowlist(DOC), required=REQUIRED,
                                    comparison=measurements)

    def test_the_aerie_row_comes_from_an_aerie_measurement_and_the_json_has_the_numbers(self):
        report = self.build([measurement(), aerie_measurement()])
        self.assertEqual(report["comparison"]["aerie"]["counts"], {"vendor": 0, "google": 0, "tracker": 0, "other": 0})
        self.assertEqual([m["browser"]["id"] for m in report["comparison"]["others"]], ["brave"])
        page = privacy_report.render_html(report)
        self.assertIn("Aerie: 0 requests to trackers or vendors.", page)
        self.assertIn("Brave: 7 requests to trackers or vendors.", page)

    def test_the_report_run_is_not_used_as_the_aerie_row(self):
        report = self.build([measurement()])
        self.assertIsNone(report["comparison"]["aerie"])
        page = privacy_report.render_html(report)
        self.assertIn("Aerie was not measured with the comparison method", page)
        self.assertNotIn("Aerie: 0 requests", page)

    def test_without_measurements_the_report_still_lists_who_was_not_measured(self):
        page = privacy_report.render_html(self.build(None))
        self.assertIn("Gecko-based; the Bench captures Chromium NetLogs only", page)

    def test_the_gate_does_not_depend_on_other_browsers(self):
        bad = measurement(counts={"vendor": 9, "google": 9, "tracker": 9, "other": 9})
        self.assertTrue(self.build([bad, aerie_measurement()])["gate"]["passed"])

    def test_the_report_cli_reads_the_measurement_files(self):
        from tests.test_bench_privacy_report import CliTest, clean_run
        files = []
        for m in (measurement(), aerie_measurement()):
            f = Path(tempfile.mkdtemp()) / "m.json"
            f.write_text(json.dumps(m))
            files.append(str(f))
        t = CliTest()
        t.compare = files
        code, out, _ = t.run_cli(clean_run())
        self.assertEqual(code, 0)
        self.assertIn("Brave: 7 requests", (out / "report.html").read_text())


if __name__ == "__main__":
    unittest.main()
