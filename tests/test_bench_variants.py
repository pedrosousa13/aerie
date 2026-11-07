import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from aerie_bench import runner, scenario
from aerie_bench.session import CheckResult

FAKE = Path(__file__).parent / "bench_fake_browser.py"


def make_check(body):
    class Probe:
        name = "probe"
        seeds_helium_services = False

        def run(self, session):
            return CheckResult(self.name, "pass", "ok", data=body(session))

    return Probe


class VariantTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.root, ignore_errors=True))
        self.prefs_log = self.root / "prefs.log"

    def go(self, body, extra_env=None, **kw):
        env = {"FAKE_BROWSER_COPY_PREFS": str(self.prefs_log), "FAKE_BROWSER_DUMP_ARGS": str(self.root / "args.json"),
               **(extra_env or {})}
        with mock.patch.dict("aerie_bench.checks.REGISTRY", {"probe": make_check(body)}):
            return runner.run_bench(FAKE, scenario.Scenario(checks=("probe",)), self.root, timeout=10,
                                    extra_env=env, **kw)

    def test_navigate_url_reports_errors_instead_of_raising(self):
        def body(s):
            ok = s.navigate_url(s.fixtures.base_url + "/index.html")
            bad = s.navigate_url(s.fixtures.base_url + "/missing.html")
            return {"ok": ok, "bad": bad}
        res = self.go(body, extra_env={"FAKE_BROWSER_NAV_ERRORTEXT": "1"})
        self.assertEqual(res.status, "pass", res.data["checks"])
        data = res.data["checks"][0]["data"]
        self.assertIsNone(data["ok"]["error"])
        self.assertEqual(data["bad"]["error"], "net::ERR_FAKE")

    def test_evaluate_passes_promise_and_gesture_options(self):
        def body(s):
            s.navigate("/index.html")
            return {"plain": s.evaluate("1"), "awaited": s.evaluate("1", await_promise=True),
                    "gesture": s.evaluate("1", user_gesture=True)}
        data = self.go(body).data["checks"][0]["data"]
        self.assertEqual(data, {"plain": "1", "awaited": "awaited:1", "gesture": "gesture:1"})

    def test_targets_lists_browser_targets(self):
        def body(s):
            s.navigate("/index.html")
            return {"types": [t["type"] for t in s.targets()]}
        self.assertEqual(self.go(body).data["checks"][0]["data"]["types"], ["page"])

    def test_variant_runs_a_second_browser_with_its_own_flags_and_prefs(self):
        def body(s):
            s.navigate("/index.html")
            with s.variant(("--variant-flag",), {"webrtc": {"ip_handling_policy": "default"}}) as v:
                v.navigate("/index.html")
                title = v.evaluate("document.title")
            return {"variant_title": title, "variant_exit": v.exit}
        res = self.go(body)
        self.assertEqual(res.status, "pass", res.data["checks"])
        data = res.data["checks"][0]["data"]
        self.assertEqual(data["variant_title"], "Aerie Bench Fixture")
        self.assertEqual(data["variant_exit"], {"clean": True, "code": 0})
        self.assertIn("--variant-flag", json.loads((self.root / "args.json").read_text()))
        seeded = [l for l in self.prefs_log.read_text().splitlines() if l.startswith("{")]
        self.assertEqual(json.loads(seeded[0]), {"webrtc": {"ip_handling_policy": "default"}})

    def test_variant_inherits_extra_flags_and_every_profile_is_deleted(self):
        def body(s):
            with s.variant(("--v",)):
                pass
            return {}
        res = self.go(body, extra_flags=("--main-flag",))
        self.assertIn("--main-flag", json.loads((self.root / "args.json").read_text()))
        self.assertEqual(list((self.root / "bench-tmp").iterdir()), [])
        self.assertTrue(res.data["profile"]["deleted"])

    def test_variant_browser_is_stopped_when_the_check_raises(self):
        def body(s):
            with s.variant(()):
                raise RuntimeError("boom")
        res = self.go(body)
        self.assertEqual(res.data["checks"][0]["status"], "error")
        self.assertEqual(list((self.root / "bench-tmp").iterdir()), [])

    def test_session_without_launcher_refuses_variants(self):
        from aerie_bench.session import Session
        s = Session(None, None, Path("."))
        with self.assertRaises(Exception):
            with s.variant(()):
                pass

    def test_tcp_probe_is_available_to_checks(self):
        def body(s):
            with s.tcp_probe() as p:
                import socket
                socket.create_connection(("127.0.0.1", p.port), timeout=2).close()
                import time
                time.sleep(0.2)
                return {"n": p.connections}
        self.assertEqual(self.go(body).data["checks"][0]["data"]["n"], 1)


if __name__ == "__main__":
    unittest.main()
