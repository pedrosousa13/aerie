"""The rebase report for a Helium bump.

- rows_for / render_report: every patch with its apply status (clean, offset,
  fuzz, failed) and its Aerie-Purpose line, as docs/rebase/<helium-version>.md.
- traffic_ids / traffic_ids_in_archive / diff_traffic: the unique IDs of
  Chromium's net::NetworkTrafficAnnotationTag in two sources (a source tree, or
  a Chromium tarball read as a stream), so a bump shows new network traffic for
  review against docs/network-endpoints.md. Test-only annotations are left out.
- keep_traffic_section: a re-run must not wipe a traffic section filled in earlier.

The patch refresh (`./aerie patches refresh`) lives in patches.py and patch_check.py.
"""

from __future__ import annotations

import fnmatch
import os
import re
import subprocess
import tarfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Sequence

from . import patches

TRAFFIC_COMMAND = "./aerie traffic-diff --old-tree <old tree> --new-tree <new tree>"
REBASE_DIR = "docs/rebase"
TRAFFIC_HEADING = "## Network traffic annotations"
PATCHES_HEADING = "## Patches"
NOT_RUN = "The traffic diff was **not run**"

# The unique ID is the first argument in every form.
_ANNOTATION_RE = re.compile(
    r"\b(?:Define|DefinePartial|Complete|BranchedComplete)NetworkTrafficAnnotation\s*\(\s*\"([^\"\\]+)\"")
_SOURCE_SUFFIXES = (".cc", ".h", ".mm", ".cpp")
_SKIP_DIRS = frozenset({".git", "out"})
_TEST_DIRS = frozenset({"test", "tests", "testing", "test_support"})
_TEST_FILES = ("*_test.*", "*_unittest.*", "*_browsertest.*", "*_test_util*", "fake_*", "mock_*")
_MAX_MEMBER_BYTES = 32 * 1024 * 1024


@dataclass(frozen=True)
class Versions:
    old_helium: str
    new_helium: str
    old_chromium: str
    new_chromium: str


@dataclass(frozen=True)
class PatchRow:
    patch: str
    status: str  # clean | offset | fuzz | failed | skipped | not tried
    purpose: str


@dataclass(frozen=True)
class TrafficDiff:
    added: list[str]
    removed: list[str]


def rows_for(layers: Sequence[patches.Layer], results: Sequence[patches.PatchResult]) -> list[PatchRow]:
    """One row per series entry, in build order. Entries with no result were never tried."""
    by_name = {r.patch: r for r in results}
    rows = []
    for layer in layers:
        for name, path in patches.layer_entries(layer):
            result = by_name.get(name)
            if result is None:
                status = "not tried"
            elif result.status in ("ok", "failed") and result.drift:
                status = result.drift  # applies, but only shifted or loosened (a failure in the strict layer)
            elif result.status == "ok":
                status = "clean"
            else:
                status = result.status
            purpose = ""
            if path.is_file():
                purpose = patches.parse_header(path.read_text(encoding="utf-8", errors="replace")).get("Aerie-Purpose", "")
            rows.append(PatchRow(name, status, purpose))
    return rows


def _cell(text: str) -> str:
    return text.replace("|", "\\|").replace("\n", " ")


def render_report(versions: Versions, rows: Sequence[PatchRow], traffic: TrafficDiff | None) -> str:
    counts = {s: sum(r.status == s for r in rows) for s in ("clean", "offset", "fuzz", "failed")}
    other = len(rows) - sum(counts.values())
    summary = ", ".join(f"{n} {s}" for s, n in counts.items()) + (f", {other} other" if other else "")
    out = [
        f"# Rebase report: Helium {versions.new_helium}", "",
        f"Helium {versions.old_helium} -> {versions.new_helium}", "",
        f"Chromium {versions.old_chromium} -> {versions.new_chromium}", "",
        f"{len(rows)} patches: {summary}.", "",
        "Aerie's patches must apply with no offset and no fuzz. `./aerie patches refresh` regenerates "
        "the ones that drifted. Helium's patches apply with fuzz allowed, so their offset and fuzz rows "
        "are information.", "",
        TRAFFIC_HEADING, "",
    ]
    if traffic is None:
        out += [f"{NOT_RUN}: no Chromium source (a prepared tree or the downloaded tarball) was "
                "available for both the old and the new release. To run it:", "", "```", TRAFFIC_COMMAND, "```", ""]
    else:
        out += ["Test-only annotations are left out. Review each added ID against "
                "[docs/network-endpoints.md](../network-endpoints.md).", ""]
        out += _traffic_lines(traffic)
    out += [PATCHES_HEADING, "", "| Patch | Status | Purpose |", "| --- | --- | --- |"]
    out += [f"| {_cell(r.patch)} | {r.status} | {_cell(r.purpose)} |" for r in rows]
    return "\n".join(out) + "\n"


