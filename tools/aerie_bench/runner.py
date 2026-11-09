"""run_bench: the one call the CLI, tests and later issues use."""

from __future__ import annotations

import os
import shutil
import time
from contextlib import contextmanager, nullcontext
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

from . import checks as check_registry
from . import benchmarks, launcher, netlog, report
from .checks.browser_requests import services_prefs
from .checks.scenario_steps import ScenarioSteps
from .fixtures import FixtureServer
from .profile import fresh_profile
from .scenario import Scenario
from .session import CheckResult, Session


class BenchError(Exception):
    """The harness itself failed (could not launch, bad check name)."""


@dataclass
class RunResult:
    status: str  # pass | fail
    out_dir: Path
    data: dict

    @property
    def exit_code(self) -> int:
        return 0 if self.status == "pass" else 1


def results_dir(build_root: Path, now: datetime | None = None) -> Path:
    base = Path(build_root) / "bench"
    base.mkdir(parents=True, exist_ok=True, mode=0o700)
    stamp = (now or datetime.now(timezone.utc)).strftime("%Y%m%dT%H%M%SZ")
    for n in range(100):
        path = base / (stamp if n == 0 else f"{stamp}-{n}")
        try:
            path.mkdir(mode=0o700)
            return path
        except FileExistsError:
            continue
    raise BenchError(f"cannot create a results directory under {base}")


_ENV_ALLOW = ("PATH", "LANG", "LANGUAGE", "TZ", "DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY",
              "XDG_RUNTIME_DIR", "XDG_SESSION_TYPE", "XDG_CURRENT_DESKTOP", "LD_LIBRARY_PATH")


TEST_FONT_DIR = Path(__file__).resolve().parent / "fixtures" / "fonts"


def install_test_fonts(target: Path) -> None:
    """Put the Bench test fonts (fixtures/fonts/*.ttf, made by make_fonts.py) where fontconfig looks for the
    user's fonts, inside the throwaway profile (``$XDG_DATA_HOME/fonts``). The fonts check (issue #117) needs a
    font that is installed but not on the allowlist, and does not want to depend on the machine's own fonts."""
    target.mkdir(parents=True, exist_ok=True, mode=0o700)
    for font in sorted(TEST_FONT_DIR.glob("*.ttf")):
        shutil.copyfile(font, target / font.name)


def browser_env(environ, profile_path: Path, extra: dict | None = None) -> dict:
    """The browser's environment: an allowlist, with HOME/XDG/TMPDIR inside the throwaway profile,
    so no tokens from the caller's shell leak in and nothing is written to the real home."""
    env = {k: v for k, v in environ.items() if k in _ENV_ALLOW or k.startswith("LC_")}
    home = Path(profile_path) / "home"
    dirs = {"HOME": home, "XDG_CONFIG_HOME": home / ".config", "XDG_CACHE_HOME": home / ".cache",
            "XDG_DATA_HOME": home / ".local" / "share", "TMPDIR": home / "tmp"}
    for key, path in dirs.items():
        path.mkdir(parents=True, exist_ok=True, mode=0o700)
        env[key] = str(path)
    install_test_fonts(home / ".local" / "share" / "fonts")
    env.update(extra or {})
    return env


def _merge(into: dict, extra: dict) -> dict:
    """Deep-merge ``extra`` into ``into``: nested dicts combine, anything else is replaced."""
    for key, value in extra.items():
        if isinstance(value, dict) and isinstance(into.get(key), dict):
            _merge(into[key], value)
        else:
            into[key] = value
    return into


def _instantiate(scenario: Scenario, options: dict | None = None) -> list:
    out = []
    for name in scenario.checks:
        try:
            out.append(check_registry.get(name)(**(options or {}).get(name, {})))
        except KeyError as exc:
            raise BenchError(str(exc.args[0])) from exc
        except TypeError as exc:
            raise BenchError(f"bad options for check {name!r}: {exc}") from exc
    if scenario.steps:
        out.append(ScenarioSteps(scenario.steps))
    if not out:
        raise BenchError("scenario has no checks and no steps")
    return out


def _timed(check, phase: str, *args) -> CheckResult:
    start = time.monotonic()
    try:
        res = getattr(check, phase)(*args)
    except Exception as exc:  # a check bug or CDP failure must not abort the run
        res = CheckResult(check.name, "error", f"{type(exc).__name__}: {exc}")
    res.duration_s = round(res.duration_s + time.monotonic() - start, 3)
    return res


