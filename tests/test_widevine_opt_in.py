"""Widevine opt-in (#7): Bench check, local-state seeding, source gate and repo wiring.

Nothing here starts a browser. The check splits into observe() (drives the browser) and judge()
(pure), like the hardening checks; finalize() reads the NetLog.
"""

import json
import re
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from aerie_bench import checks, profile
from aerie_bench.checks import widevine_opt_in as wv
from aerie_build import treegate

REPO = Path(__file__).resolve().parent.parent

FRESH_OK = {"key_system": {"available": False, "error": "NotSupportedError"}, "listed": False}
CONTROL_OK = {"listed": True}


class JudgeTest(unittest.TestCase):
    def obs(self, main=None, control=None, online=None):
        o = {"main": main or dict(FRESH_OK), "control": control or dict(CONTROL_OK)}
        if online is not None:
            o["online"] = online
        return o

    def test_fresh_profile_without_widevine_passes(self):
        status, detail = wv.judge(self.obs())
        self.assertEqual(status, "pass", detail)
        self.assertIn("absent", detail)

    def test_widevine_available_on_a_fresh_profile_fails(self):
        status, detail = wv.judge(self.obs(main={"key_system": {"available": True}, "listed": False}))
        self.assertEqual(status, "fail")
        self.assertIn("fresh profile", detail)

    def test_widevine_listed_in_components_on_a_fresh_profile_fails(self):
        status, detail = wv.judge(self.obs(main={**FRESH_OK, "listed": True}))
        self.assertEqual(status, "fail")
        self.assertIn("components", detail)

    def test_negative_control_must_show_widevine_when_opted_in(self):
        status, detail = wv.judge(self.obs(control={"listed": False}))
        self.assertEqual(status, "fail")
        self.assertIn("negative control failed", detail)

    def test_page_that_could_not_ask_is_not_a_pass(self):
        status, detail = wv.judge(self.obs(main={"key_system": {"error": "TypeError"}, "listed": False}))
        self.assertEqual(status, "fail")
        self.assertIn("did not answer", detail)

    def test_online_run_requires_widevine_after_opt_in(self):
        self.assertEqual(wv.judge(self.obs(online={"available": True}))[0], "pass")
        status, detail = wv.judge(self.obs(online={"available": False, "error": "NotSupportedError"}))
        self.assertEqual(status, "fail")
        self.assertIn("opted in", detail)


class NetlogTest(unittest.TestCase):
    def rec(self, host, url=None):
        return {"host": host, "url": url or f"https://{host}/x", "urls": [url or f"https://{host}/x"],
                "external": True}

    def test_blocked_dummy_hosts_are_not_a_fetch(self):
        self.assertEqual(wv.escaped_requests([self.rec("update.9oo91eapis.qjz9zk")]), [])

    def test_real_host_is_a_fetch(self):
        out = wv.escaped_requests([self.rec("dl.example.com")])
        self.assertEqual([r["host"] for r in out], ["dl.example.com"])

    def test_loopback_is_ignored(self):
        r = self.rec("127.0.0.1")
        r["external"] = False
        self.assertEqual(wv.escaped_requests([r]), [])

    def test_finalize_fails_on_a_widevine_request_even_to_a_dummy_host(self):
        res = SimpleNamespace(status="pass", detail="ok", name="widevine_opt_in", data={})
        session = SimpleNamespace(netlog=[self.rec("a.qjz9zk", "https://a.qjz9zk/widevine-cdm.crx")])
        out = wv.WidevineOptIn().finalize(session, checks_result(res))
        self.assertEqual(out.status, "fail")

    def test_finalize_keeps_a_pass(self):
        session = SimpleNamespace(netlog=[self.rec("a.qjz9zk")])
        out = wv.WidevineOptIn().finalize(session, checks_result(SimpleNamespace(
            status="pass", detail="ok", name="widevine_opt_in", data={})))
        self.assertEqual(out.status, "pass")


def checks_result(ns):
    from aerie_bench.session import CheckResult
    return CheckResult(ns.name, ns.status, ns.detail, data=ns.data)


class ScriptedSession:
    """evaluate() answers from a queue; variant() hands out a second scripted session."""

    def __init__(self, answers, variant=None):
        self.answers, self.variant_session, self.variants = list(answers), variant, []
        self.fixtures = SimpleNamespace(port=1)
        self.navigated = []

    def navigate(self, path):
        self.navigated.append(path)

    def navigate_url(self, url, wait=None):
        self.navigated.append(url)
        return {"error": None}

    def evaluate(self, expression, await_promise=False, user_gesture=False):
        return self.answers.pop(0)

    def wait_for(self, expression, timeout=15.0, interval=0.25):
        return True

    def variant(self, extra_flags=(), prefs=None):
        self.variants.append(prefs)
        outer = self

        class Ctx:
            def __enter__(self_):
                return outer.variant_session

            def __exit__(self_, *a):
                return False
        return Ctx()


