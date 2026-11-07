"""Aerie layers that touch tree files, applied incrementally.

An Aerie layer is anything that changes files in the Helium-processed source
tree: the patch series, the Helium -> Aerie substitution, and later the
overlay. Chromium's build is ninja-driven and a clean build takes hours, so a
change to a layer must rewrite only the files whose final content changed.
Their mtimes stay as they are for everything else.

The model:

* Before a layer first touches a file, the file's pristine bytes (as Helium's
  steps left them) are saved under <store>/files/<path>. The manifest records
  every saved path, also paths that did not exist (a file a layer creates).
* On each run, the layers compute the final content of every file they touch
  from pristine content. Nothing is written to the tree while they compute.
* A file is written only when its bytes differ from the tree (atomic replace).
  A file in the manifest that no layer touches any more is restored to
  pristine, or deleted if it was not there before.

Crash safety. A journal file exists while a run changes the store or tree.
The order is: journal, snapshots, manifest, tree writes, manifest cleanup,
journal removed. The tree is never written before its pristine bytes are in
the manifest, so the next run can always compute the same plan again; it
finishes the job and removes leftover temp files.

To add a layer, implement `Layer.apply(workspace)`: read with `workspace.read`
(pristine content plus what earlier layers staged), stage results with
`workspace.put(path, data_or_None)`. `workspace.stage_dir` and
`workspace.materialize` are for layers that run external tools on files.
"""

from __future__ import annotations

import fcntl
import hashlib
import json
import os
import shutil
import tempfile
from contextlib import contextmanager
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
from typing import Callable, Iterable, Iterator, Protocol, Sequence

MANIFEST = "manifest.json"
JOURNAL = "journal.json"
FILES = "files"
TMP_SUFFIX = ".aerie-tmp"
# State files the pre-#24 pipeline left in a tree it had patched or substituted in place.
LEGACY_STATE = (".aerie-patches.json", ".aeriesub.stamp", ".aerie-fingerprints.json")


class LayerError(RuntimeError):
    """A layer cannot be applied to this tree."""


def _check_rel(rel: str) -> None:
    pure = PurePosixPath(rel)
    if not rel or pure.is_absolute() or ".." in pure.parts:
        raise LayerError(f"unsafe tree path: {rel!r}")


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_atomic(path: Path, data: bytes, mode_from: Path | None = None, mode: int | None = None) -> None:
    """Write through a temp file in the same directory; a crash never leaves half a file."""
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + TMP_SUFFIX)
    try:
        with open(tmp, "wb") as fh:
            fh.write(data)
            fh.flush()
            os.fsync(fh.fileno())
        if mode_from is not None and mode_from.exists():
            shutil.copymode(mode_from, tmp)
        elif mode is not None:
            os.chmod(tmp, mode)
        os.replace(tmp, path)
    finally:
        tmp.unlink(missing_ok=True)


def _read(path: Path) -> bytes | None:
    return path.read_bytes() if path.is_file() else None


class Reader:
    """Pristine content of tree files, plus staged changes. Picklable, for worker processes."""

    def __init__(self, tree: Path, files_dir: Path, snapshotted: frozenset[str],
                 staged: dict[str, bytes | None] | None = None):
        self.tree = tree
        self.files_dir = files_dir
        self.snapshotted = snapshotted
        self.staged = dict(staged or {})

    def pristine_present(self, rel: str) -> bool:
        return self.pristine(rel) is not None

    def pristine(self, rel: str) -> bytes | None:
        _check_rel(rel)
        if rel in self.snapshotted:
            return _read(self.files_dir / rel)  # absent snapshot file: the path did not exist
        return _read(self.tree / rel)

    def __call__(self, rel: str) -> bytes | None:
        if rel in self.staged:
            return self.staged[rel]
        return self.pristine(rel)


class Workspace:
    """What a layer sees and changes. Nothing here writes to the tree."""

    def __init__(self, reader: Reader, stage_dir: Path):
        self.reader = reader
        self.stage_dir = stage_dir

    @property
    def tree(self) -> Path:
        return self.reader.tree

    def read(self, rel: str) -> bytes | None:
        return self.reader(rel)

    def put(self, rel: str, data: bytes | None) -> None:
        """Stage the final content of rel (None: the file does not exist)."""
        _check_rel(rel)
        self.reader.staged[rel] = data

    def known_paths(self) -> set[str]:
        """Paths the store or earlier layers know about, apart from the files in the tree."""
        return set(self.reader.snapshotted) | set(self.reader.staged)

    def materialize(self, rels: Iterable[str]) -> None:
        """Write the current content of each path under stage_dir, for external tools."""
        for rel in rels:
            data = self.read(rel)
            if data is not None:
                target = self.stage_dir / rel
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)


class Layer(Protocol):
    name: str

    def apply(self, ws: Workspace) -> None: ...


@dataclass(frozen=True)
class Change:
    path: str
    kind: str  # create | modify | delete | restore | remove (restore of a path that did not exist)


@dataclass
class Report:
    changes: list[Change] = field(default_factory=list)
    snapshots: int = 0
    recovered: bool = False

    @property
    def idle(self) -> bool:
        return not self.changes

    def summary(self) -> str:
        counts: dict[str, int] = {}
        for c in self.changes:
            counts[c.kind] = counts.get(c.kind, 0) + 1
        if not counts:
            return "no tree file changes"
        return ", ".join(f"{n} {kind}" for kind, n in sorted(counts.items()))


