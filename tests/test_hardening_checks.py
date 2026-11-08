"""Hardening checks: judge() against recorded observations, plus driver glue against a scripted session."""

import contextlib
import json
import re
import shutil
import subprocess
import unittest
from pathlib import Path

from aerie_bench import checks
from aerie_bench.checks.hardening import common, cookies, extension, fingerprint, https, isolation, jit, prefetch, settings, webrtc
from aerie_bench.session import CheckResult

FIXTURES = Path(__file__).parent / "fixtures" / "hardening"
HARDENING = {n: c for n, c in checks.REGISTRY.items() if n.startswith("hardening_")}


def load_cases():
    for path in sorted(FIXTURES.glob("*.json")):
        doc = json.loads(path.read_text())
        for c in doc["cases"]:
            yield doc["check"], c


class JudgeAgainstRecordedObservationsTest(unittest.TestCase):
    def test_every_recorded_case(self):
        count = 0
        for check, case in load_cases():
            with self.subTest(check=check, case=case["name"]):
                status, detail = HARDENING[check].judge(case["obs"])
                self.assertEqual(status, case["expect"], detail)
                self.assertIn(case["detail_contains"], detail)
                count += 1
        self.assertGreater(count, 60)

    def test_every_check_has_a_pass_case_and_failing_control_cases(self):
        by_check: dict[str, list[dict]] = {}
        for check, case in load_cases():
            by_check.setdefault(check, []).append(case)
        self.assertEqual(set(by_check), set(HARDENING), "every hardening check needs recorded observations")
        for check, cases in by_check.items():
            self.assertTrue(any(c["expect"] == "pass" for c in cases), check)
            fails = [c for c in cases if c["expect"] == "fail"]
            self.assertGreaterEqual(len(fails), 2, check)
            self.assertTrue(any("negative control failed" in c["detail_contains"] or "detector failed" in c["detail_contains"]
                                or "did not load" in c["detail_contains"] or "negative control" in c["name"]
                                or c["name"].startswith(("control", "detector", "hints_inert", "bogus"))
                                for c in fails), f"{check}: no case where the negative control fails")

    def test_a_case_where_the_negative_control_fails_never_passes(self):
        for check, case in load_cases():
            if "negative control failed" in case["detail_contains"]:
                self.assertEqual(case["expect"], "fail", f"{check}/{case['name']}")


class RegistryTest(unittest.TestCase):
    def test_the_hardening_scenario_runs_every_hardening_check(self):
        from aerie_bench import scenario
        sc = scenario.load(Path(__file__).resolve().parent.parent / "tools" / "aerie_bench" / "scenarios" / "hardening.json")
        self.assertEqual(sorted(sc.checks), sorted(HARDENING))

    def test_checks_declare_control_and_negative_control(self):
        self.assertEqual(len(HARDENING), 14)
        for name, cls in HARDENING.items():
            self.assertTrue(cls.control, name)
            self.assertGreater(len(cls.negative_control), 30, name)
            self.assertFalse(cls.seeds_helium_services, name)

    def test_run_is_observe_then_judge(self):
        class Fake(common.HardeningCheck):
            name = "x"
            def observe(self, session):
                return {"v": 1}
            @staticmethod
            def judge(obs):
                return "pass", f"v={obs['v']}"
        res = Fake().run(None)
        self.assertEqual((res.status, res.detail, res.data), ("pass", "v=1", {"v": 1}))


