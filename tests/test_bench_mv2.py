"""The mv2_extension check: a tiny Manifest V2 extension installs, runs and blocks, and uBlock Origin is gone."""

import json
import unittest
from pathlib import Path

from aerie_bench import fixtures, runner, scenario
from aerie_bench.checks import REGISTRY, mv2_extension as mv2

REPO = Path(__file__).resolve().parents[1]
EXT = REPO / "tools" / "aerie_bench" / "extensions" / "mv2"
FAKE = Path(__file__).parent / "bench_fake_browser.py"
BASE = "http://127.0.0.1:41233"
EXT_ID = "a" * 32


def ev(method, **params):
    return {"method": method, "params": params}


GOOD_EVENTS = [
    ev("Network.requestWillBeSent", requestId="1", request={"url": BASE + "/mv2/ext-block-me.js"}, type="Script"),
    ev("Network.loadingFailed", requestId="1", type="Script", errorText="net::ERR_BLOCKED_BY_CLIENT", canceled=False),
    ev("Network.requestWillBeSent", requestId="2", request={"url": BASE + "/mv2/allowed.js"}, type="Script"),
    ev("Network.responseReceived", requestId="2", type="Script", response={"url": BASE + "/mv2/allowed.js", "status": 200}),
]
TARGETS = [{"type": "page", "url": BASE + "/mv2/index.html", "targetId": "P"},
           {"type": "background_page", "url": f"chrome-extension://{EXT_ID}/_generated_background_page.html",
            "targetId": "B"}]


PROBE_OK = {"ready": True, "attempts": 1, "seconds": 0.1}


def judge(**over):
    args = dict(probe=PROBE_OK, load={"id": EXT_ID}, load_error=None, marker="ran", allowed_ran=True,
                outcomes=mv2.collect(GOOD_EVENTS), targets=TARGETS, extension_cancels=[BASE + "/mv2/ext-block-me.js"])
    args.update(over)
    return mv2.judge(**args)


class FixtureExtensionTest(unittest.TestCase):
    def test_the_fixture_extension_is_manifest_v2_and_tiny(self):
        manifest = json.loads((EXT / "manifest.json").read_text())
        self.assertEqual(manifest["manifest_version"], 2)
        self.assertIn("webRequestBlocking", manifest["permissions"])
        self.assertTrue(manifest["background"]["persistent"])
        self.assertLess(sum(p.stat().st_size for p in EXT.rglob("*") if p.is_file()), 4096)

    def test_the_blocked_name_is_the_one_the_expectations_and_the_background_use(self):
        self.assertIn(mv2.BLOCKED_PATH.rsplit("/", 1)[1], (EXT / "background.js").read_text())
        self.assertIn(mv2.BLOCKED_PATH, (fixtures.FIXTURE_DIR / "mv2" / "index.html").read_text())
        self.assertIn(mv2.ALLOWED_PATH, (fixtures.FIXTURE_DIR / "mv2" / "index.html").read_text())

    def test_the_extension_content_script_sets_the_marker_the_check_reads(self):
        self.assertIn("data-aerie-mv2", (EXT / "content.js").read_text())

    def test_the_fixture_page_and_scripts_are_served(self):
        import urllib.request
        with fixtures.FixtureServer() as server:
            for path in ("/mv2/index.html", mv2.ALLOWED_PATH, mv2.BLOCKED_PATH):
                self.assertEqual(urllib.request.urlopen(server.base_url + path).status, 200, path)


