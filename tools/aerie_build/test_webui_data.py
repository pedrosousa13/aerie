"""Chromium's chrome/test/data/webui files, for the Views UI test binaries only.

`//chrome/test:test_support_ui` reaches `//chrome/test/data/webui:resources`,
which compiles TypeScript from that directory. Helium's source tarball keeps
only its `BUILD.gn`, so `aerie_browser_tests` and `aerie_interactive_ui_tests`
cannot be scheduled without the rest (issue #175).

`./aerie test-cpp` calls `ensure()` for those two targets only. It downloads the
directory at the pinned Chromium tag from chromium.googlesource.com, over https,
and unpacks the files the tree does not have. The server stamps every archive
member with the time of the request, so the archive bytes differ on every
download and cannot carry a sha256. The pin in helium.lock (`[test_data.webui]`)
is instead the sha256 of a digest list: the sorted lines `<sha256 of the file>
<path>` of every file. A mismatch stops the build and nothing is unpacked.
Nothing is fetched on a plain `./aerie build`, and none of these files ships.
"""

from __future__ import annotations

import hashlib
import os
import shutil
import tarfile
from pathlib import Path, PurePosixPath
from typing import Callable

from . import fallback, lock

URL_TEMPLATE = (
    "https://chromium.googlesource.com/chromium/src/+archive/refs/tags/{version}/chrome/test/data/webui.tar.gz"
)
TARGET_DIR = Path("chrome") / "test" / "data" / "webui"
MARKER_FILE = "tsconfig_base.json"
STAMP = ".aerie-test-data-webui.stamp"
MAX_BYTES = 256 * 1024 * 1024  # the real directory is about 30 MB unpacked


class TestDataError(RuntimeError):
    """The test data could not be pinned, fetched or verified."""

    __test__ = False


def url_for(chromium_version: str) -> str:
    return URL_TEMPLATE.format(version=chromium_version)


def archive_name(chromium_version: str) -> str:
    return f"chromium-test-data-webui-{chromium_version}.tar.gz"


def _clean_name(archive: Path, raw: str) -> str:
    name = raw[2:] if raw.startswith("./") else raw
    parts = PurePosixPath(name).parts
    if (not name or name.startswith("/") or "\\" in name or ".." in parts or "." in parts
            or any(not part for part in name.split("/"))):
        raise TestDataError(f"{archive.name}: unsafe member {raw!r}; the archive is not unpacked")
    return name


def _read(archive: Path) -> dict[str, bytes]:
    """Every file of the archive by relative path. Anything but plain files and directories is refused."""
    files: dict[str, bytes] = {}
    total = 0
    with tarfile.open(archive, "r:gz") as tar:
        for member in tar:
            if member.isdir() and member.name in ("", ".", "./"):
                continue
            name = _clean_name(archive, member.name.rstrip("/") if member.isdir() else member.name)
            if member.isdir():
                continue
            if not member.isfile():
                raise TestDataError(f"{archive.name}: unsafe member {member.name!r}; the archive is not unpacked")
            total += member.size
            if total > MAX_BYTES:
                raise TestDataError(f"{archive.name}: unpacks to more than {MAX_BYTES} bytes")
            handle = tar.extractfile(member)
            assert handle is not None
            files[name] = handle.read()
    if MARKER_FILE not in files:
        raise TestDataError(f"{archive.name}: has no {MARKER_FILE}; it is not Chromium's webui test data")
    return files


def _digest(files: dict[str, bytes]) -> str:
    outer = hashlib.sha256()
    for name in sorted(files):
        outer.update(f"{hashlib.sha256(files[name]).hexdigest()}  {name}\n".encode())
    return outer.hexdigest()


def content_digest(archive: Path) -> str:
    """The sha256 of the digest list of the archive's files; the same for every download of the same commit."""
    return _digest(_read(archive))


def _unpack(files: dict[str, bytes], target: Path) -> int:
    written = 0
    for name, data in files.items():
        path = target / name
        if path.exists():
            continue  # keep what the tree has, such as Helium's own BUILD.gn
        path.parent.mkdir(parents=True, exist_ok=True)
        staging = path.with_name(path.name + ".aerie-tmp")
        staging.write_bytes(data)
        os.replace(staging, path)
        written += 1
    return written


def ensure(
    tree: Path, downloads: Path, pin: dict, chromium_version: str,
    opener: fallback.Opener = fallback.default_opener, log: Callable[[str], None] = print,
) -> None:
    """Make `<tree>/chrome/test/data/webui/` complete, from the archive that `pin` ([test_data.webui]) names."""
    if not pin:
        raise TestDataError("helium.lock has no [test_data.webui] pin")
    if pin.get("chromium_version") != chromium_version:
        raise TestDataError(
            f"helium.lock pins the webui test data for Chromium {pin.get('chromium_version')}, "
            f"but this tree is Chromium {chromium_version}: pin the new digest in [test_data.webui]")
    url, digest = pin["url"], pin["sha256"]
    if not url.startswith("https://"):
        raise TestDataError(f"refusing non-https URL {url!r}")
    target = tree / TARGET_DIR
    stamp = tree / STAMP
    if stamp.is_file() and stamp.read_text().strip() == digest and (target / MARKER_FILE).is_file():
        log("test data: webui files already in the tree")
        return
    downloads.mkdir(parents=True, exist_ok=True)
    archive = downloads / archive_name(chromium_version)
    if archive.is_file():
        log(f"test data: {archive.name} already downloaded; verifying")
    else:
        log(f"test data: downloading {url} (UI test builds only; helium.lock pins digest {digest})")
        try:
            fallback.download(url, archive, opener)
        except fallback.FetchError as exc:
            raise TestDataError(str(exc)) from exc
    files = _read(archive)
    actual = _digest(files)
    if actual != digest:
        archive.replace(archive.with_name(archive.name + ".rejected"))
        raise TestDataError(
            f"{archive.name}: sha256 mismatch: helium.lock pins {digest}, the files give {actual}. "
            f"Nothing was unpacked. The file was renamed to {archive.name}.rejected; "
            f"the next run downloads {url} again")
    log(f"test data: {archive.name}: digest matches helium.lock")
    written = _unpack(files, target)
    stamp.write_text(digest + "\n")
    log(f"test data: unpacked {written} file(s) into {target}")
