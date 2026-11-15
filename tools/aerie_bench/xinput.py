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
import secrets
import select
import shutil
import subprocess
import tempfile
import time
from contextlib import contextmanager
from typing import Iterator

CURRENT_TIME = 0


class XError(Exception):
    """Xvfb or the X libraries are missing, or the display refused us."""


FAMILY_WILD = 0xFFFF  # an Xauthority entry for any family and address
COOKIE_NAME = b"MIT-MAGIC-COOKIE-1"


class Display(str):
    """``":N"`` of a private Xvfb, with the Xauthority file that lets a client in (``xauthority``, or None)."""

    xauthority: str | None = None


def xauth_entry(number: str, cookie: bytes) -> bytes:
    """One Xauthority record (the format Xlib and the X server read): any family and address, display ``number``."""
    def field(data: bytes) -> bytes:
        return len(data).to_bytes(2, "big") + data
    return (FAMILY_WILD.to_bytes(2, "big") + field(b"") + field(number.encode()) + field(COOKIE_NAME) + field(cookie))


def _write_private(path: str, data: bytes) -> None:
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(data)


def _read_display_number(proc, timeout: float) -> str:
    """The line Xvfb writes to the -displayfd pipe, read with a deadline so a silent hang cannot block forever."""
    fd = proc.stdout.fileno()
    deadline = time.monotonic() + timeout
    buf = b""
    while b"\n" not in buf:
        left = deadline - time.monotonic()
        if left <= 0:
            raise XError(f"Xvfb reported no display number within {timeout:g} s")
        ready, _, _ = select.select([fd], [], [], left)
        if not ready:
            continue
        chunk = os.read(fd, 64)
        if not chunk:
            code = proc.poll()
            raise XError(f"Xvfb exited with code {code} before it was ready")
        buf += chunk
    number = buf.split(b"\n", 1)[0].strip().decode(errors="replace")
    if not number.isdigit():
        raise XError(f"Xvfb did not report a display number (got {number!r})")
    return number


@contextmanager
def xvfb(width: int = 1280, height: int = 800, depth: int = 24, timeout: float = 15.0) -> Iterator[Display]:
    """Run an Xvfb on a free display number; yield ``":N"`` (a ``Display``). Stopped on exit, also on error.

    The server needs a cookie (``-auth``): the cookie lives in a private file, in a 0700 directory that is removed
    again, so another user of the machine cannot open the display and type into the browser."""
    tmp = tempfile.mkdtemp(prefix="aerie-xvfb-")  # mode 0700
    proc = None
    try:
        cookie = secrets.token_bytes(16)
        server_auth = os.path.join(tmp, "server.auth")
        _write_private(server_auth, xauth_entry("", cookie))  # an empty number matches any display
        try:
            proc = subprocess.Popen(["Xvfb", "-displayfd", "1", "-screen", "0", f"{width}x{height}x{depth}",
                                     "-nolisten", "tcp", "-auth", server_auth], stdin=subprocess.DEVNULL,
                                    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        except OSError as exc:
            raise XError(f"cannot start Xvfb: {exc}") from exc
        number = _read_display_number(proc, timeout)
        client_auth = os.path.join(tmp, "client.auth")
        _write_private(client_auth, xauth_entry(number, cookie))
        display = Display(f":{number}")
        display.xauthority = client_auth
        yield display
    finally:
        if proc is not None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            if proc.stdout is not None:
                proc.stdout.close()
        shutil.rmtree(tmp, ignore_errors=True)


def now_us() -> int:
    """The trace clock (CLOCK_MONOTONIC) in microseconds, to stamp the moment an input event is sent."""
    return int(time.monotonic() * 1_000_000)


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
        auth = getattr(display, "xauthority", None)
        saved = os.environ.get("XAUTHORITY")
        if auth:
            os.environ["XAUTHORITY"] = auth
        try:
            self._dpy = self._x11.XOpenDisplay(str(display).encode())
        finally:
            if auth:
                if saved is None:
                    os.environ.pop("XAUTHORITY", None)
                else:
                    os.environ["XAUTHORITY"] = saved
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

    def click(self, x: int, y: int, pause: float = 0.05) -> int:
        """Click; return the trace-clock time (``now_us``) of the button release. A sidebar row acts on the release
        (``AerieSidebarRowView::OnMouseReleased`` runs ``select``; the press only arms it), so the release is the
        first event that changes state."""
        self.move(x, y)
        time.sleep(pause)
        self.button(1, True)
        time.sleep(0.02)
        released = now_us()
        self.button(1, False)
        return released

    def drag(self, x0: int, y0: int, x1: int, y1: int, steps: int, step_seconds: float,
             pause: float = 0.05) -> int:
        """Press at (x0, y0), move to (x1, y1) in ``steps`` equal moves, one every ``step_seconds``, release.
        Returns the trace-clock time of the first move after the press: the press alone changes no width."""
        self.move(x0, y0)
        time.sleep(pause)
        self.button(1, True)
        time.sleep(pause)
        started = time.monotonic()
        first_move = now_us()
        for i in range(1, steps + 1):
            self.move(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps)
            wait = started + i * step_seconds - time.monotonic()
            if wait > 0:
                time.sleep(wait)
        time.sleep(pause)
        self.button(1, False)
        return first_move

    # Keys.

    def _keycode(self, name: str) -> int:
        sym = self.KEYSYMS[name.lower()]
        code = self._x11.XKeysymToKeycode(self._dpy, sym)
        if not code:
            raise XError(f"the display has no key for {name!r}")
        return code

    def chord(self, *keys: str) -> int:
        """Press the keys in order, release them in reverse: ``chord("ctrl", "s")``. Returns the trace-clock time
        of the first key press."""
        codes = [self._keycode(k) for k in keys]
        pressed = now_us()
        for code in codes:
            self._xtst.XTestFakeKeyEvent(self._dpy, code, 1, CURRENT_TIME)
            self._flush()
            time.sleep(0.01)
        for code in reversed(codes):
            self._xtst.XTestFakeKeyEvent(self._dpy, code, 0, CURRENT_TIME)
            self._flush()
            time.sleep(0.01)
        return pressed


def display_env(display: str) -> dict[str, str]:
    """The environment a browser needs to open its window on ``display`` (with its cookie, when it has one)."""
    env = {"DISPLAY": str(display)}
    auth = getattr(display, "xauthority", None)
    if auth:
        env["XAUTHORITY"] = auth
    return env


def have_tools() -> str | None:
    """None when Xvfb and the X libraries are there, else what is missing."""
    missing = []
    if not any(os.access(os.path.join(p, "Xvfb"), os.X_OK) for p in os.environ.get("PATH", "").split(os.pathsep)):
        missing.append("Xvfb")
    if not ctypes.util.find_library("X11") or not ctypes.util.find_library("Xtst"):
        missing.append("libX11/libXtst")
    return ", ".join(missing) or None
