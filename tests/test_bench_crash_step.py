"""The crash step of the zero_traffic check (issue #262): crash a browser on purpose, start it again, and
judge that the report stayed on the device and that no crash upload was made.

``judge`` is tested on good and bad observations; ``observe`` is run against the fake browser, which can be
told to misbehave in each way the step must catch."""

import struct
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from aerie_bench import crash_step, denylist, runner, scenario
from aerie_bench.crash_step import Observation

FAKE = Path(__file__).parent / "bench_fake_browser.py"


def good() -> Observation:
    dump = {"name": "0a1b2c3d-0000-4000-8000-000000000001.dmp", "mode": 0o600, "size": 300000}
    return Observation(
        started=True, crash_exit={"code": -5, "clean": False}, crash_dir="/p/home/.config/aerie/Crash Reports",
        handlers=[["/x/aerie_crashpad_handler", "--database=/p/home/.config/aerie/Crash Reports", "--monitor-self"]],
        dumps_after_crash=[dump], dumps_after_relaunch=[dump],
        dir_modes={".": 0o700, "pending": 0o700, "completed": 0o700}, completed=[],
        uploads_enabled=False, relaunch_exit={"code": 0, "clean": True},
        requests=[{"url": "http://127.0.0.1:1/index.html", "host": "127.0.0.1", "external": False, "scheme": "http"}],
        lookups=[], netlogs_complete={"crash": False, "relaunch": True})


class JudgeTest(unittest.TestCase):
    def setUp(self):
        self.dl = denylist.load_default()

    def verdict(self, obs):
        return crash_step.judge(obs, self.dl)

    def test_a_kept_report_and_no_upload_passes(self):
        status, reason, data = self.verdict(good())
        self.assertEqual(status, "pass", reason)
        self.assertEqual(data["reports_after_relaunch"], 1)
        self.assertEqual(data["report_modes"], ["0o600"])

    def test_the_report_never_holds_a_path_or_dump_contents(self):
        _, _, data = self.verdict(good())
        text = repr(data)
        self.assertNotIn("/p/home", text)
        self.assertNotIn("Crash Reports/", text)
        self.assertEqual(data["handler_switches"], [["--database", "--monitor-self"]])

    def test_a_browser_that_did_not_crash_proves_nothing(self):
        obs = good()
        obs.crash_exit = {"code": 0, "clean": True}
        self.assertIn("did not crash", self.verdict(obs)[1])

    def test_no_handler_found_fails(self):
        obs = good()
        obs.handlers = []
        status, reason, _ = self.verdict(obs)
        self.assertEqual(status, "fail")
        self.assertIn("handler process was not found", reason)

    def test_a_handler_with_an_upload_address_fails(self):
        for switch in ("--url=https://crash.helium.computer/crash", "--url"):
            obs = good()
            obs.handlers[0].append(switch)
            status, reason, _ = self.verdict(obs)
            self.assertEqual(status, "fail", switch)
            self.assertIn("upload address", reason)

    def test_an_upload_switch_that_is_on_or_unreadable_fails(self):
        for value in (True, None):
            obs = good()
            obs.uploads_enabled = value
            self.assertEqual(self.verdict(obs)[0], "fail", value)

    def test_a_crash_without_a_report_fails(self):
        obs = good()
        obs.dumps_after_crash = []
        obs.dumps_after_relaunch = []
        self.assertIn("left no local report", self.verdict(obs)[1])

    def test_a_report_deleted_at_startup_fails(self):
        obs = good()
        obs.dumps_after_relaunch = []
        self.assertIn("deleted when the browser started again", self.verdict(obs)[1])
        obs = good()
        obs.dumps_after_relaunch = [{"name": "other.dmp", "mode": 0o600, "size": 1}]
        self.assertIn("deleted when the browser started again", self.verdict(obs)[1])

    def test_a_report_that_others_can_read_fails(self):
        obs = good()
        obs.dumps_after_crash[0]["mode"] = 0o644
        self.assertIn("0600", self.verdict(obs)[1])

    def test_a_folder_that_others_can_read_fails(self):
        obs = good()
        obs.dir_modes["pending"] = 0o755
        self.assertIn("0700", self.verdict(obs)[1])

    def test_a_report_in_completed_fails(self):
        obs = good()
        obs.completed = ["x.dmp"]
        self.assertIn("handled as sent", self.verdict(obs)[1])

    def test_a_crash_request_in_the_netlog_fails(self):
        for url in ("https://crash.helium.computer/crash", "https://clients2.google.com/cr/report",
                    "https://example.org/crash-report"):
            obs = good()
            obs.requests.append({"url": url, "host": url.split("/")[2], "external": True, "scheme": "https"})
            status, reason, data = self.verdict(obs)
            self.assertEqual(status, "fail", url)
            self.assertIn("crash upload request", reason)
            self.assertTrue(data["crash_requests"])

    def test_a_crash_host_lookup_fails(self):
        obs = good()
        obs.lookups.append({"host": "crash.helium.computer", "event": "HOST_RESOLVER_IMPL_REQUEST"})
        self.assertEqual(self.verdict(obs)[0], "fail")

    def test_other_external_hosts_are_not_crash_requests(self):
        obs = good()
        obs.requests.append({"url": "https://example.org/", "host": "example.org", "external": True, "scheme": "https"})
        self.assertEqual(self.verdict(obs)[0], "pass")

    def test_a_relaunch_with_a_truncated_log_or_a_bad_exit_fails(self):
        obs = good()
        obs.netlogs_complete["relaunch"] = False
        self.assertIn("NetLog", self.verdict(obs)[1])
        obs = good()
        obs.relaunch_exit = {"code": 9}
        self.assertIn("exit cleanly", self.verdict(obs)[1])

    def test_a_step_that_could_not_run_fails(self):
        status, reason, _ = self.verdict(Observation(error="this session cannot start a second browser"))
        self.assertEqual(status, "fail")
        self.assertIn("could not run", reason)


class DiskTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def test_settings_dat_options_bit(self):
        for options, want in ((0, False), (1, True), (2, False), (3, True)):
            (self.root / "settings.dat").write_bytes(b"sdPC" + struct.pack("<II", 1, options) + bytes(28))
            self.assertIs(crash_step.read_uploads_enabled(self.root), want, options)

    def test_settings_dat_that_is_missing_or_not_crashpads_is_unknown(self):
        self.assertIsNone(crash_step.read_uploads_enabled(self.root))
        (self.root / "settings.dat").write_bytes(b"nope" + bytes(36))
        self.assertIsNone(crash_step.read_uploads_enabled(self.root))
        (self.root / "settings.dat").write_bytes(b"sd")
        self.assertIsNone(crash_step.read_uploads_enabled(self.root))

    def test_dumps_are_listed_by_name_mode_and_size_only(self):
        pending = self.root / "pending"
        pending.mkdir()
        (pending / "a.dmp").write_bytes(b"SECRET")
        (pending / "a.dmp").chmod(0o640)
        (pending / "a.meta").write_bytes(b"x")
        self.assertEqual(crash_step.list_dumps(self.root), [{"name": "a.dmp", "mode": 0o640, "size": 6}])
        self.assertEqual(crash_step.list_dumps(None), [])

    def test_the_crash_folder_is_found_under_the_profile_home(self):
        crash = self.root / "home" / ".config" / "aerie" / "Crash Reports"
        crash.mkdir(parents=True)
        self.assertEqual(crash_step.find_crash_dirs(self.root), [crash])

    def test_a_handler_process_is_found_by_its_database(self):
        proc = self.root / "proc"
        for pid, argv in (("101", ["/b/aerie_crashpad_handler", "--database=/db", "--monitor-self"]),
                          ("102", ["/b/aerie_crashpad_handler", "--database=/other"]),
                          ("103", ["python3", "/s/aerie_crashpad_handler", "--database=/db", "--url=u"]),
                          ("104", ["/bin/sleep", "1"]), ("self", ["x"])):
            (proc / pid).mkdir(parents=True)
            (proc / pid / "cmdline").write_bytes(b"\0".join(a.encode() for a in argv) + b"\0")
        found = crash_step.find_handlers(Path("/db"), proc=str(proc))
        self.assertEqual(sorted(a[1] for a in found), ["--database=/db", "/s/aerie_crashpad_handler"])


class ObserveWithTheFakeBrowserTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.root, ignore_errors=True))
        for name in ("DUMP_WAIT", "HANDLER_WAIT"):
            patcher = mock.patch.object(crash_step, name, 1.0)
            patcher.start()
            self.addCleanup(patcher.stop)

    def step(self, env=None, **opts):
        options = {"idle_seconds": 0, "settle_seconds": 0, **opts}
        res = runner.run_bench(FAKE, scenario.Scenario(checks=("zero_traffic",)), self.root, timeout=10,
                               extra_env=env or {}, check_options={"zero_traffic": options})
        return res.data["checks"][0]

    def test_a_kept_report_and_no_upload_passes_the_check(self):
        check = self.step()
        crash = check["data"]["crash_step"]
        self.assertEqual((check["status"], crash["status"]), ("pass", "pass"), crash)
        self.assertEqual(crash["reports_after_crash"], 1)
        self.assertEqual(crash["reports_after_relaunch"], 1)
        self.assertIn("crash step:", check["detail"])

    def test_each_way_to_get_it_wrong_fails_the_check_and_says_why(self):
        cases = {
            "FAKE_BROWSER_NO_CRASH": "did not crash",
            "FAKE_BROWSER_NO_DUMP": "left no local report",
            "FAKE_BROWSER_DELETE_DUMPS_AT_START": "deleted when the browser started again",
            "FAKE_BROWSER_HANDLER_URL": "upload address",
            "FAKE_BROWSER_UPLOADS_ON": "upload switch",
            "FAKE_BROWSER_DUMP_MODE": "0600",
            "FAKE_BROWSER_UPLOAD_REQUEST": "crash upload request",
            "FAKE_BROWSER_NO_HANDLER": "handler process was not found",
        }
        for name, expected in cases.items():
            env = {name: "644" if name == "FAKE_BROWSER_DUMP_MODE" else "1"}
            check = self.step(env=env)
            self.assertEqual(check["status"], "fail", name)
            self.assertIn(expected, check["detail"], name)
            self.assertEqual(check["data"]["crash_step"]["status"], "fail", name)

    def test_the_step_can_be_left_out(self):
        check = self.step(crash=False)
        self.assertEqual(check["status"], "pass")
        self.assertEqual(check["data"]["crash_step"], {"status": "skipped"})

    def test_the_report_has_no_dump_contents_and_no_profile_path(self):
        text = repr(self.step()["data"]["crash_step"])
        self.assertNotIn(str(self.root), text)
        self.assertNotIn("MDMP", text)


if __name__ == "__main__":
    unittest.main()
