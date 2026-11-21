"""The rebase report for a Helium bump, and `./aerie patches refresh`.

Three pieces:
- rows_for / render_report: every patch with its apply status (clean, offset,
  fuzz, failed) and its Aerie-Purpose line, as docs/rebase/<helium-version>.md.
- traffic_ids / diff_traffic: the unique IDs of Chromium's
  net::NetworkTrafficAnnotationTag in two source trees, so a bump shows new
  network traffic for review against docs/network-endpoints.md.
- refresh_layer: regenerate the Aerie patches that only apply at an offset or
  with fuzz, so the series applies exactly again. Headers are kept as they are.
"""

from __future__ import annotations

import os
import re
import subprocess
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Sequence

from . import patches

TRAFFIC_COMMAND = "./aerie traffic-diff --old-tree <old tree> --new-tree <new tree>"
REBASE_DIR = "docs/rebase"

# The unique ID is the first argument in every form.
_ANNOTATION_RE = re.compile(
    r"\b(?:Define|DefinePartial|Complete|BranchedComplete)NetworkTrafficAnnotation\s*\(\s*\"([^\"\\]+)\"")
_SOURCE_SUFFIXES = (".cc", ".h", ".mm", ".cpp", ".cxx", ".hpp")
_SKIP_DIRS = frozenset({".git", "out"})


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
            elif result.status == "ok":
                status = result.drift or "clean"
            elif result.status == "failed" and result.drift and "context has drifted" in result.detail:
                status = result.drift  # applies, but only shifted or loosened; the strict layer refuses it
            else:
                status = result.status
            purpose = ""
            if path.is_file():
                purpose = patches.parse_header(path.read_text(encoding="utf-8", errors="replace")).get("Aerie-Purpose", "")
            rows.append(PatchRow(name, status, purpose))
    return rows


def _cell(text: str) -> str:
    return text.replace("|", "\\|").replace("\n", " ")


def render_report(old_helium: str, new_helium: str, old_chromium: str, new_chromium: str,
                  rows: Sequence[PatchRow], traffic: TrafficDiff | None) -> str:
    counts = {s: sum(r.status == s for r in rows) for s in ("clean", "offset", "fuzz", "failed")}
    other = len(rows) - sum(counts.values())
    summary = ", ".join(f"{n} {s}" for s, n in counts.items()) + (f", {other} other" if other else "")
    out = [
        f"# Rebase report: Helium {new_helium}", "",
        f"Helium {old_helium} -> {new_helium}", "",
        f"Chromium {old_chromium} -> {new_chromium}", "",
        f"{len(rows)} patches: {summary}.", "",
        "Aerie's patches must apply with no offset and no fuzz. `./aerie patches refresh` regenerates "
        "the ones that drifted. Helium's patches apply with fuzz allowed, so their offset and fuzz rows "
        "are information.", "",
        "## Network traffic annotations", "",
    ]
    if traffic is None:
        out += ["The traffic diff was **not run**: the old and the new Chromium tree were not both "
                "available. To run it:", "", "```", TRAFFIC_COMMAND, "```", ""]
    else:
        out += ["Review each added ID against [docs/network-endpoints.md](../network-endpoints.md).", ""]
        out += _traffic_lines(traffic)
    out += ["## Patches", "", "| Patch | Status | Purpose |", "| --- | --- | --- |"]
    out += [f"| {_cell(r.patch)} | {r.status} | {_cell(r.purpose)} |" for r in rows]
    return "\n".join(out) + "\n"


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


def traffic_ids(tree: Path) -> set[str]:
    """Unique IDs of every NetworkTrafficAnnotationTag defined in the C++ files under tree."""
    ids: set[str] = set()
    for root, dirs, files in os.walk(tree):
        dirs[:] = [d for d in dirs if d not in _SKIP_DIRS]
        for name in files:
            if name.endswith(_SOURCE_SUFFIXES):
                try:
                    text = Path(root, name).read_text(encoding="utf-8", errors="replace")
                except OSError:
                    continue
                ids.update(_ANNOTATION_RE.findall(text))
    return ids


def diff_traffic(old: set[str], new: set[str]) -> TrafficDiff:
    return TrafficDiff(added=sorted(new - old), removed=sorted(old - new))


# --- refresh -------------------------------------------------------------------

@dataclass
class RefreshOutcome:
    refreshed: list[str] = field(default_factory=list)
    failed: list[patches.PatchResult] = field(default_factory=list)


def _split_header(text: str) -> tuple[str, str]:
    """(text before the first diff line, the rest), byte for byte."""
    pos = 0
    for line in text.splitlines(keepends=True):
        if patches._DIFF_START_RE.match(line):
            break
        pos += len(line)
    return text[:pos], text[pos:]


def _read(tree: Path, rel: str) -> bytes | None:
    path = tree / rel
    return path.read_bytes() if path.is_file() else None


def _unified(rel: str, old: bytes | None, new: bytes | None) -> str:
    with tempfile.TemporaryDirectory() as tmp:
        sides = []
        for tag, data in (("old", old), ("new", new)):
            if data is None:
                sides.append("/dev/null")
            else:
                path = Path(tmp) / tag
                path.write_bytes(data)
                sides.append(str(path))
        proc = subprocess.run(
            ["diff", "-u", "--label", "/dev/null" if old is None else f"a/{rel}",
             "--label", "/dev/null" if new is None else f"b/{rel}", *sides],
            capture_output=True, check=False)
    if proc.returncode not in (0, 1):
        raise RuntimeError(f"diff failed for {rel}: {proc.stderr.decode(errors='replace')}")
    return proc.stdout.decode("utf-8", errors="surrogateescape")


def refresh_layer(tree: Path, layer: patches.Layer) -> RefreshOutcome:
    """Apply layer's series to tree in order. A patch that applies exactly is left alone.

    A patch that applies only at an offset or with fuzz is applied with fuzz and rewritten from the
    change it made, keeping its header. A patch that does not apply is reported, and later patches
    that touch the same files are skipped, as in apply_layers.
    """
    outcome = RefreshOutcome()
    blocked: dict[str, str] = {}
    for name, path in patches.layer_entries(layer):
        text = path.read_text(encoding="utf-8", errors="surrogateescape")
        files = patches.touched_files(text)
        blockers = sorted({blocked[f] for f in files if f in blocked})
        if blockers:
            outcome.failed.append(patches.PatchResult(name, "skipped", "depends on failed " + ", ".join(blockers)))
            for f in files:
                blocked.setdefault(f, blockers[0])
            continue
        before = {f: _read(tree, f) for f in files}
        ok, output = patches._run_patch(tree, path, fuzz=False, dry_run=True)
        if ok:
            patches._run_patch(tree, path, fuzz=False, dry_run=False)
            continue
        ok, output = patches._run_patch(tree, path, fuzz=True, dry_run=True)
        if ok:
            ok, output = patches._run_patch(tree, path, fuzz=True, dry_run=False)
        if not ok:
            outcome.failed.append(patches.PatchResult(name, "failed", output))
            for f in files:
                blocked.setdefault(f, name)
            continue
        header, body = _split_header(text)
        order = list(patches._hunks_by_file(body)) or files
        diffs = [_unified(f, before[f], _read(tree, f)) for f in order if f in before]
        path.write_text(header + "".join(diffs), encoding="utf-8", errors="surrogateescape", newline="")
        outcome.refreshed.append(name.split("/", 1)[1])
    return outcome
