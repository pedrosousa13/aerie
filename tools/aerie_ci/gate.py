"""The PR gate: poll open PRs, gate the maintainer's new heads, post aerie/gate."""

from __future__ import annotations

import contextlib
import json
import os
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Mapping

from aerie_build import buildlock

from . import steps as st
from .gh import Gh, PullRequest
from .redact import Redactor

MAINTAINER = "pedrosousa13"
SKIPPED_AUTHOR, SKIPPED_GATED = "skipped-author", "skipped-gated"
SUCCESS, FAILURE, ERROR, BUSY, STALE = "success", "failure", "error", "busy", "stale"

# Slow checks are left to the nightly: zero_traffic idles for about six minutes.
GATE_BENCH_EXCLUDE = ("smoke", "zero_traffic")


@dataclass
class CiConfig:
    repo: Path
    build_root: Path
    maintainer: str = MAINTAINER
    python: str = sys.executable
    lock_timeout: float | None = None
    environ: Mapping[str, str] = field(default_factory=lambda: dict(os.environ))
    shell: st.Shell = field(default_factory=st.Shell)
    gh: Gh = field(default_factory=Gh)
    lock: Callable = buildlock.build_tree_lock
    now: Callable[[], float] = time.time

    @property
    def ci_root(self) -> Path:
        return self.build_root / "ci"

    @property
    def redact(self) -> Redactor:
        return Redactor(self.environ)


class GatedStore:
    """Head SHAs already gated, kept in <build root>/ci/gated.json."""

    def __init__(self, path: Path):
        self.path = path

    def _load(self) -> dict:
        try:
            return json.loads(self.path.read_text())
        except (OSError, ValueError):
            return {}

    def has(self, sha: str) -> bool:
        return sha in self._load()

    def record(self, sha: str, entry: dict) -> None:
        data = self._load()
        data[sha] = entry
        self.path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(data, indent=1, sort_keys=True))
        tmp.replace(self.path)


def gate_steps(cfg: CiConfig, tree: Path, checks: list[str]) -> list[st.Step]:
    cmds = st.python_steps(cfg.python, tree, cfg.build_root)
    return [
        st.Step("patch check", cmds["patches"]),
        st.Step("build", cmds["build"]),
        # C++ unit tests join here once any can run (issue #54); today this is the Python suite.
        st.Step("unit tests", cmds["unittest"]),
        st.Step("smoke launch", st.bench_argv(cfg.python, tree, cfg.build_root, ["smoke"])),
        st.Step("bench", st.bench_argv(cfg.python, tree, cfg.build_root, checks)),
    ]


def _describe(text: str, log_path: Path) -> str:
    """Summary plus the log path, whole path kept, inside GitHub's 140 characters."""
    tail = f" Log: {log_path}"
    return text[: max(0, 140 - len(tail))] + tail


def gate_pr(cfg: CiConfig, pr: PullRequest) -> str:
    # The author check comes first. Until it passes, nothing from the PR is fetched, checked out or run.
    if pr.author != cfg.maintainer:
        return SKIPPED_AUTHOR
    store = GatedStore(cfg.ci_root / "gated.json")
    if store.has(pr.head_sha):
        return SKIPPED_GATED

    sha = pr.head_sha
    short = sha[:12]
    log_dir = cfg.ci_root / "logs" / short
    log_path = log_dir / "gate.log"

    def post(state: str, text: str) -> None:
        cfg.gh.post_status(sha, state, _describe(text, log_path))

    post("pending", "Queued")
    log = st.Log(log_path, cfg.redact)
    state, summary, results = ERROR, "", []
    try:
        log.line(f"gate PR #{pr.number} {sha}")
        try:
            with cfg.lock(cfg.build_root, timeout=cfg.lock_timeout, log=log.line) as held:
                if st.fetch_ref(cfg.shell, cfg.repo, f"pull/{pr.number}/head") != sha:
                    post("error", "Superseded by a newer push; the next poll gates the new head.")
                    return STALE  # not recorded
                with st.worktree(cfg.shell, cfg.repo, cfg.ci_root / "worktrees" / short, sha) as tree:
                    # cfg.environ was copied before the lock; the steps need the marker, or their ./aerie build
                    # waits for the lock that this process holds.
                    env = st.clean_env({**cfg.environ, buildlock.ENV_HELD: held})
                    checks = st.bench_check_names(cfg.shell, cfg.python, tree, env, GATE_BENCH_EXCLUDE)
                    plan = gate_steps(cfg, tree, checks)
                    results = st.run_steps(cfg.shell, plan, cwd=tree, env=env, log=log,
                                           on_start=lambda step: post("pending", f"Running: {step.name}"))
            failed = next((r for r in results if r.rc != 0), None)
            if failed:
                state, summary = FAILURE, f"Failed at {failed.name} (exit {failed.rc})."
            else:
                state, summary = SUCCESS, f"All {len(results)} steps passed."
        except buildlock.BuildTreeBusy:
            post("error", "Build tree busy; will retry on the next poll.")
            return BUSY  # not recorded, so the next poll tries again
        except Exception as exc:  # harness failure, not a PR failure
            log.line(f"gate harness error: {exc!r}")
            state, summary = ERROR, "Gate harness error."
        log.line(summary)
        _write_summary(log_dir / "summary.json", cfg, pr, state, summary, results, log_path)
        post(state, summary)
        if state != ERROR:  # a harness error says nothing about the PR; the next poll tries again
            store.record(sha, {"pr": pr.number, "state": state, "at": cfg.now()})
        return state
    finally:
        log.close()


def _write_summary(path: Path, cfg: CiConfig, pr: PullRequest, state: str, summary: str,
                   results: list[st.StepResult], log_path: Path) -> None:
    data = {"pr": pr.number, "sha": pr.head_sha, "state": state, "summary": summary, "log": str(log_path),
            "steps": [{"name": r.name, "rc": r.rc, "seconds": round(r.seconds, 1)} for r in results]}
    path.write_text(cfg.redact(json.dumps(data, indent=1)))


@contextlib.contextmanager
def _poll_lock(path: Path):
    """One poller at a time; a second one exits at once."""
    fd = buildlock.try_flock(path)
    try:
        yield fd is not None
    finally:
        if fd is not None:
            os.close(fd)


def poll(cfg: CiConfig) -> list[tuple[PullRequest, str]]:
    with _poll_lock(cfg.ci_root / "poll.lock") as got:
        if not got:
            return []
        return [(pr, gate_pr(cfg, pr)) for pr in cfg.gh.open_prs()]
