"""Aerie's own release number, as the browser's About page shows it.

Helium shows its version (`HELIUM_PRODUCT_VERSION`, from the HELIUM_* lines
that its version step adds to chrome/VERSION) next to the Chromium version.
Aerie has its own release number in `AERIE_VERSION` at the repo root, three
numbers (`MAJOR.MINOR.PATCH`). This layer adds it to chrome/VERSION as
AERIE_MAJOR, AERIE_MINOR and AERIE_PATCH, and patches/branding/0006 makes the
version template use them, so About reads "Version 0.1.0 (Official Build,
Chromium 157.0.8103.38)".

It is an Aerie layer (see layers.py): it reads the pristine chrome/VERSION
that Helium's steps left, so a new release number replaces the old lines and
an unchanged number writes nothing.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Callable

RELEASE_FILE = "AERIE_VERSION"
VERSION_FILE = "chrome/VERSION"
_RELEASE_RE = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")


class VersionError(RuntimeError):
    pass


def parse_release(text: str) -> tuple[int, int, int]:
    match = _RELEASE_RE.fullmatch(text.strip())
    if not match:
        raise VersionError(f"{RELEASE_FILE} must hold three numbers, MAJOR.MINOR.PATCH, got {text.strip()!r}")
    major, minor, patch = (int(g) for g in match.groups())
    return major, minor, patch


class VersionLayer:
    """Adds AERIE_MAJOR, AERIE_MINOR and AERIE_PATCH to chrome/VERSION."""

    name = "aerie version"

    def __init__(self, repo: Path, log: Callable[[str], None] = lambda _m: None):
        self.repo = Path(repo)
        self._log = log

    def apply(self, ws) -> None:
        release_path = self.repo / RELEASE_FILE
        if not release_path.is_file():
            raise VersionError(f"{RELEASE_FILE} is missing from the repo root")
        major, minor, patch = parse_release(release_path.read_text(encoding="utf-8"))
        data = ws.read(VERSION_FILE)
        if data is None:
            raise VersionError(f"{VERSION_FILE} is not in the source tree")
        lines = [line for line in data.decode("utf-8").splitlines() if not line.startswith("AERIE_")]
        lines += [f"AERIE_MAJOR={major}", f"AERIE_MINOR={minor}", f"AERIE_PATCH={patch}"]
        ws.put(VERSION_FILE, ("\n".join(lines) + "\n").encode("utf-8"))
        self._log(f"aerie version: {major}.{minor}.{patch}")
