"""Apply a deliberately bad TEST patch to a scratch Chromium tree, to prove the perf gate trips.

The patches are in ``tools/aerie_bench/testdata/*.patch``. They are not in ``patches/series``, so
no build applies them by itself. ``memory-hog`` makes the browser process hold 768 MB.

    PYTHONPATH=tools python3 -m aerie_bench.testpatch check  TREE   # does it apply? changes nothing
    PYTHONPATH=tools python3 -m aerie_bench.testpatch apply  TREE   # apply it, leave a marker file
    PYTHONPATH=tools python3 -m aerie_bench.testpatch revert TREE   # take it out again

TREE is a Chromium source tree (the one ./aerie build prepares, or a copy). This tool never builds,
and refuses the Aerie checkout itself. docs/bench.md, "Proving the gate trips", has the whole procedure.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path

DATA = Path(__file__).resolve().parent / "testdata"
REPO = Path(__file__).resolve().parents[2]
DEFAULT = "memory-hog"
MARKER = ".aerie-testpatch-{name}"


class TestPatchError(Exception):
    __test__ = False  # not a test case, whatever pytest thinks of the name


def patch_path(name: str) -> Path:
    path = DATA / f"{name}.patch"
    if not re.fullmatch(r"[a-z0-9-]+", name) or not path.is_file():
        known = ", ".join(sorted(p.stem for p in DATA.glob("*.patch"))) or "none"
        raise TestPatchError(f"no test patch {name!r} (known: {known})")
    return path


def touched_files(text: str) -> list[str]:
    return [m.group(1) for m in re.finditer(r"^\+\+\+ b/(\S+)", text, re.M)]


def _tree(tree: Path) -> Path:
    tree = Path(tree).resolve()
    if tree == REPO or tree.is_relative_to(REPO) or REPO.is_relative_to(tree):
        raise TestPatchError(f"{tree} is, or is around, the Aerie checkout; give a scratch Chromium tree")
    if not tree.is_dir():
        raise TestPatchError(f"{tree} is not a directory")
    return tree


def _run_patch(tree: Path, path: Path, *flags: str) -> subprocess.CompletedProcess:
    argv = ["patch", "-p1", "--batch", "--forward", "-d", str(tree), "-i", str(path), *flags]
    try:
        return subprocess.run(argv, capture_output=True, text=True)
    except OSError as exc:
        raise TestPatchError(f"cannot run patch: {exc}") from exc


def _fits(tree: Path, path: Path, reverse: bool = False) -> None:
    for rel in touched_files(path.read_text()):
        if not (tree / rel).is_file():
            raise TestPatchError(f"{tree} has no {rel}; is it a Chromium source tree?")
    res = _run_patch(tree, path, "--dry-run", *(("-R",) if reverse else ()))
    if res.returncode != 0:
        raise TestPatchError(f"{path.name} does not {'revert' if reverse else 'apply'} cleanly:\n"
                             + (res.stdout + res.stderr).strip())


def check(tree: Path, name: str = DEFAULT) -> str:
    path, tree = patch_path(name), _tree(tree)
    if (tree / MARKER.format(name=name)).exists():
        _fits(tree, path, reverse=True)
        return f"{name} is applied in {tree}"
    _fits(tree, path)
    return f"{name} applies cleanly to {tree}"


def apply(tree: Path, name: str = DEFAULT) -> str:
    path, tree = patch_path(name), _tree(tree)
    marker = tree / MARKER.format(name=name)
    if marker.exists():
        raise TestPatchError(f"{name} is already applied in {tree} (revert it first)")
    _fits(tree, path)
    res = _run_patch(tree, path)
    if res.returncode != 0:
        raise TestPatchError(f"patch failed after a clean dry run:\n{(res.stdout + res.stderr).strip()}")
    marker.write_text(hashlib.sha256(path.read_bytes()).hexdigest() + "\n")
    files = ", ".join(touched_files(path.read_text()))
    return f"applied {name} to {tree} ({files}). Build it, run the perf gate, then revert."


def revert(tree: Path, name: str = DEFAULT) -> str:
    path, tree = patch_path(name), _tree(tree)
    marker = tree / MARKER.format(name=name)
    if not marker.exists():
        raise TestPatchError(f"{name} was not applied to {tree} by this tool (no {marker.name})")
    _fits(tree, path, reverse=True)
    res = _run_patch(tree, path, "-R")
    if res.returncode != 0:
        raise TestPatchError(f"revert failed after a clean dry run:\n{(res.stdout + res.stderr).strip()}")
    marker.unlink()
    return f"reverted {name} in {tree}. Rebuild before trusting the binary again."


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="python3 -m aerie_bench.testpatch", description=__doc__.split("\n")[0])
    p.add_argument("action", choices=("check", "apply", "revert"))
    p.add_argument("tree", metavar="TREE", help="a scratch Chromium source tree")
    p.add_argument("--patch", default=DEFAULT, metavar="NAME", help=f"test patch name (default {DEFAULT})")
    args = p.parse_args(argv)
    try:
        print({"check": check, "apply": apply, "revert": revert}[args.action](Path(args.tree), args.patch))
    except TestPatchError as exc:
        print(f"aerie_bench.testpatch: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
