import contextlib
import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from aerie_bench import cli, runner, scenario

FAKE = Path(__file__).parent / "bench_fake_browser.py"


class RunnerTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.root, ignore_errors=True))

    def run_bench(self, sc=None, env=None, **kw):
        return runner.run_bench(FAKE, sc or scenario.Scenario(), self.root, timeout=10,
                                extra_env=env or {}, **kw)

    def test_smoke_passes_and_writes_results_under_build_root(self):
        res = self.run_bench()
        self.assertEqual(res.status, "pass")
        self.assertEqual(res.out_dir.parent, self.root / "bench")
        data = json.loads((res.out_dir / "results.json").read_text())
        self.assertEqual(data["schema"], 1)
        self.assertEqual(data["checks"][0]["name"], "smoke")
        self.assertEqual(data["checks"][0]["status"], "pass")
        self.assertTrue((res.out_dir / "summary.txt").read_text().startswith("Aerie Bench"))
        self.assertTrue((res.out_dir / "smoke.png").read_bytes().startswith(b"\x89PNG"))
        self.assertEqual(data["binary"]["version"], "Fake Chromium 1.2.3.4")
        self.assertEqual(res.out_dir.stat().st_mode & 0o777, 0o700)

    def test_page_request_is_in_fixture_log_and_netlog(self):
        data = self.run_bench().data
        self.assertIn("/index.html", [r["path"] for r in data["network"]["fixture_requests"]])
        self.assertEqual(data["network"]["netlog_requests"], 1)

    def test_profile_is_fresh_and_deleted(self):
        data = self.run_bench().data
        self.assertTrue(data["profile"]["deleted"])
        self.assertFalse(Path(data["profile"]["path"]).exists())
        self.assertEqual(list((self.root / "bench-tmp").iterdir()), [])
        again = self.run_bench().data
        self.assertNotEqual(again["profile"]["path"], data["profile"]["path"])

    def test_unclean_exit_fails_smoke(self):
        res = self.run_bench(env={"FAKE_BROWSER_EXIT": "5"})
        self.assertEqual(res.status, "fail")
        self.assertIn("did not exit cleanly", res.data["checks"][0]["detail"])

    def test_browser_requests_passes_on_services_call(self):
        res = self.run_bench(scenario.Scenario(checks=("browser_requests",)),
                             env={"FAKE_BROWSER_SERVICES": "1"})
        self.assertEqual(res.status, "pass", res.data["checks"])
        self.assertTrue(res.data["seeded_helium_services"])
        self.assertEqual(res.data["checks"][0]["data"]["services_hits"], ["/bangs.json"])

    def test_browser_requests_fails_when_browser_is_silent(self):
        with mock.patch("aerie_bench.checks.browser_requests.WAIT_SECONDS", 0.3):
            from aerie_bench.checks.browser_requests import BrowserRequests
            check = BrowserRequests(wait_seconds=0.3)
            with mock.patch.dict("aerie_bench.checks.REGISTRY", {"browser_requests": lambda: check}):
                res = self.run_bench(scenario.Scenario(checks=("browser_requests",)))
        self.assertEqual(res.status, "fail")

    def test_require_services_fails_when_probe_never_arrives(self):
        # The fake makes external requests only if it also calls services; here it does neither,
        # so use an external-only netlog: services optional passes via netlog, required fails.
        sc = scenario.Scenario(checks=("browser_requests",))
        req = self.run_bench(sc, env={"FAKE_BROWSER_EXTERNAL": "1"},
                             check_options={"browser_requests": {"wait_seconds": 0.3, "require_services": True}})
        self.assertEqual(req.status, "fail")
        self.assertEqual(req.data["checks"][0]["data"]["passed_via"], None)
        opt = self.run_bench(sc, env={"FAKE_BROWSER_EXTERNAL": "1"},
                             check_options={"browser_requests": {"wait_seconds": 0.3, "require_services": False}})
        self.assertEqual(opt.status, "pass")
        self.assertEqual(opt.data["checks"][0]["data"]["passed_via"], "netlog")

    def test_require_services_auto_follows_binary_identity(self):
        from aerie_bench.checks.browser_requests import is_helium_family
        self.assertTrue(is_helium_family("/x/out/Default/helium", None))
        self.assertTrue(is_helium_family("/x/aerie", None))
        self.assertTrue(is_helium_family("/x/chrome", "Helium 0.21.1.1"))
        self.assertFalse(is_helium_family("/x/chrome", "Chromium 156"))

    def test_browser_requests_skips_only_when_asked_never_by_version(self):
        sc = scenario.Scenario(checks=("browser_requests",))
        opts = lambda v: {"browser_requests": {"wait_seconds": 0.3, "require_services": v}}
        skipped = self.run_bench(sc, check_options=opts("skip"))
        self.assertEqual(skipped.status, "pass")
        self.assertEqual(skipped.data["checks"][0]["status"], "skip")
        self.assertEqual(skipped.data["checks"][0]["data"]["skipped_by"], "--services skip")
        # A binary that calls itself Aerie is not skipped: a silent probe still fails.
        link = self.root / "fake-aerie"
        link.symlink_to(FAKE)
        res = runner.run_bench(link, sc, self.root, timeout=10, check_options=opts(True))
        self.assertEqual(res.data["checks"][0]["status"], "fail")

    def test_services_pass_records_path(self):
        res = self.run_bench(scenario.Scenario(checks=("browser_requests",)),
                             env={"FAKE_BROWSER_SERVICES": "1"})
        self.assertEqual(res.data["checks"][0]["data"]["passed_via"], "services")

    def test_wait_stops_early_when_condition_met(self):
        import time
        t = time.monotonic()
        self.run_bench(scenario.Scenario(checks=("browser_requests",)), env={"FAKE_BROWSER_SERVICES": "1"},
                       check_options={"browser_requests": {"wait_seconds": 30}})
        self.assertLess(time.monotonic() - t, 15)

    def test_undeleted_profile_fails_the_run(self):
        from aerie_bench import profile as prof
        with mock.patch.object(prof.Profile, "delete", lambda self: None):
            res = self.run_bench()
        self.assertEqual(res.status, "fail")
        self.assertFalse(res.data["profile"]["deleted"])

    def test_scenario_steps(self):
        sc = scenario.parse(json.dumps({"steps": [
            {"navigate": "/index.html"}, {"evaluate": "document.title", "expect": "Aerie Bench Fixture"},
            {"screenshot": "s.png"}]}))
        res = self.run_bench(sc)
        self.assertEqual(res.status, "pass", res.data["checks"])
        self.assertIn("s.png", res.data["files"])

    def test_failing_step_reports_expectation(self):
        sc = scenario.parse(json.dumps({"steps": [
            {"navigate": "/index.html"}, {"evaluate": "document.title", "expect": "nope"}]}))
        res = self.run_bench(sc)
        self.assertEqual(res.status, "fail")
        self.assertIn("expected 'nope'", res.data["checks"][0]["detail"])

    def test_unknown_check_and_bad_binary_are_harness_errors(self):
        with self.assertRaises(runner.BenchError):
            self.run_bench(scenario.Scenario(checks=("nope",)))
        with self.assertRaises(runner.BenchError):
            runner.run_bench("/nonexistent/chrome", scenario.Scenario(), self.root)

    def test_truncated_netlog_still_parses(self):
        res = self.run_bench(env={"FAKE_BROWSER_TRUNCATE_NETLOG": "1"})
        self.assertEqual(res.data["network"]["netlog_requests"], 1)


