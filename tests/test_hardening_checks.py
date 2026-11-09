"""Hardening checks: judge() against recorded observations, plus driver glue against a scripted session."""

import contextlib
import json
import re
import shutil
import subprocess
import unittest
from pathlib import Path

from aerie_bench import checks
from aerie_bench.checks.hardening import common, cookies, extension, fingerprint, fingerprint_popup, fingerprint_site, fingerprint_worker, https, isolation, jit, prefetch, settings, webrtc
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
        self.assertEqual(len(HARDENING), 21)
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
    def test_every_check_with_a_real_recording_that_is_judged_as_recorded(self):
        got = {}
        for check, case in load_cases():
            if case["name"].startswith("real_train"):
                got[check] = case
                self.assertEqual(HARDENING[check].judge(case["obs"])[0], case["expect"], check)
        self.assertEqual(len(got), 13)  # hardening_prefetch errored in that run: nothing to record

    def test_a_case_claiming_a_real_recording_holds_no_hand_written_rows(self):
        # patch 0016 and its contentSettings probes came after the train-1 run: only a recording from train 13b
        # (the first build with 0016) or later can hold contentSettings rows
        for check, case in load_cases():
            roles = {r.get("role", "") for r in case["obs"].get("results", []) if isinstance(r, dict)}
            if "real" in case:
                with self.subTest(check=check, case=case["name"]):
                    self.assertTrue(case["name"].startswith("real_train"), "only a recording may carry a real note")
                    if not case["name"].startswith("real_train13b_"):
                        self.assertFalse({r for r in roles if r.startswith("content_setting")},
                                         "contentSettings rows under a recording older than the probes")

    def test_extension_prefs_cases_with_content_settings_rows_say_they_are_hand_written(self):
        for check, case in load_cases():
            if check != "hardening_extension_prefs":
                continue
            has_cs = any(r.get("role", "").startswith("content_setting") for r in case["obs"]["results"])
            with self.subTest(case=case["name"]):
                if has_cs and case["name"].startswith("real_train13b_"):
                    self.assertIn("0016", case["real"])
                elif has_cs:
                    self.assertNotIn("real", case)
                    self.assertIn("0016", case.get("handwritten", ""))
                    self.assertIn("real", case["handwritten"].lower())

    def test_the_train_13b_recordings_judge_the_cookie_probes(self):
        cases = {c["name"]: c for ch, c in load_cases() if ch == "hardening_extension_prefs"}
        judge = HARDENING["hardening_extension_prefs"].judge
        with_0016 = cases["real_train13b_build"]["obs"]
        self.assertEqual(judge(with_0016)[0], "pass")
        self.assertTrue(any(r["role"] == "content_setting_refused" and r["set_error"] for r in with_0016["results"]))
        status, detail = judge(cases["real_train13b_main_without_0016"]["obs"])[:2]
        self.assertEqual(status, "fail")
        self.assertIn("could allow cookies through contentSettings", detail)

    def test_the_train_1_extension_recording_predates_the_cookie_probes(self):
        case = real_case("hardening_extension_prefs")
        self.assertEqual({r["role"] for r in case["obs"]["results"]}, {"hardened", "control"})
        self.assertEqual(HARDENING["hardening_extension_prefs"].judge(case["obs"])[0], "fail")

    def test_recordings_hold_no_lan_address(self):
        for _, case in load_cases():
            self.assertNotIn("192.168.1.107", json.dumps(case))