class HelperTest(unittest.TestCase):
    def test_classify(self):
        self.assertEqual(common.classify(None, {"interstitial": True}), "interstitial")
        self.assertEqual(common.classify("net::ERR_X", {"interstitial": False}), "net_error")
        self.assertEqual(common.classify(None, {"title": common.FIXTURE_TITLE}), "loaded")
        self.assertEqual(common.classify(None, {"title": "other"}), "other")
        self.assertEqual(common.classify(None, None), "other")

    def test_parse_candidate_and_local_addresses(self):
        c = webrtc.parse_candidate("candidate:1 1 udp 2113937151 192.168.1.5 5000 typ host generation 0")
        self.assertEqual(c, {"address": "192.168.1.5", "type": "host"})
        self.assertEqual(webrtc.parse_candidate("a=candidate:1 1 udp 1 93.184.216.34 9 typ srflx")["type"], "srflx")
        self.assertEqual(webrtc.parse_candidate("garbage"), {"address": "", "type": ""})
        for local in ("10.0.0.1", "172.16.4.4", "192.168.0.9", "169.254.1.1", "127.0.0.1", "fe80::1", "fd00::5",
                      "abc.local"):
            self.assertTrue(webrtc.is_local_address(local), local)
        for public in ("93.184.216.34", "8.8.8.8", "2001:db8::1" if False else "2606:4700::1111", "not-an-ip"):
            self.assertFalse(webrtc.is_local_address(public), public)

    def test_a_host_candidate_is_a_leak_only_if_its_address_is_local(self):
        self.assertEqual(webrtc.leaks(["candidate:1 1 udp 1 93.184.216.34 9 typ host"]), [])
        self.assertEqual(len(webrtc.leaks(["candidate:1 1 udp 1 10.0.0.4 9 typ host"])), 1)
        self.assertEqual(len(webrtc.leaks(["candidate:1 1 udp 1 abc.local 9 typ host"])), 1)
        self.assertEqual(webrtc.leaks(["candidate:1 1 udp 1 127.0.0.1 9 typ srflx"]), [])
        self.assertEqual(len(webrtc.leaks(["candidate:1 1 udp 1 192.168.1.2 9 typ srflx"])), 1)
        self.assertEqual(webrtc.leaks(["candidate:1 1 udp 1 93.184.216.34 9 typ srflx"]), [])

    def test_cookie_verdict(self):
        fp = {"cookie": "tp=1", "echo": "tp=1"}
        self.assertEqual(cookies.cookie_verdict({"first_party": fp, "third_party": {"cookie": "", "echo": ""}}), "blocked")
        self.assertEqual(cookies.cookie_verdict({"first_party": fp, "third_party": {"cookie": "tp=1", "echo": ""}}), "allowed")
        self.assertEqual(cookies.cookie_verdict({"first_party": {"cookie": ""}, "third_party": {}}), "inconclusive")
        self.assertEqual(cookies.cookie_verdict({"first_party": fp, "third_party": {"error": "x"}}), "inconclusive")

    def test_extension_id_matches_the_manifest_key(self):
        import base64
        import hashlib
        manifest = json.loads((Path(extension.__file__).parents[2] / "fixtures" / "hardening" / "extension"
                               / "manifest.json").read_text())
        digest = hashlib.sha256(base64.b64decode(manifest["key"])).hexdigest()[:32]
        self.assertEqual("".join(chr(ord("a") + int(ch, 16)) for ch in digest), extension.EXTENSION_ID)
        self.assertIn("privacy", manifest["permissions"])

    def test_default_label(self):
        tbl = settings.table()
        by_id = {c["id"]: c for c in tbl["controls"]}
        self.assertEqual(settings.default_label(by_id["https_only"]), "Always use secure connections")
        self.assertEqual(settings.default_label(by_id["fingerprinting"]), "on")
        self.assertIsNone(settings.default_label(by_id["site_isolation"]))

    def test_pref_mismatches_names_the_pref(self):
        tbl = settings.table()
        probe = {"values": {"webrtc.ip_handling_policy": "default"}, "missing": ["net.network_prediction_options"]}
        bad = settings.pref_mismatches(tbl, probe)
        self.assertTrue(any(b.startswith("webrtc.ip_handling_policy") for b in bad))
        self.assertTrue(any("net.network_prediction_options: not readable" in b for b in bad))