class JudgeTest(unittest.TestCase):
    def test_a_working_mv2_extension_passes(self):
        status, detail, data = judge()
        self.assertEqual(status, "pass", detail)
        self.assertEqual(data["extension_id"], EXT_ID)

    def test_install_failure_is_reported_with_the_browsers_error(self):
        status, detail, _ = judge(load=None, load_error="Manifest version 2 is deprecated")
        self.assertEqual(status, "fail")
        self.assertIn("did not install", detail)
        self.assertIn("deprecated", detail)

    def test_content_script_that_did_not_run_fails(self):
        status, detail, _ = judge(marker=None)
        self.assertEqual(status, "fail")
        self.assertIn("content script", detail)

    def test_blocking_webrequest_that_did_not_block_fails(self):
        events = [dict(e) for e in GOOD_EVENTS]
        events[1] = ev("Network.loadingFailed", requestId="1", type="Script", errorText="net::ERR_NAME_NOT_RESOLVED")
        status, detail, _ = judge(outcomes=mv2.collect(events))
        self.assertEqual(status, "fail")
        self.assertIn("webRequest", detail)

    def test_the_allowed_script_must_load_and_run(self):
        status, detail, _ = judge(allowed_ran=False)
        self.assertEqual(status, "fail")
        self.assertIn("allowed", detail)

    def test_the_bundled_ublock_origin_must_be_gone(self):
        targets = TARGETS + [{"type": "service_worker", "url": f"chrome-extension://{mv2.UBO_COMPONENT_ID}/js/background.js"}]
        status, detail, _ = judge(targets=targets)
        self.assertEqual(status, "fail")
        self.assertIn("uBlock Origin", detail)

    def test_the_extension_itself_must_have_cancelled_the_request(self):
        # DevTools shows the request blocked, but the extension recorded nothing: someone else blocked it.
        for cancels in ([], None, [BASE + "/mv2/other.js"]):
            status, detail, _ = judge(extension_cancels=cancels)
            self.assertEqual(status, "fail", cancels)
            self.assertIn("the block was not the extension's", detail)

    def test_the_background_script_records_what_it_cancels(self):
        text = (EXT / "background.js").read_text()
        self.assertIn("aerieCancelled", text)
        self.assertIn("details.url", text)
        self.assertIn("aerieCancelled", (REPO / "tools/aerie_bench/checks/mv2_extension.py").read_text())

    def test_a_listener_that_never_took_effect_is_reported_as_such_not_as_a_missed_request(self):
        # The probe is a fetch from the extension's own background page. If even that is not
        # cancelled, the listener never reached the browser: that is not a proxy-ordering problem.
        status, detail, data = judge(probe={"ready": False, "attempts": 40, "seconds": 10.0})
        self.assertEqual(status, "fail")
        self.assertIn("never took effect", detail)
        self.assertEqual(data["listener_probe"]["attempts"], 40)

    def test_a_cancel_of_the_probe_url_does_not_count_as_blocking_the_page_request(self):
        probe_url = BASE + "/mv2/ext-block-me.js?" + mv2.PROBE_QUERY
        status, detail, _ = judge(extension_cancels=[probe_url])
        self.assertEqual(status, "fail")
        self.assertIn("the block was not the extension's", detail)

    def test_the_background_script_offers_a_probe_and_a_ready_flag(self):
        text = (EXT / "background.js").read_text()
        self.assertIn("aerieProbe", text)
        self.assertIn("aerieReady", text)
        self.assertIn(mv2.PROBE_QUERY, (REPO / "tools/aerie_bench/checks/mv2_extension.py").read_text())

    def test_no_background_target_means_the_extension_did_not_start(self):
        status, detail, _ = judge(targets=[TARGETS[0]])
        self.assertEqual(status, "fail")
        self.assertIn("background", detail)


class RegistrationTest(unittest.TestCase):
    def test_registered_and_needs_no_launch_flag(self):
        self.assertIn("mv2_extension", REGISTRY)
        self.assertFalse(getattr(REGISTRY["mv2_extension"], "launch_flags", ()))

    def test_the_docs_and_the_check_do_not_mention_a_flag_this_chromium_lacks(self):
        for path in (REPO / "docs/bench.md", REPO / "tools/aerie_bench/checks/mv2_extension.py"):
            self.assertNotIn("enable-unsafe-extension-debugging", path.read_text(), path.name)


class EndToEndWithFakeBrowserTest(unittest.TestCase):
    def run_check(self, mode, **extra):
        import tempfile, shutil
        root = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: shutil.rmtree(root, ignore_errors=True))
        env = ({"FAKE_BROWSER_MV2": mode} if mode else {}) | extra
        res = runner.run_bench(FAKE, scenario.Scenario(checks=["mv2_extension"]), root, timeout=10, extra_env=env,
                               check_options={"mv2_extension": {"settle_seconds": 1.0, "probe_seconds": 1.0}})
        return next(c for c in res.data["checks"] if c["name"] == "mv2_extension")

    def test_a_browser_that_installs_and_runs_mv2_passes(self):
        check = self.run_check("works")
        self.assertEqual(check["status"], "pass", check["detail"])

    def test_the_check_waits_for_the_listener_before_loading_the_page(self):
        # A persistent background page registers its listener some time after the extension
        # loads. Navigating first (as the check once did) lets the page win that race.
        check = self.run_check("works", FAKE_BROWSER_MV2_PROBE_FAILS="2")
        self.assertEqual(check["status"], "pass", check["detail"])
        self.assertEqual(check["data"]["listener_probe"]["attempts"], 3)

    def test_a_listener_that_never_works_is_reported_distinctly(self):
        check = self.run_check("inert")
        self.assertEqual(check["status"], "fail")
        self.assertIn("never took effect", check["detail"])

    def test_a_browser_that_refuses_the_extension_fails_with_its_message(self):
        check = self.run_check("refuse")
        self.assertEqual(check["status"], "fail")
        self.assertIn("did not install", check["detail"])

    def test_a_browser_with_the_bundled_ubo_fails(self):
        check = self.run_check("with-ubo")
        self.assertEqual(check["status"], "fail")
        self.assertIn("uBlock Origin", check["detail"])


if __name__ == "__main__":
    unittest.main()
