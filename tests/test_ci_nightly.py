import json
import tempfile
import unittest
from pathlib import Path

from aerie_build import buildlock
from aerie_ci import nightly
from aerie_ci.gate import GateConfig

from .ci_fakes import SHA_A, FakeGh, FakeShell


class NightlyTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name) / "build"
        self.shell = FakeShell()
        self.gh = FakeGh()

    def cfg(self, **kw):
        return GateConfig(repo=Path(self._tmp.name) / "repo", build_root=self.root, shell=self.shell, gh=self.gh,
                          python="python3", lock_timeout=0,
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

    def test_results_are_kept_under_a_dated_directory(self):
        self.run_nightly()
        day = self.root / "ci" / "nightly" / "2026-11-09"
        self.assertTrue((day / "nightly.log").is_file())
        data = json.loads((day / "summary.json").read_text())
        self.assertEqual(data["state"], "pass")
        self.assertEqual(data["date"], "2026-11-09")
        self.assertEqual(data["sha"], SHA_A)
        bench = [c for c in self.shell.calls if "bench" in c and "--out" in c][-1]
        self.assertEqual(bench[bench.index("--out") + 1], str(day / "bench"))

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
        self.assertIn("bench", body)

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

    def test_worktree_is_removed(self):
        self.run_nightly()
        self.assertTrue(self.shell.ran("worktree remove --force"))
