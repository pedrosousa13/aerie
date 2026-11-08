"""Performance metrics (issue #16): thresholds, comparison, history, reports and the perf check."""

import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from aerie_bench import cli, perf, runner, scenario
from aerie_bench.checks.perf import AD_PAGES, Perf, load_manifest

FAKE = Path(__file__).parent / "bench_fake_browser.py"


def doc(memory=800.0, cold=2000.0, warm=1200.0, label="main", **extra):
    return {"schema": 1, "label": label, "binary": {"path": "/x", "version": "1.2.3"},
            "started": "2026-11-08T10:00:00+00:00",
            "metrics": {"memory_after_ad_pages_mb": memory, "startup_cold_ms": cold,
                        "startup_warm_ms": warm, **extra}, "skipped": {}}


def status(verdicts):
    return {v["metric"]: v["status"] for v in verdicts}


class CompareTest(unittest.TestCase):
    def test_an_unchanged_run_is_ok(self):
        self.assertEqual(set(status(perf.compare(doc(), doc())).values()), {"ok"})

    def test_a_memory_heavy_build_trips_the_threshold(self):
        v = perf.compare(doc(memory=800 * 1.5), doc())
        self.assertEqual(status(v)["memory_after_ad_pages_mb"], "regressed")
        self.assertEqual(len(perf.failures(v)), 1)

    def test_a_change_inside_the_threshold_is_noise(self):
        self.assertEqual(status(perf.compare(doc(memory=800 * 1.05), doc()))["memory_after_ad_pages_mb"], "ok")

    def test_a_big_percentage_under_the_absolute_floor_is_noise(self):
        # 10 MB to 14 MB is +40% and 4 MB: not a regression
        v = perf.compare(doc(memory=14), doc(memory=10))
        self.assertEqual(status(v)["memory_after_ad_pages_mb"], "ok")

    def test_improvement_is_reported_and_does_not_fail(self):
        v = perf.compare(doc(memory=400), doc())
        self.assertEqual(status(v)["memory_after_ad_pages_mb"], "improved")
        self.assertEqual(perf.failures(v), [])

    def test_a_higher_is_better_score_regresses_when_it_drops(self):
        v = perf.compare(doc(speedometer_score=80), doc(speedometer_score=100))
        self.assertEqual(status(v)["speedometer_score"], "regressed")
        v = perf.compare(doc(speedometer_score=120), doc(speedometer_score=100))
        self.assertEqual(status(v)["speedometer_score"], "improved")

    def test_missing_required_metric_fails_and_missing_benchmark_does_not(self):
        cur = doc()
        del cur["metrics"]["startup_warm_ms"]
        v = perf.compare(cur, doc(speedometer_score=100))
        self.assertEqual(status(v)["startup_warm_ms"], "missing")
        self.assertEqual(status(v)["speedometer_score"], "not_measured")
        self.assertEqual([f["metric"] for f in perf.failures(v)], ["startup_warm_ms"])

    def test_a_new_metric_without_a_baseline_value_is_new(self):
        self.assertEqual(status(perf.compare(doc(jetstream_score=5), doc()))["jetstream_score"], "new")

    def test_every_metric_documents_its_threshold_and_reason(self):
        text = (Path(__file__).parent.parent / "docs" / "bench.md").read_text()
        for name, spec in perf.METRICS.items():
            self.assertIn(f"`{name}`", text)
            self.assertTrue(spec.rationale)
        self.assertIn(perf.threshold_table(), text)


class HistoryTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp()) / "perf"
        self.addCleanup(lambda: __import__("shutil").rmtree(self.dir.parent, ignore_errors=True))

    def test_record_appends_and_replaces_the_baseline(self):
        perf.record(self.dir, doc(memory=800, label="a"))
        main = perf.record(self.dir, doc(memory=820, label="b"))
        self.assertEqual(perf.load(main)["label"], "b")
        self.assertEqual([d["label"] for d in perf.read_history(self.dir)], ["a", "b"])

    def test_a_torn_history_line_is_skipped(self):
        perf.record(self.dir, doc(label="a"))
        with open(self.dir / perf.HISTORY_FILE, "a") as f:
            f.write('{"schema": 1, "metr')
        self.assertEqual(len(perf.read_history(self.dir)), 1)

    def test_load_rejects_a_foreign_document(self):
        bad = self.dir.parent / "bad.json"
        bad.write_text('{"schema": 2, "metrics": {}}')
        with self.assertRaises(ValueError):
            perf.load(bad)

    def test_series_report_shows_every_build_with_the_change_from_the_last(self):
        text = perf.series_report([doc(memory=800, label="a"), doc(memory=880, label="b")])
        self.assertIn("| a |", text)
        self.assertIn("880.0 (+10.0%)", text)
        self.assertIn("memory_after_ad_pages_mb (MB)", text)
        self.assertIn("No recorded", perf.series_report([]))

    def test_side_by_side_names_both_browsers(self):
        text = perf.side_by_side(doc(memory=900), doc(memory=1000, label="h"), "Helium")
        self.assertIn("| memory_after_ad_pages_mb (MB) | 900.0 | 1000.0 | -10.0% |", text)
        self.assertIn("Aerie vs Helium", text)

    def test_the_module_command_judges_series_and_compares(self):
        perf.record(self.dir, doc())
        cur = self.dir / "cur.json"
        cur.write_text(json.dumps(doc(memory=2000)))
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(perf.main(["compare", str(cur), str(self.dir / "main.json")]), 1)
            self.assertEqual(perf.main(["compare", str(self.dir / "main.json"), str(self.dir / "main.json")]), 0)
            self.assertEqual(perf.main(["series", str(self.dir)]), 0)
            self.assertEqual(perf.main(["side-by-side", str(cur), str(self.dir / "main.json")]), 0)
            self.assertEqual(perf.main(["compare", str(self.dir / "nope.json"), str(cur)]), 2)
        self.assertIn("REGRESSED", out.getvalue())


class CheckTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.root, ignore_errors=True))

    def run_perf(self, baseline=None, rss_kb=None, **opts):
        sc = scenario.Scenario(checks=("perf",))
        options = {"perf": {"settle_seconds": 0, "startup_runs": 1, **opts, "baseline": baseline}}
        patch = (mock.patch("aerie_bench.procmem.tree_rss_kb", return_value={"total_kb": rss_kb, "processes": []})
                 if rss_kb is not None else contextlib.nullcontext())
        with patch:
            return runner.run_bench(FAKE, sc, self.root, timeout=10, check_options=options)

    def test_measures_memory_and_both_startups_and_writes_json(self):
        res = self.run_perf(rss_kb=512 * 1024)
        check = res.data["checks"][0]
        self.assertEqual(check["status"], "pass", check["detail"])
        metrics = check["data"]["metrics"]
        self.assertEqual(metrics["memory_after_ad_pages_mb"], 512.0)
        self.assertGreater(metrics["startup_cold_ms"], 0)
        self.assertGreater(metrics["startup_warm_ms"], 0)
        out = perf.load(res.out_dir / "perf.json")
        self.assertEqual(out["metrics"], metrics)
        self.assertIn("perf.json", res.data["files"])

    def test_the_ad_heavy_pages_were_loaded_before_memory_was_read(self):
        res = self.run_perf(rss_kb=1024)
        paths = [r["path"] for r in res.data["network"]["fixture_requests"]]
        for page in AD_PAGES:
            self.assertIn(page, paths)
        self.assertLess(paths.index(AD_PAGES[-1]), paths.index("/index.html"))

    def test_benchmarks_not_vendored_are_reported_not_hidden(self):
        res = self.run_perf(rss_kb=1024)
        skipped = res.data["checks"][0]["data"]["skipped"]
        self.assertIn("no vendored copy", skipped["speedometer_score"])
        self.assertIn("no vendored copy", skipped["jetstream_score"])
        self.assertIn("not measured", res.data["checks"][0]["detail"])

    def test_a_memory_heavy_build_fails_against_the_baseline(self):
        base = self.root / "main.json"
        base.write_text(json.dumps(doc(memory=500, cold=60000, warm=60000)))
        res = self.run_perf(baseline=str(base), rss_kb=900 * 1024)
        check = res.data["checks"][0]
        self.assertEqual(check["status"], "fail")
        self.assertIn("memory_after_ad_pages_mb", check["detail"])
        self.assertEqual(res.status, "fail")

    def test_the_same_memory_passes_against_the_baseline(self):
        base = self.root / "main.json"
        base.write_text(json.dumps(doc(memory=500, cold=60000, warm=60000)))
        res = self.run_perf(baseline=str(base), rss_kb=510 * 1024)
        self.assertEqual(res.data["checks"][0]["status"], "pass", res.data["checks"][0]["detail"])

    def test_no_baseline_file_yet_is_not_a_failure(self):
        res = self.run_perf(baseline=str(self.root / "missing.json"), rss_kb=1024)
        self.assertEqual(res.data["checks"][0]["status"], "pass")
        self.assertIn("no baseline yet", res.data["checks"][0]["detail"])

    def test_a_broken_baseline_is_an_error_not_a_pass(self):
        base = self.root / "main.json"
        base.write_text("not json")
        res = self.run_perf(baseline=str(base), rss_kb=1024)
        self.assertEqual(res.data["checks"][0]["status"], "error")

    def test_unreadable_process_memory_is_an_error(self):
        with mock.patch("aerie_bench.procmem.tree_rss_kb", return_value=None):
            res = runner.run_bench(FAKE, scenario.Scenario(checks=("perf",)), self.root, timeout=10,
                                   check_options={"perf": {"settle_seconds": 0, "startup_runs": 1}})
        self.assertEqual(res.data["checks"][0]["status"], "error")