class SettingsLinkRowTest(unittest.TestCase):
    def obs(self, **link):
        base = next(c["obs"] for ch, c in load_cases()
                    if ch == "hardening_settings_page" and c["name"].startswith("real_train"))
        obs = json.loads(json.dumps(base))
        obs["links"] = {"js_optimizer": {"clicked": True, "reached": True, **link}}
        return obs

    def test_link_that_opens_its_page_passes(self):
        self.assertEqual(settings.SettingsPage.judge(self.obs())[0], "pass")

    def test_link_whose_page_never_appears_fails(self):
        status, detail = settings.SettingsPage.judge(self.obs(reached=False))
        self.assertEqual(status, "fail")
        self.assertIn("js_optimizer", detail)
        self.assertIn("never appeared", detail)

    def test_missing_link_row_to_click_fails(self):
        status, detail = settings.SettingsPage.judge(self.obs(clicked=False, reached=False))
        self.assertEqual(status, "fail")
        self.assertIn("no row to click", detail)

    def test_link_with_no_click_result_fails(self):
        obs = self.obs()
        obs["links"] = {}
        self.assertEqual(settings.SettingsPage.judge(obs)[0], "fail")

    def test_follow_link_clicks_the_row_and_waits_for_the_target_page(self):
        class Fake:
            def __init__(self, clicked, reached):
                self.clicked, self.reached, self.scripts = clicked, reached, []

            def evaluate(self, js):
                self.scripts.append(js)
                return self.clicked

            def wait_for(self, js, timeout=0):
                self.scripts.append(js)
                return self.reached

        control = next(c for c in settings.table()["controls"] if c["id"] == "js_optimizer")
        ok = Fake(True, True)
        self.assertEqual(settings.follow_link(ok, control), {"clicked": True, "reached": True})
        self.assertIn("#control-js_optimizer-setting", ok.scripts[0])
        self.assertIn("settings-v8-page", ok.scripts[1])
        self.assertEqual(settings.follow_link(Fake(True, None), control), {"clicked": True, "reached": False})
        self.assertEqual(settings.follow_link(Fake(False, None), control), {"clicked": False, "reached": False})


class FingerprintJudgeTest(unittest.TestCase):
    def obs(self, **changes):
        obs = json.loads(json.dumps(REAL_FINGERPRINT))
        obs["a2"]["audio"] = obs["a1"]["audio"]  # what a build with stable audio noise gives
        for key in ("a1", "a2", "b1"):
            obs[key]["audio_again"] = obs[key]["audio"]
        for path, value in changes.items():
            kind, key = path.split("__")
            obs[kind][key] = value
        return obs

    def test_a_missing_second_audio_render_fails(self):
        obs = self.obs()
        del obs["b1"]["audio_again"]
        status, detail = fingerprint.Fingerprinting.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("no second audio render", detail)

    def test_audio_that_changes_every_render_fails(self):
        # The train-1 recording: Helium salted the noise with a render counter.
        status, detail = fingerprint.Fingerprinting.judge(REAL_FINGERPRINT)
        self.assertEqual(status, "fail")
        self.assertIn("audio changed between two loads", detail)

    def test_stable_audio_passes(self):
        status, detail = fingerprint.Fingerprinting.judge(self.obs())
        self.assertEqual(status, "pass", detail)
        self.assertNotIn("not stable", detail)

    def test_two_audio_renders_in_one_page_must_match(self):
        obs = self.obs()
        obs["a1"]["audio_again"] = "00000000"
        status, detail = fingerprint.Fingerprinting.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("audio changed between two renders in one page", detail)

    def test_canvas_must_stay_stable_within_a_session(self):
        obs = self.obs()
        obs["a2"]["canvas"] = "00000000"
        status, detail = fingerprint.Fingerprinting.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("canvas changed", detail)

    def test_audio_must_still_differ_across_sites_and_sessions(self):
        obs = self.obs()
        obs["b1"]["audio"] = obs["b1"]["audio_again"] = obs["a1"]["audio"]
        self.assertIn("audio is the same on two different sites", fingerprint.Fingerprinting.judge(obs)[1])
        obs = self.obs()
        obs["next_session"]["a"]["audio"] = obs["a1"]["audio"]
        self.assertIn("audio is the same in a new browser session", fingerprint.Fingerprinting.judge(obs)[1])

    def test_audio_equal_to_the_no_noise_control_fails(self):
        obs = self.obs()
        for key in ("a1", "a2"):
            obs[key]["audio"] = obs[key]["audio_again"] = obs["control"]["a"]["audio"]
        self.assertIn("equals the no-noise control", fingerprint.Fingerprinting.judge(obs)[1])


