"""The overlay: Aerie-owned source that is copied into the Chromium tree.

Aerie's own code (the Filter Engine component, for a start) lives in the
`overlay/` directory of this repo, laid out like the Chromium tree. It is an
Aerie layer (see layers.py): it runs after the patch series and before the
Helium -> Aerie substitution, and it gets the same pristine and restore
behavior as the other layers. A file that leaves the overlay is deleted from
the tree on the next run. That keeps big, Aerie-only code reviewable as
ordinary files, while upstream files still change only through small hook
patches (see CONTRIBUTING.md).

Rules:

* The overlay only adds files. A file that already exists in the tree (as
  Helium's steps and the patches left it) is an error unless it already has
  exactly the overlay's bytes: use a patch for upstream files.
* A path may not leave the tree: no `..`, no absolute path, no symlink in the
  tree on the way to the file, no symlink in the overlay.
* Files that only matter outside Chromium (`Cargo.toml`, `Cargo.lock`, cargo's
  `target/`, bytecode) are not copied.
* File modes are not kept: layers write bytes. GN runs the overlay's scripts
  through the interpreter, so none needs the executable bit.
"""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
from typing import Callable

OVERLAY_DIR = "overlay"

_SKIP_DIRS = frozenset({"target", "__pycache__", ".git"})
_SKIP_NAMES = frozenset({"Cargo.toml", "Cargo.lock"})
_SKIP_SUFFIXES = (".pyc",)


class OverlayError(RuntimeError):
    pass


def overlay_root(repo: Path) -> Path:
    return Path(repo) / OVERLAY_DIR


def collect(root: Path) -> dict[str, Path]:
    """Map tree-relative POSIX path -> source file, for every file to copy."""
    files: dict[str, Path] = {}
    if not root.is_dir():
        return files
    for current, dirs, names in os.walk(root, followlinks=False):
        for d in dirs:
            if (Path(current) / d).is_symlink():
                raise OverlayError(f"overlay entry {Path(current) / d} is not a regular file (a symlinked directory)")
        dirs[:] = sorted(d for d in dirs if d not in _SKIP_DIRS)
        for name in sorted(names):
            if name in _SKIP_NAMES or name.endswith(_SKIP_SUFFIXES):
                continue
            path = Path(current) / name
            if path.is_symlink() or not path.is_file():
                raise OverlayError(f"overlay entry {path} is not a regular file")
            files[path.relative_to(root).as_posix()] = path
    return files


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as fh:
        for block in iter(lambda: fh.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def _safe_target(tree: Path, rel: str) -> Path:
    if rel.startswith("/") or ".." in Path(rel).parts:
        raise OverlayError(f"unsafe overlay path {rel!r}")
    target = tree / rel
    # A symlinked directory in the tree must not carry a write outside it.
    if not target.parent.resolve().is_relative_to(tree.resolve()):
        raise OverlayError(f"overlay path {rel!r} leads outside the source tree (a symlink in the tree?)")
    return target


def digest(root: Path) -> str:
    """One hash over the overlay's paths and contents, to tell whether it changed."""
    outer = hashlib.sha256()
    for rel, path in sorted(collect(root).items()):
        outer.update(f"{rel}\0{_sha256(path)}\n".encode())
    return outer.hexdigest()


class OverlayLayer:
    """The overlay as a tree layer: every overlay file is a new file in the tree."""

    name = "overlay"

    def __init__(self, root: Path, log: Callable[[str], None] = lambda _m: None):
        self.root = Path(root)
        self._log = log

    def apply(self, ws) -> None:
        files = collect(self.root)
        # Judge every file before staging any (nothing is written to the tree here anyway).
        wanted: dict[str, bytes] = {}
        for rel, source in files.items():
            target = _safe_target(ws.tree, rel)
            if target.is_symlink():
                raise OverlayError(f"{rel} in the source tree is a symlink")
            data = source.read_bytes()
            have = ws.read(rel)
            if target.exists() and not target.is_file():
                raise OverlayError(f"{rel} in the source tree is not a regular file")
            if have is not None and have != data:
                raise OverlayError(
                    f"overlay file {rel} would overwrite a file that is already in the source tree; "
                    "the overlay only adds files (change upstream files with a patch)"
                )
            wanted[rel] = data
        for rel, data in wanted.items():
            ws.put(rel, data)
        self._log(f"overlay: {len(wanted)} file(s)")
