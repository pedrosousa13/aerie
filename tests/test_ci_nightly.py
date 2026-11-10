import json
import os
import tempfile
import unittest
from pathlib import Path

from aerie_build import buildlock
from aerie_ci import nightly
from aerie_ci.gate import CiConfig

from .ci_fakes import SHA_A, FakeGh, FakeShell


class NightlyTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name) / "build"
        self.ci_root = Path(self._tmp.name) / "build-ci"  # outside the build root, see docs/ci.md
        self.shell = FakeShell()
        self.gh = FakeGh()
        self.main_root = Path(self._tmp.name) / "main-build"  # the train's build root, which the nightly never uses
        self.cache = self.main_root / "ccache"

    def cfg(self, **kw):
        return CiConfig(repo=Path(self._tmp.name) / "repo", build_root=self.root, shell=self.shell, gh=self.gh,
                          python="python3", lock_timeout=0, ccache_dir=self.cache,
                          environ={"PATH": "/usr/bin", "GH_TOKEN": "ghp_" + "Q" * 36}, **kw)

    def run_nightly(self, **kw):
        return nightly.run_nightly(self.cfg(), today=lambda: "2026-11-09", **kw)

    def test_clean_build_then_full_bench_on_main(self):
        self.assertEqual(self.run_nightly(), nightly.PASS)
        j = self.shell.joined()
        self.assertTrue(any("fetch" in c and "main" in c for c in j))
        build = next(c for c in j if "aerie build" in c)
        self.assertIn("--clean", build)
        bench = [c for c in self.shell.calls if "bench" in c and "--check" in c][-1]
        checks = [bench[i + 1] for i, a in enumerate(bench) if a == "--check"]
        # the full Bench: everything registered, zero_traffic and smoke included
        self.assertEqual(checks, ["ad_blocking", "branding", "smoke", "zero_traffic"])

    def test_the_views_ui_tests_run_after_the_bench_and_are_in_the_summary(self):
        self.assertEqual(self.run_nightly(), nightly.PASS)
        cmd = next(c for c in self.shell.calls if "test-cpp" in c)
        self.assertEqual([cmd[i + 1] for i, a in enumerate(cmd) if a == "--target"],
                         ["aerie_browser_tests", "aerie_interactive_ui_tests"])
        self.assertEqual(cmd[cmd.index("--gtest-filter") + 1], "Aerie*")
        self.assertEqual(cmd[cmd.index("--build-root") + 1], str(self.root))
        self.assertGreater(self.shell.calls.index(cmd), max(i for i, c in enumerate(self.shell.calls) if "bench" in c and "--check" in c))
        data = json.loads((self.ci_root / "nightly" / "2026-11-09" / "summary.json").read_text())
        self.assertEqual([s["name"] for s in data["steps"]], ["clean build", "full bench", "views ui tests"])

    def test_a_failing_views_ui_test_fails_the_nightly_and_names_the_step(self):
        self.shell.fail = {"test-cpp": 1}
        self.assertEqual(self.run_nightly(), nightly.FAIL)
        data = json.loads((self.ci_root / "nightly" / "2026-11-09" / "summary.json").read_text())
        self.assertEqual(data["steps"][-1], {"name": "views ui tests", "rc": 1, "seconds": data["steps"][-1]["seconds"]})
        self.assertTrue(self.shell.ran("--check smoke"))  # the Bench still ran

    def test_a_registered_perf_check_is_recorded_as_the_baseline(self):
        self.shell.checks = "smoke perf"
        self.run_nightly()
        bench = [c for c in self.shell.calls if "bench" in c and "--check" in c][-1]
        self.assertEqual(bench[bench.index("--perf-record") + 1], "/mnt/linux2/aerie-perf")
        self.assertIn("--download-benchmarks", bench)
        self.assertNotIn("--perf-skip-benchmarks", bench)

    def test_results_are_kept_under_a_dated_directory(self):
        self.run_nightly()
        day = self.ci_root / "nightly" / "2026-11-09"
        self.assertTrue((day / "nightly.log").is_file())
        data = json.loads((day / "summary.json").read_text())
        self.assertEqual(data["state"], "pass")
        self.assertEqual(data["date"], "2026-11-09")
        self.assertEqual(data["sha"], SHA_A)
        bench = [c for c in self.shell.calls if "bench" in c and "--out" in c][-1]
        # Bench output lives in the nightly build root; the log and summary stay in the CI folder
        out = self.root / "bench" / "2026-11-09"
        self.assertEqual(bench[bench.index("--out") + 1], str(out))
        self.assertEqual(data["bench"], str(out))

    def test_success_reports_nothing(self):
        self.run_nightly()
        self.assertEqual(self.gh.issues, [])

    def test_failure_opens_an_issue_with_the_log_path(self):
        self.shell.fail = {"--check zero_traffic": 1}
        self.assertEqual(self.run_nightly(), nightly.FAIL)
        kind, title, body = self.gh.issues[0]
        self.assertEqual(kind, "create")
        self.assertIn("2026-11-09", title)
        self.assertIn("nightly.log", body)
        self.assertIn(str(self.root / "bench" / "2026-11-09"), body)

    def test_failure_comments_on_a_standing_issue_when_configured(self):
        self.shell.fail = {"aerie build": 2}
        nightly.run_nightly(self.cfg(), today=lambda: "2026-11-09", report_issue=15)
        self.assertEqual(self.gh.issues[0][:2], ("comment", 15))
        self.assertFalse(any("--check" in c and "bench" in c for c in self.shell.joined()))  # stopped at build

    def test_no_secret_in_the_report(self):
        self.shell.fail = {"aerie build": 2}
        self.run_nightly()
        self.assertNotIn("ghp_", json.dumps(self.gh.issues))
        for env in self.shell.envs:
            self.assertNotIn("GH_TOKEN", env)

    def test_it_takes_the_build_tree_lock(self):
        with buildlock.build_tree_lock(self.root):
            self.assertEqual(self.run_nightly(), nightly.BUSY)
        self.assertFalse(self.shell.ran("aerie build"))
        self.assertEqual(len(self.gh.issues), 1)  # a night with no run is worth knowing about

    def test_every_step_gets_the_lock_marker_so_its_build_does_not_wait_for_us(self):
        self.run_nightly()
        marker = f"{self.root / buildlock.LOCK_NAME}:{os.getpid()}"
        self.assertTrue(self.shell.envs)
        for env in self.shell.envs:
            self.assertEqual(env.get(buildlock.ENV_HELD), marker)

    def test_worktree_is_removed(self):
        self.run_nightly()
        self.assertTrue(self.shell.ran("worktree remove --force"))


    def test_every_step_uses_the_nightly_root_never_the_main_one(self):
        self.run_nightly()
        for call in self.shell.calls:
            if "--build-root" in call:
                self.assertEqual(call[call.index("--build-root") + 1], str(self.root))
        self.assertFalse(any(str(self.main_root / "src") in c for c in self.shell.joined()))

    def test_the_build_shares_the_compiler_cache_and_the_bench_does_not_need_it(self):
        self.run_nightly()
        build = next(c for c in self.shell.calls if "build" in c and "--clean" in c)
        self.assertEqual(build[build.index("--ccache-dir") + 1], str(self.cache))
        bench = [c for c in self.shell.calls if "bench" in c and "--check" in c][-1]
        self.assertNotIn("--ccache-dir", bench)

    def test_no_shared_cache_means_no_flag(self):
        cfg = CiConfig(repo=Path(self._tmp.name) / "repo", build_root=self.root, shell=self.shell, gh=self.gh,
                       python="python3", lock_timeout=0, environ={"PATH": "/usr/bin"})
        nightly.run_nightly(cfg, today=lambda: "2026-11-09")
        self.assertFalse(any("--ccache-dir" in c for c in self.shell.calls))

    def test_a_busy_main_build_root_does_not_stop_the_nightly(self):
        # A real flock on the main root. The nightly's lock is the one in its own root.
        with buildlock.build_tree_lock(self.main_root):
            self.assertEqual(self.run_nightly(), nightly.PASS)
        self.assertTrue((self.root / buildlock.LOCK_NAME).is_file())
        self.assertFalse((self.main_root / "src").exists())
