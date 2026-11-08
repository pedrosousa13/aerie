"""Build root and compiler cache settings, and the paths derived from them.

Everything the build writes goes under one build root. Precedence for each
setting is: command-line flag, then environment variable, then default.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping

DEFAULT_BUILD_ROOT = Path("/mnt/linux2/aerie-build")
DEFAULT_CACHE_SIZE = "30G"

ENV_BUILD_ROOT = "AERIE_BUILD_ROOT"
ENV_CACHE_SIZE = "AERIE_CCACHE_SIZE"
ENV_CACHE_DIR = "AERIE_CCACHE_DIR"

_SIZE_RE = re.compile(r"^([0-9]+)([KMGT])$")

# Inherited settings dropped from the build environment: ccache overrides that
# would bypass the cap or the cache location, and host toolchain settings that
# would leak into Chromium's bundled toolchains (a host GOROOT, for example,
# makes the bundled Go fail with a version mismatch).
_DROPPED_VARS = frozenset(
    {
        "CCACHE_MAXSIZE", "CCACHE_CONFIGPATH", "CCACHE_TEMPDIR",
        "CC", "CXX", "AR", "NM", "LD", "CPP",
        "CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS",
        "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "LIBRARY_PATH", "LD_LIBRARY_PATH",
        "PKG_CONFIG_PATH", "PKG_CONFIG_LIBDIR", "PKG_CONFIG_SYSROOT_DIR",
        "PYTHONPATH", "PYTHONHOME", "PYTHONSTARTUP",
        "NODE_OPTIONS", "NODE_PATH",
    }
)
_DROPPED_PREFIXES = ("GO", "RUST", "CARGO_")


class ConfigError(ValueError):
    """A build setting is invalid."""


def resolve_build_root(flag: str | None, environ: Mapping[str, str], repo_root: Path) -> Path:
    raw = flag or environ.get(ENV_BUILD_ROOT) or str(DEFAULT_BUILD_ROOT)
    root = Path(raw).expanduser()
    if not root.is_absolute():
        raise ConfigError(f"build root must be an absolute path, got {raw!r}")
    root = Path(*root.parts)  # normalise duplicate slashes
    if root == Path(root.anchor):
        raise ConfigError("build root cannot be the filesystem root")
    repo = Path(repo_root)
    if root == repo or root.is_relative_to(repo):
        raise ConfigError(f"build root {root} must be outside the repo checkout {repo}")
    if repo.is_relative_to(root):
        raise ConfigError(f"build root {root} must not contain the repo checkout {repo}")
    return root


def resolve_cache_size(flag: str | None, environ: Mapping[str, str]) -> str:
    raw = flag if flag is not None else environ.get(ENV_CACHE_SIZE, DEFAULT_CACHE_SIZE)
    match = _SIZE_RE.match(raw.strip().upper())
    if not match or int(match.group(1)) == 0:
        raise ConfigError(
            f"compiler cache size must be a whole number with a K/M/G/T unit, e.g. 30G; got {raw!r}"
        )
    return f"{int(match.group(1))}{match.group(2)}"


def resolve_ccache_dir(flag: str | None, environ: Mapping[str, str]) -> Path | None:
    """A compiler cache folder outside the build root, or None for the default (<root>/ccache)."""
    raw = flag or environ.get(ENV_CACHE_DIR)
    if not raw:
        return None
    path = Path(raw).expanduser()
    if not path.is_absolute():
        raise ConfigError(f"compiler cache folder must be an absolute path, got {raw!r}")
    return Path(*path.parts)


@dataclass(frozen=True)
class BuildPaths:
    """The layout of a build root.

    ccache_dir moves only the compiler cache out of the root, so that two build roots can share it.
    """

    root: Path
    ccache_dir: Path | None = None

    @property
    def downloads(self) -> Path:
        return self.root / "downloads"

    @property
    def src(self) -> Path:
        return self.root / "src"

    @property
    def toolchain(self) -> Path:
        return self.root / "toolchain"

    @property
    def ccache(self) -> Path:
        return self.ccache_dir or self.root / "ccache"

    @property
    def home(self) -> Path:
        return self.root / "home"

    @property
    def tmp(self) -> Path:
        return self.root / "tmp"

    @property
    def xdg(self) -> Path:
        return self.root / "xdg"

    @property
    def pycache(self) -> Path:
        return self.root / "pycache"

    @property
    def logs(self) -> Path:
        return self.root / "logs"

    @property
    def pristine(self) -> Path:
        """Pristine copies of the tree files Aerie's layers change, one directory per tree."""
        return self.root / "aerie-pristine"

    def tree(self, name: str) -> Path:
        return self.src / name

    def out_dir(self, name: str) -> Path:
        return self.tree(name) / "out" / "Default"

    def scratch_dirs(self) -> list[Path]:
        """Directories that must exist before any build step runs."""
        return [
            self.downloads,
            self.src,
            self.toolchain,
            self.ccache,
            self.home,
            self.tmp,
            self.pycache,
            self.logs,
            *(self.xdg / d for d in ("cache", "config", "data", "state", "runtime")),
        ]


def isolated_env(paths: BuildPaths, base: Mapping[str, str]) -> dict[str, str]:
    """Return a copy of base where every per-user write location points into the build root."""
    env = {
        k: v
        for k, v in base.items()
        if k not in _DROPPED_VARS and not k.startswith(_DROPPED_PREFIXES)
    }
    tmp = str(paths.tmp)
    env.update(
        HOME=str(paths.home),
        TMPDIR=tmp,
        TMP=tmp,
        TEMP=tmp,
        XDG_CACHE_HOME=str(paths.xdg / "cache"),
        XDG_CONFIG_HOME=str(paths.xdg / "config"),
        XDG_DATA_HOME=str(paths.xdg / "data"),
        XDG_STATE_HOME=str(paths.xdg / "state"),
        XDG_RUNTIME_DIR=str(paths.xdg / "runtime"),
        PYTHONPYCACHEPREFIX=str(paths.pycache),
        PIP_CACHE_DIR=str(paths.xdg / "cache" / "pip"),
        CCACHE_DIR=str(paths.ccache),
        # depot_tools: never self-update, never upload metrics.
        DEPOT_TOOLS_UPDATE="0",
        DEPOT_TOOLS_METRICS="0",
    )
    return env
