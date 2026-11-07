"""The inputs that decide what Helium's own steps produce for a source tree.

A digest of them is stored in the tree when it is prepared. If they change,
the processed files in the tree cannot be un-applied, so the tree has to be
rebuilt (./aerie build --clean), and the patch check cannot compare against it.
"""

from __future__ import annotations

import hashlib
import os
from pathlib import Path

from . import lock

STAMP = ".helium-inputs.sha256"
PATHS = (
    "helium-chromium/patches", "helium-linux/patches", "helium-chromium/i18n", "helium-chromium/resources",
    "helium-chromium/domain_regex.list", "helium-chromium/domain_substitution.list",
    "helium-chromium/pruning.list",
    # Helium patches Aerie re-cut for a new Chromium (issue #8): they replace Helium's own.
    "patches/helium-adapted",
)
# Build outputs inside those paths. The resources step writes generate_resources.txt's
# outputs here after the digest is recorded, so counting them would make every fresh
# tree refuse its next incremental build.
GENERATED = ("helium-chromium/resources/generated",)
# Files by name, wherever they sit in the repo (Helium's series is adapted by them, issue #8).
NAMES = frozenset({"helium-drop.txt", "helium-adapt.txt"})
_SKIP_DIRS = frozenset({".git", ".scratch", "node_modules", "__pycache__"})


def _generated(repo: Path, path: Path) -> bool:
    rel = path.relative_to(repo).as_posix()
    return any(rel.startswith(g + "/") for g in GENERATED)


def digest(repo: Path) -> str:
    files: set[Path] = set()
    for rel in PATHS:
        path = repo / rel
        if path.is_file():
            files.add(path)
        elif path.is_dir():
            files.update(p for p in path.rglob("*") if p.is_file() and not _generated(repo, p))
    for root, dirs, names in os.walk(repo):
        dirs[:] = [d for d in dirs if d not in _SKIP_DIRS]
        files.update(Path(root) / n for n in names if n in NAMES)
    result = hashlib.sha256()
    for path in sorted(files):
        result.update(f"{path.relative_to(repo).as_posix()} {lock.sha256_file(path)}\n".encode())
    return result.hexdigest()


def matches(repo: Path, tree: Path) -> bool:
    """True when the tree was prepared from the repo's current Helium inputs."""
    stamp = tree / STAMP
    return stamp.is_file() and stamp.read_text().strip() == digest(repo)
