"""The build: fetch pinned sources, apply Helium's patches, compile.

The steps mirror helium-linux/scripts/build.sh and shared.sh, with three
changes: all paths sit under the build root, every download is also checked
against helium.lock, and each source-tree step leaves a stamp so an
interrupted build resumes where it stopped.
"""

from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Sequence

from . import (
    aerie_version, branding, ccache, fallback, filter_lists, helium, helium_deps, helium_inputs, helium_steps,
    layers, lock, overlay, patch_check, patches, protection_lists, rust_crates, substitution,
)
from .config import BuildPaths, isolated_env

ARCH = "x64"
TARGETS = ("chrome", "chromedriver")
HOST_TOOLS = ("git", "tar", "zstd", "patch")
MIN_FREE_BYTES = 150 * 1024**3


class BuildError(RuntimeError):
    pass


@dataclass
class Context:
    repo: Path
    paths: BuildPaths
    pins: lock.Lock
    cache_size: str
    jobs: int | None
    log_path: Path
    source_mode: str = "auto"
    opener: fallback.Opener = fallback.default_opener

    @property
    def core(self) -> Path:
        return self.repo / "helium-chromium"

    @property
    def platform(self) -> Path:
        return self.repo / "helium-linux"

    @property
    def tree_name(self) -> str:
        return f"helium-{self.pins.helium_version}"

    @property
    def tree(self) -> Path:
        return self.paths.tree(self.tree_name)

    @property
    def pristine_dir(self) -> Path:
        return self.paths.pristine / self.tree_name

    @property
    def out_dir(self) -> Path:
        return self.paths.out_dir(self.tree_name)

    @property
    def binary(self) -> Path:
        return self.out_dir / "aerie"

    @property
    def ccache_bin(self) -> Path:
        version = self.pins.tools["ccache"]["version"]
        return self.paths.toolchain / f"ccache-{version}" / "ccache"

    @property
    def venv(self) -> Path:
        return self.paths.toolchain / "venv"

    @property
    def python(self) -> Path:
        return self.venv / "bin" / "python3"

    def current_tools(self) -> set[str]:
        return {self.ccache_bin.parent.name, self.venv.name}


class Logger:
    def __init__(self, path: Path):
        path.parent.mkdir(parents=True, exist_ok=True)
        self._fh = open(path, "a", encoding="utf-8", errors="replace")

    def __call__(self, message: str) -> None:
        line = f"[aerie {time.strftime('%H:%M:%S')}] {message}"
        print(line, flush=True)
        self._fh.write(line + "\n")
        self._fh.flush()

    def raw(self, text: str) -> None:
        sys.stdout.write(text)
        sys.stdout.flush()
        self._fh.write(text)

    def close(self) -> None:
        self._fh.close()


def run(log: Logger, cmd: Sequence[str | Path], env: dict, cwd: Path | None = None) -> None:
    cmd = [str(c) for c in cmd]
    log(f"$ {' '.join(cmd)}" + (f"  (in {cwd})" if cwd else ""))
    proc = subprocess.Popen(
        cmd,
        cwd=cwd,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        stdin=subprocess.DEVNULL,
        text=True,
        errors="replace",
    )
    assert proc.stdout is not None
    for line in proc.stdout:
        log.raw(line)
    if proc.wait() != 0:
        raise BuildError(f"command failed with exit code {proc.returncode}: {' '.join(cmd)}")


def git_head(path: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "HEAD"],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise BuildError(
            f"{path} is not a git checkout; run: git submodule update --init helium-chromium helium-linux"
        )
    return result.stdout.strip()


# --- steps -------------------------------------------------------------------