@unittest.skipUnless(shutil.which("node"), "node is not installed")
class ProbeScriptsAreValidJavascriptTest(unittest.TestCase):
    def check(self, source):
        # Wrap in a function so a top-level `return`-less expression and `await` are both fine to parse.
        proc = subprocess.run(["node", "--check", "-"], input=f"async function __f() {{ {source} }}",
                              capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stderr[:500] + "\n" + source[:300])

    def test_scripts(self):
        self.check("return " + common.INTERSTITIAL_JS)
        self.check("return " + https.CLICK_PROCEED_JS)
        self.check(common.DEEP_JS + "return deepQueryAll(document, '.x').map(e => e.dataset.host)")
        self.check(common.DEEP_JS + settings.LIST_ROWS_JS % json.dumps(["a", "b"]))
        self.check("return " + settings.ATTACK_JS)
        self.check("return " + prefetch.FETCH_CONTROLS_JS % (prefetch.CONTROL_PATH, prefetch.PROBE_ABORT_MS, 1234))
        probe_js = "return " + ("""(async (names) => { return names; })(%s)""" % json.dumps(["a"]))
        self.check(probe_js)

    def test_prefetch_control_fetch_does_not_wait_for_the_silent_probe(self):
        # The probe listener accepts and never answers. A fetch stub that only settles on abort
        # models it: the script must still finish, and only because it aborts the fetch itself.
        script = f"""
globalThis.location = {{protocol: 'http:'}};
const seen = [];
globalThis.fetch = (url, opts = {{}}) => new Promise((resolve, reject) => {{
  seen.push(url);
  if (!url.includes('127.0.0.1')) return resolve({{}});
  if (!opts.signal) return;  // would hang forever
  opts.signal.addEventListener('abort', () => reject(new Error('aborted')));
}});
const started = Date.now();
const timeout = new Promise((_, rej) => setTimeout(() => rej(new Error('hung')), 5000));
Promise.race([{prefetch.FETCH_CONTROLS_JS % (prefetch.CONTROL_PATH, 100, 1234)}, timeout])
  .then(ok => {{ console.log(JSON.stringify({{ok, seen, ms: Date.now() - started}})); }},
        e => {{ console.log(JSON.stringify({{error: e.message}})); process.exit(1); }});
"""
        proc = subprocess.run(["node", "-e", script], capture_output=True, text=True, timeout=20)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        out = json.loads(proc.stdout)
        self.assertTrue(out["ok"])
        self.assertEqual(out["seen"], [prefetch.CONTROL_PATH, "http://127.0.0.1:1234"])
        self.assertGreaterEqual(out["ms"], 90)

    def test_prefetch_abort_is_about_one_second(self):
        self.assertEqual(prefetch.PROBE_ABORT_MS, 1000)

    def test_fixture_pages_and_extension_script(self):
        root = Path(extension.__file__).parents[2] / "fixtures" / "hardening"
        for page in sorted(root.glob("*.html")) + [root / "extension" / "probe.js"]:
            text = page.read_text()
            scripts = [text] if page.suffix == ".js" else re.findall(
                r"<script(?![^>]*speculationrules)[^>]*>(.*?)</script>", text, re.S)
            for s in scripts:
                if s.strip():
                    self.check(s)

    def test_speculation_rules_are_valid_json(self):
        text = (Path(extension.__file__).parents[2] / "fixtures" / "hardening" / "prefetch.html").read_text()
        rules = json.loads(re.search(r'<script type="speculationrules">(.*?)</script>', text, re.S).group(1))
        self.assertEqual(set(rules), {"prefetch", "prerender"})


def real_case(check):
    for _, case in load_cases():
        if _ == check and case["name"].startswith("real_train"):
            return case
    raise AssertionError(f"no recorded real case for {check}")


# Observed on a real build (train-1 Bench run, hardening-1/results.json; the settings-page case is from train 4).
REAL_FINGERPRINT = real_case("hardening_fingerprinting")["obs"]


class RealRecordingsTest(unittest.TestCase):
    def test_twelve_checks_have_a_real_recording_that_is_judged_as_recorded(self):
        got = {}
        for check, case in load_cases():
            if case["name"].startswith("real_train"):
                got[check] = case
                self.assertEqual(HARDENING[check].judge(case["obs"])[0], case["expect"], check)
        self.assertEqual(len(got), 12)  # hardening_prefetch errored in that run: nothing to record

    def test_recordings_hold_no_lan_address(self):
        for _, case in load_cases():
            self.assertNotIn("192.168.1.107", json.dumps(case))


class FingerprintJudgeTest(unittest.TestCase):
    def obs(self, **changes):
        obs = json.loads(json.dumps(REAL_FINGERPRINT))
        for path, value in changes.items():
            kind, key = path.split("__")
            obs[kind][key] = value
        return obs

    def test_real_audio_that_changes_every_render_passes(self):
        status, detail = fingerprint.Fingerprinting.judge(self.obs())
        self.assertEqual(status, "pass", detail)
        self.assertIn("#38", detail)

    def test_canvas_must_stay_stable_within_a_session(self):
        obs = self.obs()
        obs["a2"]["canvas"] = "00000000"
        status, detail = fingerprint.Fingerprinting.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("canvas changed", detail)

    def test_audio_must_still_differ_across_sites_and_sessions(self):
        obs = self.obs()
        obs["b1"]["audio"] = obs["a1"]["audio"]
        self.assertIn("audio is the same on two different sites", fingerprint.Fingerprinting.judge(obs)[1])
        obs = self.obs()
        obs["next_session"]["a"]["audio"] = obs["a1"]["audio"]
        self.assertIn("audio is the same in a new browser session", fingerprint.Fingerprinting.judge(obs)[1])

    def test_audio_equal_to_the_no_noise_control_fails(self):
        obs = self.obs()
        obs["a2"]["audio"] = obs["control"]["a"]["audio"]
        self.assertIn("equals the no-noise control", fingerprint.Fingerprinting.judge(obs)[1])


