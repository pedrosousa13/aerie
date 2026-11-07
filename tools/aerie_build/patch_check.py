"""./aerie patches check: does the whole patch series apply to the pinned Helium?

The check follows the build. It applies helium-chromium's and helium-linux's
patches to a sparse Chromium tree under <build root>/check/, runs Helium's
domain substitution, name substitution and version steps on it (helium_steps),
and only then applies Aerie's patches, strictly. So an Aerie patch is judged on
the text the build gives it, not on Chromium's original text. The sparse tree
holds only the files the series touch, plus the few files those steps need,
pulled from the pinned Chromium tarballs. It is cheap enough for hosted CI and
never touches the real source tree.

Left out: Helium's i18n step (it needs every translated string file; it only
edits .xtb files, which Aerie patches may not touch, and the lint says so) and
the resource replacement step.

When a prepared source tree with a pristine snapshot exists, the Aerie patches
are also tried against it, as `./aerie build --dry-run-layers` does.
"""

from __future__ import annotations

import configparser
import contextlib
import fcntl
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Sequence

from . import helium_deps, helium_inputs, helium_steps, layers as tree_layers, lock, patches, treegate
from .config import BuildPaths

MANIFEST = ".aerie-sparse.json"
BUILDING = ".aerie-sparse.building"
STALE_WORK_SECONDS = 3600
AERIE_PATCHES = "patches"
# Besides the files the patches touch: name substitution wants OWNERS and the grit
# package, the version step wants chrome/VERSION. A trailing / means a directory.
STEP_INPUTS = ("OWNERS", "chrome/VERSION", "tools/grit/grit/")


def drop_file(repo: Path) -> Path:
    return repo / AERIE_PATCHES / patches.DROP_FILE


def layers(repo: Path) -> list[patches.Layer]:
    """Patch directories in build order. Aerie's applies with fuzz off.

    Helium's two layers skip the entries in patches/helium-drop.txt.
    """
    drops, _ = patches.parse_drops(drop_file(repo))
    adapts, _ = patches.parse_adapts(repo / AERIE_PATCHES)
    return [
        patches.with_drops(patches.Layer("helium-chromium", repo / "helium-chromium" / "patches", fuzz=True),
                           drops, adapts),
        patches.with_drops(patches.Layer("helium-linux", repo / "helium-linux" / "patches", fuzz=True),
                           drops, adapts),
        patches.Layer("aerie", repo / AERIE_PATCHES, fuzz=False),
    ]


