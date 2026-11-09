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
    p.add_argument("--adblock-baseline", metavar="FILE",
                   help="adblock_score: compare with this stored score (the adblock_score.json of an earlier run). "
                        "Fails when the score dropped by more than 2 points or a host that was blocked now gets "
                        "through. A file that cannot be read is an error. Without it the check passes and "
                        "records the score")
    p.add_argument("--adblock-online", type=float, default=0.0, metavar="SEC",
                   help="adblock_score: also load the live https://adblock.turtlecute.org in a second browser, wait up "
                        "to SEC for its test to finish and record its score. Informational, never fails the run; "
                        "needs the network, so it is off (0) by default")
    p.add_argument("--adblock-update-pin", nargs="?", const="", metavar="COMMIT",
                   help="download the Turtlecute host list at COMMIT (default: the newest commit that changed it), "
                        "check that it parses, write tools/aerie_bench/adblock_hosts.lock.json and cache the file; "
                        "then exit. Review the diff of the lock and commit it")
    p.add_argument("--workspaces-enforce", action="store_true",
                   help="workspace_isolation: judge every probe for real. Without it the probes listed in "
                        "EXPECTED_TO_FAIL (all of them until Workspaces ship) may fail and the check reports xfail")
    p.add_argument("--baseline", metavar="PATH",
                   help="also run zero_traffic on this browser (an unmodified Helium build, or any browser "
                        "that talks to Google) and require it to FAIL; if it passes the check is vacuous")
    p.add_argument("--perf-baseline", metavar="FILE",
                   help="perf: compare against this perf document (main.json of a --perf-record directory) "
                        "and fail when a metric regressed past its threshold. A file that does not exist "
                        "fails the run, unless --perf-first-run is given")
    p.add_argument("--perf-first-run", action="store_true",
                   help="perf: with --perf-baseline, pass when the baseline file does not exist yet "
                        "(the very first run, before any main build was recorded)")
    p.add_argument("--download-benchmarks", action="store_true",
                   help="download the pinned Speedometer and JetStream copies into the build root, check their "
                        "sha256 and unpack them (helium.lock, docs/bench.md); needs the network, once")
    p.add_argument("--perf-record", metavar="DIR",
                   help="perf: after a passing run, append perf.json to DIR/history.jsonl and make it DIR/main.json "
                        "(use it on main builds only)")
    p.add_argument("--perf-compare-binary", metavar="PATH",
                   help="perf: run the same suite on this browser (Helium, stock Chromium) and write "
                        "perf-comparison.md with both side by side")
    p.add_argument("--perf-compare-name", default="Helium", metavar="NAME",
                   help="perf: how the --perf-compare-binary browser is named in the report (default Helium)")
    p.add_argument("--perf-skip-benchmarks", action="store_true",
                   help="perf: do not run Speedometer and JetStream; their metrics are recorded as skipped. "
                        "Without this flag a benchmark that cannot be measured fails the check")
    p.add_argument("--perf-settle", type=float, default=3.0, metavar="SEC",
                   help="perf: wait after the ad-heavy pages before reading memory (default 3)")
    p.add_argument("--flag", action="append", default=[], metavar="FLAG",
                   help="extra browser command-line flag (repeatable)")


def _update_adblock_pin(args: argparse.Namespace, build_root: Path) -> int:
    from . import adblock_hosts
    try:
        adblock_hosts.update_pin(adblock_hosts.LOCK_FILE, build_root, commit=args.adblock_update_pin or None)
    except adblock_hosts.HostListError as exc:
        print(f"aerie bench: error: cannot pin the host list: {exc}", file=sys.stderr)
        return EXIT_HARNESS
    return EXIT_PASS


