"""Performance metrics (issue #16): thresholds, comparison, history, reports and the perf check."""

import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from aerie_bench import cli, perf, runner, scenario
from aerie_bench.session import Session
from aerie_bench.checks.perf import AD_PAGES, Perf

FAKE = Path(__file__).parent / "bench_fake_browser.py"


def doc(memory=800.0, cold=2000.0, warm=1200.0, label="main", scores=True, **extra):
    """A perf document with every metric (scores=False: only memory and startup)."""
    bench = {"speedometer_score": 100.0, "jetstream_score": 50.0} if scores else {}
    return {"schema": 1, "label": label, "binary": {"path": "/x", "version": "1.2.3"},
            "started": "2026-11-08T10:00:00+00:00",
            "metrics": {"memory_after_ad_pages_mb": memory, "startup_cold_ms": cold,
                        "startup_warm_ms": warm, **bench, **extra}, "skipped": {}, "failed": {}}


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

    def test_every_metric_is_required_unless_the_run_skipped_it_on_request(self):
        self.assertTrue(all(m.required for m in perf.METRICS.values()))
        cur = doc(scores=False)
        del cur["metrics"]["startup_warm_ms"]
        v = perf.compare(cur, doc())
        self.assertEqual(status(v)["startup_warm_ms"], "missing")
        self.assertEqual(status(v)["speedometer_score"], "missing")
        self.assertEqual({f["metric"] for f in perf.failures(v)},
                         {"startup_warm_ms", "speedometer_score", "jetstream_score"})
        cur["skipped"] = {"speedometer_score": "switched off", "jetstream_score": "switched off"}
        self.assertEqual({f["metric"] for f in perf.failures(perf.compare(cur, doc()))}, {"startup_warm_ms"})
        self.assertEqual(status(perf.compare(cur, doc()))["speedometer_score"], "not_measured")

    def test_a_new_metric_without_a_baseline_value_is_new(self):
        self.assertEqual(status(perf.compare(doc(), doc(scores=False)))["jetstream_score"], "new")

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
        # benchmarks=False unless a test says otherwise: the fake browser cannot run Speedometer.
        options = {"perf": {"settle_seconds": 0, "startup_runs": 1, "benchmarks": False, **opts,
                            "baseline": baseline}}
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
        self.assertEqual(check["data"]["failed"], {})

    def test_the_ad_heavy_pages_were_loaded_before_memory_was_read(self):
        res = self.run_perf(rss_kb=1024)
        paths = [r["path"] for r in res.data["network"]["fixture_requests"]]
        for page in AD_PAGES:
            self.assertIn(page, paths)
        self.assertLess(paths.index(AD_PAGES[-1]), paths.index("/index.html"))

    def test_memory_is_read_in_a_browser_of_its_own(self):
        seen = []
        real = Session.variant

        def variant(session, *a, **k):
            seen.append(session.browser_pid)
            return real(session, *a, **k)

        with mock.patch.object(Session, "variant", variant):
            self.run_perf(rss_kb=1024)
        # the memory browser and the two startup launches; the shared browser is never measured
        self.assertEqual(len(seen), 3)

    def test_missing_benchmark_copies_fail_the_check_and_say_how_to_fetch_them(self):
        res = self.run_perf(rss_kb=1024, benchmarks=True)
        check = res.data["checks"][0]
        self.assertEqual(check["status"], "fail")
        self.assertEqual(res.status, "fail")
        self.assertEqual(set(check["data"]["failed"]), {"speedometer_score", "jetstream_score"})
        self.assertIn("--download-benchmarks", check["detail"])
        self.assertEqual(check["data"]["skipped"], {})
        # what was measured is still written, and the failure is in the document too
        out = perf.load(res.out_dir / "perf.json")
        self.assertIn("memory_after_ad_pages_mb", out["metrics"])
        self.assertEqual(set(out["failed"]), {"speedometer_score", "jetstream_score"})

    def test_skipping_the_benchmarks_is_allowed_only_on_request_and_is_recorded(self):
        res = self.run_perf(rss_kb=1024, benchmarks=False)
        check = res.data["checks"][0]
        self.assertEqual(check["status"], "pass", check["detail"])
        self.assertEqual(set(check["data"]["skipped"]), {"speedometer_score", "jetstream_score"})
        self.assertIn("skipped on request", check["detail"])
        self.assertNotIn("speedometer_score", check["data"]["metrics"])

    def test_the_perf_json_carries_every_metric_when_the_benchmarks_ran(self):
        stub = {"speedometer": dict(entry="index.html", done="true", score="321", timeout_s=5),
                "jetstream": dict(entry="index.html", done="true", score="654", timeout_s=5)}
        (self.root / "bench-vendor").mkdir()
        for name in stub:
            (self.root / "bench-vendor" / name).mkdir()
            (self.root / "bench-vendor" / name / "index.html").write_text("<title>b</title>")
        sc = scenario.Scenario(checks=("perf",))
        options = {"perf": {"settle_seconds": 0, "startup_runs": 1, "baseline": None, "drivers": stub}}
        real_evaluate = Session.evaluate

        def evaluate(session, expression, *args, **kwargs):
            # the fake browser echoes an expression back as a string; a number is what a real page gives
            return float(expression) if expression in ("321", "654") else real_evaluate(session, expression, *args, **kwargs)

        with mock.patch("aerie_bench.procmem.tree_rss_kb", return_value={"total_kb": 2048, "processes": []}), \
                mock.patch.object(Session, "evaluate", evaluate):
            res = runner.run_bench(FAKE, sc, self.root, timeout=10, check_options=options)
        check = res.data["checks"][0]
        self.assertEqual(check["status"], "pass", check["detail"])
        self.assertEqual(check["data"]["metrics"]["speedometer_score"], 321.0)
        names = {"memory_after_ad_pages_mb", "startup_cold_ms", "startup_warm_ms",
                 "speedometer_score", "jetstream_score"}
        self.assertEqual(set(check["data"]["metrics"]), names)
        self.assertEqual(set(perf.load(res.out_dir / "perf.json")["metrics"]), names)
        self.assertEqual(set(perf.METRICS), names)

    def test_a_memory_heavy_build_fails_against_the_baseline(self):
        base = self.root / "main.json"
        base.write_text(json.dumps(doc(memory=500, cold=60000, warm=60000, scores=False)))
        res = self.run_perf(baseline=str(base), rss_kb=900 * 1024)
        check = res.data["checks"][0]
        self.assertEqual(check["status"], "fail")
        self.assertIn("memory_after_ad_pages_mb", check["detail"])
        self.assertEqual(res.status, "fail")

    def test_the_same_memory_passes_against_the_baseline(self):
        base = self.root / "main.json"
        base.write_text(json.dumps(doc(memory=500, cold=60000, warm=60000, scores=False)))
        res = self.run_perf(baseline=str(base), rss_kb=510 * 1024)
        self.assertEqual(res.data["checks"][0]["status"], "pass", res.data["checks"][0]["detail"])

    def test_a_baseline_that_does_not_exist_fails_loudly(self):
        res = self.run_perf(baseline=str(self.root / "missing.json"), rss_kb=1024)
        check = res.data["checks"][0]
        self.assertEqual(check["status"], "fail")
        self.assertEqual(res.status, "fail")
        self.assertIn("no baseline at", check["detail"])
        self.assertIn("--perf-first-run", check["detail"])

    def test_the_first_run_flag_lets_a_missing_baseline_pass(self):
        res = self.run_perf(baseline=str(self.root / "missing.json"), rss_kb=1024, first_run=True)
        self.assertEqual(res.data["checks"][0]["status"], "pass")
        self.assertIn("first run", res.data["checks"][0]["detail"])

    def test_the_first_run_flag_does_not_excuse_a_broken_or_regressed_baseline(self):
        base = self.root / "main.json"
        base.write_text("not json")
        self.assertEqual(self.run_perf(baseline=str(base), rss_kb=1024, first_run=True).data["checks"][0]["status"],
                         "error")
        base.write_text(json.dumps(doc(memory=500, cold=60000, warm=60000, scores=False)))
        self.assertEqual(self.run_perf(baseline=str(base), rss_kb=900 * 1024, first_run=True
                                       ).data["checks"][0]["status"], "fail")

    def test_a_broken_baseline_is_an_error_not_a_pass(self):
        base = self.root / "main.json"
        base.write_text("not json")
        res = self.run_perf(baseline=str(base), rss_kb=1024)
        self.assertEqual(res.data["checks"][0]["status"], "error")

    def test_unreadable_process_memory_is_an_error(self):
        with mock.patch("aerie_bench.procmem.tree_rss_kb", return_value=None):
            res = runner.run_bench(FAKE, scenario.Scenario(checks=("perf",)), self.root, timeout=10,
                                   check_options={"perf": {"settle_seconds": 0, "startup_runs": 1,
                                                                    "benchmarks": False}})
        self.assertEqual(res.data["checks"][0]["status"], "error")


