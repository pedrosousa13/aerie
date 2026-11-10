"""Command line for ./aerie."""

from __future__ import annotations

import argparse
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

from . import (
    bump,
    buildlock,
    ccache,
    config,
    fallback,
    filter_lists,
    lock,
    patch_check,
    pipeline,
    protection_lists,
    prune,
    rust_crates,
    test_fonts,
)

REPO = Path(__file__).resolve().parents[2]
LOCK_PATH = REPO / "helium.lock"


def _add_common(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--build-root",
        metavar="DIR",
        help=f"where all build files go (env {config.ENV_BUILD_ROOT}; default {config.DEFAULT_BUILD_ROOT})",
    )


def _add_ccache_dir(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--ccache-dir",
        metavar="DIR",
        help=f"compiler cache folder, to share it between build roots (env {config.ENV_CACHE_DIR}; default <build root>/ccache)",
    )


def _add_lock_timeout(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--lock-timeout",
        type=float,
        metavar="SEC",
        help="how long to wait for the build-tree lock, which another build or the local CI may hold "
        "(default: wait; 0 fails at once)",
    )


def _locked(ctx: pipeline.Context, args: argparse.Namespace):
    return buildlock.build_tree_lock(
        ctx.paths.root, timeout=args.lock_timeout, log=lambda m: print(f"aerie: {m}", file=sys.stderr, flush=True)
    )


def _context(args: argparse.Namespace) -> pipeline.Context:
    root = config.resolve_build_root(args.build_root, os.environ, REPO)
    size = config.resolve_cache_size(getattr(args, "ccache_size", None), os.environ)
    paths = config.BuildPaths(root, config.resolve_ccache_dir(getattr(args, "ccache_dir", None), os.environ))
    stamp = time.strftime("%Y%m%d-%H%M%S")
    return pipeline.Context(
        repo=REPO,
        paths=paths,
        pins=lock.load_lock(LOCK_PATH),
        cache_size=size,
        jobs=getattr(args, "jobs", None),
        source_mode=fallback.resolve_mode(getattr(args, "source", None), os.environ),
        log_path=paths.logs / f"build-{stamp}.log",
    )


def cmd_build(args: argparse.Namespace) -> int:
    ctx = _context(args)
    if args.dry_run_layers:
        if args.clean:
            raise pipeline.BuildError("--dry-run-layers cannot be combined with --clean")
        return pipeline.show_layer_changes(ctx)
    with _locked(ctx, args):
        binary = pipeline.build(ctx, clean=args.clean)
    print(binary)
    return 0


def cmd_test_cpp(args: argparse.Namespace) -> int:
    ctx = _context(args)
    with _locked(ctx, args):
        return pipeline.test_cpp(ctx, targets=args.target, clean=args.clean)


def cmd_prune(args: argparse.Namespace) -> int:
    ctx = _context(args)
    with _locked(ctx, args):
        return _prune(ctx, args)


def _adblock_host_cache() -> set[str]:
    """The Bench's cached copy of the pinned adblock_score host list; pruning it would lose the offline fallback."""
    from aerie_bench import adblock_hosts
    try:
        return {adblock_hosts.cache_name(adblock_hosts.load_pin())}
    except adblock_hosts.HostListError:
        return set()


