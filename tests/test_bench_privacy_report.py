import contextlib
import copy
import io
import json
import tempfile
import re
import unittest
from pathlib import Path
from unittest import mock

from aerie_bench import cli, privacy_report as pr
from aerie_bench.runner import RunResult

DOC = (Path(__file__).resolve().parent.parent / "docs" / "network-endpoints.md").read_text()
REQUIRED = ("zero_traffic", "adblock_score", "hardening_https_only", "hardening_fingerprinting",
            "hardening_fingerprinting_site")


def req(url, phase_idx=0, net_error=None, **extra):
    from urllib.parse import urlsplit
    p = urlsplit(url)
    host = p.hostname or ""
    loop = host in ("127.0.0.1", "localhost")
    return {"url": url, "urls": [url], "method": "GET", "scheme": p.scheme, "host": host,
            "net_error": net_error, "external": not loop, **extra}


def clean_run():
    reqs = ([req("http://127.0.0.1:4000/index.html")] * 3          # first_launch
            + [req("http://127.0.0.1:4000/index.html")] * 1       # idle
            + [req("http://127.0.0.1:4000/browse-a.html")] * 2)   # browse
    return {
        "scenario": "privacy-report",
        "binary": {"path": "/home/alice/builds/aerie", "version": "Aerie 1.2.3 /home/alice/x"},
        "profile": {"path": "/home/alice/prof"},
        "network": {"requests": reqs, "external_requests": []},
        "exit": {"code": 0}, "status": "pass",
        "checks": [
            {"name": "zero_traffic", "status": "pass", "detail": "no request to a denylisted host",
             "data": {"phases": {"first_launch": 3, "idle": 4, "browse": 6}, "idle_seconds": 300.0,
                      "settle_seconds": 60.0, "refused": [], "refusal_counter": 0,
                      "refusal_counter_error": None, "hits": [], "crash_step": {"status": "pass"}}},
            {"name": "adblock_score", "status": "pass", "detail": "ok",
             "data": {"score": {"overall": {"blocked": 90, "total": 100},
                                "categories": {"ads": {"blocked": 40, "total": 50}}},
                      "hostlist": {"sha256": "ab" * 32, "date": "2026-01-01", "url": "https://x/y"}}},
            {"name": "hardening_https_only", "status": "pass", "detail": "ok", "data": {}},
            {"name": "hardening_fingerprinting", "status": "pass", "detail": "ok", "data": {}},
            {"name": "hardening_fingerprinting_site", "status": "pass", "detail": "ok", "data": {}},
        ],
    }


META = {"commit": "0123456789ab", "binary_sha256": "cd" * 32, "binary_name": "aerie",
        "command": "./aerie bench --report <dir>"}


def build(run, **kw):
    return pr.build(run, META, pr.load_allowlist(DOC), required=REQUIRED, **kw)


class GateTest(unittest.TestCase):
    def test_clean_recording_passes(self):
        rep = build(clean_run())
        self.assertTrue(rep["gate"]["passed"], rep["gate"])
        self.assertEqual(rep["requests"], [])

    def test_stray_request_fails_and_names_host(self):
        run = clean_run()
        run["network"]["requests"].insert(3, req("https://tracker.example.net/ping?id=1"))
        rep = build(run)
        self.assertFalse(rep["gate"]["passed"])
        self.assertTrue(any(re.search(r"(?<![\w.-])tracker\.example\.net(?![\w.-])", f) for f in rep["gate"]["failures"]))
        self.assertEqual(rep["requests"][0]["host"], "tracker.example.net")
        self.assertEqual(rep["requests"][0]["phase"], "idle")
        self.assertFalse(rep["requests"][0]["allowed"])

    def test_allowlisted_host_is_listed_with_reason_and_passes(self):
        run = clean_run()
        run["network"]["requests"].append(req("https://easylist.to/easylist/easylist.txt"))
        rep = build(run)
        self.assertTrue(rep["gate"]["passed"], rep["gate"])
        self.assertTrue(rep["requests"][0]["allowed"])
        self.assertIn("filter lists", rep["requests"][0]["reason"])

    def test_policy_refused_request_is_counted_not_a_stray(self):
        run = clean_run()
        run["network"]["requests"].append(req("https://evil.example.org/x", net_error=-22))
        run["checks"][0]["data"]["refused"] = [{"host": "evil.example.org", "phase": "browse", "net_error": -22}]
        run["checks"][0]["data"]["refusal_counter"] = 1
        rep = build(run)
        self.assertTrue(rep["gate"]["passed"], rep["gate"])
        self.assertEqual(rep["netpolicy"]["counter"], 1)
        self.assertEqual(rep["netpolicy"]["refused"][0]["host"], "evil.example.org")

    def test_failing_or_missing_check_fails_gate(self):
        run = clean_run()
        run["checks"][2]["status"] = "fail"
        self.assertFalse(build(run)["gate"]["passed"])
        run = clean_run()
        del run["checks"][1]
        rep = build(run)
        self.assertTrue(any("adblock_score" in f for f in rep["gate"]["failures"]))

    def test_short_idle_phase_fails_gate(self):
        run = clean_run()
        run["checks"][0]["data"]["idle_seconds"] = 20.0
        self.assertFalse(build(run)["gate"]["passed"])


