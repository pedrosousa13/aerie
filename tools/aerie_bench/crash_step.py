"""A crash on purpose, then a relaunch: the proof that a crash report stays on the device (issue #262).

The ``zero_traffic`` check runs this after its three phases. It starts a browser on a throwaway
profile, crashes it (``Browser.crash`` over DevTools, which raises SIGTRAP in the browser process
like a real crash), starts a second browser on the same profile, and judges what is left:

* the browser did crash (a clean exit proves nothing);
* the crash handler process was found, and was started without an upload address (no ``--url``). The
  NetLog does not see the handler's requests (docs/network-endpoints.md), so the command line of the
  handler and the database's upload switch are the evidence, together with the absence of an upload in
  the database: nothing in ``completed/``;
* the database's upload switch (``settings.dat``) is off;
* the crash left a local report in ``Crash Reports/pending`` and it is still there after the relaunch
  (Helium deleted pending reports at startup; Aerie keeps them), readable by the user only (files 0600,
  folders 0700);
* neither browser requested anything on the denylist, and nothing at all with "crash" in the host or
  path, in its NetLog.

``observe`` drives the browsers and reads the disk and ``/proc``; ``judge`` is a pure function of what
``observe`` saw, run by the tests on good and bad observations. The report holds names and modes of
files, never their contents: a dump can hold page memory.
"""

from __future__ import annotations

import os
import stat
import struct
import time
from dataclasses import dataclass, field
from pathlib import Path
from urllib.parse import urlsplit

from .cdp import CdpError

DUMP_WAIT = 20.0
HANDLER_WAIT = 10.0
SETTINGS_MAGIC = b"sdPC"
UPLOADS_ENABLED = 1


@dataclass
class Observation:
    started: bool = False                 # the first browser started and the crash was sent
    error: str | None = None              # why the step could not run
    crash_exit: dict = field(default_factory=dict)
    crash_dir: str | None = None          # the folder ("Crash Reports") the handler writes to
    handlers: list[list[str]] = field(default_factory=list)  # command lines of the crash handler(s) seen
    dumps_after_crash: list[dict] = field(default_factory=list)    # {"name", "mode", "size"}
    dumps_after_relaunch: list[dict] = field(default_factory=list)
    dir_modes: dict = field(default_factory=dict)                  # folder name -> mode
    completed: list[str] = field(default_factory=list)             # names in completed/
    uploads_enabled: bool | None = None                            # settings.dat; None when unreadable
    relaunch_exit: dict = field(default_factory=dict)
    requests: list[dict] = field(default_factory=list)             # NetLog records of both browsers
    lookups: list[dict] = field(default_factory=list)
    netlogs_complete: dict = field(default_factory=dict)           # which launch's log was whole


# ---------------------------------------------------------------- reading the disk and /proc

def find_crash_dirs(profile: Path) -> list[Path]:
    """The ``Crash Reports`` folders under the profile's own home. The browser keeps them under the
    default user data directory (``$XDG_CONFIG_HOME/<product>``), not under ``--user-data-dir``, and the
    Bench points XDG_CONFIG_HOME inside the throwaway profile."""
    return sorted((Path(profile) / "home" / ".config").glob("*/Crash Reports"))


def list_dumps(crash_dir: Path | None) -> list[dict]:
    if not crash_dir:
        return []
    out = []
    for path in sorted((Path(crash_dir) / "pending").glob("*.dmp")):
        try:
            st = path.lstat()
        except OSError:
            continue
        out.append({"name": path.name, "mode": stat.S_IMODE(st.st_mode), "size": st.st_size})
    return out


def folder_modes(crash_dir: Path | None) -> dict:
    modes = {}
    if crash_dir:
        for name in ("", "pending", "completed", "new", "attachments"):
            path = Path(crash_dir) / name if name else Path(crash_dir)
            try:
                modes[name or "."] = stat.S_IMODE(path.lstat().st_mode)
            except OSError:
                pass
    return modes


def list_completed(crash_dir: Path | None) -> list[str]:
    if not crash_dir:
        return []
    try:
        return sorted(p.name for p in (Path(crash_dir) / "completed").iterdir())
    except OSError:
        return []