def preflight(ctx: Context, log: Logger) -> None:
    missing = [tool for tool in HOST_TOOLS if shutil.which(tool) is None]
    if missing:
        raise BuildError(f"missing host tools: {', '.join(missing)} (see README prerequisites)")
    for path, expected in ((ctx.core, ctx.pins.core_commit), (ctx.platform, ctx.pins.platform_commit)):
        head = git_head(path)
        if head != expected:
            raise BuildError(
                f"{path.name} is at {head}, helium.lock pins {expected}; "
                "run: git submodule update --init helium-chromium helium-linux"
            )
    chromium = (ctx.core / "chromium_version.txt").read_text().strip()
    if chromium != ctx.pins.chromium_version:
        raise BuildError(f"helium-chromium wants Chromium {chromium}, helium.lock says {ctx.pins.chromium_version}")
    version = helium.helium_version(ctx.core, ctx.platform)
    if version != ctx.pins.helium_version:
        raise BuildError(f"submodules make Helium {version}, helium.lock says {ctx.pins.helium_version}")
    check_aerie_patches(ctx.repo)
    for directory in ctx.paths.scratch_dirs():
        directory.mkdir(parents=True, exist_ok=True)
    free = shutil.disk_usage(ctx.paths.root).free
    log(f"build root {ctx.paths.root}: {free / 1024**3:.0f} GiB free")
    if free < MIN_FREE_BYTES and not (ctx.tree / ".downloaded.stamp").exists():
        log("WARNING: a clean build needs about 150-200 GB; this build may run out of space")
    log(f"Helium {version} (Chromium {chromium}); source tree {ctx.tree}")


def check_aerie_patches(repo: Path) -> None:
    """Fail before any download when Aerie's own patch directory is malformed."""
    problems = patches.lint_patch_dir(
        patch_check.layers(repo)[-1].directory, patches.load_substitution_rules(repo / "helium-chromium"))
    problems += patch_check.drop_problems(repo)[1]
    if problems:
        raise BuildError(
            "Aerie patch series has problems:\n"
            + "\n".join(f"  {p.patch}: {p.message}" for p in problems)
        )


def install_ccache(ctx: Context, log: Logger, env: dict) -> None:
    tool = ctx.pins.tools["ccache"]
    if ctx.ccache_bin.is_file():
        log(f"ccache {tool['version']} already installed")
    else:
        archive = ctx.paths.downloads / Path(tool["url"]).name
        signature = archive.with_name(archive.name + ".minisig")
        for url, dest in ((tool["url"], archive), (tool["url"] + ".minisig", signature)):
            if not dest.is_file():
                log(f"downloading {url}")
                partial = dest.with_name(dest.name + ".partial")
                urllib.request.urlretrieve(url, partial)
                partial.replace(dest)
        lock.verify_tool_archive("ccache", tool, archive, signature.read_text(encoding="utf-8"))
        log(f"{archive.name}: minisign signature OK (ccache release key)")
        target = ctx.ccache_bin.parent
        with tarfile.open(archive) as tar:
            member = next(m for m in tar.getmembers() if m.name.endswith("/ccache") and m.isfile())
            member.name = "ccache"
            target.mkdir(parents=True, exist_ok=True)
            tar.extract(member, target, filter="data")
        ctx.ccache_bin.chmod(0o755)
    ccache.write_conf(ctx.paths.ccache, ctx.cache_size)
    run(log, [ctx.ccache_bin, "--show-config"], env)


def install_python_env(ctx: Context, log: Logger, env: dict) -> None:
    requirements = ctx.repo / "tools" / "requirements.txt"
    stamp = ctx.venv / ".requirements.sha256"
    digest = lock.sha256_file(requirements)
    if stamp.is_file() and stamp.read_text().strip() == digest:
        log("python venv up to date")
        return
    run(log, [sys.executable, "-m", "venv", "--clear", ctx.venv], env)
    run(
        log,
        [ctx.python, "-m", "pip", "install", "--disable-pip-version-check", "--no-input",
         "--require-hashes", "--only-binary=:all:", "-r", requirements],
        env,
    )
    stamp.write_text(digest + "\n")


def _retrieve(ctx: Context, log: Logger, env: dict, inis: list[Path]) -> None:
    # Downloads anything missing and checks the publisher's digests.
    run(log, [ctx.python, ctx.core / "utils" / "downloads.py", "retrieve", "-i", *inis,
              "-c", ctx.paths.downloads, "--hide-progress-bar"], env)


def _unpack(ctx: Context, log: Logger, env: dict, inis: list[Path]) -> None:
    run(log, [ctx.python, ctx.core / "utils" / "downloads.py", "unpack", "-i", *inis,
              "-c", ctx.paths.downloads, ctx.tree], env)