class GateHolesTest(unittest.TestCase):
    def test_unknown_commit_or_version_fails(self):
        rep = pr.build(clean_run(), {**META, "commit": None}, pr.load_allowlist(DOC), required=REQUIRED)
        self.assertFalse(rep["gate"]["passed"])
        self.assertTrue(any("commit" in f for f in rep["gate"]["failures"]))
        run = clean_run()
        run["binary"]["version"] = None
        rep = build(run)
        self.assertTrue(any("version" in f for f in rep["gate"]["failures"]))

    def test_unreadable_refusal_counter_fails(self):
        run = clean_run()
        run["checks"][0]["data"]["refusal_counter"] = None
        rep = build(run)
        self.assertFalse(rep["gate"]["passed"])
        self.assertTrue(any("counter" in f for f in rep["gate"]["failures"]))

    def test_lookalike_host_is_not_allowed(self):
        run = clean_run()
        run["network"]["requests"].append(req("https://evileasylist.to/x"))
        self.assertFalse(build(run)["gate"]["passed"])


class RerunCommandTest(unittest.TestCase):
    def args(self, **over):
        a = cli.argparse.ArgumentParser()
        cli.add_arguments(a, binary_required=False)
        ns = a.parse_args([])
        for k, v in over.items():
            setattr(ns, k, v)
        return ns

    def test_default_build_binary(self):
        cmd = cli.rerun_command(self.args(report="/home/alice/out"), "aerie", explicit_binary=False)
        self.assertEqual(cmd, "./aerie bench --report <dir>")

    def test_flags_that_change_the_result_are_kept_and_paths_removed(self):
        cmd = cli.rerun_command(self.args(report="/home/alice/out", binary="/home/alice/b/aerie",
                                          no_sandbox=True, zero_traffic_settle=5.0, flag=["--foo=a b"],
                                          adblock_baseline="/home/alice/base.json"), "aerie", explicit_binary=True)
        self.assertEqual(cmd, "./aerie bench --report <dir> --binary ./aerie --no-sandbox "
                              "--zero-traffic-settle 5.0 --adblock-baseline <file> '--flag=--foo=a b'")
        self.assertNotIn("alice", cmd)


    def test_zero_valued_flags_are_kept(self):
        cmd = cli.rerun_command(self.args(report="out", zero_traffic_settle=0.0, adblock_online=0), "aerie", True)
        self.assertIn("--zero-traffic-settle 0.0", cmd)

    def test_path_like_flag_values_become_placeholders(self):
        cmd = cli.rerun_command(self.args(report="out", flag=["--user-data-dir=/data/p", "--x=/srv/x/y",
                                                              "--y=~/q", "--plain=1"]), "aerie", False)
        self.assertEqual(cmd, "./aerie bench --report <dir> --flag=--user-data-dir=<file> --flag=--x=<file> "
                              "--flag=--y=<file> --flag=--plain=1")


class GenericPathTest(unittest.TestCase):
    def test_unlisted_roots_are_scrubbed_from_text_but_url_paths_stay(self):
        run = clean_run()
        run["checks"][2]["detail"] = "profile at /data/p and /srv/x/y/z failed"
        run["network"]["requests"].append(req("https://easylist.to/ads/x/y.js"))
        meta = {**META, "command": "./aerie bench --report <dir> --flag=--a=/work/q/r"}
        rep = pr.build(run, meta, pr.load_allowlist(DOC), required=REQUIRED)
        for text in (pr.render_json(rep), pr.render_html(rep)):
            for bad in ("/data/p", "/srv/x", "/work/q"):
                self.assertNotIn(bad, text)
        self.assertEqual(rep["requests"][0]["path"], "/ads/x/y.js")

    def test_guard_names_any_absolute_path_and_spares_ordinary_text(self):
        with self.assertRaises(pr.ReportError):
            pr.check_local_path("/srv/x/y")
        pr.check_local_path("text/html and/or ./aerie <dir> https://a.b/c/d")

    def test_binary_name_goes_through_the_guard(self):
        meta = {**META, "binary_name": "/srv/x/aerie"}
        rep = pr.build(clean_run(), meta, pr.load_allowlist(DOC), required=REQUIRED)
        self.assertNotIn("/srv/x", pr.render_html(rep))


class ContentTest(unittest.TestCase):
    def test_content(self):
        rep = build(clean_run())
        self.assertEqual(rep["commit"], "0123456789ab")
        self.assertEqual([p["name"] for p in rep["phases"]], ["first_launch", "idle", "browse"])
        self.assertEqual([p["requests"] for p in rep["phases"]], [3, 1, 2])
        self.assertEqual([p["non_loopback"] for p in rep["phases"]], [0, 0, 0])
        self.assertEqual(rep["blocking"]["blocked"], 90)
        self.assertEqual(rep["blocking"]["percent"], 90.0)
        self.assertEqual(len(rep["fingerprinting"]), 2)
        self.assertEqual([c["name"] for c in rep["hardening"]], ["hardening_https_only"])
        self.assertEqual(rep["rerun"]["binary_sha256"], "cd" * 32)
        html = pr.render_html(rep)
        self.assertIn("./aerie bench --report", html)
        self.assertIn("&lt;dir&gt;", html)
        self.assertIn("cd" * 32, html)


