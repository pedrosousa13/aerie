"""The X helpers of the Bench (issue #195): the private Xvfb and the tools check. Real input is exercised by the check itself."""

import io
import unittest
from unittest import mock

from aerie_bench import xinput


class FakeProc:
    def __init__(self, out, code=None):
        self.stdout = io.StringIO(out)
        self.returncode = code
        self.terminated = False

    def poll(self):
        return self.returncode

    def terminate(self):
        self.terminated = True

    def wait(self, timeout=None):
        return 0

    def kill(self):
        pass


class XvfbTest(unittest.TestCase):
    def test_it_yields_the_display_number_the_server_reports_and_stops_it(self):
        proc = FakeProc("97\n")
        with mock.patch.object(xinput.subprocess, "Popen", return_value=proc) as popen:
            with xinput.xvfb(1280, 800) as display:
                self.assertEqual(display, ":97")
        self.assertTrue(proc.terminated)
        argv = popen.call_args[0][0]
        self.assertIn("-nolisten", argv)
        self.assertEqual(argv[argv.index("-nolisten") + 1], "tcp")
        self.assertIn("1280x800x24", argv)

    def test_it_is_stopped_when_the_body_raises(self):
        proc = FakeProc("97\n")
        with mock.patch.object(xinput.subprocess, "Popen", return_value=proc):
            with self.assertRaises(RuntimeError):
                with xinput.xvfb():
                    raise RuntimeError("boom")
        self.assertTrue(proc.terminated)

    def test_a_server_that_exits_before_it_is_ready_is_an_error(self):
        with mock.patch.object(xinput.subprocess, "Popen", return_value=FakeProc("", code=1)):
            with self.assertRaises(xinput.XError):
                with xinput.xvfb():
                    pass

    def test_a_missing_binary_is_an_error(self):
        with mock.patch.object(xinput.subprocess, "Popen", side_effect=FileNotFoundError("Xvfb")):
            with self.assertRaises(xinput.XError):
                with xinput.xvfb():
                    pass


class ToolsTest(unittest.TestCase):
    def test_have_tools_names_what_is_missing(self):
        with mock.patch.dict(xinput.os.environ, {"PATH": "/nonexistent"}):
            self.assertIn("Xvfb", xinput.have_tools())

    def test_the_browser_gets_the_display_only(self):
        self.assertEqual(xinput.display_env(":5"), {"DISPLAY": ":5"})

    def test_the_keys_a_check_presses_have_keysyms(self):
        for key in ("ctrl", "s"):
            self.assertIn(key, xinput.XInput.KEYSYMS)


if __name__ == "__main__":
    unittest.main()