def _verify_downloads(ctx: Context, log: Logger, env: dict, inis: list[Path]) -> None:
    """Retrieve what is missing and check every file against helium.lock. Unpacks nothing."""
    _retrieve(ctx, log, env, inis)
    names = lock.required_downloads(inis, ctx.pins.chromium_version)
    log(f"checking {len(names)} download(s) against helium.lock")
    lock.verify_downloads(ctx.pins, ctx.paths.downloads, names)


def _helium_inis(ctx: Context) -> list[Path]:
    return [ctx.core / "downloads.ini", ctx.platform / "downloads.ini"]


def _deps_inis(ctx: Context) -> list[Path]:
    """Helium's deps.ini without the assets Aerie drops (the uBlock Origin build)."""
    return [helium_deps.write_filtered(ctx.core / "deps.ini", ctx.paths.tmp / "helium-deps" / "deps.ini")]


def fetch_official_sources(ctx: Context, log: Logger, env: dict) -> None:
    """Unpack the official Chromium tarball in place of Helium's two prepared layers."""
    if not ctx.pins.fallback:
        raise BuildError("helium.lock has no [fallback.chromium] entry")
    if shutil.which("xz") is None:
        raise BuildError("missing host tool: xz (needed to unpack the official Chromium tarball)")
    _verify_downloads(ctx, log, env, _deps_inis(ctx))
    tarball = fallback.fetch_official(ctx.pins.fallback, ctx.paths.downloads, ctx.opener, log)
    run(log, ["tar", "-xJf", tarball, "-C", ctx.tree, "--strip-components=1"], env)
    (ctx.tree / fallback.SOURCE_MARKER).write_text("official\n")
    _unpack(ctx, log, env, _deps_inis(ctx))


def _unpacked_source(ctx: Context) -> str:
    marker = ctx.tree / fallback.SOURCE_MARKER
    # Trees unpacked before the marker existed came from Helium's tarballs.
    return marker.read_text().strip() if marker.is_file() else "helium"


def _fetch_helium_sources(ctx: Context, log: Logger, env: dict) -> None:
    # Everything is verified before anything is unpacked.
    _verify_downloads(ctx, log, env, _helium_inis(ctx))
    _verify_downloads(ctx, log, env, _deps_inis(ctx))
    _unpack(ctx, log, env, _helium_inis(ctx))
    _unpack(ctx, log, env, _deps_inis(ctx))
    (ctx.tree / fallback.SOURCE_MARKER).write_text("helium\n")


def _fresh_tree(ctx: Context, log: Logger) -> None:
    if ctx.tree.exists():
        log(f"removing partly unpacked tree {ctx.tree}")
        shutil.rmtree(ctx.tree)
    # The snapshot belongs to the tree it was taken from.
    layers.clear_store(ctx.pristine_dir)
    ctx.tree.mkdir(parents=True)


def _reverify_unpacked(ctx: Context, log: Logger, env: dict, have: str) -> None:
    """Check the downloads again on every build, even when the tree is already unpacked.

    A tarball that changed on disk since the last build is caught (hashing the
    ~3 GB of tarballs takes seconds).
    """
    if have == "official":
        name = ctx.pins.fallback["url"].rsplit("/", 1)[-1] if ctx.pins.fallback else None
        if name and (ctx.paths.downloads / name).is_file():
            fallback.fetch_official(ctx.pins.fallback, ctx.paths.downloads, ctx.opener, log)
    else:
        _verify_downloads(ctx, log, env, _helium_inis(ctx))
    _verify_downloads(ctx, log, env, _deps_inis(ctx))


