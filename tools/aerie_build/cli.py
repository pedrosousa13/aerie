"""Command line for ./aerie."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

from . import ccache, config, lock, patch_check, pipeline, prune

REPO = Path(__file__).resolve().parents[2]
LOCK_PATH = REPO / "helium.lock"


def _add_common(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--build-root",
        metavar="DIR",
        help=f"where all build files go (env {config.ENV_BUILD_ROOT}; default {config.DEFAULT_BUILD_ROOT})",
    )


def _context(args: argparse.Namespace) -> pipeline.Context:
    root = config.resolve_build_root(args.build_root, os.environ, REPO)
    size = config.resolve_cache_size(getattr(args, "ccache_size", None), os.environ)
    paths = config.BuildPaths(root)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    return pipeline.Context(
        repo=REPO,
        paths=paths,
        pins=lock.load_lock(LOCK_PATH),
        cache_size=size,
        jobs=getattr(args, "jobs", None),
        log_path=paths.logs / f"build-{stamp}.log",
    )


def cmd_build(args: argparse.Namespace) -> int:
    ctx = _context(args)
    binary = pipeline.build(ctx, clean=args.clean)
    print(binary)
    return 0


def cmd_prune(args: argparse.Namespace) -> int:
    ctx = _context(args)
    plan = prune.plan_prune(
        ctx.paths,
        pinned_downloads=set(ctx.pins.downloads) | {Path(ctx.pins.tools["ccache"]["url"]).name},
        current_tree=ctx.tree_name,
        current_tools=ctx.current_tools(),
    )
    if not plan:
        print(f"nothing to prune in {ctx.paths.root}")
    prune.execute(plan, dry_run=args.dry_run, root=ctx.paths.root)
    if ctx.ccache_bin.is_file() and not args.dry_run:
        # Re-apply the configured cap and evict down to it now.
        ccache.write_conf(ctx.paths.ccache, ctx.cache_size)
        env = config.isolated_env(ctx.paths, os.environ)
        sys.stdout.flush()
        subprocess.run([str(ctx.ccache_bin), "--cleanup"], env=env, check=True)
    return 0


def cmd_patches_check(args: argparse.Namespace) -> int:
    ctx = _context(args)
    return patch_check.run_check(REPO, ctx.paths, ctx.pins)


def cmd_info(args: argparse.Namespace) -> int:
    ctx = _context(args)
    print(f"helium version   {ctx.pins.helium_version} (Chromium {ctx.pins.chromium_version})")
    print(f"build root       {ctx.paths.root}")
    print(f"source tree      {ctx.tree}")
    print(f"binary           {ctx.binary}{'' if ctx.binary.exists() else '  (not built)'}")
    print(f"compiler cache   {ctx.paths.ccache} (cap {ctx.cache_size})")
    print(f"logs             {ctx.paths.logs}")
    return 0


def cmd_bench(args: argparse.Namespace) -> int:
    from aerie_bench import cli as bench_cli

    ctx = _context(args)
    return bench_cli.run(args, ctx.paths.root, ctx.binary)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="aerie", description="Build Aerie (currently: unmodified Helium).")
    sub = parser.add_subparsers(dest="command", required=True)

    p_build = sub.add_parser("build", help="fetch, patch and compile the browser")
    _add_common(p_build)
    p_build.add_argument(
        "--ccache-size",
        metavar="SIZE",
        help=f"compiler cache cap, e.g. 30G (env {config.ENV_CACHE_SIZE}; default {config.DEFAULT_CACHE_SIZE})",
    )
    p_build.add_argument("-j", "--jobs", type=int, help="parallel build jobs (default: autoninja's choice)")
    p_build.add_argument("--clean", action="store_true", help="delete the source tree first and start over")
    p_build.set_defaults(func=cmd_build)

    p_prune = sub.add_parser("prune", help="delete stale downloads, old source trees and old tools")
    _add_common(p_prune)
    p_prune.add_argument("--ccache-size", metavar="SIZE", help="compiler cache cap to enforce")
    p_prune.add_argument("-n", "--dry-run", action="store_true", help="list what would be deleted")
    p_prune.set_defaults(func=cmd_prune)

    p_info = sub.add_parser("info", help="print the build root layout and binary path")
    _add_common(p_info)
    p_info.set_defaults(func=cmd_info)

    p_patches = sub.add_parser("patches", help="work with the Aerie patch series")
    patches_sub = p_patches.add_subparsers(dest="patches_command", required=True)
    p_check = patches_sub.add_parser(
        "check",
        help="check that Helium's and Aerie's patches all apply to the pinned Chromium (sparse tree)",
    )
    _add_common(p_check)
    p_check.set_defaults(func=cmd_patches_check)

    p_bench = sub.add_parser("bench", help="launch a built browser, drive it, record its traffic (see docs/bench.md)")
    _add_common(p_bench)
    from aerie_bench import cli as bench_cli

    bench_cli.add_arguments(p_bench, binary_required=False)
    p_bench.set_defaults(func=cmd_bench)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (config.ConfigError, lock.VerificationError, pipeline.BuildError) as exc:
        print(f"aerie: error: {exc}", file=sys.stderr)
        return 1