class BenchmarkDriverTest(unittest.TestCase):
    def setUp(self):
        self.vendor = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.vendor, ignore_errors=True))

    def write(self, name, manifest):
        (self.vendor / name).mkdir()
        (self.vendor / name / "aerie-bench.json").write_text(json.dumps(manifest))

    def session(self, score=123.456, finished=True, load_error=None):
        s = mock.Mock()
        s.fixtures.base_url = "http://127.0.0.1:1"
        s.navigate_url.return_value = {"url": "", "error": load_error}
        s.wait_for.return_value = finished
        s.evaluate.return_value = score
        return s

    def test_missing_and_broken_manifests_say_why(self):
        self.assertIn("no vendored copy", load_manifest(self.vendor, "speedometer")[1])
        self.write("jetstream", {"entry": "index.html"})
        self.assertIn("bad manifest", load_manifest(self.vendor, "jetstream")[1])

    def test_a_vendored_benchmark_is_started_awaited_and_scored(self):
        self.write("speedometer", {"entry": "index.html?x=1", "start": "run()", "done": "done()", "score": "score()"})
        s = self.session()
        value, why = Perf(vendor=self.vendor).run_benchmark(s, "speedometer")
        self.assertEqual((value, why), (123.46, ""))
        s.navigate_url.assert_called_once_with("http://127.0.0.1:1/vendor/speedometer/index.html?x=1")
        s.evaluate.assert_any_call("run()")

    def test_timeout_load_error_and_non_numeric_score_are_not_measured(self):
        self.write("speedometer", {"entry": "i.html", "done": "d", "score": "s", "timeout_s": 5})
        for kwargs, text in (({"finished": False}, "did not finish"), ({"load_error": "net::ERR"}, "did not load"),
                             ({"score": "n/a"}, "no numeric score"), ({"score": True}, "no numeric score")):
            value, why = Perf(vendor=self.vendor).run_benchmark(self.session(**kwargs), "speedometer")
            self.assertIsNone(value)
            self.assertIn(text, why)

    def test_vendor_files_are_served_under_vendor_and_nothing_escapes(self):
        import urllib.error
        import urllib.request
        from aerie_bench import fixtures
        name = "zz-test-copy"
        target = fixtures.VENDOR_DIR / name
        target.mkdir()
        self.addCleanup(lambda: __import__("shutil").rmtree(target, ignore_errors=True))
        (target / "index.html").write_text("<title>vendored</title>")
        with fixtures.FixtureServer() as srv:
            self.assertIn(b"vendored", urllib.request.urlopen(f"{srv.base_url}/vendor/{name}/").read())
            with self.assertRaises(urllib.error.HTTPError):
                urllib.request.urlopen(f"{srv.base_url}/vendor/../fixtures/index.html")


class CliTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.root, ignore_errors=True))

    def invoke(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err), \
                mock.patch("aerie_bench.procmem.tree_rss_kb", return_value={"total_kb": 100 * 1024, "processes": []}):
            code = cli.main(["--binary", str(FAKE), "--build-root", str(self.root), "--check", "perf",
                             "--perf-settle", "0", "--timeout", "10", *argv])
        return code, out.getvalue(), err.getvalue()

    def test_record_makes_the_baseline_and_the_next_run_compares_against_it(self):
        store = self.root / "perfstore"
        code, out, _ = self.invoke("--perf-record", str(store))
        self.assertEqual(code, 0, out)
        self.assertTrue((store / "main.json").is_file())
        code, out, _ = self.invoke("--perf-baseline", str(store / "main.json"))
        self.assertIn("within thresholds", out)

    def test_a_failing_run_is_not_recorded(self):
        base = self.root / "main.json"
        base.write_text(json.dumps(doc(memory=10, cold=60000, warm=60000)))
        code, out, _ = self.invoke("--perf-baseline", str(base), "--perf-record", str(self.root / "s"))
        self.assertEqual(code, 1)
        self.assertIn("not recorded", out)
        self.assertFalse((self.root / "s" / "main.json").exists())

    def test_comparison_mode_writes_a_side_by_side_report(self):
        code, out, _ = self.invoke("--perf-compare-binary", str(FAKE), "--perf-compare-name", "Stock Chromium",
                                   "--out", str(self.root / "r"))
        self.assertEqual(code, 0, out)
        report = (self.root / "r" / "perf-comparison.md").read_text()
        self.assertIn("Aerie vs Stock Chromium", report)
        self.assertIn("memory_after_ad_pages_mb", report)
        self.assertTrue((self.root / "r-compare" / "perf.json").is_file())

    def test_perf_flags_need_the_perf_check(self):
        code, _, err = self.invoke_other("--perf-record", str(self.root / "s"))
        self.assertEqual(code, 2)
        self.assertIn("need --check perf", err)

    def invoke_other(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = cli.main(["--binary", str(FAKE), "--build-root", str(self.root), *argv])
        return code, out.getvalue(), err.getvalue()


if __name__ == "__main__":
    unittest.main()