def read_uploads_enabled(crash_dir: Path | None) -> bool | None:
    """The upload switch of Crashpad's ``settings.dat``: magic, version, options (bit 0 = uploads on)."""
    if not crash_dir:
        return None
    try:
        data = (Path(crash_dir) / "settings.dat").read_bytes()
    except OSError:
        return None
    if len(data) < 12 or data[:4] != SETTINGS_MAGIC:
        return None
    options, = struct.unpack_from("<I", data, 8)
    return bool(options & UPLOADS_ENABLED)


def find_handlers(crash_dir: Path | None, proc: str = "/proc") -> list[list[str]]:
    """Command lines of the crash handler processes that write to ``crash_dir``. With no folder it finds
    none: another browser on the machine has a handler of its own, and it is not this run's."""
    found = []
    if not crash_dir:
        return found
    try:
        entries = [e for e in os.listdir(proc) if e.isdigit()]
    except OSError:
        return found
    want = os.path.realpath(crash_dir)
    for pid in entries:
        try:
            raw = Path(proc, pid, "cmdline").read_bytes()
        except OSError:
            continue
        argv = [a.decode("utf-8", "replace") for a in raw.split(b"\0") if a]
        # argv[0] is the handler; a stand-in that is a script shows up as its interpreter and then the script.
        if not any("crashpad_handler" in os.path.basename(a) for a in argv[:2]):
            continue
        databases = [a.split("=", 1)[1] for a in argv if a.startswith("--database=")]
        if not any(os.path.realpath(d) == want for d in databases):
            continue
        found.append(argv)
    return found


# ---------------------------------------------------------------- the verdict

def _redact(argv: list[str]) -> list[str]:
    """Only the switch names: a value can hold a path of the profile, and so can the program."""
    return [a.split("=", 1)[0] for a in argv if a.startswith("--")]


def crash_requests(requests: list[dict], lookups: list[dict], dl) -> list[str]:
    """Names of what the NetLogs hold that is, or looks like, a crash upload."""
    bad = []
    for r in requests:
        if r.get("external") is False:
            continue
        for url in r.get("urls", [r["url"]]):
            host = (urlsplit(url).hostname or "").lower()
            path = urlsplit(url).path.lower()
            if dl.match_url(url) or "crash" in host or "crash" in path:
                bad.append(host or url)
    for lk in lookups:
        host = (lk.get("host") or "").lower()
        if dl.match_host(host) or "crash" in host:
            bad.append(host)
    return sorted(set(bad))


def judge(obs: Observation, dl) -> tuple[str, str, dict]:
    """(status, reason, data). ``fail`` names the first thing that is wrong; the data holds every fact."""
    data = {
        "ran": obs.started, "crash_exit_code": obs.crash_exit.get("code"),
        "handler_processes": len(obs.handlers),
        "handler_switches": [_redact(a) for a in obs.handlers],
        "uploads_enabled": obs.uploads_enabled,
        "reports_after_crash": len(obs.dumps_after_crash),
        "reports_after_relaunch": len(obs.dumps_after_relaunch),
        "report_modes": sorted({oct(d["mode"]) for d in obs.dumps_after_crash + obs.dumps_after_relaunch}),
        "folder_modes": {k: oct(v) for k, v in obs.dir_modes.items()},
        "completed_reports": len(obs.completed),
        "relaunch_exit_code": obs.relaunch_exit.get("code"),
    }
    if obs.error or not obs.started:
        return "fail", f"the crash step could not run: {obs.error or 'the browser did not start'}", data
    problems = []
    code = obs.crash_exit.get("code")
    if code in (0, None):
        problems.append("the browser did not crash when it was told to")
    if not obs.handlers:
        problems.append("the crash handler process was not found, so it cannot be shown that it has no upload address")
    for argv in obs.handlers:
        if any(a == "--url" or a.startswith("--url=") for a in argv[1:]):
            problems.append("the crash handler was started with an upload address (--url)")
            break
    if obs.uploads_enabled is not False:
        problems.append("the crash database's upload switch is not off"
                        if obs.uploads_enabled else "the crash database's upload switch could not be read")
    if not obs.dumps_after_crash:
        problems.append("the crash left no local report in Crash Reports/pending")
    elif not obs.dumps_after_relaunch:
        problems.append("the report was deleted when the browser started again")
    elif {d["name"] for d in obs.dumps_after_crash} - {d["name"] for d in obs.dumps_after_relaunch}:
        problems.append("a report was deleted when the browser started again")
    loose = [d["name"] for d in obs.dumps_after_crash + obs.dumps_after_relaunch if d["mode"] & 0o077]
    if loose:
        problems.append(f"{len(loose)} report file(s) can be read by others (the mode is not 0600)")
    open_dirs = [k for k, v in obs.dir_modes.items() if v & 0o077]
    if open_dirs:
        problems.append(f"folder(s) {', '.join(sorted(open_dirs))} of the crash database can be read by others (not 0700)")
    if obs.completed:
        problems.append(f"{len(obs.completed)} report(s) in Crash Reports/completed: one was handled as sent")
    if obs.relaunch_exit.get("code") != 0:
        problems.append(f"the relaunched browser did not exit cleanly (exit code {obs.relaunch_exit.get('code')})")
    # The first browser crashed: its log is cut short, as expected. The relaunch's must be whole.
    if not obs.netlogs_complete.get("relaunch", False):
        problems.append("the relaunched browser's NetLog is truncated or missing")
    uploads = crash_requests(obs.requests, obs.lookups, dl)
    data["crash_requests"] = uploads
    if uploads:
        problems.append(f"a crash upload request in the NetLog: {', '.join(uploads)}")
    if problems:
        return "fail", "; ".join(problems), data
    return ("pass", f"the forced crash left {len(obs.dumps_after_relaunch)} local report(s), still there after the "
            "relaunch, readable by the user only; the crash handler has no upload address and the upload switch "
            "is off; no crash request was made", data)