class BenchmarkDriverTest(unittest.TestCase):
    def setUp(self):
        self.vendor = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.vendor, ignore_errors=True))
        self.drivers = {"speedometer": {"entry": "index.html?x=1", "done": "done()", "score": "score()", "progress": "prog()",
                                    "timeout_s": 5, "stall_s": 2}}

    def copy(self, name="speedometer"):
        (self.vendor / name).mkdir(exist_ok=True)
        (self.vendor / name / "index.html").write_text("<title>b</title>")

    def session(self, score=123.456, finished=True, load_error=None, hidden=None):
        s = mock.Mock()
        s.fixtures.base_url = "http://127.0.0.1:1"
        s.fixtures.vendor_root = self.vendor
        s.navigate_url.return_value = {"url": "", "error": load_error}
        s.foreground.return_value = hidden
        s.wait_progress.return_value = {True: "done", False: "timeout"}.get(finished, finished)
        s.evaluate.return_value = score
        # each benchmark runs in a browser of its own: the run's session hands out a variant
        browser = mock.MagicMock()
        browser.__enter__.return_value = s
        s.variant.return_value = browser
        outer = mock.Mock()
        outer.fixtures = s.fixtures
        outer.variant = s.variant
        s.outer = outer
        return s

    def perf(self):
        return Perf(drivers=self.drivers)

    def test_a_missing_copy_says_why_and_how_to_fetch_it(self):
        value, why = self.perf().run_benchmark(self.session().outer, "speedometer")
        self.assertIsNone(value)
        self.assertIn("no local copy", why)
        self.assertIn("--download-benchmarks", why)

    def test_a_copy_is_loaded_from_vendor_awaited_and_scored(self):
        self.copy()
        s = self.session()
        value, why = self.perf().run_benchmark(s.outer, "speedometer")
        self.assertEqual((value, why), (123.46, ""))
        s.navigate_url.assert_called_once_with("http://127.0.0.1:1/vendor/speedometer/index.html?x=1")
        s.wait_progress.assert_called_once_with("done()", "prog()", timeout=5.0, stall=2.0, interval=1.0)
        s.evaluate.assert_called_once_with("score()")
        s.variant.assert_called_once_with()  # a fresh browser, not the run's shared one
        s.foreground.assert_called_once_with()

    def test_the_page_is_brought_to_the_front_before_the_benchmark_loads(self):
        self.copy()
        s = self.session()
        order = mock.Mock()
        order.attach_mock(s.foreground, "foreground")
        order.attach_mock(s.navigate_url, "navigate_url")
        self.perf().run_benchmark(s.outer, "speedometer")
        self.assertEqual([c[0] for c in order.mock_calls[:2]], ["foreground", "navigate_url"])

    def test_a_page_that_stays_hidden_fails_fast_and_says_why(self):
        self.copy()
        s = self.session(hidden="hidden")
        value, why = self.perf().run_benchmark(s.outer, "speedometer")
        self.assertIsNone(value)
        self.assertIn("hidden", why)
        s.wait_progress.assert_not_called()

    def test_a_benchmark_that_stops_moving_fails_with_a_stall_not_a_timeout(self):
        self.copy()
        value, why = self.perf().run_benchmark(self.session(finished="stalled").outer, "speedometer")
        self.assertIsNone(value)
        self.assertIn("no progress for 2 s", why)

    def test_timeout_load_error_and_unusable_score_are_not_measured(self):
        self.copy()
        for kwargs, text in (({"finished": False}, "did not finish"), ({"load_error": "net::ERR"}, "did not load"),
                             ({"score": "n/a"}, "no usable score"), ({"score": True}, "no usable score"),
                             ({"score": None}, "no usable score"), ({"score": 0}, "no usable score")):
            value, why = self.perf().run_benchmark(self.session(**kwargs).outer, "speedometer")
            self.assertIsNone(value)
            self.assertIn(text, why)


class CliTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.root, ignore_errors=True))

    def invoke(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err), \
                mock.patch("aerie_bench.procmem.tree_rss_kb", return_value={"total_kb": 100 * 1024, "processes": []}):
            code = cli.main(["--binary", str(FAKE), "--build-root", str(self.root), "--check", "perf",
                             "--perf-settle", "0", "--timeout", "10", "--perf-skip-benchmarks", *argv])
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
        base.write_text(json.dumps(doc(memory=10, cold=60000, warm=60000, scores=False)))
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

    def test_a_missing_baseline_fails_unless_the_first_run_flag_is_given(self):
        missing = str(self.root / "nope" / "main.json")
        code, out, _ = self.invoke("--perf-baseline", missing)
        self.assertEqual(code, 1)
        self.assertIn("no baseline at", out)
        code, out, _ = self.invoke("--perf-baseline", missing, "--perf-first-run")
        self.assertEqual(code, 0, out)

    def test_first_run_needs_a_baseline_path(self):
        code, _, err = self.invoke("--perf-first-run")
        self.assertEqual(code, 2)
        self.assertIn("--perf-first-run needs --perf-baseline", err)

    def test_download_benchmarks_fetches_before_the_run_and_a_failure_stops_it(self):
        with mock.patch("aerie_bench.benchmarks.fetch_all") as fetch:
            code, _, _ = self.invoke("--download-benchmarks")
        self.assertEqual(code, 0)
        fetch.assert_called_once_with(self.root)
        from aerie_bench import benchmarks
        with mock.patch("aerie_bench.benchmarks.fetch_all", side_effect=benchmarks.BenchmarkError("offline")):
            code, _, err = self.invoke("--download-benchmarks")
        self.assertEqual(code, 2)
        self.assertIn("offline", err)

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


