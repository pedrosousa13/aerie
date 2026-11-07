"""Helium bundled assets that Aerie does not download.

Helium's deps.ini lists assets that are unpacked into the Chromium tree after
the tarballs: the uBlock Origin build, the onboarding page, search engine data.
Aerie drops the uBlock Origin build (Aerie blocks natively, see docs/adblock.md).
The submodule is never edited, so the build reads a copy of deps.ini without
those sections.

Like a dropped patch, a dropped section must still exist in Helium's file, so a
Helium bump that renames it shows up instead of silently bringing the asset back,
and it needs a row in docs/helium-dependency-ledger.md.
"""

from __future__ import annotations

import configparser
from pathlib import Path

# section name -> why Aerie does not download it
DROPPED_SECTIONS = {
    "ublock_origin": "Aerie blocks natively (issue #8); the bundled uBlock Origin is removed",
}


class DepsError(RuntimeError):
    pass


def sections(ini: Path) -> list[str]:
    parser = configparser.ConfigParser(defaults={"_chromium_version": "0"}, interpolation=None)
    with open(ini, encoding="utf-8") as fh:
        parser.read_file(fh, source=str(ini))
    return parser.sections()


def check(ini: Path, dropped=DROPPED_SECTIONS) -> list[str]:
    """Problems: a dropped section that is no longer in Helium's file."""
    present = set(sections(ini))
    return [f"deps.ini no longer has the [{name}] section that Aerie drops ({reason}): "
            "update tools/aerie_build/helium_deps.py and its ledger row"
            for name, reason in dropped.items() if name not in present]


def write_filtered(ini: Path, dest: Path, dropped=DROPPED_SECTIONS) -> Path:
    """Copy ini to dest without the dropped sections, keeping every other line as it is."""
    problems = check(ini, dropped)
    if problems:
        raise DepsError("; ".join(problems))
    out: list[str] = []
    skipping = False
    for line in Path(ini).read_text(encoding="utf-8").splitlines(keepends=True):
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            skipping = stripped[1:-1] in dropped
        if not skipping:
            out.append(line)
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text("".join(out), encoding="utf-8")
    return dest
