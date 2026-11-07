"""Remove stale downloads and old build trees from a build root.

Kept: downloads pinned in helium.lock (and their published .hashes and
.minisig files),
the source tree of the pinned Helium version, current toolchain directories,
and the compiler cache (ccache trims itself to its size cap).
"""

from __future__ import annotations

import shutil
from pathlib import Path
from typing import Callable, Iterable

from .config import BuildPaths

_ALWAYS_STALE_SUFFIXES = (".partial", ".rejected")


def _children(directory: Path) -> list[Path]:
    return sorted(directory.iterdir()) if directory.is_dir() else []


def _download_is_stale(path: Path, pinned: set[str]) -> bool:
    name = path.name
    if name.endswith(_ALWAYS_STALE_SUFFIXES):
        return True
    for sidecar in (".hashes", ".minisig"):
        if name.endswith(sidecar):
            return name[: -len(sidecar)] not in pinned
    return name not in pinned


def plan_prune(
    paths: BuildPaths,
    pinned_downloads: set[str],
    current_tree: str,
    current_tools: set[str],
) -> list[Path]:
    stale: list[Path] = []
    stale += [p for p in _children(paths.downloads) if _download_is_stale(p, pinned_downloads)]
    stale += [p for p in _children(paths.src) if p.name != current_tree]
    stale += [p for p in _children(paths.toolchain) if p.name not in current_tools]
    return stale


def execute(
    plan: Iterable[Path],
    dry_run: bool,
    log: Callable[[str], None] = print,
    root: Path | None = None,
) -> None:
    for path in plan:
        if root is not None and not path.resolve().is_relative_to(root.resolve()):
            raise ValueError(f"refusing to delete {path}: outside build root {root}")
        if dry_run:
            log(f"would remove {path}")
            continue
        log(f"removing {path}")
        if path.is_dir() and not path.is_symlink():
            shutil.rmtree(path)
        else:
            path.unlink()