def run(args: argparse.Namespace, build_root: Path, default_binary: Path | None) -> int:
    if args.adblock_update_pin is not None:
        return _update_adblock_pin(args, build_root)
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
        if (args.perf_baseline or args.perf_record or args.perf_compare_binary or args.perf_first_run) \
                and "perf" not in sc.checks:
            raise BenchError("--perf-baseline, --perf-first-run, --perf-record and --perf-compare-binary "
                             "need --check perf")
        if args.perf_first_run and not args.perf_baseline:
            raise BenchError("--perf-first-run needs --perf-baseline")
        if args.download_benchmarks:
            from aerie_build import lock

            from . import benchmarks
            try:
                benchmarks.fetch_all(build_root)
            except (benchmarks.BenchmarkError, lock.VerificationError) as exc:
                raise BenchError(f"cannot fetch the benchmark copies: {exc}") from exc
        if (args.adblock_baseline or args.adblock_online) and "adblock_score" not in sc.checks:
            raise BenchError("--adblock-baseline and --adblock-online need --check adblock_score")
        if args.adblock_online < 0:
            raise BenchError("--adblock-online takes a number of seconds, 0 or more")
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
                   "adblock_score": {"build_root": str(build_root), "baseline": args.adblock_baseline,
                                     "online_seconds": args.adblock_online},
                   "workspace_isolation": {"enforce": args.workspaces_enforce},
                   "perf": {"baseline": args.perf_baseline, "settle_seconds": args.perf_settle,
                            "first_run": args.perf_first_run, "benchmarks": not args.perf_skip_benchmarks}}
        common = dict(headless=not args.headed, no_sandbox=args.no_sandbox, timeout=args.timeout,
                      extra_flags=tuple(args.flag), check_options=options)
        result = run_bench(binary, sc, build_root, out_dir=Path(args.out) if args.out else None, **common)
        print((result.out_dir / "summary.txt").read_text(), end="")
        print(f"results: {result.out_dir / 'results.json'}")
        code = EXIT_PASS if result.status == "pass" else EXIT_FAIL
        if "perf" in sc.checks:
            code = max(code, _perf_followup(args, result, build_root, common))
        if args.baseline:
            base_out = Path(str(args.out).rstrip("/") + "-baseline") if args.out else None
            base = run_bench(Path(args.baseline), scenario.Scenario("zero-traffic-baseline", ("zero_traffic",)),
                             build_root, out_dir=base_out, **common)
            code = max(code, _report_baseline(base, args.baseline))
        return code
    except (BenchError, scenario.ScenarioError) as exc:
        print(f"aerie bench: error: {exc}", file=sys.stderr)
        return EXIT_HARNESS


def _perf_followup(args, result, build_root, common) -> int:
    """Record the run as the new baseline and run the comparison browser, as asked.

    Always passes: the comparison browser is a reference, so a crash or missing metrics only prints a note."""
    from . import perf
    doc_path = result.out_dir / "perf.json"
    if not doc_path.is_file():
        return EXIT_PASS  # the check errored before it wrote anything; its own status already failed the run
    doc = perf.load(doc_path)
    if args.perf_record:
        if result.status == "pass":
            print(f"perf: recorded in {perf.record(Path(args.perf_record), doc)}")
        else:
            print("perf: run did not pass; not recorded")
    if args.perf_compare_binary:
        out = Path(str(args.out).rstrip("/") + "-compare") if args.out else None
        other = run_bench(Path(args.perf_compare_binary), scenario.Scenario("perf-comparison", ("perf",)),
                          build_root, out_dir=out, **{**common, "check_options": {"perf": {
                              **common["check_options"]["perf"], "baseline": None}}})
        other_path = other.out_dir / "perf.json"
        if not other_path.is_file():
            print(f"perf: the comparison browser gave no metrics ({other.out_dir}); no comparison written")
            return EXIT_PASS
        text = perf.side_by_side(doc, perf.load(other_path), args.perf_compare_name)
        (result.out_dir / "perf-comparison.md").write_text(text)
        print("\n" + text + f"report: {result.out_dir / 'perf-comparison.md'}")
    return EXIT_PASS


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
    elif check["status"] in ("xfail", "xpass"):
        print(f"baseline  {check['status'].upper()}: {check['detail']}; zero_traffic is not marked expected to "
              "fail, so this is not a real failure and proves nothing")
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