def drop_problems(repo: Path) -> tuple[list[patches.Drop], list[patches.Problem]]:
    """The parsed drops, and everything wrong with the drop file or with a drop."""
    drops, problems = patches.parse_drops(drop_file(repo))
    adapts, adapt_problems = patches.parse_adapts(repo / AERIE_PATCHES)
    helium = [
        patches.Layer("helium-chromium", repo / "helium-chromium" / "patches", fuzz=True),
        patches.Layer("helium-linux", repo / "helium-linux" / "patches", fuzz=True),
    ]
    ini = repo / "helium-chromium" / "deps.ini"
    deps = [patches.Problem("helium-chromium/deps.ini", m) for m in helium_deps.check(ini)] if ini.is_file() else []
    return drops, (problems + adapt_problems + patches.check_drops(drops, helium)
                   + patches.check_adapts(adapts, drops, helium) + deps)


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
    """Lint Aerie's patches, then apply the series the way the build does. Returns the exit status."""
    status = 0
    series = layers(repo)
    core, platform = repo / "helium-chromium", repo / "helium-linux"

    problems = patches.lint_patch_dir(series[-1].directory, patches.load_substitution_rules(core))
    _, drop_issues = drop_problems(repo)
    problems = problems + drop_issues
    for problem in problems:
        log(f"LINT    {problem.patch}: {problem.message}")
        status = 1

    version = pins.chromium_version
    # The same deps.ini the build reads: without the assets Aerie does not download.
    # A problem with deps.ini itself (already linted above) is the only reason to read it unfiltered.
    deps_ini = core / "deps.ini"
    if not helium_deps.check(deps_ini):
        deps_ini = helium_deps.write_filtered(deps_ini, paths.tmp / "helium-deps" / "deps.ini")
    sources = sources_from_ini(
        [core / "downloads.ini", platform / "downloads.ini", deps_ini],
        version, paths.downloads,
    )
    check_root = paths.root / "check"
    sparse = check_root / "sparse"
    wanted = sorted({*patches.wanted_files(series), *STEP_INPUTS})
    log(f"{len(wanted)} files touched by {sum(len(patches.layer_entries(l)) for l in series)} patches")
    check_root.mkdir(parents=True, exist_ok=True)
    _sweep_stale_work(check_root)
    work: Path | None = None
    gates_ok = True
    gate_lines = ""
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
        results = patches.apply_layers(work, series[:-1])
        if all(r.status == "ok" for r in results):
            _helium_steps(repo, work, check_root)
            results += patches.apply_layers(work, series[-1:])
            if all(r.status == "ok" for r in results):
                # Only when everything applied: a half-patched tree would fail the gates for the wrong reason.
                gate_report, gates_ok = treegate.format_report(treegate.run(work))
                gate_lines = gate_report
        else:
            log("Aerie patches were not tried: Helium's own patches did not apply")
    finally:
        if work is not None:
            shutil.rmtree(work, ignore_errors=True)
    report, ok = patches.format_report(results)
    if gate_lines:
        log(gate_lines)
    log(report)
    if not _check_prepared_tree(repo, paths, pins, check_root, log):
        ok = False
    return status if ok and gates_ok else 1


def _helium_steps(repo: Path, work: Path, check_root: Path) -> None:
    """Domain substitution, name substitution and version, on the patched sparse tree."""
    core, platform = repo / "helium-chromium", repo / "helium-linux"
    env = {**os.environ, "PYTHONDONTWRITEBYTECODE": "1"}

    def run(cmd) -> None:
        proc = subprocess.run([str(c) for c in cmd], env=env, capture_output=True, text=True,
                              errors="replace", stdin=subprocess.DEVNULL, check=False)
        if proc.returncode != 0:
            raise RuntimeError(f"Helium step failed on the sparse tree: {' '.join(str(c) for c in cmd[1:3])}\n"
                               + (proc.stdout + proc.stderr)[-2000:])

    domain_list = helium_steps.filtered_domain_list(core, work, check_root / f"{work.name}.domain-list")
    try:
        helium_steps.post_patch_steps(
            sys.executable, core, platform, work, run, lambda _name, action: action(),
            domain_list=domain_list, i18n=False)
    finally:
        domain_list.unlink(missing_ok=True)


def _check_prepared_tree(repo: Path, paths: BuildPaths, pins: lock.Lock, check_root: Path,
                         log: Callable[[str], None]) -> bool:
    """Also try Aerie's patches against the real prepared tree, if there is one. Writes nothing to it."""
    name = f"helium-{pins.helium_version}"
    tree, store = paths.tree(name), paths.pristine / name
    if not (tree / ".resources.stamp").exists():
        return True
    if not helium_inputs.matches(repo, tree):
        log(f"prepared tree {tree}: built from different Helium inputs; skipped")
        return True
    legacy = [n for n in tree_layers.LEGACY_STATE if (tree / n).exists()]
    if legacy:
        log(f"prepared tree {tree}: skipped, it was changed in place by an older build; run ./aerie build --clean")
        return True
    try:
        report = tree_layers.apply_layers(
            tree, store, [patches.PatchLayer(layers(repo)[-1])], check_root / "stage", dry_run=True)
    except patches.PatchStateError as exc:
        log(f"prepared tree {tree}: Aerie patches do not apply:\n{exc}")
        return False
    except tree_layers.LayerError as exc:
        log(f"prepared tree {tree}: cannot be checked: {exc}")
        return False
    log(f"prepared tree {tree}: Aerie patches apply; {report.summary()} (dry run)")
    return True


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
