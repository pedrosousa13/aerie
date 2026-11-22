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


class IdentifyTest(unittest.TestCase):
    def test_brave_is_found_from_its_version_line(self):
        b = comparison.identify("Brave Browser 131.1.73.101", "brave-browser")
        self.assertEqual((b["id"], b["name"], b["version"]), ("brave", "Brave", "131.1.73.101"))

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


def measurement(bid="brave", **over):
    base = {"schema": 1, "browser": {"id": bid, "name": bid.title(), "version": "131.1.73.101"},
            "date": "2026-11-22", "settings": {"profile": "fresh", "settings": "defaults", "idle_seconds": 300.0,
                                               "settle_seconds": 60.0, "defaults": True},
            "vendor_known": True, "tracker_list": {"sha256": "ab" * 32, "date": "2026-11-09"},
            "counts": {"vendor": 3, "google": 3, "tracker": 1, "other": 2}, "total": 9,
            "blocking": {"blocked": 60, "total": 100, "percent": 60.0},
            "command": "./aerie bench --measure --binary ./brave-browser"}
    return {**base, **over}


class TableTest(unittest.TestCase):
    def render(self, others, aerie=None):
        return comparison.render_html(aerie or measurement("aerie", counts={"vendor": 0, "google": 0, "tracker": 0, "other": 0},
                                                           total=0, blocking={"blocked": 90, "total": 100, "percent": 90.0}),
                                      others)

    def test_headline_names_each_browsers_count_of_trackers_vendors_and_google(self):
        html = self.render([measurement()])
        self.assertIn("Aerie: 0 requests to trackers or vendors.", html)
        self.assertIn("Brave: 7.", html)

    def test_each_row_has_version_date_and_settings(self):
        html = self.render([measurement()])
        for s in ("131.1.73.101", "2026-11-22", "fresh profile", "default settings", "300"):
            self.assertIn(s, html)

    def test_not_measured_rows_say_why_and_zen_has_its_own_slot(self):
        html = self.render([])
        self.assertIn("Zen", html)
        self.assertIn("not measured yet", html)
        self.assertIn("Gecko-based; the Bench captures Chromium NetLogs only", html)
        self.assertIn("Dia", html)
        self.assertIn("Mac-only", html)

    def test_a_browser_that_was_measured_is_not_also_listed_as_not_measured(self):
        html = self.render([measurement("zen")])
        self.assertEqual(html.count("not measured yet"), 0)

    def test_a_measurement_without_a_tracker_list_says_the_tracker_column_is_unknown(self):
        html = self.render([measurement(tracker_list=None)])
        self.assertIn("no tracker list", html)

    def test_the_table_says_requests_are_attempts_and_blocking_is_scored_offline(self):
        html = self.render([measurement()])
        self.assertIn("attempts", html)
        self.assertIn("without a network", html)

    def test_a_shortened_run_is_marked_in_the_row(self):
        short = measurement(settings={"profile": "fresh", "settings": "defaults", "idle_seconds": 30.0,
                                      "settle_seconds": 10.0, "defaults": False})
        self.assertIn("NOT the full run", self.render([short]))

    def test_links_the_method(self):
        self.assertIn("browser-comparison.md", self.render([]))

    def test_html_escapes_browser_text(self):
        html = self.render([measurement("x", browser={"id": "x", "name": "<b>X</b>", "version": "1"})])
        self.assertNotIn("<b>X</b>", html)


class MeasureTest(unittest.TestCase):
    def run_data(self, version="Brave Browser 131.1.73.101", with_list=True):
        checks = [{"name": "zero_traffic", "status": "pass", "detail": "",
                   "data": {"idle_seconds": 300.0, "settle_seconds": 60.0}}]
        if with_list:
            checks.append({"name": "adblock_score", "status": "pass", "detail": "",
                           "data": {"score": {"overall": {"blocked": 1, "total": 2}, "categories": {},
                                              "blocked_hosts": ["tracker.adtech.example"],
                                              "through_hosts": ["other.adtech.example"], "unjudged": []},
                                    "hostlist": {"sha256": "ab" * 32, "date": "2026-11-09"}}})
        return {"binary": {"path": "/opt/brave/brave-browser", "version": version},
                "network": {"requests": brave_records()}, "checks": checks, "started": "2026-11-22T10:00:00+00:00"}

    def test_a_run_becomes_a_measurement(self):
        m = comparison.measure(self.run_data(), "./aerie bench --measure --binary ./brave-browser")
        self.assertEqual(m["browser"]["id"], "brave")
        self.assertEqual(m["date"], "2026-11-22")
        self.assertEqual(m["counts"], {"vendor": 3, "google": 3, "tracker": 1, "other": 2})
        self.assertEqual(m["blocking"], {"blocked": 1, "total": 2, "percent": 50.0})
        self.assertEqual(m["settings"]["idle_seconds"], 300.0)
        self.assertNotIn("/opt", json.dumps(m))

    def test_without_the_host_list_the_tracker_class_is_marked_unknown(self):
        m = comparison.measure(self.run_data(with_list=False), "cmd")
        self.assertIsNone(m["tracker_list"])
        self.assertIsNone(m["blocking"])

    def test_a_short_idle_is_recorded_so_the_table_does_not_hide_it(self):
        d = self.run_data()
        d["checks"][0]["data"]["idle_seconds"] = 30.0
        m = comparison.measure(d, "cmd")
        self.assertEqual(m["settings"]["idle_seconds"], 30.0)
        self.assertFalse(m["settings"]["defaults"])


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
        self.assertIn("Brave: 7", stdout)
        self.assertNotIn(str(FAKE), json.dumps(m))

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


class ReportTableTest(unittest.TestCase):
    def build(self, others):
        from tests.test_bench_privacy_report import META, DOC, REQUIRED, clean_run
        return privacy_report.build(clean_run(), META, privacy_report.load_allowlist(DOC), required=REQUIRED,
                                    comparison=others)

    def test_the_report_holds_the_table_and_the_json_has_the_numbers(self):
        report = self.build([measurement()])
        self.assertEqual(report["comparison"]["aerie"]["counts"], {"vendor": 0, "google": 0, "tracker": 0, "other": 0})
        self.assertEqual(report["comparison"]["others"][0]["browser"]["id"], "brave")
        page = privacy_report.render_html(report)
        self.assertIn("Aerie: 0 requests to trackers or vendors.", page)
        self.assertIn("Brave: 7.", page)
        self.assertIn("not measured yet", page)

    def test_without_measurements_the_report_still_lists_who_was_not_measured(self):
        page = privacy_report.render_html(self.build(None))
        self.assertIn("Gecko-based; the Bench captures Chromium NetLogs only", page)

    def test_the_gate_does_not_depend_on_other_browsers(self):
        self.assertTrue(self.build([measurement(counts={"vendor": 9, "google": 9, "tracker": 9, "other": 9})])["gate"]["passed"])

    def test_the_report_cli_reads_the_measurement_files(self):
        from tests.test_bench_privacy_report import CliTest, clean_run
        f = Path(tempfile.mkdtemp()) / "m.json"
        f.write_text(json.dumps(measurement()))
        t = CliTest()
        t.compare = [str(f)]
        code, out, _ = t.run_cli(clean_run())
        self.assertEqual(code, 0)
        self.assertIn("Brave: 7.", (out / "report.html").read_text())


if __name__ == "__main__":
    unittest.main()