class SessionWaitTest(unittest.TestCase):
    """Session.wait_progress and Session.foreground, with a scripted page instead of a browser."""

    def session(self, values):
        """A Session whose evaluate() answers from ``values``: {expression: callable or constant}."""
        s = Session(mock.Mock(), mock.Mock(), Path("."), timeout=1)
        s._sid = "S"
        s.evaluate = lambda expr, *a, **k: values[expr]() if callable(values[expr]) else values[expr]
        return s

    def test_a_finished_benchmark_returns_done(self):
        s = self.session({"done": True, "prog": 0})
        self.assertEqual(s.wait_progress("done", "prog", timeout=5, stall=5, interval=0), "done")

    def test_progress_that_never_changes_is_a_stall_long_before_the_timeout(self):
        s = self.session({"done": False, "prog": 3})
        with mock.patch("aerie_bench.session.time.sleep"), \
                mock.patch("aerie_bench.session.time.monotonic", side_effect=iter(range(0, 1000, 10))):
            self.assertEqual(s.wait_progress("done", "prog", timeout=3600, stall=30, interval=1), "stalled")

    def test_progress_that_keeps_moving_runs_to_the_timeout(self):
        n = iter(range(10000))
        s = self.session({"done": False, "prog": lambda: next(n)})
        with mock.patch("aerie_bench.session.time.sleep"), \
                mock.patch("aerie_bench.session.time.monotonic", side_effect=iter(range(0, 1000, 10))):
            self.assertEqual(s.wait_progress("done", "prog", timeout=100, stall=30, interval=1), "timeout")

    def test_foreground_is_none_when_the_page_becomes_visible(self):
        s = self.session({"document.visibilityState === 'visible'": True})
        self.assertIsNone(s.foreground())
        methods = [c.args[0] for c in s.client.send.call_args_list]
        self.assertIn("Page.bringToFront", methods)

    def test_foreground_reports_the_state_a_hidden_page_stays_in(self):
        s = self.session({"document.visibilityState === 'visible'": False, "document.visibilityState": "hidden"})
        with mock.patch("aerie_bench.session.time.sleep"):
            self.assertEqual(s.foreground(timeout=0), "hidden")


class DriverTableTest(unittest.TestCase):
    def test_every_driver_has_a_total_limit_a_stall_limit_and_a_progress_probe(self):
        from aerie_bench.benchmarks import DRIVERS
        for name, d in DRIVERS.items():
            self.assertIn("progress", d, name)
            self.assertLessEqual(d["stall_s"], 300, name)
            self.assertLessEqual(d["timeout_s"], 1200, name)
            self.assertLess(d["stall_s"], d["timeout_s"], name)
