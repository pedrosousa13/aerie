"""./aerie patches check: does the whole patch series apply to the pinned Helium?

Applies helium-chromium, helium-linux and Aerie's patches, in build order, to
a sparse Chromium tree under <build root>/check/. The sparse tree holds only
the files the series touch, pulled from the pinned Chromium tarballs. It is
cheap enough for hosted CI and never touches the real source tree.
"""

from __future__ import annotations

import configparser
import contextlib
import fcntl
import json
import os
import shutil
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Sequence

from . import lock, patches
from .config import BuildPaths

MANIFEST = ".aerie-sparse.json"
BUILDING = ".aerie-sparse.building"
STALE_WORK_SECONDS = 3600
AERIE_PATCHES = "patches"


def layers(repo: Path) -> list[patches.Layer]:
    """Patch directories in build order. Aerie's applies with fuzz off."""
    return [
        patches.Layer("helium-chromium", repo / "helium-chromium" / "patches", fuzz=True),
        patches.Layer("helium-linux", repo / "helium-linux" / "patches", fuzz=True),
        patches.Layer("aerie", repo / AERIE_PATCHES, fuzz=False),
    ]


@dataclass(frozen=True)
class Source:
    """One archive Helium unpacks into the tree, as its downloads INI describes it."""

    archive: Path
    strip: str  # leading directory inside the archive, "" for none
    output: str  # where it unpacks, relative to the tree root, "" for the root


def sources_from_ini(inis: Sequence[Path], chromium_version: str, downloads: Path) -> list[Source]:
    """Archives in unpack order, read from Helium's downloads.ini / deps.ini files."""
    sources = []
    for ini in inis:
        parser = configparser.ConfigParser(defaults={"_chromium_version": chromium_version})
        with open(ini, encoding="utf-8") as fh:
            parser.read_file(fh, source=str(ini))
        for section in parser.sections():
            output = parser.get(section, "output_path", fallback="").strip()
            while output.startswith("./"):
                output = output[2:]
            sources.append(Source(
                downloads / parser.get(section, "download_filename"),
                parser.get(section, "strip_leading_dirs", fallback="").strip(),
                output.strip("/"),
            ))
    return sources


def _under(output: str, path: str) -> str | None:
    if not output:
        return path
    return path[len(output) + 1:] if path.startswith(output + "/") else None


def _read_manifest(path: Path) -> dict | None:
    try:
        data = json.loads(path.read_text())
    except (OSError, ValueError):
        return None
    ok = isinstance(data, dict) and isinstance(data.get("present"), list) and isinstance(data.get("absent"), list)
    return data if ok else None


def ensure_sparse(
    sparse: Path,
    chromium_version: str,
    wanted: Sequence[str],
    sources: Sequence[Source],
    verify: Callable[[Path], None],
) -> list[str]:
    """Make sure sparse holds every wanted file the sources contain.

    Sources are read in unpack order and later ones overwrite earlier ones,
    like Helium's unpack step. Returns the wanted files no source has (patches
    create those). Files already extracted, and files known to be absent, are
    not looked up again, so an unchanged series reads no archive at all.
    verify runs on each archive before it is read.
    """
    signature = [[src.archive.name, src.strip, src.output] for src in sources]
    manifest_path = sparse / MANIFEST
    building = sparse / BUILDING
    state = {"chromium_version": chromium_version, "sources": signature, "present": [], "absent": []}
    if sparse.exists():
        old = _read_manifest(manifest_path)
        if (
            old is not None
            and not building.exists()
            and old.get("chromium_version") == chromium_version
            and old.get("sources") == signature
        ):
            state = old
        else:
            shutil.rmtree(sparse)
    sparse.mkdir(parents=True, exist_ok=True)
    known = set(state["present"]) | set(state["absent"])
    todo = sorted(set(wanted) - known)
    found: set[str] = set()
    if todo:
        building.touch()  # present until the manifest is written: a crash leaves the cache untrusted
        for src in sources:
            here = {rel: path for path in todo if (rel := _under(src.output, path)) is not None}
            if not here:
                continue
            verify(src.archive)
            got = patches.extract_wanted(src.archive, src.strip, here, sparse / src.output)
            found |= {here[rel] for rel in got}
    state["present"] = sorted(set(state["present"]) | found)
    state["absent"] = sorted(set(state["absent"]) | (set(todo) - found))
    tmp = sparse / (MANIFEST + ".tmp")
    tmp.write_text(json.dumps(state, indent=1) + "\n")
    os.replace(tmp, manifest_path)
    building.unlink(missing_ok=True)
    return sorted(set(wanted) & set(state["absent"]))


def run_check(
    repo: Path,
    paths: BuildPaths,
    pins: lock.Lock,
    log: Callable[[str], None] = print,
) -> int:
    """Lint Aerie's patches, then apply all three series. Returns the exit status."""
    status = 0
    series = layers(repo)

    problems = patches.lint_patch_dir(series[-1].directory)
    for problem in problems:
        log(f"LINT    {problem.patch}: {problem.message}")
        status = 1

    version = pins.chromium_version
    sources = sources_from_ini(
        [repo / "helium-chromium" / "downloads.ini", repo / "helium-linux" / "downloads.ini",
         repo / "helium-chromium" / "deps.ini"],
        version, paths.downloads,
    )
    check_root = paths.root / "check"
    sparse = check_root / "sparse"
    wanted = patches.wanted_files(series)
    log(f"{len(wanted)} files touched by {sum(len(patches.layer_entries(l)) for l in series)} patches")
    check_root.mkdir(parents=True, exist_ok=True)
    _sweep_stale_work(check_root)
    work: Path | None = None
    try:
        work = Path(tempfile.mkdtemp(prefix="work-", dir=check_root))
        # The sparse cache is updated in place, so only one check may use it at a
        # time. The lock covers the update and the copy; patching runs unlocked.
        with _locked(check_root / "sparse.lock"):
            ensure_sparse(
                sparse, version, wanted, sources,
                verify=lambda tarball: lock.verify_downloads(pins, paths.downloads, [tarball.name]),
            )
            patches.copy_tree(sparse, work)
        (work / MANIFEST).unlink()
        (work / BUILDING).unlink(missing_ok=True)
        results = patches.apply_layers(work, series)
    finally:
        if work is not None:
            shutil.rmtree(work, ignore_errors=True)
    report, ok = patches.format_report(results)
    log(report)
    return status if ok else 1


def _sweep_stale_work(check_root: Path) -> None:
    """Remove work-* dirs a killed run left behind (older than an hour)."""
    cutoff = time.time() - STALE_WORK_SECONDS
    for path in check_root.glob("work-*"):
        try:
            if path.is_dir() and path.stat().st_mtime < cutoff:
                shutil.rmtree(path, ignore_errors=True)
        except OSError:
            pass


@contextlib.contextmanager
def _locked(path: Path):
    """Hold an exclusive flock on path for the duration of the block."""
    with open(path, "a") as fh:
        fcntl.flock(fh, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(fh, fcntl.LOCK_UN)
