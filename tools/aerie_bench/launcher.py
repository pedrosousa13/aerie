"""Start a browser binary with a CDP pipe, and stop it again."""

from __future__ import annotations

import os
import re
import signal
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path

from .cdp import CdpClient, CdpError

# Host resolver rules, first match wins among MAP rules, EXCLUDE rules always win.
# *.bench.test names are fixture sites that need a name which is not exempt from
# HTTPS-only mode; they resolve to the loopback fixture server. Loopback names stay
# resolvable. Everything else fails to resolve (the attempt is still in the NetLog).
RESOLVER_RULES = (
    "MAP *.bench.test 127.0.0.1",
    "MAP * ~NOTFOUND",
    "EXCLUDE 127.0.0.1",
    "EXCLUDE 127.0.0.2",
    "EXCLUDE ::1",
    "EXCLUDE [::1]",
    "EXCLUDE localhost",
    "EXCLUDE *.localhost",
)

# Deliberately absent: --disable-background-networking, --disable-component-update,
# --disable-sync. They would hide the browser-level traffic the Bench exists to record.
BASE_FLAGS = (
    "--no-first-run",
    "--no-default-browser-check",
    "--password-store=basic",
    # Offline by construction: every host except loopback fails to resolve, but
    # the attempt still shows up in the NetLog. No proxy may route around that.
    # (rules: see RESOLVER_RULES)
    "--host-resolver-rules=" + " , ".join(RESOLVER_RULES),
    "--no-proxy-server",
)


def build_argv(binary: str | Path, profile: Path, netlog: Path, *, headless: bool = True,
               no_sandbox: bool = False, extra: tuple[str, ...] = ()) -> list[str]:
    argv = [str(binary), f"--user-data-dir={profile}", "--remote-debugging-pipe",
            f"--log-net-log={netlog}", *BASE_FLAGS]
    if headless:
        argv.append("--headless=new")
    if no_sandbox:
        argv.append("--no-sandbox")
    argv.extend(extra)
    argv.append("about:blank")
    return argv


@dataclass
class BrowserProcess:
    popen: subprocess.Popen
    client: CdpClient

    def _signal_group(self, sig: int) -> None:
        # start_new_session=True made the browser a group leader: this reaches
        # renderer, GPU and zygote children too.
        try:
            os.killpg(self.popen.pid, sig)
        except (ProcessLookupError, PermissionError):
            pass

    def shutdown(self, timeout: float = 20.0) -> dict:
        """Ask the browser to close; escalate to SIGTERM then SIGKILL on the whole process
        group. One deadline covers all of it. Returns exit info."""
        deadline = time.monotonic() + timeout

        def left() -> float:
            return max(0.0, deadline - time.monotonic())

        clean = True
        try:
            self.client.send("Browser.close", timeout=left())
        except CdpError:
            pass  # the pipe may close before the reply arrives
        try:
            code = self.popen.wait(timeout=left())
        except subprocess.TimeoutExpired:
            clean = False
            self._signal_group(signal.SIGTERM)
            try:
                code = self.popen.wait(timeout=min(2.0, max(left(), 0.5)))
            except subprocess.TimeoutExpired:
                self._signal_group(signal.SIGKILL)
                code = self.popen.wait()
        self._signal_group(signal.SIGKILL)  # stragglers left behind by an exited main process
        self.client.close()
        return {"clean": clean and code == 0, "code": code}

    def kill(self) -> None:
        self._signal_group(signal.SIGKILL)
        self.popen.wait()
        self.client.close()


FATAL_MAX = 200


def fatal_line(stderr_path: Path | str | None) -> str | None:
    """The last FATAL line a browser wrote to stderr, without its pid/file prefix, or None.

    Chromium writes `[pid:tid:mmdd/hhmmss.us:FATAL:file.cc:line] message`. Only the first sentence
    of a long message is kept ("No usable sandbox!" and not the two doc URLs after it).
    """
    if not stderr_path:
        return None
    try:
        lines = Path(stderr_path).read_text(errors="replace").splitlines()
    except OSError:
        return None
    for line in reversed(lines):
        m = re.search(r":FATAL:[^\]]*\]\s*(?P<msg>.+)$", line)
        if m:
            msg = m.group("msg").strip()
            first = re.split(r"(?<=[.!?])\s", msg, maxsplit=1)[0]
            return first[:FATAL_MAX]
    return None


def _why_it_died(popen: subprocess.Popen, stderr_path: Path | None, wait: float = 2.0) -> str | None:
    """Called after the CDP pipe closed. If the process is gone (or goes within `wait`), say why."""
    try:
        code = popen.wait(timeout=wait)
    except subprocess.TimeoutExpired:
        return None  # still running: it only closed the pipe
    line = fatal_line(stderr_path)
    if line:
        return f"browser exited with code {code}: {line}"
    return f"browser exited with code {code}"


def spawn(argv: list[str], env: dict[str, str] | None = None, stderr_path: Path | None = None) -> BrowserProcess:
    """Spawn with the browser's fd 3 / 4 wired to a CDP client."""
    to_browser_r, to_browser_w = os.pipe()    # we write, browser reads on fd 3
    from_browser_r, from_browser_w = os.pipe()  # browser writes on fd 4, we read

    def wire() -> None:
        os.dup2(to_browser_r, 3)
        os.dup2(from_browser_w, 4)

    err = open(stderr_path, "wb") if stderr_path else subprocess.DEVNULL
    try:
        # close_fds=False: CPython would close fds 3/4 after preexec_fn otherwise.
        # Python-created fds are non-inheritable, so nothing else leaks.
        popen = subprocess.Popen(argv, env=env, close_fds=False, preexec_fn=wire,
                                 stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                 stderr=err, start_new_session=True)
    except BaseException:
        for fd in (to_browser_w, from_browser_r):
            os.close(fd)
        raise
    finally:
        if stderr_path:
            err.close()
        os.close(to_browser_r)
        os.close(from_browser_w)
    client = CdpClient(from_browser_r, to_browser_w)
    client.closed_hint = lambda: _why_it_died(popen, stderr_path)
    return BrowserProcess(popen, client)


def version(binary: str | Path, timeout: float = 15.0, env: dict | None = None) -> str | None:
    try:
        out = subprocess.run([str(binary), "--version"], capture_output=True, text=True,
                             timeout=timeout, stdin=subprocess.DEVNULL, env=env)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return out.stdout.strip() or None