class FingerprintSiteJudgeTest(unittest.TestCase):
    def case(self, name):
        doc = json.loads((FIXTURES / "hardening_fingerprinting_site.json").read_text())
        return next(c for c in doc["cases"] if c["name"] == name)

    def test_a_passing_observation_passes(self):
        status, detail = fingerprint_site.FingerprintingSite.judge(self.case("keyed_on_the_site_and_off_switch_site_wide")["obs"])
        self.assertEqual(status, "pass", detail)

    def test_negative_control_origin_keyed_values_fail_the_same_site_step(self):
        # An origin-keyed build gives x.a and y.a different seeds: everything else in the run is fine.
        obs = json.loads(json.dumps(self.case("keyed_on_the_site_and_off_switch_site_wide")["obs"]))
        obs["regular"]["y.a"] = {"canvas": "c7", "audio": "u7", "audio_again": "u7", "cores": "6"}
        self.assertNotEqual(obs["regular"]["x.a"]["canvas"], obs["regular"]["y.a"]["canvas"])
        status, detail = fingerprint_site.FingerprintingSite.judge(obs)
        self.assertEqual(status, "fail")
        self.assertIn("x.a.localhost and y.a.localhost, which are one site", detail)

    def test_the_recorded_origin_keyed_case_fails_the_same_site_step(self):
        case = self.case("origin_keying_gives_two_hosts_of_one_site_different_values")
        status, detail = fingerprint_site.FingerprintingSite.judge(case["obs"])
        self.assertEqual((status, case["expect"]), ("fail", "fail"))
        self.assertIn("which are one site", detail)


class FingerprintSiteDriverTest(unittest.TestCase):
    def test_seeded_exceptions_are_stored_as_primary_comma_secondary(self):
        prefs = fingerprint_site.seeded_exceptions({"http://[*.]a.localhost": 2, "http://x.a.localhost:4100": 2})
        rules = prefs["profile"]["content_settings"]["exceptions"]["helium_noise"]
        self.assertEqual(rules, {"http://[*.]a.localhost,*": {"setting": 2}, "http://x.a.localhost:4100,*": {"setting": 2}})

    def test_seeded_exceptions_use_the_pref_name_with_an_underscore(self):
        prefs = fingerprint_site.seeded_exceptions({fingerprint_site.SITE_PATTERN: 2})
        exceptions = prefs["profile"]["content_settings"]["exceptions"]
        self.assertEqual(list(exceptions), ["helium_noise"])
        self.assertIn("http://[*.]a.localhost,*", exceptions["helium_noise"])

    def test_turning_a_site_off_calls_the_site_settings_handler_on_the_settings_page(self):
        class Fake:
            def __init__(self):
                self.urls, self.scripts = [], []

            def navigate_url(self, url):
                self.urls.append(url)
                return {"url": url, "error": None}

            def evaluate(self, js):
                self.scripts.append(js)
                return True
        fake = Fake()
        self.assertTrue(fingerprint_site.turn_off_for_site(fake))
        self.assertEqual(fake.urls, ["chrome://settings/"])
        self.assertIn("setCategoryPermissionForPattern", fake.scripts[0])
        self.assertIn('"http://[*.]a.localhost"', fake.scripts[0])
        self.assertIn("'helium-noise', 'block', false", fake.scripts[0])

    def test_the_site_pattern_carries_the_scheme_of_the_fixture_site(self):
        # The key is scheme plus eTLD+1 and the fixture serves plain http, so the switch must be http-only.
        self.assertEqual(fingerprint_site.SITE_PATTERN, "http://[*.]a.localhost")

    def test_the_fixture_page_reports_the_core_count(self):
        text = (Path(fingerprint_site.__file__).parents[2] / "fixtures" / "hardening" / "fingerprint.html").read_text()
        self.assertIn("hardwareConcurrency", text)


