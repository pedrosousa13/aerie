"""A private X server and real input events for checks that need a window (issue #195).

``Xvfb`` starts a virtual display (no window manager, no network: ``-nolisten tcp``) and stops it again.
``XInput`` sends pointer and key events through the XTEST extension with ``ctypes`` and libX11/libXtst, so a
check presses keys and drags the mouse the way a user does, with its own timing, and with nothing to
install. The browser sees ordinary X events; nothing here talks to it over CDP.

Without a window manager the keyboard focus follows the pointer, so move the pointer into the window before
sending a key.
"""

from __future__ import annotations

import ctypes
import ctypes.util
import os
import subprocess
import time
from contextlib import contextmanager
from typing import Iterator

CURRENT_TIME = 0


class XError(Exception):
    """Xvfb or the X libraries are missing, or the display refused us."""


@contextmanager
def xvfb(width: int = 1280, height: int = 800, depth: int = 24, timeout: float = 15.0) -> Iterator[str]:
    """Run an Xvfb on a free display number; yield ``":N"``. Stopped on exit, also on error."""
    try:
        proc = subprocess.Popen(["Xvfb", "-displayfd", "1", "-screen", "0", f"{width}x{height}x{depth}",
                                 "-nolisten", "tcp"], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                stderr=subprocess.DEVNULL, text=True)
    except OSError as exc:
        raise XError(f"cannot start Xvfb: {exc}") from exc
    try:
        deadline = time.monotonic() + timeout
        line = ""
        while time.monotonic() < deadline and not line.strip():
            line = proc.stdout.readline()
            if not line and proc.poll() is not None:
                raise XError(f"Xvfb exited with code {proc.returncode} before it was ready")
        if not line.strip().isdigit():
            raise XError(f"Xvfb did not report a display number (got {line!r})")
        yield f":{line.strip()}"
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()


class XInput:
    """One connection to a display, for fake pointer and key events."""

    # X keysyms of the keys a check presses.
    KEYSYMS = {"ctrl": 0xFFE3, "shift": 0xFFE1, "alt": 0xFFE9, "s": 0x0073, "escape": 0xFF1B}

    def __init__(self, display: str) -> None:
        x11 = ctypes.util.find_library("X11")
        xtst = ctypes.util.find_library("Xtst")
        if not x11 or not xtst:
            raise XError("libX11 and libXtst are needed to send input (apt install libx11-6 libxtst6)")
        self._x11 = ctypes.CDLL(x11)
        self._xtst = ctypes.CDLL(xtst)
        self._x11.XOpenDisplay.restype = ctypes.c_void_p
        self._x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self._x11.XKeysymToKeycode.restype = ctypes.c_ubyte
        self._x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        self._x11.XFlush.argtypes = [ctypes.c_void_p]
        self._x11.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self._x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
        self._xtst.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                                    ctypes.c_ulong]
        self._xtst.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
        self._xtst.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
        self._dpy = self._x11.XOpenDisplay(display.encode())
        if not self._dpy:
            raise XError(f"cannot open the X display {display}")

    def close(self) -> None:
        if self._dpy:
            self._x11.XCloseDisplay(self._dpy)
            self._dpy = None

    def _flush(self) -> None:
        self._x11.XFlush(self._dpy)

    def sync(self) -> None:
        self._x11.XSync(self._dpy, 0)

    # Pointer.

    def move(self, x: int, y: int) -> None:
        self._xtst.XTestFakeMotionEvent(self._dpy, -1, int(x), int(y), CURRENT_TIME)
        self._flush()

    def button(self, number: int, down: bool) -> None:
        self._xtst.XTestFakeButtonEvent(self._dpy, number, 1 if down else 0, CURRENT_TIME)
        self._flush()

    def click(self, x: int, y: int, pause: float = 0.05) -> None:
        self.move(x, y)
        time.sleep(pause)
        self.button(1, True)
        time.sleep(0.02)
        self.button(1, False)

    def drag(self, x0: int, y0: int, x1: int, y1: int, steps: int, step_seconds: float,
             pause: float = 0.05) -> None:
        """Press at (x0, y0), move to (x1, y1) in ``steps`` equal moves, one every ``step_seconds``, release."""
        self.move(x0, y0)
        time.sleep(pause)
        self.button(1, True)
        time.sleep(pause)
        started = time.monotonic()
        for i in range(1, steps + 1):
            self.move(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps)
            wait = started + i * step_seconds - time.monotonic()
            if wait > 0:
                time.sleep(wait)
        time.sleep(pause)
        self.button(1, False)

    # Keys.

    def _keycode(self, name: str) -> int:
        sym = self.KEYSYMS[name.lower()]
        code = self._x11.XKeysymToKeycode(self._dpy, sym)
        if not code:
            raise XError(f"the display has no key for {name!r}")
        return code

    def chord(self, *keys: str) -> None:
        """Press the keys in order, release them in reverse: ``chord("ctrl", "s")``."""
        codes = [self._keycode(k) for k in keys]
        for code in codes:
            self._xtst.XTestFakeKeyEvent(self._dpy, code, 1, CURRENT_TIME)
            self._flush()
            time.sleep(0.01)
        for code in reversed(codes):
            self._xtst.XTestFakeKeyEvent(self._dpy, code, 0, CURRENT_TIME)
            self._flush()
            time.sleep(0.01)


def display_env(display: str) -> dict[str, str]:
    """The environment a browser needs to open its window on ``display``."""
    return {"DISPLAY": display}


def have_tools() -> str | None:
    """None when Xvfb and the X libraries are there, else what is missing."""
    missing = []
    if not any(os.access(os.path.join(p, "Xvfb"), os.X_OK) for p in os.environ.get("PATH", "").split(os.pathsep)):
        missing.append("Xvfb")
    if not ctypes.util.find_library("X11") or not ctypes.util.find_library("Xtst"):
        missing.append("libX11/libXtst")
    return ", ".join(missing) or None
