"""Chromium's test fonts, for the C++ unit tests only.

Every test target that depends on `//base/test:test_support` also depends on
`//third_party/test_fonts:test_fonts`, which copies files from
`third_party/test_fonts/test_fonts/`. Helium's source tarball does not have that
directory (upstream fetches it as a `gcs` entry in DEPS, and Helium's pruning
list names it too), so the test targets cannot be scheduled without it.

`./aerie test-cpp` calls `ensure()` after the tree is prepared. It reads the
bucket, object name and sha256 from the tree's own DEPS entry, so a Chromium
bump moves the pin by itself. The archive is downloaded over https from
Google storage, checked against that sha256, cached in the downloads
directory and unpacked into the tree. Nothing is fetched on a plain
`./aerie build`, and the shipped browser never contains these fonts: they are
only copied into test output.
"""

from __future__ import annotations

import ast
import os
import re
import shutil
import tarfile
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from . import fallback, lock

DEPS_KEY = "src/third_party/test_fonts/test_fonts"
FONT_DIR = Path("third_party") / "test_fonts" / "test_fonts"
URL_TEMPLATE = "https://storage.googleapis.com/{bucket}/{object}"
STAMP = ".aerie-test-fonts.stamp"
REQUIRED_FONT = "Ahem.ttf"
MAX_BYTES = 512 * 1024 * 1024  # the real archive is about 33 MB

_BUCKET_RE = re.compile(r"^[a-z0-9][a-z0-9_.-]*$")
_OBJECT_RE = re.compile(r"^[0-9a-f]{40}$")
_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


class TestFontsError(RuntimeError):
    """The test fonts could not be pinned, fetched or verified."""


@dataclass(frozen=True)
class FontPin:
    bucket: str
    object_name: str
    sha256: str

    @property
    def url(self) -> str:
        return URL_TEMPLATE.format(bucket=self.bucket, object=self.object_name)

    @property
    def archive_name(self) -> str:
        return f"chromium-test-fonts-{self.object_name}.tar.gz"


def parse_deps(text: str) -> FontPin:
    """Read the pin from the text of the tree's DEPS file."""
    match = re.search(r"""^\s*['"]""" + re.escape(DEPS_KEY) + r"""['"]\s*:\s*\{""", text, re.MULTILINE)
    if not match:
        raise TestFontsError(f"DEPS has no {DEPS_KEY!r} entry; cannot pin the test fonts")
    start = match.end() - 1
    depth = 0
    for index in range(start, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            break
    else:
        raise TestFontsError(f"DEPS entry {DEPS_KEY!r} is not closed")
    try:
        entry = ast.literal_eval(text[start:index + 1])
    except (ValueError, SyntaxError) as exc:
        raise TestFontsError(f"cannot read the DEPS entry {DEPS_KEY!r}: {exc}") from exc
    if entry.get("dep_type") != "gcs":
        raise TestFontsError(f"DEPS entry {DEPS_KEY!r} is not a gcs dependency (dep_type {entry.get('dep_type')!r})")
    objects = entry.get("objects")
    if not isinstance(objects, list) or len(objects) != 1 or not isinstance(objects[0], dict):
        raise TestFontsError(f"DEPS entry {DEPS_KEY!r} must list exactly one object")
    bucket, obj = entry.get("bucket"), objects[0]
    name, sha = obj.get("object_name"), obj.get("sha256sum")
    if not isinstance(bucket, str) or not _BUCKET_RE.match(bucket):
        raise TestFontsError(f"DEPS entry {DEPS_KEY!r}: unsafe bucket {bucket!r}")
    if not isinstance(name, str) or not _OBJECT_RE.match(name):
        raise TestFontsError(f"DEPS entry {DEPS_KEY!r}: unsafe object_name {name!r}")
    if not isinstance(sha, str) or not _SHA256_RE.match(sha):
        raise TestFontsError(f"DEPS entry {DEPS_KEY!r}: sha256sum must be 64 lowercase hex digits, got {sha!r}")
    return FontPin(bucket, name, sha)


def _verify(pin: FontPin, archive: Path) -> None:
    actual = lock.sha256_file(archive)
    if actual != pin.sha256:
        archive.replace(archive.with_name(archive.name + ".rejected"))
        raise TestFontsError(
            f"{archive.name}: sha256 mismatch: DEPS pins {pin.sha256}, the file has {actual}. "
            f"Nothing was unpacked. The file was renamed to {archive.name}.rejected; "
            f"the next run downloads {pin.url} again")


def _members(archive: Path) -> dict[str, bytes]:
    """Every file of the archive, by bare name. Anything but a plain file in the top directory is refused."""
    files: dict[str, bytes] = {}
    total = 0
    with tarfile.open(archive, "r:gz") as tar:
        for member in tar:
            name = member.name[2:] if member.name.startswith("./") else member.name
            if member.isdir() and name in ("", "."):
                continue
            if not member.isfile() or not name or "/" in name or "\\" in name or name in (".", ".."):
                raise TestFontsError(f"{archive.name}: unsafe member {member.name!r}; the archive is not unpacked")
            total += member.size
            if total > MAX_BYTES:
                raise TestFontsError(f"{archive.name}: unpacks to more than {MAX_BYTES} bytes")
            handle = tar.extractfile(member)
            assert handle is not None
            files[name] = handle.read()
    if REQUIRED_FONT not in files:
        raise TestFontsError(f"{archive.name}: has no {REQUIRED_FONT}; it is not Chromium's test font bundle")
    return files


def _unpack(archive: Path, target: Path) -> None:
    files = _members(archive)
    staging = target.with_name(target.name + ".aerie-tmp")
    shutil.rmtree(staging, ignore_errors=True)
    staging.mkdir(parents=True)
    for name, data in files.items():
        (staging / name).write_bytes(data)
    shutil.rmtree(target, ignore_errors=True)
    os.replace(staging, target)


def ensure(
    tree: Path, downloads: Path, opener: fallback.Opener = fallback.default_opener,
    log: Callable[[str], None] = print,
) -> None:
    """Make `<tree>/third_party/test_fonts/test_fonts/` exist, from the archive DEPS names."""
    try:
        deps = (tree / "DEPS").read_text(encoding="utf-8")
    except OSError as exc:
        raise TestFontsError(f"cannot read {tree / 'DEPS'}: {exc}") from exc
    pin = parse_deps(deps)
    target = tree / FONT_DIR
    stamp = tree / STAMP
    if stamp.is_file() and stamp.read_text().strip() == pin.sha256 and (target / REQUIRED_FONT).is_file():
        log(f"test fonts: already in the tree ({pin.object_name})")
        return
    downloads.mkdir(parents=True, exist_ok=True)
    archive = downloads / pin.archive_name
    if archive.is_file():
        log(f"test fonts: {archive.name} already downloaded; verifying")
    else:
        log(f"test fonts: downloading {pin.url} (test builds only; DEPS pins sha256 {pin.sha256})")
        fallback.download(pin.url, archive, opener)
    _verify(pin, archive)
    log(f"test fonts: {archive.name}: sha256 matches DEPS")
    _unpack(archive, target)
    stamp.write_text(pin.sha256 + "\n")
    log(f"test fonts: unpacked into {target}")
