"""The workspace_isolation check (issue #73), against a fake browser with Workspaces."""

import re
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from aerie_bench.checks import REGISTRY
from aerie_bench.checks import workspace_isolation as wi
from aerie_bench.session import CheckResult

from tests.workspace_fakes import ALL_LEAKS, FakeBrowser, FakeSession

REPO = Path(__file__).resolve().parent.parent
# Which leak each probe has to notice. A leak the fake cannot show on the sockets probe is covered below.
LEAK_TO_PROBE = {
    "cookie": "cookies",
    "localStorage": "dom_storage", "sessionStorage": "dom_storage", "indexedDB": "dom_storage",
    "opfs": "dom_storage", "broadcastChannel": "dom_storage", "webLocks": "dom_storage",
    "sharedWorker": "dom_storage", "blobUrl": "dom_storage",
    "cacheStorage": "cache_storage", "serviceWorker": "service_workers", "httpCache": "http_cache",
    "httpAuth": "http_auth", "hsts": "hsts", "permissions": "permissions", "history": "history",
    "visited": "history", "process": "renderer_process", "move": "tab_move", "external": "external_link",
    "noopener": "noopener", "dns": "dns", "clientCert": "client_cert",
}


def run(browser, enforce=True, profile=None):
    check = wi.WorkspaceIsolation(enforce=enforce, driver_factory=lambda session, site: browser)
    session = FakeSession(browser, profile)
    browser.code_cache_hook = (lambda ws: None) if profile is None else _code_cache_hook(profile, browser)
    res = check.run(session)
    return check, session, res


def _code_cache_hook(profile, browser):
    def hook(ws):
        if "codeCache" in browser.leaks:
            ws = "shared"
        (Path(profile) / "Storage" / "ext" / "localhost" / ws / "Code Cache").mkdir(parents=True, exist_ok=True)
    return hook


class CorrectBrowserTest(unittest.TestCase):
    def test_every_probe_passes_on_a_browser_that_isolates_everything(self):
        with tempfile.TemporaryDirectory() as tmp:
            _, _, res = run(FakeBrowser(), profile=tmp)
        bad = {i: p["detail"] for i, p in res.data["probes"].items() if p["raw"] == "fail" and i != "sockets"}
        self.assertEqual(bad, {})

    def test_the_check_is_registered(self):
        self.assertIs(REGISTRY["workspace_isolation"], wi.WorkspaceIsolation)
        self.assertTrue(REGISTRY["workspace_isolation"].negative_control)


# A shared cookie jar or localStorage also breaks the probes that log two users in.
LOGIN_PROBES = {"two_logins", "tab_move"}
ALSO_FAILS = {"cookie": LOGIN_PROBES | {"external_link", "noopener"}, "localStorage": LOGIN_PROBES}


class EveryLeakIsCaughtTest(unittest.TestCase):
    def test_each_leak_fails_its_probe_and_no_unrelated_probe(self):
        for leak, probe in LEAK_TO_PROBE.items():
            with self.subTest(leak=leak), tempfile.TemporaryDirectory() as tmp:
                _, _, res = run(FakeBrowser(leaks=(leak,)), profile=tmp)
                failed = {i for i, p in res.data["probes"].items() if p["raw"] == "fail" and i != "sockets"}
                self.assertEqual(failed, {probe} | ALSO_FAILS.get(leak, set()), res.data["probes"][probe]["detail"])

    def test_every_leak_the_fake_can_show_is_mapped(self):
        self.assertEqual(set(LEAK_TO_PROBE), set(ALL_LEAKS))

    def test_a_dom_storage_leak_names_the_surface(self):
        _, _, res = run(FakeBrowser(leaks=("webLocks",)))
        self.assertIn("webLocks", res.data["probes"]["dom_storage"]["detail"])

    def test_a_shared_code_cache_directory_fails_the_code_cache_probe(self):
        with tempfile.TemporaryDirectory() as tmp:
            _, _, res = run(FakeBrowser(leaks=()), profile=tmp)
            self.assertEqual(res.data["probes"]["code_cache"]["raw"], "pass")
        with tempfile.TemporaryDirectory() as tmp:
            browser = FakeBrowser()
            browser.leaks = {"codeCache"}  # not a state leak the other probes read
            _, _, res = run(browser, profile=tmp)
            self.assertEqual(res.data["probes"]["code_cache"]["raw"], "fail")

    def test_no_profile_path_fails_the_code_cache_probe(self):
        _, _, res = run(FakeBrowser())
        self.assertEqual(res.data["probes"]["code_cache"]["raw"], "fail")