class FingerprintWorkerFixtureTest(unittest.TestCase):
    """The page and the workers must measure the same thing, or a worker/window difference means nothing."""

    DIR = Path(fingerprint_worker.__file__).parents[2] / "fixtures" / "hardening"

    def test_the_page_measures_the_window_a_dedicated_worker_and_a_shared_worker(self):
        page = (self.DIR / "fingerprint_worker.html").read_text()
        self.assertIn("new Worker('fingerprint_worker.js')", page)
        self.assertIn("new SharedWorker('fingerprint_worker.js')", page)
        self.assertIn("measureContext(document.createElement('canvas'))", page)
        self.assertIn("fingerprint_measure.js", page)

    def test_the_worker_script_serves_both_worker_kinds_with_the_shared_measurement(self):
        script = (self.DIR / "fingerprint_worker.js").read_text()
        self.assertIn("importScripts('fingerprint_measure.js')", script)
        self.assertIn("onconnect", script)
        self.assertIn("OffscreenCanvas", script)

    def test_the_measurement_reads_pixels_and_the_core_count_and_draws_no_text(self):
        measure = (self.DIR / "fingerprint_measure.js").read_text()
        self.assertIn("getImageData", measure)
        self.assertIn("navigator.hardwareConcurrency", measure)
        self.assertNotIn("fillText", measure)  # fonts could differ between a window and a worker

    def test_the_check_turns_protection_off_for_the_site_and_for_the_whole_browser(self):
        class Fake:
            def __init__(self):
                self.fixtures = FakeFixtures()
                self.variants = []

            @contextlib.contextmanager
            def variant(self, flags=(), prefs=None):
                self.variants.append((flags, prefs))
                yield self

            def navigate_url(self, url, wait=None):
                return {"url": url, "error": None}

            def wait_for(self, expr, timeout=0):
                return True

            def evaluate(self, js):
                ctx = {"canvas": "c", "cores": 4}
                return {"window": ctx, "dedicated": ctx, "shared": ctx}
        fake = Fake()
        fingerprint_worker.FingerprintingWorker().observe(fake)
        flags = [v[0] for v in fake.variants]
        self.assertIn(("--disable-features=HeliumNoise",), flags)
        prefs = [v[1] for v in fake.variants if v[1]]
        self.assertIn({"helium": {"noise": {"enabled": False}}}, prefs)
        self.assertTrue(any(fingerprint_site.SITE_PATTERN + ",*" in
                            p.get("profile", {}).get("content_settings", {}).get("exceptions", {}).get("helium_noise", {})
                            for p in prefs))


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

    def variant(self, flags=(), prefs=None, profile=None):
        self.variant_calls.append((tuple(flags), prefs))
        self.variant_profiles = getattr(self, "variant_profiles", []) + [profile]
        return contextlib.nullcontext(self.variants[len(self.variant_calls) - 1])

    def new_profile(self):
        self.profile = object()
        return contextlib.nullcontext(self.profile)

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
                self.rules = set()

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
                if "setCategoryPermissionForPattern" in expression:
                    self.sent.append(expression)
                    pattern = expression.split("chrome.send")[1].split('"')[1]
                    self.rules.add(pattern.replace("[*.]", "").replace(".localhost", ""))
                    return True
                if "getPref" in expression:
                    return {"value": 1}
                if "deepQuery" in expression:
                    return True
                def speed(host):
                    if host in self.rules:
                        return slow  # measured on a real build: a subdomain rule applies at once
                    return slow if (self.mode == "off" and host == "a") else fast

                host = self.url.split("//")[1].split(":")[0].replace(".localhost", "") if "localhost" in self.url else ""
                own = speed(host)
                if "child=" in self.url:
                    child = self.url.split("child=")[1].split(".")[0]
                    return {**own, "child": speed(child)}
                return own

        first, restarted, control = JitSession("on"), JitSession("off"), JitSession("on")
        added = JitSession("on")
        main = JitSession("on", variants=[first, restarted, added, control])
        res = jit.JsOptimizer().run(main)
        self.assertEqual(res.status, "pass", res.detail)
        self.assertEqual(len(first.sent), 1)
        self.assertIn("'javascript-optimizer'", first.sent[0])
        self.assertIn("a.localhost", first.sent[0])
        self.assertIn('"block"', first.sent[0])
        self.assertEqual(main.variant_calls, [((), None)] * 4)
        # the change and the restart share one profile; the Add site rules and the control get their own
        profiles = main.variant_profiles
        self.assertIsNotNone(profiles[0])
        self.assertEqual(profiles[0], profiles[1])
        self.assertIsNotNone(profiles[2])
        self.assertIsNone(profiles[3])
        # the Add site call writes a host pattern for the site, then one for a subdomain only
        self.assertEqual(len(added.sent), 2)
        self.assertIn("setCategoryPermissionForPattern", added.sent[0])
        self.assertIn("'javascript-optimizer'", added.sent[0])
        self.assertIn("[*.]a.localhost", added.sent[0])
        # the argument order the V8 page dialog sends: pattern, secondary pattern, type, value, incognito
        # (SiteSettingsHandler::HandleSetCategoryPermissionForPattern reads exactly these five)
        self.assertIn("[pattern, '', 'javascript-optimizer', value, false]", added.sent[0])
        self.assertTrue(added.sent[0].rstrip().endswith('("[*.]a.localhost", "block")'))
        # the second subdomain visit comes after leaving the page
        self.assertIn("about:blank", added.log)
        self.assertIn("s.b.localhost", added.sent[1])

    def test_probe_extension_has_the_content_settings_permission_and_tries_cookies(self):
        root = Path(extension.__file__).parents[2] / "fixtures" / "hardening" / "extension"
        manifest = json.loads((root / "manifest.json").read_text())
        self.assertIn("contentSettings", manifest["permissions"])
        script = (root / "probe.js").read_text()
        self.assertIn("chrome.contentSettings.cookies.set", script)
        self.assertIn("'allow'", script)
        self.assertIn("secondaryPattern: site", script)

    def test_extension_check_loads_the_probe_extension_in_a_variant(self):
        results = next(c for c in json.loads((FIXTURES / "hardening_extension_prefs.json").read_text())["cases"]
                       if c["name"] == "real_train13b_build")["obs"]["results"]
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