def fetch_sources(ctx: Context, log: Logger, env: dict) -> None:
    stamp = ctx.tree / ".downloaded.stamp"
    if stamp.exists():
        have = _unpacked_source(ctx)
        if ctx.source_mode in ("helium", "official") and have != ctx.source_mode:
            raise BuildError(
                f"--source {ctx.source_mode} was requested, but {ctx.tree} was unpacked from the "
                f"{have} source. Run ./aerie build --clean --source {ctx.source_mode} to fetch and "
                "unpack again (this discards the tree), or drop --source to keep it."
            )
        _reverify_unpacked(ctx, log, env, have)
        log(f"sources already unpacked ({have})")
        return
    source = fallback.choose_source(ctx.source_mode, ctx.pins.chromium_version, ctx.opener)
    if source == "official" and ctx.source_mode == "auto":
        log("WARNING: Helium's tarball host is unreachable; falling back to the official Chromium tarball")
    _fresh_tree(ctx, log)
    if source == "official":
        fetch_official_sources(ctx, log, env)
    else:
        try:
            _fetch_helium_sources(ctx, log, env)
        except BuildError as exc:
            if ctx.source_mode != "auto":
                raise
            log(f"WARNING: Helium's tarball download failed ({exc}); falling back to the official Chromium tarball")
            source = "official"
            _fresh_tree(ctx, log)
            fetch_official_sources(ctx, log, env)
    log(f"source: {source}")
    stamp.touch()


def _stamped(ctx: Context, log: Logger, name: str, action: Callable[[], None]) -> None:
    stamp = ctx.tree / f".{name}.stamp"
    if stamp.exists():
        log(f"{name}: already done")
        return
    action()
    stamp.touch()


LEGACY_STATE = layers.LEGACY_STATE
HELIUM_INPUTS_STAMP = helium_inputs.STAMP
helium_inputs_digest = helium_inputs.digest


def check_helium_inputs(ctx: Context, log: Callable[[str], None], write: bool = True) -> None:
    """Refuse an incremental build when Helium's own inputs changed since the tree was prepared.

    The digest is recorded when the tree is first prepared. A tree prepared
    before this check existed adopts the current digest.
    """
    stamp = ctx.tree / HELIUM_INPUTS_STAMP
    digest = helium_inputs_digest(ctx.repo)
    if stamp.exists():
        if stamp.read_text().strip() != digest:
            raise BuildError(
                "Helium's patch series, domain or pruning lists, translations or drop/adapt lists changed "
                "since this source tree was prepared, and its processed files cannot be un-applied; "
                "rebuild the source tree with: ./aerie build --clean")
    elif write:
        stamp.write_text(digest + "\n")


def refuse_legacy_tree(ctx: Context) -> None:
    """A tree the old pipeline patched or substituted in place has no pristine bytes to restore."""
    found = [name for name in LEGACY_STATE if (ctx.tree / name).exists()]
    if found:
        raise BuildError(
            f"{ctx.tree} was changed in place by an older Aerie build ({', '.join(found)}); "
            "its pristine files were never saved. Rebuild the source tree with: ./aerie build --clean")


def aerie_layers(ctx: Context, log: Callable[[str], None]) -> list[layers.Layer]:
    """The Aerie layers, in order: the overlay and the art go between the patches and the substitution."""
    return [
        patches.PatchLayer(patch_check.layers(ctx.repo)[-1], log),
        overlay.OverlayLayer(overlay.overlay_root(ctx.repo), log),
        branding.BrandingLayer(branding.branding_root(ctx.repo), log),
        aerie_version.VersionLayer(ctx.repo, log),
        substitution.SubstitutionLayer(
            lambda: substitution.helium_fingerprint(ctx.core / "utils", ctx.tree), log=log),
    ]


def apply_aerie_layers(ctx: Context, log: Callable[[str], None], dry_run: bool = False) -> layers.Report:
    """Re-apply the Aerie layers to the Helium-processed tree, touching only files whose content changes."""
    refuse_legacy_tree(ctx)
    try:
        report = layers.apply_layers(
            ctx.tree, ctx.pristine_dir, aerie_layers(ctx, log), ctx.paths.tmp / "aerie-layer-stage",
            log, dry_run=dry_run)
    except (layers.LayerError, patches.PatchStateError, overlay.OverlayError, branding.BrandingError,
            aerie_version.VersionError) as exc:
        raise BuildError(str(exc)) from exc
    verb = "would change" if dry_run else "changed"
    log(f"Aerie layers {verb}: {report.summary()}; {report.snapshots} new pristine snapshot(s)")
    for change in report.changes:
        log(f"  {change.kind:8} {change.path}")
    return report


