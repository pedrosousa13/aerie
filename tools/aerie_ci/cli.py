"""python3 -m tools.aerie_ci poll | gate PR | nightly | install

A future Actions workflow on a real runner calls the same commands.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

from aerie_build import buildlock, config

from . import gate, install, nightly

REPO = Path(__file__).resolve().parents[2]
GATE_LOCK_WAIT = 6 * 3600.0
NIGHTLY_LOCK_WAIT = 12 * 3600.0
ENV_NIGHTLY_ISSUE = "AERIE_CI_NIGHTLY_ISSUE"


def _make_config(args: argparse.Namespace) -> gate.CiConfig:
    root = config.resolve_build_root(args.build_root, os.environ, REPO)
    return gate.CiConfig(repo=REPO, build_root=root, lock_timeout=args.lock_timeout)


def cmd_poll(args: argparse.Namespace) -> int:
    for pr, outcome in gate.poll(_make_config(args)):
        print(f"PR #{pr.number} {pr.head_sha[:12]}: {outcome}")
    return 0


def cmd_gate(args: argparse.Namespace) -> int:
    cfg = _make_config(args)
    for pr in cfg.gh.open_prs():
        if pr.number == args.pr:
            outcome = gate.gate_pr(cfg, pr)  # the author check lives in gate_pr
            print(f"PR #{pr.number} {pr.head_sha[:12]}: {outcome}")
            return 0 if outcome == gate.SUCCESS else 1
    print(f"aerie ci: no open PR #{args.pr}", file=sys.stderr)
    return 1


def cmd_nightly(args: argparse.Namespace) -> int:
    issue = os.environ.get(ENV_NIGHTLY_ISSUE)
    outcome = nightly.run_nightly(_make_config(args), report_issue=int(issue) if issue else None)
    print(f"nightly: {outcome}")
    return 0 if outcome == nightly.PASS else 1


def cmd_install(args: argparse.Namespace) -> int:
    dest = Path(args.dest) if args.dest else install.default_dest()
    for path in install.install(dest, REPO, enable=args.enable):
        print(f"wrote {path}")
    if not args.enable:
        print("not started. To start the timers: systemctl --user daemon-reload && "
              "systemctl --user enable --now aerie-ci-poll.timer aerie-ci-nightly.timer")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="python3 -m tools.aerie_ci", description="Local CI for the Aerie build machine.")
    sub = parser.add_subparsers(dest="command", required=True)

    def common(p: argparse.ArgumentParser, wait: float) -> None:
        p.add_argument("--build-root", metavar="DIR", help=f"build root (env {config.ENV_BUILD_ROOT})")
        p.add_argument("--lock-timeout", type=float, default=wait, metavar="SEC",
                       help=f"longest wait for the build-tree lock (default {int(wait)})")

    p = sub.add_parser("poll", help="gate every open PR of the maintainer that has a new head SHA")
    common(p, GATE_LOCK_WAIT)
    p.set_defaults(func=cmd_poll)
    p = sub.add_parser("gate", help="gate one PR now (still only the maintainer's)")
    common(p, GATE_LOCK_WAIT)
    p.add_argument("pr", type=int)
    p.set_defaults(func=cmd_gate)
    p = sub.add_parser("nightly", help="clean build and full Bench on main")
    common(p, NIGHTLY_LOCK_WAIT)
    p.set_defaults(func=cmd_nightly)
    p = sub.add_parser("install", help="write the systemd user units (does not start them unless --enable)")
    p.add_argument("--dest", metavar="DIR", help="default ~/.config/systemd/user")
    p.add_argument("--enable", action="store_true", help="also run systemctl --user daemon-reload and enable --now")
    p.set_defaults(func=cmd_install)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (config.ConfigError, buildlock.BuildTreeBusy, RuntimeError) as exc:
        print(f"aerie ci: error: {exc}", file=sys.stderr)
        return 1