# ---------------------------------------------------------------- driving the browsers

def observe(session, settle_seconds: float = 3.0, sleep=time.sleep) -> Observation:
    """Crash a browser on a fresh profile and start another on it. ``session`` is the run's Session."""
    obs = Observation()
    try:
        profile_cm = session.new_profile()
    except CdpError as exc:
        obs.error = str(exc)
        return obs
    try:
        with profile_cm as profile:
            _crash_and_relaunch(session, profile, obs, settle_seconds, sleep)
    except CdpError as exc:
        obs.error = str(exc)
    except OSError as exc:
        obs.error = f"{type(exc).__name__}: {exc}"
    return obs


def _wait_for(predicate, timeout: float, sleep=time.sleep, step: float = 0.25):
    deadline = time.monotonic() + timeout
    while True:
        value = predicate()
        if value:
            return value
        if time.monotonic() >= deadline:
            return value
        sleep(step)


def _crash_and_relaunch(session, profile, obs: Observation, settle: float, sleep) -> None:
    with session.variant(profile=profile) as first:
        first.navigate("/index.html")
        crash_dirs = _wait_for(lambda: find_crash_dirs(profile.path), HANDLER_WAIT, sleep)
        obs.crash_dir = str(crash_dirs[0]) if crash_dirs else None
        obs.handlers = _wait_for(lambda: find_handlers(crash_dirs[0] if crash_dirs else None), HANDLER_WAIT, sleep) or []
        obs.started = True
        try:
            first.client.send("Browser.crash", timeout=10)
        except CdpError:
            pass  # the pipe closes under the command: the browser is gone
        # The handler writes the dump after the crash. Give it the time, with the browser already gone.
        crash_dirs = find_crash_dirs(profile.path) or crash_dirs
        crash_dir = crash_dirs[0] if crash_dirs else None
        obs.dumps_after_crash = _wait_for(lambda: list_dumps(crash_dir), DUMP_WAIT, sleep) or []
    obs.crash_exit = first.exit
    obs.requests += list(first.netlog)
    obs.lookups += list(getattr(first, "netlog_lookups", []))
    obs.netlogs_complete["crash"] = bool(getattr(first, "netlog_complete", False))
    with session.variant(profile=profile) as second:
        second.navigate("/index.html")
        sleep(settle)
        crash_dirs = find_crash_dirs(profile.path)
        crash_dir = crash_dirs[0] if crash_dirs else None
        obs.dumps_after_relaunch = list_dumps(crash_dir)
        obs.uploads_enabled = read_uploads_enabled(crash_dir)
        obs.dir_modes = folder_modes(crash_dir)
        obs.completed = list_completed(crash_dir)
    obs.relaunch_exit = second.exit
    obs.requests += list(second.netlog)
    obs.lookups += list(getattr(second, "netlog_lookups", []))
    obs.netlogs_complete["relaunch"] = bool(getattr(second, "netlog_complete", False))
