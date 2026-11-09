"""The built-in filter lists: EasyList and EasyPrivacy, committed and pinned by hash.

The list files are committed in the repo, in `overlay/third_party/filter_lists/`
(they reach the source tree as `third_party/filter_lists/`), next to
`filter_lists.lock.json`. The build reads only those files and never fetches
anything. The lock pins each list by sha256, size and its own `! Version:`
header, and the build checks the files in the tree against it (fail closed). So
a changed list, committed or not, stops the build.

easylist.to stays the recorded source (`url` in the lock). It always serves the
newest list, so the lists are refreshed on purpose: `./aerie lists update`
downloads both, checks title and version, writes the files and the lock, and a
person reviews the diff and commits it. The lists expire after 4 days upstream;
Aerie does not update them at run time.

Licence: EasyList and EasyPrivacy are dual licensed, GPL-3.0-or-later or
CC BY-SA 3.0, at the user's option. Aerie uses the GPL option (see the
README.chromium next to the lock).
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import urllib.error
import urllib.parse
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from . import fallback

LOCK_RELATIVE = Path("third_party") / "filter_lists" / "filter_lists.lock.json"
SCHEMA = 1
MAX_BYTES = 32 * 1024 * 1024
_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
_VERSION_RE = re.compile(r"^!\s*Version:\s*(\S+)\s*$", re.MULTILINE)
_TITLE_RE = re.compile(r"^!\s*Title:\s*(.+?)\s*$", re.MULTILINE)
_FILE_RE = re.compile(r"^[a-z0-9][a-z0-9_.-]*\.txt$")


class ListError(RuntimeError):
    """A filter list failed verification or could not be fetched."""


@dataclass(frozen=True)
class ListPin:
    name: str
    file: str
    url: str
    sha256: str
    size: int
    version: str


def lock_path(repo: Path) -> Path:
    return Path(repo) / "overlay" / LOCK_RELATIVE


def load_lock(path: Path) -> list[ListPin]:
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise ListError(f"cannot read the filter list lock {path}: {exc}") from exc
    if data.get("schema") != SCHEMA:
        raise ListError(f"{path}: unsupported schema {data.get('schema')!r}")
    pins = []
    seen: set[str] = set()
    for entry in data.get("lists", []):
        try:
            pin = ListPin(entry["name"], entry["file"], entry["url"], entry["sha256"],
                          int(entry["size"]), str(entry["version"]))
        except (KeyError, TypeError, ValueError) as exc:
            raise ListError(f"{path}: bad entry {entry!r}: {exc!r}") from exc
        if not _FILE_RE.match(pin.file):
            raise ListError(f"{path}: unsafe file name {pin.file!r}")
        if pin.file in seen:
            raise ListError(f"{path}: duplicate file {pin.file!r}")
        seen.add(pin.file)
        if not _SHA256_RE.match(pin.sha256):
            raise ListError(f"{path}: {pin.file}: sha256 must be 64 lowercase hex digits")
        if urllib.parse.urlsplit(pin.url).scheme != "https":
            raise ListError(f"{path}: {pin.file}: url must be https")
        if not 0 < pin.size <= MAX_BYTES:
            raise ListError(f"{path}: {pin.file}: size {pin.size} is out of range")
        pins.append(pin)
    if not pins:
        raise ListError(f"{path}: no lists pinned")
    return pins


def header_version(data: bytes) -> str | None:
    match = _VERSION_RE.search(data[:4096].decode("utf-8", errors="replace"))
    return match.group(1) if match else None


def header_title(data: bytes) -> str | None:
    match = _TITLE_RE.search(data[:4096].decode("utf-8", errors="replace"))
    return match.group(1) if match else None


def verify_hash(pin: ListPin, data: bytes, found_version: str | None, version_word: str = "list version") -> None:
    """Raise ListError unless data has the pinned size and sha256. Shared with protection_lists."""
    actual = hashlib.sha256(data).hexdigest()
    if len(data) != pin.size or actual != pin.sha256:
        raise ListError(
            f"{pin.file}: sha256 mismatch: the lock says {pin.sha256} ({pin.size} bytes), the file has "
            f"{actual} ({len(data)} bytes, {version_word} {found_version or 'unknown'}, the lock has "
            f"{pin.version}). Lists change only through a reviewed commit: run ./aerie lists update"
        )


def verify_bytes(pin: ListPin, data: bytes) -> None:
    """Raise ListError unless data is exactly the pinned list."""
    verify_hash(pin, data, header_version(data))
    version = header_version(data)
    if version != pin.version:
        raise ListError(f"{pin.file}: header version {version!r} does not match the lock ({pin.version!r})")


def _download(url: str, opener: fallback.Opener) -> bytes:
    request = fallback._request(url)
    try:
        response = opener(request)
        data = response.read(MAX_BYTES + 1)
        close = getattr(response, "close", None)
        if close:
            close()
    except (urllib.error.URLError, OSError, TimeoutError) as exc:
        raise ListError(f"cannot download {url}: {exc}") from exc
    if len(data) > MAX_BYTES:
        raise ListError(f"{url} is larger than {MAX_BYTES} bytes; refusing it")
    return data


def _write_atomic(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".part")
    tmp.write_bytes(data)
    os.replace(tmp, path)


def verify_dir(pins: list[ListPin], directory: Path) -> None:
    """Check the list files in a directory (the repo or the source tree) against the lock."""
    for pin in pins:
        path = Path(directory) / pin.file
        if not path.is_file():
            raise ListError(f"{path} is missing")
        verify_bytes(pin, path.read_bytes())


def update(path: Path, opener: fallback.Opener = fallback.default_opener,
           log: Callable[[str], None] = print) -> list[tuple[ListPin, ListPin]]:
    """Download each list at its URL, write the list files next to the lock, rewrite the lock.

    Returns (old, new) pairs. All lists are downloaded and checked before anything is
    written. This is the one place a list is pinned without a hash to compare to: a
    deliberate act whose result goes through review.
    """
    path = Path(path)
    old_pins = load_lock(path)
    downloaded: dict[str, bytes] = {}
    new_pins = []
    for pin in old_pins:
        data = _download(pin.url, opener)
        if not data:
            raise ListError(f"{pin.url}: empty list; refusing to pin it")
        version = header_version(data)
        title = header_title(data)
        if not version:
            raise ListError(f"{pin.url}: no '! Version:' header; refusing to pin it")
        if title != pin.name:
            raise ListError(f"{pin.url}: title {title!r} is not {pin.name!r}; refusing to pin it")
        downloaded[pin.file] = data
        new_pins.append(ListPin(pin.name, pin.file, pin.url, hashlib.sha256(data).hexdigest(), len(data), version))
    doc = json.loads(path.read_text(encoding="utf-8"))
    by_file = {p.file: p for p in new_pins}
    for entry in doc["lists"]:
        new = by_file[entry["file"]]
        entry.update(sha256=new.sha256, size=new.size, version=new.version)
    for name, data in downloaded.items():
        _write_atomic(path.parent / name, data)
    path.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
    for old, new in zip(old_pins, new_pins):
        log(f"{new.file}: {old.version} -> {new.version}" + (" (unchanged)" if old.sha256 == new.sha256 else ""))
    return list(zip(old_pins, new_pins))
