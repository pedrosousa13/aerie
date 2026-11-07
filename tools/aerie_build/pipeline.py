"""The build: fetch pinned sources, apply Helium's patches, compile.

The steps mirror helium-linux/scripts/build.sh and shared.sh, with three
changes: all paths sit under the build root, every download is also checked
against helium.lock, and each source-tree step leaves a stamp so an
interrupted build resumes where it stopped.
"""

from __future__ import annotations

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

from . import ccache, helium, lock
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
    def out_dir(self) -> Path:
        return self.paths.out_dir(self.tree_name)

    @property
    def binary(self) -> Path:
        return self.out_dir / "helium"

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
    for directory in ctx.paths.scratch_dirs():
        directory.mkdir(parents=True, exist_ok=True)
    free = shutil.disk_usage(ctx.paths.root).free
    log(f"build root {ctx.paths.root}: {free / 1024**3:.0f} GiB free")
    if free < MIN_FREE_BYTES and not (ctx.tree / ".downloaded.stamp").exists():
        log("WARNING: a clean build needs about 150-200 GB; this build may run out of space")
    log(f"Helium {version} (Chromium {chromium}); source tree {ctx.tree}")


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


def fetch_sources(ctx: Context, log: Logger, env: dict) -> None:
    """Retrieve and verify every download on every build; unpack only once.

    The pinned sha256 check runs even when the tree is already unpacked, so a
    tarball that changed on disk since the last build is caught (hashing the
    ~3 GB of tarballs takes seconds).
    """
    groups = [
        [ctx.core / "downloads.ini", ctx.platform / "downloads.ini"],
        [ctx.core / "deps.ini"],
    ]
    for inis in groups:
        _retrieve(ctx, log, env, inis)
    names = [n for inis in groups for n in lock.required_downloads(inis, ctx.pins.chromium_version)]
    log(f"checking {len(names)} download(s) against helium.lock")
    lock.verify_downloads(ctx.pins, ctx.paths.downloads, names)

    stamp = ctx.tree / ".downloaded.stamp"
    if stamp.exists():
        log("sources already unpacked")
        return
    if ctx.tree.exists():
        log(f"removing partly unpacked tree {ctx.tree}")
        shutil.rmtree(ctx.tree)
    ctx.tree.mkdir(parents=True)
    for inis in groups:
        _unpack(ctx, log, env, inis)
    stamp.touch()


def _stamped(ctx: Context, log: Logger, name: str, action: Callable[[], None]) -> None:
    stamp = ctx.tree / f".{name}.stamp"
    if stamp.exists():
        log(f"{name}: already done")
        return
    action()
    stamp.touch()


def prepare_tree(ctx: Context, log: Logger, env: dict) -> None:
    py, core, tree, utils = ctx.python, ctx.core, ctx.tree, ctx.core / "utils"

    def patch():
        run(log, [py, utils / "prune_binaries.py", "--keep-build-dependencies", tree,
                  core / "pruning.list"], env)
        run(log, [py, utils / "patches.py", "apply", tree, core / "patches",
                  ctx.platform / "patches"], env)

    _stamped(ctx, log, "patched", patch)
    _stamped(ctx, log, "domsub", lambda: run(log, [
        py, utils / "domain_substitution.py", "apply", "-r", core / "domain_regex.list",
        "-f", core / "domain_substitution.list", tree], env))
    _stamped(ctx, log, "namesub", lambda: run(log, [
        py, utils / "name_substitution.py", "--sub", "-t", tree,
        "--backup-path", tree / ".aerie-namesubs.tar"], env))
    _stamped(ctx, log, "i18n", lambda: run(log, [py, utils / "i18n_apply.py", "-t", tree], env))
    _stamped(ctx, log, "version", lambda: run(log, [
        py, utils / "helium_version.py", "--tree", core, "--platform-tree", ctx.platform,
        "--chromium-tree", tree], env))

    def resources():
        run(log, [py, utils / "generate_resources.py",
                  core / "resources" / "generate_resources.txt", core / "resources"], env)
        run(log, [py, utils / "replace_resources.py",
                  core / "resources" / "helium_resources.txt", core / "resources", tree], env)

    _stamped(ctx, log, "resources", resources)


def configure(ctx: Context, log: Logger, env: dict) -> None:
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


def build(ctx: Context, clean: bool = False) -> Path:
    log = Logger(ctx.log_path)
    started = time.monotonic()
    try:
        log(f"log file: {ctx.log_path}")
        preflight(ctx, log)
        if clean and ctx.tree.exists():
            log(f"--clean: removing {ctx.tree}")
            shutil.rmtree(ctx.tree)
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
