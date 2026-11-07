"""run_bench: the one call the CLI, tests and later issues use."""

from __future__ import annotations

import os
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

from . import checks as check_registry
from . import launcher, netlog, report
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
    env.update(extra or {})
    return env


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

    with FixtureServer() as server, fresh_profile(Path(build_root) / "bench-tmp") as profile:
        if seeded:
            profile.seed_prefs(services_prefs(server.base_url))
        argv = launcher.build_argv(binary, profile.path, netlog_path, headless=headless,
                                   no_sandbox=no_sandbox, extra=extra_flags)
        env = browser_env(os.environ, profile.path, extra_env)
        binary_version = launcher.version(binary, env=env)
        try:
            proc = launcher.spawn(argv, env=env, stderr_path=out / "browser-stderr.log")
        except OSError as exc:
            raise BenchError(f"cannot start {binary}: {exc}") from exc
        session = Session(proc.client, server, out, timeout=timeout, binary=str(binary),
                          binary_version=binary_version, netlog_path=netlog_path)
        try:
            for c in instances:
                results[c.name] = _timed(c, "run", session) if hasattr(c, "run") else CheckResult(c.name, "pending")
            session.exit = proc.shutdown()
        except BaseException:
            proc.kill()
            raise
        session.netlog = netlog.parse_file(netlog_path)
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
                               + [n for n in ("netlog.json", "browser-stderr.log") if (out / n).exists()]))
    report.write(out, data)
    return RunResult(status, out, data)