class NegativeControlTest(unittest.TestCase):
    def test_a_probe_that_cannot_read_its_own_state_fails_loudly(self):
        class Blind(FakeBrowser):
            def open(self, url, ws):
                tab = super().open(url, ws)
                tab.w_get = lambda surface: ""      # reads nothing, ever
                return tab
        _, _, res = run(Blind())
        cookies = res.data["probes"]["cookies"]
        self.assertEqual(cookies["raw"], "fail")
        self.assertIn("negative control failed", cookies["detail"])

    def test_no_pids_fails_the_renderer_probe_as_a_control_failure(self):
        _, _, res = run(FakeBrowser(pids=False))
        self.assertIn("negative control failed", res.data["probes"]["renderer_process"]["detail"])

    def test_a_cache_that_never_caches_fails_the_control(self):
        class NoCache(FakeBrowser):
            def open(self, url, ws):
                tab = super().open(url, ws)
                tab.w_fetchCached = lambda ident, delay: (self.requests.append({"path": f"/ws/cached?id={ident}"})
                                                          or {"ms": delay + 5.0, "text": ident})
                return tab
        _, _, res = run(NoCache())
        self.assertIn("negative control failed", res.data["probes"]["http_cache"]["detail"])


class ExpectedToFailTest(unittest.TestCase):
    def test_every_probe_is_expected_to_fail_for_now(self):
        self.assertEqual(wi.EXPECTED_TO_FAIL, set(wi.PROBE_IDS))

    def test_no_driver_is_an_expected_failure_not_an_error(self):
        _, _, res = run(FakeBrowser(available="chrome://workspace-test is not served"), enforce=False)
        self.assertEqual(res.status, "xfail")
        self.assertTrue(res.ok)
        self.assertIn("expected to fail", res.detail)
        self.assertEqual({p["status"] for p in res.data["probes"].values()}, {"xfail"})
        self.assertIn("not served", res.data["probes"]["cookies"]["detail"])

    def test_no_driver_fails_for_real_when_enforced(self):
        _, _, res = run(FakeBrowser(available="not served"), enforce=True)
        self.assertEqual(res.status, "fail")
        self.assertFalse(res.ok)

    def test_a_probe_that_passes_while_marked_is_an_unexpected_pass(self):
        _, _, res = run(FakeBrowser(leaks=ALL_LEAKS), enforce=False)
        self.assertEqual(res.status, "xfail")  # everything but sockets fails, sockets is judged later
        _, _, res = run(FakeBrowser(leaks=("cookie",)), enforce=False)
        self.assertEqual(res.status, "fail")
        self.assertFalse(res.ok)
        self.assertIn("xpass", res.detail)
        self.assertEqual(res.data["probes"]["history"]["status"], "xpass")

    def test_a_failure_in_an_unmarked_probe_fails_the_run(self):
        original = wi.EXPECTED_TO_FAIL
        try:
            wi.EXPECTED_TO_FAIL = frozenset(set(original) - {"cookies"})
            _, _, res = run(FakeBrowser(leaks=("cookie",)), enforce=False)
        finally:
            wi.EXPECTED_TO_FAIL = original
        self.assertEqual(res.status, "fail")
        self.assertIn("failed and not expected to: cookies", res.detail)

    def test_with_the_marker_set_empty_a_clean_browser_passes(self):
        original = wi.EXPECTED_TO_FAIL
        try:
            wi.EXPECTED_TO_FAIL = frozenset()
            _, _, res = run(FakeBrowser(), enforce=False)
        finally:
            wi.EXPECTED_TO_FAIL = original
        self.assertEqual(res.data["probes"]["cookies"]["status"], "pass")

    def test_result_statuses(self):
        self.assertTrue(CheckResult("x", "xfail").ok)
        self.assertFalse(CheckResult("x", "xpass").ok)