class ObserveTest(unittest.TestCase):
    def setUp(self):
        old, wv.SETTLE_SECONDS = wv.SETTLE_SECONDS, 0
        self.addCleanup(setattr, wv, "SETTLE_SECONDS", old)

    def test_control_variant_is_seeded_with_the_local_state_opt_in(self):
        main = ScriptedSession([{"available": False, "error": "NotSupportedError"}, "CRLSet Subresource Filter"])
        control = ScriptedSession(["Widevine Content Decryption Module 0.0.0.0"])
        main.variant_session = control
        obs = wv.WidevineOptIn().observe(main)
        self.assertEqual(obs["main"], {"key_system": {"available": False, "error": "NotSupportedError"},
                                       "listed": False})
        self.assertEqual(obs["control"], {"listed": True})
        self.assertEqual(main.variants, [{profile.LOCAL_STATE_KEY: {"aerie": {"widevine": {"enabled": True}}}}])
        self.assertNotIn("online", obs)

    def test_online_option_asks_the_opted_in_browser_for_widevine(self):
        main = ScriptedSession([{"available": False}, "CRLSet"])
        control = ScriptedSession(["Widevine", {"available": True}])
        main.variant_session = control
        obs = wv.WidevineOptIn(online_seconds=1).observe(main)
        self.assertEqual(obs["online"], {"available": True})


class RegistryTest(unittest.TestCase):
    def test_registered_with_a_negative_control_and_no_services_seed(self):
        cls = checks.get("widevine_opt_in")
        self.assertFalse(cls.seeds_helium_services)
        self.assertGreater(len(cls.negative_control), 30)


class LocalStateSeedTest(unittest.TestCase):
    def test_local_state_key_goes_to_local_state_not_preferences(self):
        with tempfile.TemporaryDirectory() as root:
            with profile.fresh_profile(Path(root)) as p:
                p.seed_prefs({"a": 1, profile.LOCAL_STATE_KEY: {"aerie": {"widevine": {"enabled": True}}}})
                prefs = json.loads((p.path / "Default" / "Preferences").read_text())
                state = json.loads((p.path / "Local State").read_text())
        self.assertEqual(prefs, {"a": 1})
        self.assertEqual(state, {"aerie": {"widevine": {"enabled": True}}})

    def test_no_local_state_file_unless_asked(self):
        with tempfile.TemporaryDirectory() as root:
            with profile.fresh_profile(Path(root)) as p:
                p.seed_prefs({"a": 1})
                self.assertFalse((p.path / "Local State").exists())


REG_OK = """void RegisterComponentsForUpdate() {
#if BUILDFLAG(ENABLE_WIDEVINE_CDM_COMPONENT)
  // Aerie: Widevine is opt-in.
  aerie_widevine::RegisterWhenEnabled(
      g_browser_process->local_state(),
      base::BindRepeating(&RegisterWidevineCdmComponent, cus));
#endif  // BUILDFLAG(ENABLE_WIDEVINE_CDM_COMPONENT)
}
"""
CFG_OK = """std::vector<GURL> ChromeConfigurator::UpdateUrl() const {
  return {aerie_widevine::UpdateUrl(g_browser_process->local_state(),
                                    helium::GetComponentUpdateURL(GetProfilePrefs()))};
}

std::vector<GURL> ChromeConfigurator::PingUrl() const {
  return {aerie_widevine::UpdateUrl(g_browser_process->local_state(),
                                    helium::GetComponentUpdateURL(GetProfilePrefs()))};
}
"""
REG = "chrome/browser/component_updater/registration.cc"
CFG = "chrome/browser/component_updater/chrome_component_updater_configurator.cc"


class GateTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)

    def write(self, rel, text):
        p = self.root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)

    def test_gate_is_in_the_run_list(self):
        self.assertIn(treegate.check_widevine_opt_in, treegate.GATES)

    def test_good_tree_passes(self):
        self.write(REG, REG_OK)
        self.write(CFG, CFG_OK)
        self.assertEqual(treegate.check_widevine_opt_in(self.root, require=True), [])

    def test_unconditional_registration_fails(self):
        self.write(REG, REG_OK + "\nvoid F() { RegisterWidevineCdmComponent(cus); }\n")
        out = treegate.check_widevine_opt_in(self.root)
        self.assertEqual([f.gate for f in out], ["widevine_opt_in"])
        self.assertIn("unconditionally", out[0].message)

    def test_missing_gate_call_fails(self):
        self.write(REG, "void RegisterComponentsForUpdate() {}\n")
        self.assertEqual(len(treegate.check_widevine_opt_in(self.root)), 1)

    def test_update_url_must_go_through_the_opt_in_for_both_urls(self):
        self.write(CFG, CFG_OK.replace("aerie_widevine::UpdateUrl", "Id", 1))
        out = treegate.check_widevine_opt_in(self.root)
        self.assertEqual([f.gate for f in out], ["widevine_opt_in"])

    def test_missing_files_skipped_unless_required(self):
        self.assertEqual(treegate.check_widevine_opt_in(self.root), [])
        self.assertEqual(len(treegate.check_widevine_opt_in(self.root, require=True)), 2)


if __name__ == "__main__":
    unittest.main()