def _prune(ctx: pipeline.Context, args: argparse.Namespace) -> int:
    plan = prune.plan_prune(
        ctx.paths,
        pinned_downloads=set(ctx.pins.downloads)
        | _adblock_host_cache()
        | {Path(ctx.pins.tools["ccache"]["url"]).name}
        | ({Path(ctx.pins.fallback["url"]).name} if ctx.pins.fallback else set())
        | {table["archive"] for table in ctx.pins.bench.values()},
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


def cmd_patches_gates(args: argparse.Namespace) -> int:
    from . import treegate

    ctx = _context(args)
    tree = Path(args.tree) if args.tree else ctx.tree
    if not tree.is_dir():
        print(f"aerie patches gates: no source tree at {tree}", file=sys.stderr)
        return 2
    report, ok = treegate.format_report(treegate.run(tree, require=True))
    print(report)
    return 0 if ok else 1


def cmd_bump(args: argparse.Namespace) -> int:
    bump.check_gitmodules(REPO)
    pins = lock.load_lock(LOCK_PATH)
    release = bump.resolve_release(
        bump.remote_tags(bump.PLATFORM_UPSTREAM), bump.remote_tags(bump.CORE_UPSTREAM), to=args.to)
    print(f"current   {pins.platform_tag} (Chromium {pins.chromium_version})")
    print(f"latest    {release.platform_tag}")
    if release.platform_tag == pins.platform_tag or (args.to is None and not bump.is_newer(release.platform_tag, pins.platform_tag)):
        print("helium.lock is up to date")
        return bump.OK
    if args.check:
        print("a newer Helium release exists; run ./aerie bump")
        return bump.UPDATE_AVAILABLE
    old, new = bump.apply(REPO, release, log=lambda m: print(f"aerie: {m}", file=sys.stderr, flush=True))
    text = bump.summary(old.helium_version, old.chromium_version, new.helium_version, new.chromium_version)
    print(text)
    print("helium.lock and both submodules are moved; nothing is committed")
    patch_lines: list[str] = []
    ok: bool | None = None
    if not args.no_check:
        root = config.resolve_build_root(args.build_root, os.environ, REPO)
        paths = config.BuildPaths(root)
        bump.download_missing(REPO, new, paths.downloads, log=lambda m: print(f"aerie: {m}", file=sys.stderr, flush=True))

        def log(line: str) -> None:
            patch_lines.append(line)
            print(line, flush=True)

        ok = patch_check.run_check(REPO, paths, new, log=log) == 0
    if args.report:
        Path(args.report).write_text(bump.render_report(text, patch_lines, ok), encoding="utf-8")
    if ok is False:
        print("\nSome patches no longer apply (listed above with the hunk that failed). Fix them, then "
              "see docs/helium-bump.md.", file=sys.stderr)
        return bump.PATCHES_FAIL
    return bump.OK


def cmd_lists_update(args: argparse.Namespace) -> int:
    filter_lists.update(filter_lists.lock_path(REPO))
    protection_lists.update(protection_lists.lock_path(REPO))  # phishing and malware lists, same run
    print("lists and lock rewritten; review the diff, check the new lists, commit.")
    return 0


def cmd_crates_resolve(args: argparse.Namespace) -> int:
    import tempfile

    ctx = _context(args)
    tree = Path(args.tree) if args.tree else ctx.tree
    ctx.paths.tmp.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ctx.paths.tmp, prefix="crates-resolve-") as work:
        text = rust_crates.resolve(tree, ctx.pins.chromium_version, Path(work))
    out = REPO / rust_crates.LOCK_FILE
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text, encoding="utf-8")
    print(f"wrote {out}; review the diff, commit it.")
    return 0


def cmd_crates_audit_lock(args: argparse.Namespace) -> int:
    text = (REPO / rust_crates.LOCK_FILE).read_text(encoding="utf-8")
    out = Path(args.out)
    out.write_text(rust_crates.audit_lock(text), encoding="utf-8")
    print(f"wrote {out}; run: cargo audit --file {out}")
    return 0


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
    # The Bench drives the binary in the shared tree, so a build must not replace it half way through.
    with _locked(ctx, args):
        return bench_cli.run(args, ctx.paths.root, ctx.binary)


def cmd_locked(args: argparse.Namespace) -> int:
    command = args.cmd[1:] if args.cmd[:1] == ["--"] else args.cmd
    if not command:
        print("aerie locked: give a command after --, e.g. ./aerie locked -- sh -c './aerie build && ./aerie bench'",
              file=sys.stderr)
        return 2
    ctx = _context(args)
    with _locked(ctx, args):
        # The command inherits AERIE_BUILD_LOCK_HELD, so the ./aerie build and ./aerie bench it starts do not wait.
        try:
            proc = subprocess.Popen(command)
        except OSError as exc:
            print(f"aerie locked: cannot run {command[0]}: {exc}", file=sys.stderr)
            return 127
        # A stop signal goes to the command, and the lock stays until the command has exited: a build
        # that is still shutting down is still using the tree.
        signals = (signal.SIGTERM, signal.SIGINT)
        previous = {sig: signal.signal(sig, lambda num, frame: proc.send_signal(num)) for sig in signals}
        try:
            code = proc.wait()
        finally:
            for sig, handler in previous.items():
                signal.signal(sig, handler)
        return 128 - code if code < 0 else code


