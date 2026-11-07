"""The branding layer: Aerie's art replaces the art Helium puts in the tree.

Helium copies its logo, icons and a few page icons over Chromium's files
(helium-chromium/resources/helium_resources.txt). Aerie runs one more copy
after that: `branding/resources.txt` lists `<art file> <tree path>` pairs, the
art files are in `branding/art/`. It is an Aerie layer (see layers.py), so it
gets the same pristine and restore behavior as the patches and the overlay: an
entry that leaves the list gives the tree file back to Helium's art on the
next run, and a run with no change writes nothing.

Unlike the overlay, this layer only replaces. A destination that is not in
the tree is an error, because it means a path was mistyped or Helium stopped
shipping that file, and the build would otherwise keep the old art without a
word. The final art can replace the placeholders without any code change
(issue #21).
"""

from __future__ import annotations

from pathlib import Path, PurePosixPath
from typing import Callable

BRANDING_DIR = "branding"
LIST_NAME = "resources.txt"
ART_DIR = "art"


class BrandingError(RuntimeError):
    pass


def branding_root(repo: Path) -> Path:
    return Path(repo) / BRANDING_DIR


def list_path(root: Path) -> Path:
    return Path(root) / LIST_NAME


def parse_list(text: str) -> list[tuple[str, str]]:
    """`<art file> <tree path>` lines to (art, destination) pairs. `#` starts a comment line."""
    rows: list[tuple[str, str]] = []
    seen: set[str] = set()
    for number, line in enumerate(text.splitlines(), start=1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split()
        if len(fields) != 2:
            raise BrandingError(f"{LIST_NAME} line {number}: expected '<art file> <tree path>'")
        art, dest = fields
        pure = PurePosixPath(dest)
        if pure.is_absolute() or ".." in pure.parts or PurePosixPath(art).is_absolute() or ".." in PurePosixPath(art).parts:
            raise BrandingError(f"{LIST_NAME} line {number}: unsafe path in {line!r}")
        if dest in seen:
            raise BrandingError(f"{LIST_NAME} line {number}: {dest} is listed twice")
        seen.add(dest)
        rows.append((art, dest))
    return rows


class BrandingLayer:
    """Replaces the listed tree files with Aerie's art."""

    name = "branding"

    def __init__(self, root: Path, log: Callable[[str], None] = lambda _m: None):
        self.root = Path(root)
        self._log = log

    def apply(self, ws) -> None:
        listing = list_path(self.root)
        if not listing.is_file():
            return
        wanted: dict[str, bytes] = {}
        for art, dest in parse_list(listing.read_text(encoding="utf-8")):
            source = self.root / ART_DIR / art
            if not source.is_file() or source.is_symlink():
                raise BrandingError(f"{LIST_NAME}: art file {BRANDING_DIR}/{ART_DIR}/{art} is missing")
            data = source.read_bytes()
            if not data:
                raise BrandingError(f"{LIST_NAME}: art file {art} is empty")
            if ws.read(dest) is None:
                raise BrandingError(
                    f"{LIST_NAME}: {dest} is not in the source tree; "
                    "the branding layer only replaces files Helium or Chromium already ship")
            wanted[dest] = data
        for dest, data in wanted.items():
            ws.put(dest, data)
        self._log(f"branding: {len(wanted)} file(s)")
