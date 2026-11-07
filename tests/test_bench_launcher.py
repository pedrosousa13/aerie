import sys
import tempfile
import unittest
from pathlib import Path

from aerie_bench import launcher

FAKE = Path(__file__).parent / "bench_fake_browser.py"


class BuildArgvTest(unittest.TestCase):
    def argv(self, **kw):
        return launcher.build_argv("/b/chrome", Path("/p"), Path("/n.json"), **kw)

    def test_core_flags(self):
        a = self.argv()
        self.assertEqual(a[0], "/b/chrome")
        for f in ("--user-data-dir=/p", "--remote-debugging-pipe", "--log-net-log=/n.json",
                  "--headless=new", "--no-first-run"):
            self.assertIn(f, a)
        self.assertTrue(any(x.startswith("--host-resolver-rules=") and "EXCLUDE 127.0.0.1" in x for x in a))

    def test_resolver_rules_keep_loopback_and_fixture_names_reachable(self):
        rules = [x for x in self.argv() if x.startswith("--host-resolver-rules=")][0].split("=", 1)[1]
        parts = [r.strip() for r in rules.split(",")]
        for want in ("EXCLUDE localhost", "EXCLUDE *.localhost", "EXCLUDE 127.0.0.1", "EXCLUDE ::1"):
            self.assertIn(want, parts)
        self.assertLess(parts.index("MAP *.bench.test 127.0.0.1"), parts.index("MAP * ~NOTFOUND"))

    def test_never_hides_background_networking(self):
        joined = " ".join(self.argv())
        for bad in ("disable-background-networking", "disable-component-update", "disable-sync"):
            self.assertNotIn(bad, joined)

    def test_headed_and_sandbox_are_explicit(self):
        a = self.argv(headless=False)
        self.assertFalse(any(x.startswith("--headless") for x in a))
        self.assertNotIn("--no-sandbox", a)
        self.assertIn("--no-sandbox", self.argv(no_sandbox=True))

    def test_no_remote_debugging_port(self):
        self.assertFalse(any("remote-debugging-port" in x for x in self.argv()))


class ProcessTest(unittest.TestCase):
    def test_clean_shutdown(self):
        p = launcher.spawn([sys.executable, str(FAKE)])
        info = p.shutdown(timeout=5)
        self.assertEqual(info, {"clean": True, "code": 0})
        self.assertIsNotNone(p.popen.poll())

    def test_nonzero_exit_is_not_clean(self):
        import os
        env = dict(os.environ, FAKE_BROWSER_EXIT="3")
        p = launcher.spawn([sys.executable, str(FAKE)], env=env)
        self.assertEqual(p.shutdown(timeout=5), {"clean": False, "code": 3})

    def test_unresponsive_browser_is_killed(self):
        p = launcher.spawn([sys.executable, "-c", "import time; time.sleep(60)"])
        info = p.shutdown(timeout=0.5)
        self.assertFalse(info["clean"])
        self.assertIsNotNone(p.popen.poll())

    def test_hung_browser_children_are_killed_too(self):
        import os, time
        with tempfile.TemporaryDirectory() as d:
            pidfile = Path(d) / "child.pid"
            code = ("import os,sys,time\n"
                    "pid=os.fork()\n"
                    "if pid==0:\n time.sleep(120)\n"
                    f"open({str(pidfile)!r},'w').write(str(pid))\n"
                    "time.sleep(120)\n")
            p = launcher.spawn([sys.executable, "-c", code])
            for _ in range(50):
                if pidfile.exists() and pidfile.read_text():
                    break
                time.sleep(0.1)
            child = int(pidfile.read_text())
            p.shutdown(timeout=0.5)
            for _ in range(50):
                try:
                    os.kill(child, 0)
                except ProcessLookupError:
                    break
                time.sleep(0.1)
            else:
                os.kill(child, 9)
                self.fail("child of the browser survived shutdown")

    def test_shutdown_uses_one_shared_deadline(self):
        import time
        p = launcher.spawn([sys.executable, "-c", "import time; time.sleep(60)"])
        t = time.monotonic()
        p.shutdown(timeout=1.0)
        # close wait + process wait + terminate must all fit in about timeout, not 2x timeout plus
        self.assertLess(time.monotonic() - t, 3.5)

    def test_version(self):
        self.assertEqual(launcher.version(Path(FAKE)), "Fake Chromium 1.2.3.4")
        self.assertIsNone(launcher.version("/nonexistent/browser"))


if __name__ == "__main__":
    unittest.main()
