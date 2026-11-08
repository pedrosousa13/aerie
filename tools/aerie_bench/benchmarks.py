"""Pinned local copies of Speedometer and JetStream for the perf check (issue #16).

Nothing from these projects is in the repository. helium.lock pins an archive of each (URL, commit,
sha256, licence) in a ``[bench.<name>]`` table. ``fetch`` downloads the archive once into
``<build root>/downloads``, refuses it unless the sha256 matches, and unpacks it into
``<build root>/bench-vendor/<name>``, which the fixture server serves under ``/vendor/<name>/``.
``DRIVERS`` says how to start each benchmark and read its score; they were checked against
the pinned copies (docs/bench.md, "Pinned benchmark copies").

``python3 -m aerie_bench.benchmarks fetch --build-root DIR`` does the same without a browser.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
import tarfile
import tempfile
import tomllib
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable

REPO = Path(__file__).resolve().parents[2]
LOCK_FILE = REPO / "helium.lock"
VENDOR_DIRNAME = "bench-vendor"
PIN_FILE = ".aerie-pin"
USER_AGENT = "aerie-bench (+https://github.com/pedrosousa13/aerie)"

# How the Bench runs each benchmark: load /vendor/<name>/<entry>, poll ``done`` until it is true
# (at most ``timeout_s``), then read the number ``score`` evaluates to. A score that is not a
# number means the run failed (``score`` gives null for that).
DRIVERS: dict[str, dict] = {
    # Speedometer 3.1: the page starts itself (startAutomatically) and sets _hasResults when the last
    # iteration ended or an error stopped it. A stopped run has no Score metric, so score is undefined.
    "speedometer": {
        "entry": "index.html?startAutomatically",
        "done": "globalThis.benchmarkClient?._hasResults === true",
        "score": "globalThis.benchmarkClient?._metrics?.Score?.mean ?? null",
        "timeout_s": 1200,
    },
    # JetStream 3: the overall score is the geometric mean of the workload scores (the driver's own
    # "Overall Score"). Any recorded error, or a workload without a positive score, gives null.
    "jetstream": {
        "entry": "index.html?startAutomatically=true",
        "done": "globalThis.JetStream?.isDone === true",
        "score": ("(() => { const j = globalThis.JetStream; if (!j || j.errors.length) return null;"
                  " const s = j.benchmarks.map(b => b.score);"
                  " if (!s.length || !s.every(x => x > 0)) return null;"
                  " return Math.exp(s.reduce((a, x) => a + Math.log(x), 0) / s.length); })()"),
        "timeout_s": 3600,
    },
}


class BenchmarkError(Exception):
    """A benchmark copy could not be fetched, verified or unpacked."""


@dataclass(frozen=True)
class Pin:
    name: str
    title: str
    url: str
    archive: str
    commit: str
    sha256: str
    license: str


def load_pins(lock_path: Path = LOCK_FILE) -> dict[str, Pin]:
    with open(lock_path, "rb") as fh:
        table = tomllib.load(fh).get("bench", {})
    return {name: Pin(name=name, title=t["title"], url=t["url"], archive=t["archive"], commit=t["commit"],
                      sha256=t["sha256"].lower(), license=t["license"]) for name, t in table.items()}


def archive_names(pins: dict[str, Pin]) -> set[str]:
    """The files these pins keep in <build root>/downloads (``./aerie prune`` must not delete them)."""
    return {p.archive for p in pins.values()}


def vendor_root(build_root: Path) -> Path:
    return Path(build_root) / VENDOR_DIRNAME


def _marker(pin: Pin) -> str:
    return json.dumps({"commit": pin.commit, "sha256": pin.sha256}, sort_keys=True)


def present(pin: Pin, build_root: Path) -> bool:
    """True when the unpacked copy in the build root is the pinned one."""
    try:
        return (vendor_root(build_root) / pin.name / PIN_FILE).read_text() == _marker(pin)
    except OSError:
        return False


def _sha256(path: Path) -> str:
    from aerie_build import lock
    return lock.sha256_file(path)


def _download(pin: Pin, target: Path, opener: Callable) -> None:
    if not pin.url.startswith("https://"):
        raise BenchmarkError(f"{pin.name}: refusing non-https URL {pin.url!r}")
    partial = target.with_name(target.name + ".partial")
    try:
        response = opener(urllib.request.Request(pin.url, headers={"User-Agent": USER_AGENT}))
        try:
            with open(partial, "wb") as out:
                shutil.copyfileobj(response, out, 1 << 20)
        finally:
            response.close()
    except (urllib.error.URLError, OSError) as exc:
        partial.unlink(missing_ok=True)
        raise BenchmarkError(f"{pin.name}: cannot fetch {pin.url}: {exc}") from exc
    partial.replace(target)


def _verify(pin: Pin, archive: Path) -> None:
    from aerie_build import lock
    actual = _sha256(archive)
    if actual != pin.sha256:
        archive.replace(archive.with_name(archive.name + ".rejected"))
        raise lock.VerificationError(
            f"{pin.name}: sha256 mismatch for {archive.name}: helium.lock pins {pin.sha256}, got {actual}; "
            f"kept as {archive.name}.rejected. Check the upstream commit, then update helium.lock.")


def _unpack(pin: Pin, archive: Path, dest: Path) -> None:
    """Unpack the archive's single top directory as ``dest``, through a temporary directory."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=f".{pin.name}-", dir=dest.parent))
    try:
        with tarfile.open(archive) as tar:
            members = tar.getmembers()
            for m in members:
                parts = Path(m.name).parts
                if m.name.startswith("/") or ".." in parts or not (m.isfile() or m.isdir()):
                    raise BenchmarkError(f"{pin.name}: unsafe archive member {m.name!r}")
            tar.extractall(work, members=members, filter="data")
        tops = [p for p in work.iterdir()]
        if len(tops) != 1 or not tops[0].is_dir():
            raise BenchmarkError(f"{pin.name}: expected one top directory in {archive.name}")
        (tops[0] / PIN_FILE).write_text(_marker(pin))
        if dest.exists():
            shutil.rmtree(dest)
        os.replace(tops[0], dest)
    except (tarfile.TarError, OSError) as exc:
        raise BenchmarkError(f"{pin.name}: cannot unpack {archive.name}: {exc}") from exc
    finally:
        shutil.rmtree(work, ignore_errors=True)