def cmd_hardening(args: argparse.Namespace) -> int:
    from aerie_hardening import __main__ as hardening

    return hardening.main([args.hardening_command, "--repo", str(REPO)])


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
    _add_ccache_dir(p_build)
    p_build.add_argument("-j", "--jobs", type=int, help="parallel build jobs (default: autoninja's choice)")
    p_build.add_argument(
        "--source",
        choices=fallback.SOURCE_MODES,
        help=f"where Chromium source comes from (env {fallback.ENV_SOURCE}; default auto: Helium's tarballs, "
        "or the official Chromium tarball if Helium's host is unreachable)",
    )
    p_build.add_argument("--clean", action="store_true", help="delete the source tree and the pristine snapshot first and start over (after a Helium bump)")
    p_build.add_argument(
        "--dry-run-layers",
        action="store_true",
        help="list the source-tree files the Aerie layers (patches, substitution) would change, then stop; "
        "writes nothing to the tree",
    )
    _add_lock_timeout(p_build)
    p_build.set_defaults(func=cmd_build)

    p_test = sub.add_parser(
        "test-cpp",
        help="build and run Aerie's C++ unit tests (fetches Chromium's test fonts from Google storage, test builds only)",
    )
    _add_common(p_test)
    p_test.add_argument("--ccache-size", metavar="SIZE", help=f"compiler cache cap (env {config.ENV_CACHE_SIZE})")
    _add_ccache_dir(p_test)
    p_test.add_argument("-j", "--jobs", type=int, help="parallel build jobs (default: autoninja's choice)")
    p_test.add_argument("--source", choices=fallback.SOURCE_MODES, help="same as ./aerie build --source")
    p_test.add_argument("--clean", action="store_true", help="same as ./aerie build --clean")
    p_test.add_argument(
        "--target",
        action="append",
        metavar="NAME",
        choices=[name for name, _ in pipeline.TEST_BINARIES],
        help="build and run only this test binary (repeatable; default: all of them)",
    )
    _add_lock_timeout(p_test)
    p_test.set_defaults(func=cmd_test_cpp)

    p_prune = sub.add_parser("prune", help="delete stale downloads, old source trees and old tools")
    _add_common(p_prune)
    p_prune.add_argument("--ccache-size", metavar="SIZE", help="compiler cache cap to enforce")
    _add_ccache_dir(p_prune)
    p_prune.add_argument("-n", "--dry-run", action="store_true", help="list what would be deleted")
    _add_lock_timeout(p_prune)
    p_prune.set_defaults(func=cmd_prune)

    p_locked = sub.add_parser(
        "locked",
        help="run a command while holding the build-tree lock (a train's build plus Bench, as one unit)",
    )
    _add_common(p_locked)
    _add_lock_timeout(p_locked)
    p_locked.add_argument("cmd", nargs=argparse.REMAINDER, metavar="-- CMD", help="the command and its arguments")
    p_locked.set_defaults(func=cmd_locked)

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
    p_gates = patches_sub.add_parser(
        "gates",
        help="run the source gates (Helium hostnames, search list, network defaults) on a patched source tree",
    )
    _add_common(p_gates)
    p_gates.add_argument("--tree", metavar="DIR", help="tree to check (default: the build's source tree)")
    p_gates.set_defaults(func=cmd_patches_gates)

    p_bump = sub.add_parser(
        "bump",
        help="move the Helium submodules and helium.lock to the newest Helium release, then run the patch check "
        "(exit 20 with --check when a newer release exists, 3 when patches no longer apply)",
    )
    _add_common(p_bump)
    p_bump.add_argument("--check", action="store_true", help="only look: print the current and latest release")
    p_bump.add_argument("--to", metavar="TAG", help="bump to this helium-linux release tag instead of the latest")
    p_bump.add_argument("--no-check", action="store_true", help="skip the patch check (it downloads the new Chromium tarballs)")
    p_bump.add_argument("--report", metavar="FILE", help="also write the result as Markdown (for a pull request body)")
    p_bump.set_defaults(func=cmd_bump)

    p_lists = sub.add_parser("lists", help="work with the built-in filter lists (EasyList, EasyPrivacy)")
    lists_sub = p_lists.add_subparsers(dest="lists_command", required=True)
    p_update = lists_sub.add_parser(
        "update",
        help="download the current lists, write them and the hash lock into overlay/third_party/filter_lists (review the diff, then commit)",
    )
    _add_common(p_update)
    p_update.set_defaults(func=cmd_lists_update)

    p_crates = sub.add_parser("crates", help="work with the vendored Rust crates (adblock-rust)")
    crates_sub = p_crates.add_subparsers(dest="crates_command", required=True)
    p_resolve = crates_sub.add_parser(
        "resolve",
        help="resolve adblock against the tree's Cargo.toml/Cargo.lock and rewrite rust/crates.lock.toml "
        "(needs crates.io; reads the tree, never writes it)",
    )
    _add_common(p_resolve)
    p_resolve.add_argument("--tree", metavar="DIR", help="source tree with the Aerie patches applied (default: the build tree)")
    p_resolve.set_defaults(func=cmd_crates_resolve)
    p_audit = crates_sub.add_parser(
        "audit-lock",
        help="write rust/crates.lock.toml as a Cargo.lock for `cargo audit --file` (offline)",
    )
    p_audit.add_argument("out", metavar="FILE", help="where to write the Cargo.lock")
    p_audit.set_defaults(func=cmd_crates_audit_lock)

    p_bench = sub.add_parser("bench", help="launch a built browser, drive it, record its traffic (see docs/bench.md)")
    _add_common(p_bench)
    from aerie_bench import cli as bench_cli

    bench_cli.add_arguments(p_bench, binary_required=False)
    _add_lock_timeout(p_bench)
    p_bench.set_defaults(func=cmd_bench)

    p_hardening = sub.add_parser(
        "hardening", help="generate or check docs/hardening.md and the settings page markup (see docs/hardening.md)")
    p_hardening.add_argument("hardening_command", choices=("generate", "check"))
    p_hardening.set_defaults(func=cmd_hardening)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (config.ConfigError, lock.VerificationError, pipeline.BuildError, fallback.FetchError,
            filter_lists.ListError, bump.BumpError, rust_crates.CratesError, buildlock.BuildTreeBusy,
            test_fonts.TestFontsError) as exc:
        print(f"aerie: error: {exc}", file=sys.stderr)
        return 1