def dry_run_layers(ctx: Context, log: Callable[[str], None]) -> layers.Report:
    """Report which tree files the next build would change. Writes nothing to the tree or the store."""
    if not (ctx.tree / ".resources.stamp").exists():
        raise BuildError(f"{ctx.tree} is not a prepared source tree yet; run ./aerie build first")
    if shutil.which("patch") is None:
        raise BuildError("missing host tool: patch")
    check_aerie_patches(ctx.repo)
    check_helium_inputs(ctx, log, write=False)
    return apply_aerie_layers(ctx, log, dry_run=True)


def prepare_adblock(ctx: Context, log: Logger, env: dict) -> None:
    """What the Filter Engine needs in the tree besides patches and the overlay, each step fail-closed.

    Runs after the Aerie layers, which put the overlay (Aerie-owned source and
    the committed EasyList and EasyPrivacy files) into the tree.

    1. The committed lists in the tree are checked against the lock. Nothing is downloaded.
    2. adblock-rust is vendored by gnrt under the committed crate lock and
       checked against crates.io (see rust_crates.py).
    Step 2 leaves a stamp keyed by the lock, by the two files the Aerie
    patches change for it and by the crate patches gnrt applies, so an
    unchanged tree is not touched again, and a changed lock or patch redoes
    the step.
    """
    pins = filter_lists.load_lock(filter_lists.lock_path(ctx.repo))
    try:
        filter_lists.verify_dir(pins, ctx.tree / "third_party" / "filter_lists")
    except filter_lists.ListError as exc:
        raise BuildError(f"filter lists: {exc}") from exc
    log("filter lists: committed lists verified against the lock")

    protection_pins = filter_lists.load_lock(protection_lists.lock_path(ctx.repo))
    try:
        protection_lists.verify_dir(protection_pins, ctx.tree / "third_party" / "filter_lists")
    except filter_lists.ListError as exc:
        raise BuildError(f"protection lists: {exc}") from exc
    log("protection lists: committed phishing and malware lists verified against the lock")

    key = hashlib.sha256()
    for path in (ctx.repo / rust_crates.LOCK_FILE, ctx.tree / rust_crates.CARGO_TOML,
                 ctx.tree / rust_crates.GNRT_CONFIG):
        key.update(path.read_bytes())
    key.update(rust_crates.crate_patches_digest(ctx.tree).encode())
    stamp_key = key.hexdigest()
    stamp = ctx.tree / ".aerie-crates.stamp"
    if stamp.exists() and stamp.read_text().strip() == stamp_key:
        log("crates: already vendored under this lock")
        return
    try:
        rust_crates.vendor_adblock(
            ctx.tree, ctx.repo, ctx.pins.chromium_version, ctx.paths.downloads / "crates",
            ctx.python, env, log, run, ctx.opener)
    except rust_crates.CratesError as exc:
        raise BuildError(f"rust crates: {exc}") from exc
    stamp.write_text(stamp_key + "\n")


def prepare_tree(ctx: Context, log: Logger, env: dict) -> None:
    py, core, tree, utils = ctx.python, ctx.core, ctx.tree, ctx.core / "utils"
    refuse_legacy_tree(ctx)
    check_helium_inputs(ctx, log)

    helium_layers = patch_check.layers(ctx.repo)[:2]

    def patch():
        run(log, [py, utils / "prune_binaries.py", "--keep-build-dependencies", tree,
                  core / "pruning.list"], env)
        # Helium's own applier, on its series minus the entries in patches/helium-drop.txt.
        scratch = ctx.paths.tmp / "helium-patches"
        dirs = [patches.filtered_series_dir(layer, scratch / layer.label) for layer in helium_layers]
        run(log, [py, utils / "patches.py", "apply", tree, *dirs], env)

    _stamped(ctx, log, "patched", patch)

    helium_steps.post_patch_steps(
        py, core, ctx.platform, tree,
        run=lambda cmd: run(log, cmd, env),
        stamped=lambda name, action: _stamped(ctx, log, name, action),
        namesub_backup=tree / ".aerie-namesubs.tar",
    )

    def resources():
        run(log, [py, utils / "generate_resources.py",
                  core / "resources" / "generate_resources.txt", core / "resources"], env)
        run(log, [py, utils / "replace_resources.py",
                  core / "resources" / "helium_resources.txt", core / "resources", tree], env)

    _stamped(ctx, log, "resources", resources)

    # Last: Helium's steps are stamped and never re-run on this tree, so the
    # Aerie layers see its final content and are re-applied on every build.
    apply_aerie_layers(ctx, log)
    prepare_adblock(ctx, log, env)