def fetch(pin: Pin, build_root: Path, opener: Callable = urllib.request.urlopen,
          log: Callable[[str], None] = print) -> Path:
    """Make the unpacked, verified copy of ``pin`` exist in the build root. Returns its directory.

    A copy that already matches the pin is left alone. An archive already in downloads/ is checked
    against the sha256 again before it is unpacked; one that fails is renamed ``.rejected``.
    """
    dest = vendor_root(build_root) / pin.name
    if present(pin, build_root):
        log(f"{pin.title}: already in {dest}")
        return dest
    downloads = Path(build_root) / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    archive = downloads / pin.archive
    if archive.is_file():
        log(f"{pin.archive} already downloaded; verifying")
    else:
        log(f"downloading {pin.url}")
        _download(pin, archive, opener)
    _verify(pin, archive)
    log(f"{pin.archive}: sha256 matches helium.lock; unpacking to {dest}")
    _unpack(pin, archive, dest)
    return dest


def fetch_all(build_root: Path, names: Iterable[str] | None = None, opener: Callable = urllib.request.urlopen,
              log: Callable[[str], None] = print, lock_path: Path = LOCK_FILE) -> dict[str, Path]:
    pins = load_pins(lock_path)
    wanted = list(names) if names else list(pins)
    unknown = [n for n in wanted if n not in pins]
    if unknown:
        raise BenchmarkError(f"no pin for {', '.join(unknown)} in helium.lock")
    return {n: fetch(pins[n], build_root, opener, log) for n in wanted}


def main(argv: list[str] | None = None) -> int:
    from aerie_build import config, lock
    p = argparse.ArgumentParser(prog="python3 -m aerie_bench.benchmarks", description=__doc__.split("\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    f = sub.add_parser("fetch", help="download, verify and unpack the pinned copies into the build root")
    f.add_argument("--build-root", metavar="DIR", help="build root (default: as ./aerie)")
    sub.add_parser("list", help="print the pins")
    args = p.parse_args(argv)
    try:
        if args.cmd == "list":
            for pin in load_pins().values():
                print(f"{pin.name}: {pin.title}, commit {pin.commit}, sha256 {pin.sha256}, licence {pin.license}")
            return 0
        root = config.resolve_build_root(args.build_root, os.environ, REPO)
        fetch_all(root)
    except (BenchmarkError, lock.VerificationError, config.ConfigError) as exc:
        print(f"aerie_bench.benchmarks: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
