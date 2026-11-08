"""The nightly: a clean build and the full Bench on main, results kept under a date."""

from __future__ import annotations

import json
import time
from pathlib import Path
from typing import Callable

from aerie_build import buildlock

from . import steps as st
from .gate import CiConfig

PASS, FAIL, BUSY = "pass", "fail", "busy"


def run_nightly(cfg: CiConfig, *, today: Callable[[], str] = lambda: time.strftime("%Y-%m-%d"),
                report_issue: int | None = None) -> str:
    date = today()
    day = cfg.ci_root / "nightly" / date
    bench_out = cfg.build_root / "bench" / date  # Bench output lives in the nightly build root
    log = st.Log(day / "nightly.log", cfg.redact)
    state, summary, sha, results = FAIL, "", "", []
    try:
        log.line(f"nightly {date}")
        try:
            with cfg.lock(cfg.build_root, timeout=cfg.lock_timeout, log=log.line) as held:
                sha = st.fetch_ref(cfg.shell, cfg.repo, "main")
                with st.worktree(cfg.shell, cfg.repo, cfg.ci_root / "worktrees" / f"nightly-{date}", sha) as tree:
                    env = st.clean_env({**cfg.environ, buildlock.ENV_HELD: held})  # see gate.py
                    cmds = st.python_steps(cfg.python, tree, cfg.build_root, cfg.ccache_dir)
                    # Every registered check, zero_traffic included. --clean drops the source tree and the
                    # pristine snapshot; the compiler cache stays (see docs/ci.md).
                    checks = st.bench_check_names(cfg.shell, cfg.python, tree, env, exclude=())
                    plan = [st.Step("clean build", cmds["clean_build"]),
                            st.Step("full bench", st.bench_argv(cfg.python, tree, cfg.build_root, checks,
                                                                out=bench_out))]
                    results = st.run_steps(cfg.shell, plan, cwd=tree, env=env, log=log)
            failed = next((r for r in results if r.rc != 0), None)
            state = FAIL if failed else PASS
            summary = f"Failed at {failed.name} (exit {failed.rc})." if failed else "Clean build and full Bench passed."
        except buildlock.BuildTreeBusy:
            state, summary = BUSY, "Build tree busy; the nightly did not run."
        except Exception as exc:
            log.line(f"nightly harness error: {exc!r}")
            state, summary = FAIL, "Nightly harness error."
        log.line(summary)
        data = {"date": date, "sha": sha, "state": state, "summary": summary, "log": str(log.path),
                "bench": str(bench_out),
                "steps": [{"name": r.name, "rc": r.rc, "seconds": round(r.seconds, 1)} for r in results]}
        (day / "summary.json").write_text(cfg.redact(json.dumps(data, indent=1)))
        if state != PASS:
            _report(cfg, date, summary, log.path, bench_out, report_issue)
        return state
    finally:
        log.close()


def _report(cfg: CiConfig, date: str, summary: str, log_path: Path, bench_out: Path, issue: int | None) -> None:
    body = f"Nightly CI {date} on main: {summary}\n\nLog: {log_path}\nBench results: {bench_out}\n"
    if issue is not None:
        cfg.gh.comment_issue(issue, body)
    else:
        cfg.gh.create_issue(f"Nightly CI failed {date}", body)