def record(url, sockets):
    return {"url": url, "sockets": sockets}


class SocketsTest(unittest.TestCase):
    A = [record(f"http://localhost:1/ws/ping?w=ws1", [10])] * 3
    B = [record(f"http://localhost:1/ws/ping?w=ws2", [20])] * 3

    def test_separate_sockets_reused_inside_each_workspace_pass(self):
        res = wi.judge_sockets(self.A + self.B, True)
        self.assertTrue(res.ok, res.detail)

    def test_a_socket_carrying_both_workspaces_fails(self):
        mixed = self.B[:2] + [record("http://localhost:1/ws/ping?w=ws2", [10])]
        res = wi.judge_sockets(self.A + mixed, True)
        self.assertFalse(res.ok)
        self.assertIn("[10]", res.detail)

    def test_no_reuse_at_all_is_a_control_failure_not_a_pass(self):
        a = [record("http://localhost:1/ws/ping?w=ws1", [n]) for n in (1, 2, 3)]
        res = wi.judge_sockets(a + self.B, True)
        self.assertFalse(res.ok)
        self.assertIn("negative control failed", res.detail)

    def test_missing_socket_info_and_an_incomplete_log_fail(self):
        self.assertFalse(wi.judge_sockets([record("http://localhost:1/ws/ping?w=ws1", [])] * 3 + self.B, True).ok)
        self.assertFalse(wi.judge_sockets(self.A + self.B, False).ok)
        self.assertFalse(wi.judge_sockets([], True).ok)

    def test_finalize_judges_the_sockets_probe_from_the_netlog(self):
        check, session, res = run(FakeBrowser(), enforce=True)
        self.assertEqual(res.data["probes"]["sockets"]["raw"], "fail")  # not judged yet
        session.netlog = self.A + self.B
        done = check.finalize(session, res)
        self.assertEqual(done.data["probes"]["sockets"]["raw"], "pass")
        session.netlog = self.A + self.B[:2] + [record("http://localhost:1/ws/ping?w=ws2", [10])]
        check2, session2, res2 = run(FakeBrowser(), enforce=True)
        session2.netlog = session.netlog
        self.assertEqual(check2.finalize(session2, res2).data["probes"]["sockets"]["raw"], "fail")


class AdrCoverageTest(unittest.TestCase):
    """Every row of ADR 0006's shared-state table that says "split" has a probe, or a stated reason."""

    @staticmethod
    def split_rows():
        text = (REPO / "docs" / "adr" / "0006-workspace-isolation.md").read_text()
        table = text[text.index("## Shared-state verdicts"):text.index("## Gap G8")]
        rows = []
        for line in table.splitlines():
            cells = [c.strip() for c in line.strip().strip("|").split(" | ")]
            if len(cells) < 3 or not re.fullmatch(r"\d+[a-z]?", cells[0]):
                continue
            verdict = cells[2]
            if re.search(r"\bsplit\b", verdict, re.I) and not verdict.startswith("**Accepted"):
                rows.append(cells[0])
        return rows

    def test_the_adr_table_was_read(self):
        rows = self.split_rows()
        self.assertIn("1", rows)
        self.assertIn("11", rows)
        self.assertIn("12", rows)
        self.assertNotIn("13", rows)   # accepted
        self.assertNotIn("16", rows)   # n/a

    def test_every_split_row_has_a_probe_or_a_waiver(self):
        covered = {row for p in wi.PROBES for row in p.adr_rows} | set(wi.WAIVED_ROWS)
        self.assertEqual([r for r in self.split_rows() if r not in covered], [])

    def test_a_waiver_is_not_also_a_probe_and_names_a_split_row(self):
        probed = {row for p in wi.PROBES for row in p.adr_rows}
        self.assertEqual(probed & set(wi.WAIVED_ROWS), set())
        self.assertLessEqual(set(wi.WAIVED_ROWS), set(self.split_rows()))

    def test_probes_only_name_rows_that_exist(self):
        text = (REPO / "docs" / "adr" / "0006-workspace-isolation.md").read_text()
        for p in wi.PROBES:
            for row in p.adr_rows:
                self.assertRegex(text, rf"\n\| {row} \|", p.id)

    def test_no_split_row_is_waived(self):
        self.assertEqual(wi.WAIVED_ROWS, {})
        self.assertIn("7", self.split_rows())
        self.assertIn("10", self.split_rows())

    def test_the_issue_checks_are_all_present(self):
        ids = set(wi.PROBE_IDS)
        for needed in ("cookies", "dom_storage", "cache_storage", "service_workers", "http_cache", "hsts",
                       "http_auth", "permissions", "sockets", "tab_move", "external_link", "history"):
            self.assertIn(needed, ids)

    def test_the_docs_name_every_probe_and_every_waiver(self):
        doc = (REPO / "docs" / "bench.md").read_text()
        for pid in wi.PROBE_IDS:
            self.assertIn(f"`{pid}`", doc)
        for row in wi.WAIVED_ROWS:
            self.assertIn(f"row {row}", doc)