class FingerprintPopupTest(unittest.TestCase):
    DIR = Path(fingerprint_worker.__file__).parents[2] / "fixtures" / "hardening"

    def test_the_page_reads_four_surfaces_through_the_window_it_opens(self):
        page = (self.DIR / "fingerprint_popup.html").read_text()
        self.assertIn("window.open('', '_blank', FEATURES)", page)
        for needle in ("w.document.createElement('canvas')", "UNMASKED_RENDERER_WEBGL", "w.navigator.deviceMemory",
                       "w.screen.width", "window.opener.postMessage"):
            self.assertIn(needle, page)

    def test_the_check_runs_four_browsers_with_the_same_window_size_and_gestures(self):
        calls = []

        class Fake:
            def __init__(self):
                self.fixtures = FakeFixtures()

            @contextlib.contextmanager
            def variant(self, flags=(), prefs=None):
                calls.append((flags, prefs))
                yield self

            def navigate_url(self, url, wait=None):
                return {"url": url, "error": None}

            def wait_for(self, expr, timeout=0):
                return True

            def evaluate(self, js, await_promise=False, user_gesture=False):
                if js == "window.__result":
                    return {"self": {"canvas": "c", "renderer": "r", "memory": 8, "screen": "1x1"}}
                self.opened = getattr(self, "opened", []) + [(js, user_gesture)]
                return {"canvas": "c", "renderer": "r", "memory": 8, "screen": "1x1"}
        fake = Fake()
        fingerprint_popup.FingerprintingPopup().observe(fake)
        self.assertEqual(len(calls), 4)
        self.assertTrue(all(fingerprint_popup.WINDOW_FLAG in flags for flags, _ in calls))
        self.assertIn(("--disable-features=HeliumNoise",), [tuple(f for f in flags if f != fingerprint_popup.WINDOW_FLAG) for flags, _ in calls])
        self.assertTrue(fake.opened and all(gesture for _, gesture in fake.opened), "window.open needs a user gesture")
