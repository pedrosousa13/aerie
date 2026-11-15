"""The X helpers of the Bench (issue #195): the private Xvfb and the tools check. Real input is exercised by the check itself."""

import os
import stat
import time
import unittest
from types import SimpleNamespace
from unittest import mock

from aerie_bench import xinput


class FakeProc:
    """A process whose stdout is a real pipe, so that select works on it."""

    def __init__(self, out=b"", code=None, close=True):
        r, w = os.pipe()
        self.stdout = os.fdopen(r, "rb", buffering=0)
        self._w = w
        os.write(w, out)
        if close:
            os.close(w)
            self._w = None
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

    def done(self):
        self.stdout.close()
        if self._w is not None:
            os.close(self._w)


class XvfbTest(unittest.TestCase):
    def start(self, proc):
        self.addCleanup(proc.done)
        return mock.patch.object(xinput.subprocess, "Popen", return_value=proc)

    def test_it_yields_the_display_number_the_server_reports_and_stops_it(self):
        proc = FakeProc(b"97\n")
        with self.start(proc) as popen:
            with xinput.xvfb(1280, 800) as display:
                self.assertEqual(display, ":97")
        self.assertTrue(proc.terminated)
        argv = popen.call_args[0][0]
        self.assertEqual(argv[argv.index("-nolisten") + 1], "tcp")
        self.assertIn("1280x800x24", argv)

    def test_the_server_wants_a_cookie_from_a_private_file_that_is_removed_afterwards(self):
        proc = FakeProc(b"97\n")
        with self.start(proc) as popen:
            with xinput.xvfb() as display:
                argv = popen.call_args[0][0]
                server_auth = argv[argv.index("-auth") + 1]
                directory = os.path.dirname(server_auth)
                self.assertEqual(stat.S_IMODE(os.stat(directory).st_mode), 0o700)
                self.assertEqual(stat.S_IMODE(os.stat(server_auth).st_mode), 0o600)
                self.assertEqual(stat.S_IMODE(os.stat(display.xauthority).st_mode), 0o600)
                server, client = open(server_auth, "rb").read(), open(display.xauthority, "rb").read()
                cookie = server[-16:]
                self.assertNotEqual(cookie, bytes(16))
                self.assertEqual(client[-16:], cookie)       # the client holds the same cookie
                self.assertIn(b"97", client)                  # for the display it was told about
                self.assertIn(b"MIT-MAGIC-COOKIE-1", client)
        self.assertFalse(os.path.exists(directory))

    def test_the_browser_gets_the_cookie_file_too(self):
        proc = FakeProc(b"5\n")
        with self.start(proc):
            with xinput.xvfb() as display:
                env = xinput.display_env(display)
                self.assertEqual(env, {"DISPLAY": ":5", "XAUTHORITY": display.xauthority})

    def test_a_silent_server_times_out_instead_of_blocking(self):
        proc = FakeProc(b"", close=False)  # alive, says nothing
        with self.start(proc):
            started = time.monotonic()
            with self.assertRaisesRegex(xinput.XError, "no display number within"):
                with xinput.xvfb(timeout=0.3):
                    pass
            self.assertLess(time.monotonic() - started, 3)
        self.assertTrue(proc.terminated)

    def test_the_directory_is_removed_when_the_body_raises(self):
        proc = FakeProc(b"97\n")
        with self.start(proc) as popen:
            with self.assertRaises(RuntimeError):
                with xinput.xvfb() as display:
                    directory = os.path.dirname(display.xauthority)
                    raise RuntimeError("boom")
        self.assertTrue(proc.terminated)
        self.assertFalse(os.path.exists(directory))

    def test_a_server_that_exits_before_it_is_ready_is_an_error(self):
        with self.start(FakeProc(b"", code=1)):
            with self.assertRaises(xinput.XError):
                with xinput.xvfb():
                    pass

    def test_a_missing_binary_is_an_error_and_leaves_no_directory_behind(self):
        made = []
        real = xinput.tempfile.mkdtemp
        with mock.patch.object(xinput.subprocess, "Popen", side_effect=FileNotFoundError("Xvfb")), \
                mock.patch.object(xinput.tempfile, "mkdtemp", side_effect=lambda **k: made.append(real(**k)) or made[-1]):
            with self.assertRaises(xinput.XError):
                with xinput.xvfb():
                    pass
        self.assertTrue(made and not os.path.exists(made[0]))


class InputTimeTest(unittest.TestCase):
    """The first state-changing event is stamped on the trace clock, after the waits that come before it."""

    def make(self):
        x = xinput.XInput.__new__(xinput.XInput)
        x.events = []
        x.move = lambda *a: x.events.append("move")
        x.button = lambda n, down: x.events.append(("button", down))
        x._keycode = lambda name: 1
        x._flush = lambda: None
        x._xtst = SimpleNamespace(XTestFakeKeyEvent=lambda *a: x.events.append(("key", a[2])))
        x._dpy = 1
        return x

    def test_click_reports_the_press_not_the_move_before_it(self):
        x = self.make()
        before = xinput.now_us()
        with mock.patch.object(xinput.time, "sleep"):
            pressed = x.click(1, 2, pause=0.0)
        self.assertGreaterEqual(pressed, before)
        self.assertEqual(x.events[0], "move")

    def test_click_stamps_after_the_pause_that_follows_the_move(self):
        x = self.make()
        stamps = []
        orig = xinput.now_us

        def sleep(seconds):
            stamps.append(("sleep", orig()))
        with mock.patch.object(xinput.time, "sleep", side_effect=sleep):
            pressed = x.click(1, 2, pause=0.05)
        self.assertGreaterEqual(pressed, stamps[0][1])  # the press is stamped after the pause, not before it

    def test_chord_reports_the_first_key_down(self):
        x = self.make()
        with mock.patch.object(xinput.time, "sleep"):
            pressed = x.chord("ctrl", "s")
        self.assertIsInstance(pressed, int)
        self.assertEqual(x.events[0], ("key", 1))

    def test_drag_reports_the_first_move_after_the_press(self):
        x = self.make()
        with mock.patch.object(xinput.time, "sleep"):
            moved = x.drag(0, 0, 10, 0, 2, 0.01)
        self.assertIsInstance(moved, int)
        press = x.events.index(("button", True))
        self.assertEqual(x.events[press + 1], "move")


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