class FakeFixtures:
    def __init__(self):
        self.port = 41000
        self.root = Path("/fixtures")
        self.requests = []

    def snapshot(self):
        return list(self.requests)


class FakeSession:
    """Scripted stand-in for Session. ``pages`` maps a URL fragment to what the page 'does'."""

    def __init__(self, pages, variants=None):
        self.fixtures = FakeFixtures()
        self.pages = pages
        self.variants = variants or {}
        self.current = None
        self.log = []
        self.variant_calls = []

    def navigate_url(self, url, wait=None):
        self.log.append(url)
        for frag, page in self.pages.items():
            if frag in url:
                self.current = page
                if page.get("on_navigate"):
                    page["on_navigate"](self)
                return {"url": url, "error": page.get("error")}
        self.current = {}
        return {"url": url, "error": "net::ERR_NAME_NOT_RESOLVED"}

    def evaluate(self, expression, await_promise=False, user_gesture=False):
        page = self.current or {}
        if expression.startswith("(() => {\n  const proceed"):
            return page.get("state", {"title": "", "interstitial": False})
        if "#proceed-button" in expression and "click()" in expression:
            return page.get("clickable", False) and (page["on_click"](self) or True)
        if "window.__result" in expression or "window.__state" in expression:
            return page.get("result")
        return page.get("eval")

    def wait_for(self, expression, timeout=15.0, interval=0.25):
        if expression.startswith("document.title ==="):
            return (self.current or {}).get("loads_after_click", False)
        return True

    def variant(self, flags=(), prefs=None):
        self.variant_calls.append((tuple(flags), prefs))
        return contextlib.nullcontext(self.variants[len(self.variant_calls) - 1])

    def targets(self):
        return (self.current or {}).get("targets", [])

    def stun_probe(self):
        import types
        return contextlib.nullcontext(types.SimpleNamespace(port=3478, requests=1))