class Store:
    """The pristine snapshot of one source tree: <dir>/files/... and <dir>/manifest.json."""

    def __init__(self, directory: Path):
        self.dir = directory
        self.files_dir = directory / FILES
        self.entries: dict[str, dict] = {}

    def load(self) -> None:
        path = self.dir / MANIFEST
        self.entries = json.loads(path.read_text()) if path.is_file() else {}
        for rel, entry in self.entries.items():
            if entry["present"]:
                data = _read(self.files_dir / rel)
                if data is None or _sha(data) != entry["sha256"]:
                    raise LayerError(
                        f"the pristine snapshot of {rel} in {self.dir} is missing or damaged; "
                        "rebuild the source tree with: ./aerie build --clean")

    def save(self) -> None:
        write_atomic(self.dir / MANIFEST, (json.dumps(self.entries, indent=1, sort_keys=True) + "\n").encode())

    def reader(self, tree: Path) -> Reader:
        return Reader(tree, self.files_dir, frozenset(self.entries))

    def snapshot(self, rel: str, data: bytes | None, mode: int | None = None) -> None:
        if data is not None:
            write_atomic(self.files_dir / rel, data)
        self.entries[rel] = {"present": data is not None, "sha256": _sha(data) if data is not None else None,
                             "mode": mode}

    def mode(self, rel: str) -> int | None:
        return (self.entries.get(rel) or {}).get("mode")

    def forget(self, rels: Iterable[str]) -> None:
        """Drop entries. The caller saves the manifest, then calls drop_files."""
        for rel in rels:
            self.entries.pop(rel, None)

    def drop_files(self, rels: Iterable[str]) -> None:
        """Delete snapshot files that no manifest entry refers to. Orphans are harmless."""
        for rel in rels:
            (self.files_dir / rel).unlink(missing_ok=True)


@contextmanager
def locked(path: Path) -> Iterator[None]:
    """Exclusive flock on a lock file, so two runs never change one tree at once."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "a") as fh:
        fcntl.flock(fh, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(fh, fcntl.LOCK_UN)


def lock_path(store_dir: Path) -> Path:
    return store_dir.parent / (store_dir.name + ".lock")


def clear_store(store_dir: Path) -> None:
    """Delete a store (--clean, a fresh unpack, prune), under the same lock as apply_layers."""
    with locked(lock_path(store_dir)):
        shutil.rmtree(store_dir, ignore_errors=True)


def apply_layers(
    tree: Path,
    store_dir: Path,
    layers: Sequence[Layer],
    stage_root: Path,
    log: Callable[[str], None] = lambda _m: None,
    dry_run: bool = False,
) -> Report:
    """Bring the tree to "pristine + layers", writing only what differs.

    One run at a time per store (flock on <store>.lock). Each run stages in its
    own directory under stage_root.
    """
    with locked(lock_path(store_dir)):
        return _apply(tree, store_dir, layers, stage_root, log, dry_run)


def _apply(tree, store_dir, layers, stage_root, log, dry_run) -> Report:
    store = Store(store_dir)
    store.load()
    report = Report()
    journal = store_dir / JOURNAL
    if journal.is_file():
        report.recovered = True
        log("a previous layer re-apply was interrupted; recovering")
        if not dry_run:
            for rel in json.loads(journal.read_text()).get("paths", []):
                (tree / (rel + TMP_SUFFIX)).unlink(missing_ok=True)
                (store.files_dir / (rel + TMP_SUFFIX)).unlink(missing_ok=True)

    stage_root.mkdir(parents=True, exist_ok=True)
    stage_dir = Path(tempfile.mkdtemp(prefix="stage-", dir=stage_root))
    try:
        ws = Workspace(store.reader(tree), stage_dir)
        for layer in layers:
            log(f"layer {layer.name}")
            layer.apply(ws)
    finally:
        shutil.rmtree(stage_dir, ignore_errors=True)

    pristine = ws.reader.pristine
    final = {rel: data for rel, data in ws.reader.staged.items() if data != pristine(rel)}
    stale = sorted(set(store.entries) - set(final))
    targets: dict[str, bytes | None] = {rel: pristine(rel) for rel in stale}
    targets.update(final)

    plan: list[tuple[Change, bytes | None]] = []
    for rel in sorted(targets):
        want, have = targets[rel], _read(tree / rel)
        if want == have:
            continue
        restore = rel not in final
        if want is None:
            kind = "remove" if restore else "delete"
        elif have is None:
            kind = "restore" if restore else "create"
        else:
            kind = "restore" if restore else "modify"
        plan.append((Change(rel, kind), want))
    new = sorted(set(final) - set(store.entries))
    report.changes = [c for c, _ in plan]
    report.snapshots = len(new)
    if dry_run:
        return report
    if not plan and not new and not stale:
        journal.unlink(missing_ok=True)  # an interrupted run had finished everything but its last step
        return report

    store_dir.mkdir(parents=True, exist_ok=True)
    write_atomic(journal, json.dumps({"paths": sorted({*targets, *new})}).encode())
    for rel in new:  # the tree still holds pristine bytes here
        path = tree / rel
        store.snapshot(rel, pristine(rel), path.stat().st_mode & 0o7777 if path.is_file() else None)
    store.save()
    for change, want in plan:
        path = tree / change.path
        if want is None:
            path.unlink(missing_ok=True)
        else:
            write_atomic(path, want, mode_from=path, mode=store.mode(change.path))
    # Manifest first, files after: a crash in between leaves harmless orphans, never a
    # manifest entry without its snapshot.
    store.forget(stale)
    store.save()
    store.drop_files(stale)
    journal.unlink()
    return report