def configure(ctx: Context, log: Logger, env: dict) -> None:
    marker = ctx.tree / fallback.SOURCE_MARKER
    if marker.is_file() and marker.read_text().strip() == "official":
        missing = fallback.missing_toolchain(ctx.tree)
        if missing:
            raise BuildError(fallback.toolchain_message(missing))
    ctx.out_dir.mkdir(parents=True, exist_ok=True)
    args = helium.gn_args(
        (ctx.core / "flags.gn").read_text(),
        (ctx.platform / "flags.linux.gn").read_text(),
        ARCH,
        str(ctx.ccache_bin),
    )
    (ctx.out_dir / "args.gn").write_text(args)
    log("args.gn:\n" + args)
    run(log, [ctx.tree / "buildtools" / "linux64" / "gn", "gen", "out/Default",
              "--fail-on-unused-args"], env, cwd=ctx.tree)


def compile_targets(ctx: Context, log: Logger, env: dict) -> None:
    env = dict(env, SISO_PATH=str(ctx.tree / "third_party" / "siso" / "cipd" / "siso"))
    cmd = [ctx.python, ctx.tree / "third_party" / "depot_tools" / "autoninja.py",
           "-C", ctx.out_dir]
    if ctx.jobs:
        cmd += ["-j", str(ctx.jobs)]
    run(log, [*cmd, *TARGETS], env, cwd=ctx.tree)
    if not os.access(ctx.binary, os.X_OK):
        raise BuildError(f"build finished but {ctx.binary} is missing")
    run(log, [ctx.ccache_bin, "--show-stats"], env)


def show_layer_changes(ctx: Context) -> int:
    """./aerie build --dry-run-layers: list what the next build would change in the tree."""
    log = Logger(ctx.log_path)
    try:
        report = dry_run_layers(ctx, log)
    finally:
        log.close()
    print(f"{len(report.changes)} file(s) would change: {report.summary()}")
    return 0


def build(ctx: Context, clean: bool = False) -> Path:
    log = Logger(ctx.log_path)
    started = time.monotonic()
    try:
        log(f"log file: {ctx.log_path}")
        preflight(ctx, log)
        if clean and ctx.tree.exists():
            log(f"--clean: removing {ctx.tree}")
            shutil.rmtree(ctx.tree)
        if clean and ctx.pristine_dir.exists():
            log(f"--clean: removing pristine snapshot {ctx.pristine_dir}")
            layers.clear_store(ctx.pristine_dir)
        env = isolated_env(ctx.paths, os.environ)
        env["PATH"] = os.pathsep.join([str(ctx.venv / "bin"), env.get("PATH", "")])
        steps = [
            ("install ccache", install_ccache),
            ("install python env", install_python_env),
            ("fetch sources", fetch_sources),
            ("prepare tree", prepare_tree),
            ("configure", configure),
            ("compile", compile_targets),
        ]
        for title, step in steps:
            step_start = time.monotonic()
            log(f"== {title}")
            step(ctx, log, env)
            log(f"== {title}: done in {_fmt(time.monotonic() - step_start)}")
        log(f"build finished in {_fmt(time.monotonic() - started)}")
        log(f"binary: {ctx.binary}")
        return ctx.binary
    except Exception as exc:
        log(f"BUILD FAILED after {_fmt(time.monotonic() - started)}: {exc}")
        raise
    finally:
        log.close()


def _fmt(seconds: float) -> str:
    seconds = int(seconds)
    return f"{seconds // 3600}h{seconds % 3600 // 60:02d}m{seconds % 60:02d}s"