class BrowserEnvTest(unittest.TestCase):
    def test_allowlist_and_isolated_home(self):
        environ = {"PATH": "/bin", "LANG": "C", "DISPLAY": ":0", "XAUTHORITY": "/x", "TZ": "UTC",
                   "HOME": "/home/me", "GITHUB_TOKEN": "secret", "AWS_SECRET_ACCESS_KEY": "s",
                   "XDG_CONFIG_HOME": "/home/me/.config", "LC_ALL": "C", "WAYLAND_DISPLAY": "w"}
        with tempfile.TemporaryDirectory() as d:
            env = runner.browser_env(environ, Path(d))
            self.assertEqual(env["HOME"], str(Path(d) / "home"))
            self.assertEqual(env["XDG_CONFIG_HOME"], str(Path(d) / "home" / ".config"))
            self.assertTrue((Path(d) / "home" / ".config").is_dir())
            for k in ("PATH", "LANG", "DISPLAY", "XAUTHORITY", "TZ", "LC_ALL", "WAYLAND_DISPLAY"):
                self.assertEqual(env[k], environ[k])
            self.assertNotIn("GITHUB_TOKEN", env)
            self.assertNotIn("AWS_SECRET_ACCESS_KEY", env)

    def test_extra_env_reaches_the_browser_and_overrides(self):
        with tempfile.TemporaryDirectory() as d:
            env = runner.browser_env({"PATH": "/bin"}, Path(d), {"FOO": "1"})
            self.assertEqual(env["FOO"], "1")