class RedactionTest(unittest.TestCase):
    def dirty(self):
        run = clean_run()
        run["network"]["requests"].append(req(
            "https://easylist.to/a/b.txt?token=SECRET123&u=bob#frag", headers={"Cookie": "sid=COOKIEVAL"}))
        run["checks"][2]["detail"] = "failed at /home/alice/.config/aerie/Default with Cookie: sid=COOKIEVAL"
        run["checks"][2]["status"] = "fail"
        run["checks"][2]["data"] = {"profile": "/home/alice/prof", "cookie": "COOKIEVAL"}
        return run

    def test_no_leaks_in_html_or_json(self):
        rep = build(self.dirty(), forbidden=("/mnt/linux2/aerie-build",))
        for text in (pr.render_json(rep), pr.render_html(rep)):
            for bad in ("/home/", "alice", "SECRET123", "COOKIEVAL", "token=", "frag", "?"):
                self.assertNotIn(bad, text, bad)
        self.assertEqual(rep["requests"][0]["path"], "/a/b.txt")

    def test_guard_refuses_to_write_a_leak(self):
        with self.assertRaises(pr.ReportError):
            pr.check_clean("see /home/alice/x", ())
        with self.assertRaises(pr.ReportError):
            pr.check_clean("see /mnt/linux2/aerie-build/x", ("/mnt/linux2/aerie-build",))

    def test_html_is_static_and_escaped(self):
        run = clean_run()
        run["network"]["requests"].append(req("https://<script>alert(1)</script>.example/x"))
        run["checks"][2]["detail"] = "<img src=x onerror=alert(1)>"
        rep = build(run)
        html = pr.render_html(rep)
        self.assertNotIn("<script", html.lower())
        self.assertNotIn("<img", html)
        self.assertNotRegex(html, r"(src|href)=\"https?:")
        self.assertIn("Content-Security-Policy", html)


class CliTest(unittest.TestCase):
    def run_cli(self, run_data):
        out = Path(tempfile.mkdtemp()) / "rep"
        args = mock.Mock(report=str(out), binary=None, check=None, scenario=None, adblock_update_pin=None,
                         headed=False, no_sandbox=False, timeout=5.0, flag=[], out=None)
        for k, v in dict(services="auto", services_wait=1, zero_traffic_idle=300.0, zero_traffic_settle=60.0,
                         zero_traffic_strict=False, zero_traffic_no_crash=False, widevine_online=0.0,
                         adblock_baseline=None, adblock_online=0.0, workspaces_enforce=False, baseline=None,
                         perf_baseline=None, perf_record=None, perf_compare_binary=None, perf_first_run=False,
                         perf_commit=None, sidebar_frames_baseline=None, sidebar_frames_record=None,
                         sidebar_frames_first_run=False, sidebar_frames_repeats=5,
                         download_benchmarks=False).items():
            setattr(args, k, v)
        binary = Path(tempfile.mkdtemp()) / "aerie"
        binary.write_bytes(b"binary")
        seen = {}

        def fake(binary_, sc, root, **kw):
            seen["checks"] = sc.checks
            have = {c["name"] for c in run_data["checks"]}
            run_data["checks"] += [{"name": n, "status": "pass", "detail": "ok", "data": {}}
                                   for n in sc.checks if n not in have]
            d = Path(tempfile.mkdtemp())
            (d / "summary.txt").write_text("summary\n")
            return RunResult("pass", d, run_data)

        with mock.patch.object(cli, "run_bench", fake), contextlib.redirect_stdout(io.StringIO()):
            code = cli.run(args, Path(tempfile.mkdtemp()), binary)
        return code, out, seen

    def test_clean_writes_files_and_exits_zero(self):
        code, out, seen = self.run_cli(clean_run())
        self.assertEqual(code, 0)
        self.assertTrue((out / "report.html").is_file())
        data = json.loads((out / "report.json").read_text())
        self.assertTrue(data["gate"]["passed"])
        self.assertEqual(data["rerun"]["binary_sha256"],
                         __import__("hashlib").sha256(b"binary").hexdigest())
        self.assertIn("zero_traffic", seen["checks"])
        self.assertIn("adblock_score", seen["checks"])
        self.assertTrue(any(c.startswith("hardening_fingerprinting") for c in seen["checks"]))

    def test_stray_exits_nonzero_but_still_writes_report(self):
        run = clean_run()
        run["network"]["requests"].append(req("https://tracker.example.net/p"))
        code, out, _ = self.run_cli(run)
        self.assertEqual(code, 1)
        self.assertIn("tracker.example.net", (out / "report.html").read_text())