def _section(text: str) -> tuple[int, int] | None:
    start = text.find(TRAFFIC_HEADING)
    end = text.find(PATCHES_HEADING)
    return (start, end) if 0 <= start < end else None


def keep_traffic_section(new: str, existing: str | None) -> str:
    """new, but with the traffic section of existing if new has none (not run) and existing has one."""
    if not existing:
        return new
    mine, theirs = _section(new), _section(existing)
    if mine is None or theirs is None or NOT_RUN not in new[mine[0]:mine[1]] or NOT_RUN in existing[theirs[0]:theirs[1]]:
        return new
    return new[:mine[0]] + existing[theirs[0]:theirs[1]] + new[mine[1]:]


def _traffic_lines(traffic: TrafficDiff) -> list[str]:
    if not traffic.added and not traffic.removed:
        return ["No annotations were added or removed.", ""]
    out: list[str] = []
    for title, ids in (("Added", traffic.added), ("Removed", traffic.removed)):
        out += [f"{title} ({len(ids)}):", ""]
        out += [f"- `{i}`" for i in ids] or ["- none"]
        out.append("")
    return out


def render_traffic(traffic: TrafficDiff) -> str:
    return "\n".join(_traffic_lines(traffic))


def is_test_path(path: str) -> bool:
    """A path (relative to the Chromium root) that only tests use: annotations there are not browser traffic."""
    parts = PurePosixPath(path).parts
    if any(p in _TEST_DIRS for p in parts[:-1]):
        return True
    return any(fnmatch.fnmatchcase(parts[-1], pat) for pat in _TEST_FILES) if parts else False


def _wanted(path: str) -> bool:
    return path.endswith(_SOURCE_SUFFIXES) and not is_test_path(path)


def traffic_ids(tree: Path) -> set[str]:
    """Unique IDs of every NetworkTrafficAnnotationTag defined in the non-test C++ files under tree."""
    ids: set[str] = set()
    for root, dirs, files in os.walk(tree):
        dirs[:] = [d for d in dirs if d not in _SKIP_DIRS]
        for name in files:
            rel = (Path(root) / name).relative_to(tree).as_posix()
            if _wanted(rel):
                try:
                    ids.update(_ANNOTATION_RE.findall(Path(root, name).read_text(encoding="utf-8", errors="replace")))
                except OSError:
                    continue
    return ids


def _scan_tar(stream) -> set[str]:
    ids: set[str] = set()
    with tarfile.open(fileobj=stream, mode="r|*") as tf:
        for member in tf:
            if member.isfile() and member.size <= _MAX_MEMBER_BYTES and _wanted(member.name):
                data = tf.extractfile(member).read()
                ids.update(_ANNOTATION_RE.findall(data.decode("utf-8", errors="replace")))
    return ids


def traffic_ids_in_archive(archive: Path) -> set[str]:
    """Like traffic_ids, for a Chromium source tarball. It is read as a stream; nothing is written to disk."""
    if not archive.name.endswith(".zst"):
        with open(archive, "rb") as fh:
            return _scan_tar(fh)
    with open(archive, "rb") as fh:
        zstd = subprocess.Popen(["zstd", "-dc"], stdin=fh, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            ids = _scan_tar(zstd.stdout)
            zstd.stdout.read()  # let zstd finish and report a damaged file
        except BaseException:
            zstd.kill()
            raise
        finally:
            zstd.stdout.close()
        error = zstd.stderr.read().decode(errors="replace")
        zstd.stderr.close()
        if zstd.wait() != 0:
            raise RuntimeError(f"zstd could not decompress {archive.name}: {error.strip()}")
    return ids


def side_ids(*sources: Path | None) -> set[str] | None:
    """The annotation IDs of one release, from the first source that exists: a tree (directory) or a tarball.

    None when none exists.
    """
    for source in sources:
        if source is not None and source.is_dir():
            return traffic_ids(source)
        if source is not None and source.is_file():
            return traffic_ids_in_archive(source)
    return None


def diff_traffic(old: set[str], new: set[str]) -> TrafficDiff:
    return TrafficDiff(added=sorted(new - old), removed=sorted(old - new))