class CliTest(unittest.TestCase):
    def invoke(self, *argv, root):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = cli.main(["--build-root", str(root), *argv])
        return code, out.getvalue(), err.getvalue()

    def test_exit_codes(self):
        with tempfile.TemporaryDirectory() as d:
            code, out, _ = self.invoke("--binary", str(FAKE), "--timeout", "10", root=d)
            self.assertEqual(code, 0)
            self.assertIn("result    PASS", out)
            self.assertIn("results: ", out)
            code, _, err = self.invoke("--binary", "/nonexistent", root=d)
            self.assertEqual(code, 2)
            self.assertIn("aerie bench: error", err)
            code, _, _ = self.invoke("--binary", str(FAKE), "--check", "bogus", root=d)
            self.assertEqual(code, 2)

    def invoke_run(self, argv, default_binary, root):
        import argparse
        p = argparse.ArgumentParser()
        cli.add_arguments(p, binary_required=False)
        with mock.patch.object(cli, "run_bench") as rb, contextlib.redirect_stdout(io.StringIO()):
            rb.side_effect = RuntimeError("stop")
            try:
                cli.run(p.parse_args(argv), Path(root), default_binary)
            except RuntimeError:
                pass
        return rb.call_args.kwargs["check_options"]["browser_requests"]["require_services"]

    def test_default_build_binary_skips_the_probe_unless_told_to_require_it(self):
        # No --binary means our own (Aerie) build, which makes no requests: skip, never guessed by name.
        self.assertEqual(self.invoke_run([], FAKE, "/tmp/x"), "skip")
        self.assertEqual(self.invoke_run(["--services", "auto"], FAKE, "/tmp/x"), "skip")
        self.assertIs(self.invoke_run(["--services", "require"], FAKE, "/tmp/x"), True)
        self.assertIs(self.invoke_run(["--services", "optional"], FAKE, "/tmp/x"), False)

    def test_default_build_binary_run_prints_one_note_and_records_it(self):
        import argparse
        p = argparse.ArgumentParser()
        cli.add_arguments(p, binary_required=False)
        out = io.StringIO()
        with tempfile.TemporaryDirectory() as d, contextlib.redirect_stdout(out):
            code = cli.run(p.parse_args(["--check", "browser_requests", "--timeout", "10"]), Path(d), FAKE)
            results = json.loads(next(Path(d).glob("bench/*/results.json")).read_text())
        self.assertEqual(code, 0, out.getvalue())
        self.assertEqual(out.getvalue().count("aerie bench: note:"), 1)
        check = results["checks"][0]
        self.assertEqual(check["status"], "skip")
        self.assertIn("--baseline <helium binary>", check["data"]["skipped_by"])
        self.assertIn("--baseline", check["data"]["needs_baseline"])

    def test_skip_is_explicit_and_works_for_the_default_build_binary(self):
        self.assertEqual(self.invoke_run(["--binary", str(FAKE), "--services", "skip"], FAKE, "/tmp/x"), "skip")

    def test_explicit_binary_keeps_auto_and_overrides(self):
        self.assertIsNone(self.invoke_run(["--binary", str(FAKE)], FAKE, "/tmp/x"))
        self.assertIs(self.invoke_run(["--binary", str(FAKE), "--services", "optional"], FAKE, "/tmp/x"), False)
        self.assertIs(self.invoke_run(["--services", "optional"], FAKE, "/tmp/x"), False)

    def test_docs_list_every_flag(self):
        import argparse
        p = argparse.ArgumentParser()
        cli.add_arguments(p, binary_required=False)
        flags = {o for a in p._actions for o in a.option_strings if o.startswith("--") and o != "--help"}
        doc = (Path(__file__).parent.parent / "docs" / "bench.md").read_text()
        for f in flags:
            self.assertIn(f, doc, f"docs/bench.md does not mention {f}")


if __name__ == "__main__":
    unittest.main()