class DriverGlueTest(unittest.TestCase):
    def test_https_only_observe_and_judge_on_a_strict_browser(self):
        state = {"title": "Connection not secure", "interstitial": True, "type": "HTTPS_ONLY",
                 "proceed_visible": True, "href": "http://plain.bench.test:41000/index.html"}
        fake = FakeSession({"plain.bench.test": {"state": state, "clickable": True, "loads_after_click": True}})

        def on_click(s):
            s.fixtures.requests.append({"host": "plain.bench.test:41000", "path": "/index.html"})
        fake.pages["plain.bench.test"]["on_click"] = on_click
        with unittest.mock.patch("time.sleep"):
            res = https.HttpsOnly().run(fake)
        self.assertEqual(res.status, "pass", res.detail)
        self.assertEqual(res.data["hits_before_click"], 0)
        self.assertEqual(res.data["hits_after_click"], 1)

    def test_https_only_on_a_browser_that_loads_http_fails(self):
        page = {"state": {"title": common.FIXTURE_TITLE, "interstitial": False}}
        fake = FakeSession({"plain.bench.test": page})
        with unittest.mock.patch("time.sleep"):
            res = https.HttpsOnly().run(fake)
        self.assertEqual(res.status, "fail")
        self.assertIn("strict HTTPS-only mode is not on", res.detail)

    def test_localhost_dev_observe_covers_every_name(self):
        loaded = {"state": {"title": common.FIXTURE_TITLE, "interstitial": False, "href": "http://x/"}}
        strict = {"state": {"title": "x", "interstitial": True, "type": "HTTPS_ONLY"}}
        refused = {"error": "net::ERR_CONNECTION_REFUSED", "state": {"title": "", "interstitial": False}}
        fake = FakeSession({"strict.bench.test": strict, "127.0.0.2": refused, "[::1]": refused,
                            "localhost": loaded, "127.0.0.1": loaded})
        fake.fixtures.requests = [{"host": f"{h}:41000", "path": "/index.html"}
                                  for h in ("localhost", "dev.localhost", "127.0.0.1")]
        res = https.LocalhostDev().run(fake)
        self.assertEqual(res.status, "pass", res.detail)
        self.assertEqual(sorted(res.data["served"]), ["127.0.0.1", "dev.localhost", "localhost"])
        self.assertEqual(sorted(res.data["unserved"]), ["127.0.0.2", "[::1]"])

    def test_cookie_check_runs_the_control_in_a_second_browser(self):
        def pages(third):
            return {"cookie_top": {"result": {"cookie": "tp=1", "echo": "tp=1"}},
                    "cookie_frame": {"result": third}}
        main = FakeSession(pages({"cookie": "", "echo": ""}),
                           variants=[FakeSession(pages({"cookie": "tp=1", "echo": "tp=1"}))])
        res = cookies.ThirdPartyCookies().run(main)
        self.assertEqual(res.status, "pass", res.detail)
        self.assertEqual(main.variant_calls, [((), cookies.ALLOW_THIRD_PARTY_PREFS)])

    def test_webrtc_check_uses_leaky_flags_in_the_control(self):
        raw = "candidate:1 1 udp 2113937151 192.168.1.23 51890 typ host generation 0"
        srflx = "candidate:2 1 udp 1 127.0.0.1 40001 typ srflx raddr 0.0.0.0 rport 0"
        main = FakeSession({"webrtc.html": {"result": {"state": "complete", "candidates": [srflx]}}},
                           variants=[FakeSession({"webrtc.html": {"result": {"state": "complete", "candidates": [raw]}}})])
        res = webrtc.WebRtc().run(main)
        self.assertEqual(res.status, "pass", res.detail)
        self.assertEqual(main.variant_calls[0], (webrtc.LEAKY_FLAGS, webrtc.LEAKY_PREFS))
        self.assertIn("stun=", main.log[0])

    def test_site_isolation_control_disables_isolation(self):
        frame = {"result": {"ready": True}, "targets": [{"type": "iframe", "url": "http://b.localhost:41000/x"}]}
        control = {"result": {"ready": True}, "targets": []}
        main = FakeSession({"embed.html": frame}, variants=[FakeSession({"embed.html": control})])
        res = isolation.SiteIsolation().run(main)
        self.assertEqual(res.status, "pass", res.detail)
        self.assertEqual(main.variant_calls[0][0], ("--disable-site-isolation-trials",))

    def test_js_optimizer_check_changes_the_setting_restarts_and_runs_a_control(self):
        fast, slow = {"ms": 30.0}, {"ms": 300.0}

        # The page results depend on the host and on the mode; script them per phase.
        class JitSession(FakeSession):
            def __init__(self, mode, variants=()):
                super().__init__({}, variants=list(variants))
                self.mode, self.browser_pid = mode, None
                self.sent = []
                self.restarted = 0

            def navigate_url(self, url, wait=None):
                self.log.append(url)
                self.url = url
                return {"url": url, "error": None}

            def wait_for(self, expression, timeout=15.0, interval=0.25):
                return True

            def evaluate(self, expression, await_promise=False, user_gesture=False):
                if "setOriginPermissions" in expression:
                    self.sent.append(expression)
                    self.mode = "off"
                    return True
                if "getPref" in expression:
                    return {"value": 1}
                if "deepQuery" in expression:
                    return True
                host = self.url.split("//")[1].split(":")[0].split(".")[0] if "localhost" in self.url else ""
                own = slow if (self.mode == "off" and host == "a") else fast
                if "child=" in self.url:
                    child = self.url.split("child=")[1].split(".")[0]
                    kid = slow if (self.mode == "off" and child == "a") else fast
                    return {**own, "child": kid}
                return own

        class Box:
            def __init__(self, session):
                self.session = session
                self.restarts = 0

            def restart(self):
                self.restarts += 1
                return self.session

        main = JitSession("on")
        box_session = JitSession("on")
        control = JitSession("on")
        main.variants = [control]
        main.restartable = lambda flags=(), prefs=None: contextlib.nullcontext(Box(box_session))
        res = jit.JsOptimizer().run(main)
        self.assertEqual(res.status, "pass", res.detail)
        self.assertEqual(len(box_session.sent), 1)
        self.assertIn("'javascript-optimizer'", box_session.sent[0])
        self.assertIn("a.localhost", box_session.sent[0])
        self.assertIn('"block"', box_session.sent[0])
        self.assertEqual(main.variant_calls, [((), None)])

    def test_extension_check_loads_the_probe_extension_in_a_variant(self):
        results = json.loads((FIXTURES / "hardening_extension_prefs.json").read_text())["cases"][0]["obs"]["results"]
        variant = FakeSession({"probe.html": {"eval": results}})
        main = FakeSession({}, variants=[variant])
        res = extension.ExtensionPrefs().run(main)
        self.assertEqual(res.status, "pass", res.detail)
        flags = main.variant_calls[0][0]
        self.assertTrue(flags[0].startswith("--load-extension=") and flags[0].endswith("/hardening/extension"))
        self.assertIn(extension.EXTENSION_ID, variant.log[0])


import unittest.mock  # noqa: E402  (used above through unittest.mock.patch)

if __name__ == "__main__":
    unittest.main()
