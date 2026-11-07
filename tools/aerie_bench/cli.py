"""Command line for the Bench. Wired into ./aerie as ``./aerie bench``."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from . import checks, scenario
from .runner import BenchError, run_bench

EXIT_PASS, EXIT_FAIL, EXIT_HARNESS = 0, 1, 2


def add_arguments(p: argparse.ArgumentParser, *, binary_required: bool) -> None:
    p.add_argument("--binary", metavar="PATH", required=binary_required,
                   help="browser to test (any Chromium-family binary)"
                        + ("" if binary_required else "; default: the binary ./aerie build produces"))
    p.add_argument("--check", metavar="NAME", action="append",
                   help="run this check (repeatable); available: " + ", ".join(
                       sorted(n for n in checks.REGISTRY if n != "scenario")) + "; default: smoke")
    p.add_argument("--scenario", metavar="FILE", help="JSON scenario file (checks and page steps)")
    p.add_argument("--headed", action="store_true", help="show the browser window (default: headless)")
    p.add_argument("--no-sandbox", action="store_true",
                   help="pass --no-sandbox (containers only; recorded in the results)")
    p.add_argument("--out", metavar="DIR", help="results directory (default: <build root>/bench/<UTC timestamp>)")
    p.add_argument("--timeout", type=float, default=30.0, metavar="SEC", help="per CDP command timeout (default 30)")
    p.add_argument("--services", choices=("auto", "require", "optional"), default="auto",
                   help="browser_requests: require the seeded Helium services call to reach the fixture "
                        "server (auto: required for the default build binary, and for an explicit --binary "
                        "that identifies as Helium/Aerie; optional otherwise)")
    p.add_argument("--services-wait", type=float, default=20.0, metavar="SEC",
                   help="browser_requests: longest wait for the first browser-level request (default 20)")
    p.add_argument("--flag", action="append", default=[], metavar="FLAG",
                   help="extra browser command-line flag (repeatable)")


def run(args: argparse.Namespace, build_root: Path, default_binary: Path | None) -> int:
    binary = args.binary or default_binary
    services = {"auto": None, "require": True, "optional": False}[args.services]
    if not args.binary and services is None:
        services = True  # our own build is never guessed at by name or version string
    if binary is None:
        print("aerie bench: --binary is required", file=sys.stderr)
        return EXIT_HARNESS
    try:
        if args.scenario:
            sc = scenario.load(Path(args.scenario))
        else:
            sc = scenario.Scenario()
        if args.check:
            sc = scenario.Scenario(sc.name, tuple(args.check), sc.steps)
        result = run_bench(binary, sc, build_root, out_dir=Path(args.out) if args.out else None,
                           headless=not args.headed, no_sandbox=args.no_sandbox,
                           timeout=args.timeout, extra_flags=tuple(args.flag),
                           check_options={"browser_requests": {
                               "wait_seconds": args.services_wait,
                               "require_services": services}})
    except (BenchError, scenario.ScenarioError) as exc:
        print(f"aerie bench: error: {exc}", file=sys.stderr)
        return EXIT_HARNESS
    print((result.out_dir / "summary.txt").read_text(), end="")
    print(f"results: {result.out_dir / 'results.json'}")
    return EXIT_PASS if result.status == "pass" else EXIT_FAIL


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="python3 -m aerie_bench", description="Run the Aerie Bench.")
    p.add_argument("--build-root", metavar="DIR", help="build root (env AERIE_BUILD_ROOT; default /mnt/linux2/aerie-build)")
    add_arguments(p, binary_required=True)
    args = p.parse_args(argv)
    from aerie_build import config
    import os
    repo = Path(__file__).resolve().parents[2]
    try:
        root = config.resolve_build_root(args.build_root, os.environ, repo)
    except config.ConfigError as exc:
        print(f"aerie bench: error: {exc}", file=sys.stderr)
        return EXIT_HARNESS
    return run(args, root, None)
