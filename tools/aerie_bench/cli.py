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
    p.add_argument("--services", choices=("auto", "require", "optional", "skip"), default="auto",
                   help="browser_requests: require the seeded Helium services call to reach the fixture "
                        "server (auto: skipped for the default build binary, which is Aerie; for an explicit "
                        "--binary, required if it identifies as Helium/Aerie, optional otherwise; skip: do not run the probe, for Aerie builds, "
                        "which make no requests of their own)")
    p.add_argument("--services-wait", type=float, default=20.0, metavar="SEC",
                   help="browser_requests: longest wait for the first browser-level request (default 20)")
    p.add_argument("--zero-traffic-idle", type=float, default=300.0, metavar="SEC",
                   help="zero_traffic: idle phase length (default 300, the issue's 5 minutes)")
    p.add_argument("--zero-traffic-settle", type=float, default=60.0, metavar="SEC",
                   help="zero_traffic: quiet time after the first page, the first-launch phase (default 60)")
    p.add_argument("--zero-traffic-strict", action="store_true",
                   help="zero_traffic: also fail on blocked attempts to .qjz9zk dummy hosts and trk: URLs")
    p.add_argument("--widevine-online", type=float, default=0.0, metavar="SEC",
                   help="widevine_opt_in: also wait up to SEC in an opted-in browser for Widevine to download "
                        "and become available (needs a network that reaches Google; default 0, off)")
    p.add_argument("--workspaces-enforce", action="store_true",
                   help="workspace_isolation: judge every probe for real. Without it the probes listed in "
                        "EXPECTED_TO_FAIL (all of them until Workspaces ship) may fail and the check reports xfail")
    p.add_argument("--baseline", metavar="PATH",
                   help="also run zero_traffic on this browser (an unmodified Helium build, or any browser "
                        "that talks to Google) and require it to FAIL; if it passes the check is vacuous")
    p.add_argument("--flag", action="append", default=[], metavar="FLAG",
                   help="extra browser command-line flag (repeatable)")


def run(args: argparse.Namespace, build_root: Path, default_binary: Path | None) -> int:
    binary = args.binary or default_binary
    services = {"auto": None, "require": True, "optional": False, "skip": "skip"}[args.services]
    skip_note = None
    if not args.binary and services is None:
        # Our own build is Aerie, which makes no requests of its own: the services probe cannot pass on it.
        # Never decided by name or version string, only by "no --binary was given".
        services = "skip"
        skip_note = ("default build binary (no --binary): browser_requests is skipped; proving that the Bench "
                     "sees browser-level traffic needs --baseline <helium binary> (or --services require)")
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
        if args.baseline and "zero_traffic" not in sc.checks:
            raise BenchError("--baseline needs --check zero_traffic")
        if skip_note and "browser_requests" in sc.checks:
            print(f"aerie bench: note: {skip_note}")
        options = {"browser_requests": {"wait_seconds": args.services_wait, "require_services": services,
                                        "skip_note": skip_note},
                   "zero_traffic": {"idle_seconds": args.zero_traffic_idle,
                                    "settle_seconds": args.zero_traffic_settle,
                                    "strict": args.zero_traffic_strict},
                   "widevine_opt_in": {"online_seconds": args.widevine_online},
                   "workspace_isolation": {"enforce": args.workspaces_enforce}}
        common = dict(headless=not args.headed, no_sandbox=args.no_sandbox, timeout=args.timeout,
                      extra_flags=tuple(args.flag), check_options=options)
        result = run_bench(binary, sc, build_root, out_dir=Path(args.out) if args.out else None, **common)
        print((result.out_dir / "summary.txt").read_text(), end="")
        print(f"results: {result.out_dir / 'results.json'}")
        code = EXIT_PASS if result.status == "pass" else EXIT_FAIL
        if args.baseline:
            base_out = Path(str(args.out).rstrip("/") + "-baseline") if args.out else None
            base = run_bench(Path(args.baseline), scenario.Scenario("zero-traffic-baseline", ("zero_traffic",)),
                             build_root, out_dir=base_out, **common)
            code = max(code, _report_baseline(base, args.baseline))
        return code
    except (BenchError, scenario.ScenarioError) as exc:
        print(f"aerie bench: error: {exc}", file=sys.stderr)
        return EXIT_HARNESS


def _report_baseline(base, binary) -> int:
    """The baseline must fail zero_traffic. A pass means the check cannot see traffic."""
    check = next(c for c in base.data["checks"] if c["name"] == "zero_traffic")
    print(f"\nbaseline  {binary}")
    print(f"results:  {base.out_dir / 'results.json'}")
    if check["status"] == "fail":
        print(f"baseline  FAILED as expected: {check['detail']}")
        return EXIT_PASS
    if check["status"] == "pass":
        print("baseline  PASSED: the check is vacuous (it saw no traffic on a browser that should have "
              "contacted Google or Helium). Do not trust a pass on any other browser.")
    else:
        print(f"baseline  {check['status'].upper()}: {check['detail']}; the check did not run, so it proves nothing")
    return EXIT_FAIL


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
