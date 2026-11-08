"""Shared plumbing for the gate and the nightly: running commands, logs, worktrees."""

from __future__ import annotations

import contextlib
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterator, Mapping, Sequence, TextIO

from .redact import Redactor, is_secret_name


class Shell:
    """Runs commands. Tests replace it; nothing else in aerie_ci starts a process."""

    def run(self, argv: Sequence, *, cwd: Path | None = None, env: Mapping[str, str] | None = None,
            log: "Log | None" = None) -> int:
        proc = subprocess.Popen([str(a) for a in argv], cwd=cwd, env=dict(env) if env is not None else None,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
        assert proc.stdout is not None
        for line in proc.stdout:
            if log is not None:
                log.write(line)
        return proc.wait()

    def capture(self, argv: Sequence, *, cwd: Path | None = None, env: Mapping[str, str] | None = None) -> str:
        proc = subprocess.run([str(a) for a in argv], cwd=cwd, env=dict(env) if env is not None else None,
                              capture_output=True, text=True, timeout=600)
        if proc.returncode != 0:
            raise RuntimeError(f"{' '.join(str(a) for a in argv[:4])} failed: {proc.stderr.strip()[:300]}")
        return proc.stdout


class Log:
    """A log file that redacts everything written to it."""

    def __init__(self, path: Path, redact: Redactor):
        path.parent.mkdir(parents=True, exist_ok=True)
        self.path = path
        self._redact = redact
        self._fh: TextIO = open(path, "a", encoding="utf-8")

    def write(self, text: str) -> None:
        self._fh.write(self._redact(text))
        self._fh.flush()

    def line(self, text: str) -> None:
        self.write(text + "\n")

    def close(self) -> None:
        self._fh.close()


@dataclass(frozen=True)
class Step:
    name: str
    argv: tuple


@dataclass(frozen=True)
class StepResult:
    name: str
    rc: int
    seconds: float


def clean_env(environ: Mapping[str, str]) -> dict[str, str]:
    """The environment PR code runs in: the caller's, minus anything that looks like a credential."""
    return {k: v for k, v in environ.items() if not is_secret_name(k)}


def run_steps(shell: Shell, steps: Sequence[Step], *, cwd: Path, env: Mapping[str, str], log: Log,
              on_start: Callable[[Step], None] = lambda step: None,
              monotonic: Callable[[], float] = time.monotonic) -> list[StepResult]:
    """Run steps in order and stop at the first one that fails."""
    results: list[StepResult] = []
    for step in steps:
        on_start(step)
        log.line(f"== {step.name}")
        started = monotonic()
        rc = shell.run(step.argv, cwd=cwd, env=env, log=log)
        results.append(StepResult(step.name, rc, monotonic() - started))
        log.line(f"== {step.name}: exit {rc}")
        if rc != 0:
            break
    return results


def fetch_ref(shell: Shell, repo: Path, refspec: str) -> str:
    """Fetch a ref from origin and return the commit it points to. Data only: nothing in it runs."""
    shell.capture(["git", "-C", repo, "fetch", "--quiet", "origin", refspec])
    return shell.capture(["git", "-C", repo, "rev-parse", "FETCH_HEAD"]).strip()


@contextlib.contextmanager
def worktree(shell: Shell, repo: Path, path: Path, sha: str) -> Iterator[Path]:
    """A detached worktree of repo at sha, removed afterwards even on failure."""
    # A gate killed mid-run (timeout, SIGTERM, reboot) leaves its worktree registered. Clear it, or
    # `add` fails and the SHA is never gated.
    with contextlib.suppress(Exception):
        shell.capture(["git", "-C", repo, "worktree", "remove", "--force", path])
    with contextlib.suppress(Exception):
        shell.capture(["git", "-C", repo, "worktree", "prune"])
    shell.capture(["git", "-C", repo, "worktree", "add", "--detach", path, sha])
    try:
        shell.capture(["git", "-C", path, "submodule", "update", "--init", "--recursive", "--quiet"])
        yield path
    finally:
        with contextlib.suppress(Exception):
            shell.capture(["git", "-C", repo, "worktree", "remove", "--force", path])
        with contextlib.suppress(Exception):
            shell.capture(["git", "-C", repo, "worktree", "prune"])


def python_steps(python: str, tree: Path, build_root: Path, ccache_dir: Path | None = None) -> dict[str, tuple]:
    """Commands for the existing build and Bench entry points, run from a checkout."""
    aerie = str(tree / "aerie")
    root = ["--build-root", str(build_root)]
    cache = ["--ccache-dir", str(ccache_dir)] if ccache_dir else []
    return {
        "patches": (python, aerie, "patches", "check", *root),
        "build": (python, aerie, "build", *root, *cache),
        "clean_build": (python, aerie, "build", "--clean", *root, *cache),
        "unittest": (python, "-m", "unittest", "discover", "-s", "tests", "-t", "."),
    }


def bench_argv(python: str, tree: Path, build_root: Path, checks: Sequence[str], out: Path | None = None,
               extra: Sequence[str] = ()) -> tuple:
    argv = [python, str(tree / "aerie"), "bench", "--build-root", str(build_root)]
    for name in checks:
        argv += ["--check", name]
    if out is not None:
        argv += ["--out", str(out)]
    return tuple(argv) + tuple(extra)


_LIST_CHECKS = "from aerie_bench import checks; print(' '.join(sorted(checks.REGISTRY)))"


def bench_check_names(shell: Shell, python: str, tree: Path, env: Mapping[str, str],
                      exclude: Sequence[str]) -> list[str]:
    """The Bench checks that exist in this checkout (asked of the checkout itself), minus exclude."""
    names = shell.capture([python, "-I", "-c", f"import sys; sys.path.insert(0, 'tools'); {_LIST_CHECKS}"],
                          cwd=tree, env=env).split()
    return sorted(n for n in names if n not in set(exclude) | {"scenario"})