def run_bench(binary: Path | str, scenario: Scenario, build_root: Path, *, out_dir: Path | None = None,
              headless: bool = True, no_sandbox: bool = False, timeout: float = 30.0,
              extra_flags: tuple[str, ...] = (),
              check_options: dict | None = None, extra_env: dict | None = None) -> RunResult:
    binary = Path(binary)
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise BenchError(f"browser binary not found or not executable: {binary}")
    instances = _instantiate(scenario, check_options)
    out = out_dir or results_dir(build_root)
    out.mkdir(parents=True, exist_ok=True, mode=0o700)
    netlog_path = out / "netlog.json"
    started = datetime.now(timezone.utc)
    t0 = time.monotonic()
    results: dict[str, CheckResult] = {}
    seeded = any(c.seeds_helium_services for c in instances)
    seeding_checks = [c.name for c in instances if hasattr(c, "profile_prefs")]

    with FixtureServer(vendor_root=benchmarks.vendor_root(build_root)) as server, fresh_profile(Path(build_root) / "bench-tmp") as profile:
        prefs: dict = services_prefs(server.base_url) if seeded else {}
        for c in instances:
            if hasattr(c, "profile_prefs"):
                _merge(prefs, c.profile_prefs(server))
        if prefs:
            profile.seed_prefs(prefs)
        # A check can ask for browser flags once the fixture server is up (``browser_flags(server)``).
        check_flags = tuple(f for c in instances if hasattr(c, "browser_flags") for f in c.browser_flags(server))
        argv = launcher.build_argv(binary, profile.path, netlog_path, headless=headless,
                                   no_sandbox=no_sandbox, extra=(*extra_flags, *check_flags))
        env = browser_env(os.environ, profile.path, extra_env)
        binary_version = launcher.version(binary, env=env)
        try:
            proc = launcher.spawn(argv, env=env, stderr_path=out / "browser-stderr.log")
        except OSError as exc:
            raise BenchError(f"cannot start {binary}: {exc}") from exc
        session = Session(proc.client, server, out, timeout=timeout, binary=str(binary),
                          binary_version=binary_version, netlog_path=netlog_path)
        session.profile_path = profile.path
        variant_count = [0]

        @contextmanager
        def variant(variant_flags: tuple[str, ...], prefs: dict | None, profile=None):
            variant_count[0] += 1
            # ``profile``: a profile from session.new_profile(), to restart a browser on the same
            # profile; the caller owns it, so it is not deleted here. Default: a fresh one.
            with (nullcontext(profile) if profile is not None
                  else fresh_profile(Path(build_root) / "bench-tmp")) as vprofile:
                if prefs:
                    vprofile.seed_prefs(prefs)
                # Discarded with the profile; one file per launch. Parsed into the variant session
                # after the browser exits, before the profile goes (vsession.netlog).
                vnetlog = vprofile.path / f"netlog-{variant_count[0]}.json"
                vargv = launcher.build_argv(binary, vprofile.path, vnetlog, headless=headless,
                                            no_sandbox=no_sandbox, extra=(*extra_flags, *variant_flags))
                venv = browser_env(os.environ, vprofile.path, extra_env)
                try:
                    vproc = launcher.spawn(vargv, env=venv,
                                           stderr_path=out / f"browser-stderr-variant-{variant_count[0]}.log")
                except OSError as exc:
                    raise BenchError(f"cannot start {binary}: {exc}") from exc
                vsession = Session(vproc.client, server, out, timeout=timeout, binary=str(binary),
                                   binary_version=binary_version, netlog_path=vnetlog)
                vsession.browser_pid = vproc.popen.pid
                try:
                    yield vsession
                except BaseException:
                    vproc.kill()
                    raise
                vsession.exit = vproc.shutdown()
                vparsed = netlog.load_file(vnetlog)
                vsession.netlog, vsession.netlog_lookups = vparsed.records, vparsed.lookups
                vsession.netlog_sent = vparsed.sent
                vsession.netlog_complete = vparsed.complete

        session.variant_launcher = variant
        session.profile_factory = lambda: fresh_profile(Path(build_root) / "bench-tmp")
        session.browser_pid = proc.popen.pid
        try:
            for c in instances:
                results[c.name] = _timed(c, "run", session) if hasattr(c, "run") else CheckResult(c.name, "pending")
            session.exit = proc.shutdown()
        except BaseException:
            proc.kill()
            raise
        parsed = netlog.load_file(netlog_path)
        session.netlog, session.netlog_lookups, session.netlog_complete = parsed.records, parsed.lookups, parsed.complete
        for c in instances:
            if hasattr(c, "finalize"):
                results[c.name] = _timed(c, "finalize", session, results[c.name])
        fixture_requests = server.snapshot()
    # Profile and server are gone here.

    ext = netlog.external(session.netlog)
    status = "pass" if all(r.ok for r in results.values()) and session.exit.get("code") is not None else "fail"
    data = {
        "scenario": scenario.name,
        "binary": {"path": str(binary), "version": binary_version},
        "started": started.isoformat(timespec="seconds"),
        "duration_s": round(time.monotonic() - t0, 3),
        "flags": [a for a in argv[1:]],
        "headless": headless,
        "no_sandbox": no_sandbox,
        "seeded_helium_services": seeded,
        "seeded_prefs_by": seeding_checks,
        "profile": {"path": str(profile.path), "deleted": profile.deleted and not profile.path.exists()},
        "checks": [{"name": r.name, "status": r.status, "detail": r.detail,
                    "duration_s": r.duration_s, "data": r.data} for r in results.values()],
        "network": {
            "netlog": netlog_path.name if netlog_path.exists() else None,
            "netlog_requests": len(session.netlog),
            "fixture_requests": fixture_requests,
            "external_requests": ext,
            "requests": session.netlog,
        },
        "exit": session.exit,
        "status": status,
    }
    if not data["profile"]["deleted"]:
        data["status"] = status = "fail"
    data["files"] = sorted(set(["results.json", "summary.txt"] + session.artifacts
                               + [n for n in ("netlog.json", "browser-stderr.log") if (out / n).exists()]
                               + sorted(p.name for p in out.glob("browser-stderr-variant-*.log"))))
    report.write(out, data)
    return RunResult(status, out, data)