class FlagsTest(unittest.TestCase):
    def test_flags_carry_the_test_switch_the_certificate_pin_and_localhost_hsts(self):
        server = SimpleNamespace(enable_tls=lambda: "PIN=")
        flags = wi.WorkspaceIsolation().browser_flags(server)
        self.assertEqual(flags, (wi.TEST_FLAG, "--ignore-certificate-errors-spki-list=PIN=",
                                 "--disable-features=IgnoreHSTSForLocalhost"))

    def test_without_tls_the_hsts_probe_says_why(self):
        def broken():
            raise RuntimeError("no openssl")
        check = wi.WorkspaceIsolation(enforce=True, driver_factory=lambda s, site: FakeBrowser())
        self.assertEqual(check.browser_flags(SimpleNamespace(enable_tls=broken)), (wi.TEST_FLAG,))
        browser = FakeBrowser()
        session = FakeSession(browser)
        del session.fixtures.tls_port
        res = check.run(session)
        self.assertIn("no openssl", res.data["probes"]["hsts"]["detail"])


if __name__ == "__main__":
    unittest.main()


class RunnerIntegrationTest(unittest.TestCase):
    """The check through the real runner, with a browser that has no Workspaces (today's builds)."""

    FAKE = Path(__file__).parent / "bench_fake_browser.py"

    def run_bench(self, *argv):
        import contextlib
        import io
        import json
        from unittest import mock
        from aerie_bench import cli
        with tempfile.TemporaryDirectory() as root, \
                mock.patch("aerie_bench.workspace_driver.ChromeDriver.available",
                           return_value="chrome://workspace-test is not served"):
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                code = cli.main(["--build-root", root, "--binary", str(self.FAKE), "--timeout", "10",
                                 "--check", "workspace_isolation", *argv])
            results = sorted((Path(root) / "bench").glob("*/results.json"))[0]
            data = json.loads(results.read_text())
            return code, out.getvalue(), data, data["flags"]

    def test_today_the_check_is_expected_to_fail_and_the_run_is_green(self):
        code, out, data, args = self.run_bench()
        self.assertEqual(code, 0)
        self.assertIn("XFAIL  workspace_isolation", out)
        self.assertEqual(data["status"], "pass")
        self.assertEqual(data["checks"][0]["status"], "xfail")
        self.assertIn(wi.TEST_FLAG, args)

    def test_enforcing_it_fails_the_run(self):
        code, out, data, _ = self.run_bench("--workspaces-enforce")
        self.assertEqual(code, 1)
        self.assertEqual(data["checks"][0]["status"], "fail")

    @unittest.skipUnless(__import__("shutil").which("openssl"), "needs openssl")
    def test_the_browser_gets_the_certificate_pin_of_the_https_fixture(self):
        _, _, _, args = self.run_bench()
        self.assertTrue(any(a.startswith("--ignore-certificate-errors-spki-list=") for a in args), args)
