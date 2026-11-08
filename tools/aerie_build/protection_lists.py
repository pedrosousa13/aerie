"""The phishing and malware domain lists: committed and pinned by hash, like the ad lists.

Same rules as `filter_lists` (read its docstring): the files are committed in
`overlay/third_party/filter_lists/` next to `protection_lists.lock.json`, the
build reads only those files, and a changed list stops the build until a person
runs `./aerie lists update` and commits the diff. `./aerie lists update` runs
this module's `update` right after the ad lists', so both kinds follow one
schedule, and each list comes from its publisher's URL.

These are plain domain lists, not Adblock syntax, so the header is different:
`# Title:` names the list and `# Generated:` or `# Updated:` (a timestamp) is
its version. A list that has no such header is versioned by the day it was
retrieved, "retrieved YYYY-MM-DD", and its hash is the only identity check.
Entries are one domain per line, or hosts format (`0.0.0.0 domain`).

Lists:

* malware-filter's phishing-filter, hosts variant
  (https://gitlab.com/malware-filter/phishing-filter): phishing domains from
  OpenPhish, IPThreat and PhishTank. The list is CC BY-SA 4.0. See
  docs/phishing-protection.md.
* URLhaus (abuse.ch): its unauthenticated host file is gone (410, 2026-11) and
  the new export needs a personal Auth-Key. It is NOT pinned. See
  docs/phishing-protection.md.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import re
from pathlib import Path
from typing import Callable

from . import fallback
from .filter_lists import ListError, ListPin, _download, _write_atomic, load_lock

LOCK_RELATIVE = Path("third_party") / "filter_lists" / "protection_lists.lock.json"
_GENERATED_RE = re.compile(r"^#\s*(?:Generated|Updated):\s*(\S+)\s*$", re.MULTILINE)
_TITLE_RE = re.compile(r"^#\s*Title:\s*(.+?)\s*$", re.MULTILINE)
_DOMAIN_LINE_RE = re.compile(r"^[A-Za-z0-9._-]+\.[A-Za-z0-9-]+$")
# A headerless list must be nearly all domain-shaped lines, so that an error
# page or another kind of file is not pinned as a list. A big list can have a
# few junk lines (the engine skips them).
MIN_DOMAIN_FRACTION = 0.99


def lock_path(repo: Path) -> Path:
    return Path(repo) / "overlay" / LOCK_RELATIVE


def header_version(data: bytes) -> str | None:
    match = _GENERATED_RE.search(data[:4096].decode("utf-8", errors="replace"))
    return match.group(1) if match else None


def header_title(data: bytes) -> str | None:
    match = _TITLE_RE.search(data[:4096].decode("utf-8", errors="replace"))
    return match.group(1) if match else None


def _entry_lines(data: bytes) -> list[str]:
    """The lines that are neither blank nor comments, stripped."""
    return [line.strip() for line in data.decode("utf-8", errors="replace").splitlines()
            if line.strip() and not line.lstrip().startswith("#")]


def entry_count(data: bytes) -> int:
    """Lines that are neither blank nor comments. A rough count, enough to catch an empty list."""
    return len(_entry_lines(data))


def domain_fraction(data: bytes) -> float:
    """Share of the entry lines (not blank, not comments) that look like a domain."""
    lines = _entry_lines(data)
    if not lines:
        return 0.0
    return sum(1 for line in lines if _DOMAIN_LINE_RE.match(line)) / len(lines)


def _entry_domain(line: str) -> str:
    """The domain on an entry line: the line itself, or the host of a hosts-format line (`0.0.0.0 domain`)."""
    parts = line.split()
    if len(parts) == 2 and parts[0] in ("0.0.0.0", "127.0.0.1"):
        return parts[1].lower()
    return line.lower()


def _selfcheck_path(lock: Path) -> Path:
    """Where the build's self-check file sits, relative to the lock in overlay/third_party/filter_lists/."""
    return lock.resolve().parents[2] / "components" / "aerie_adblock" / "protection_selfcheck.json"


def _missing_selfcheck_hosts(selfcheck: Path, datas: list[bytes]) -> list[str]:
    """The 'listed' hosts of the self-check file that no list covers (a host is covered by itself or a parent)."""
    domains = {_entry_domain(line) for data in datas for line in _entry_lines(data)}
    missing = []
    for host in json.loads(selfcheck.read_text(encoding="utf-8")).get("listed", []):
        root = host.lower()
        while root not in domains and "." in root:
            root = root.split(".", 1)[1]
        if root not in domains:
            missing.append(host)
    return missing


def verify_bytes(pin: ListPin, data: bytes) -> None:
    """Raise ListError unless data is exactly the pinned list."""
    actual = hashlib.sha256(data).hexdigest()
    if len(data) != pin.size or actual != pin.sha256:
        raise ListError(
            f"{pin.file}: sha256 mismatch: the lock says {pin.sha256} ({pin.size} bytes), the file has "
            f"{actual} ({len(data)} bytes, list generated {header_version(data) or 'unknown'}, the lock has "
            f"{pin.version}). Lists change only through a reviewed commit: run ./aerie lists update")
    version = header_version(data)
    if version is not None and version != pin.version:
        raise ListError(f"{pin.file}: header Generated {version!r} does not match the lock ({pin.version!r})")
    if entry_count(data) == 0:
        raise ListError(f"{pin.file}: the list has no domains")


def verify_dir(pins: list[ListPin], directory: Path) -> None:
    """Check the list files in a directory (the repo or the source tree) against the lock."""
    for pin in pins:
        path = Path(directory) / pin.file
        if not path.is_file():
            raise ListError(f"{path} is missing")
        verify_bytes(pin, path.read_bytes())


def _today() -> str:
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d")


def update(path: Path, opener: fallback.Opener = fallback.default_opener,
           log: Callable[[str], None] = print, today: Callable[[], str] = _today,
           selfcheck: Path | None = None) -> list[tuple[ListPin, ListPin]]:
    """Download each list from its publisher, write the files next to the lock, rewrite the lock.

    Returns (old, new) pairs. Everything is downloaded and checked before anything is written. That
    includes the build's self-check hosts (protection_selfcheck.json, found from the lock's place in
    the overlay unless `selfcheck` names it; skipped when the file is not there): a list that dropped
    one fails here, not at the next build.
    """
    path = Path(path)
    old_pins = load_lock(path)
    downloaded: dict[str, bytes] = {}
    new_pins = []
    for pin in old_pins:
        data = _download(pin.url, opener)
        if not data:
            raise ListError(f"{pin.url}: empty list; refusing to pin it")
        if entry_count(data) == 0:
            raise ListError(f"{pin.url}: no domains in the list; refusing to pin it")
        title = header_title(data)
        version = header_version(data)
        if title is None and version is None:
            # A bare domain list: nothing to read a version from.
            if domain_fraction(data) < MIN_DOMAIN_FRACTION:
                raise ListError(f"{pin.url}: not a plain domain list (an error page?); refusing to pin it")
            version = f"retrieved {today()}"
        else:
            if title is None or not title.startswith(pin.name):
                raise ListError(f"{pin.url}: title {title!r} does not start with {pin.name!r}; refusing to pin it")
            if not version:
                raise ListError(f"{pin.url}: no '# Generated:' or '# Updated:' header; refusing to pin it")
        downloaded[pin.file] = data
        new_pins.append(ListPin(pin.name, pin.file, pin.url, hashlib.sha256(data).hexdigest(), len(data), version))
    check_file = Path(selfcheck) if selfcheck is not None else _selfcheck_path(path)
    if check_file.is_file():
        missing = _missing_selfcheck_hosts(check_file, list(downloaded.values()))
        if missing:
            raise ListError(
                f"the new list no longer holds the self-check host(s) {', '.join(missing)} "
                f"({check_file.name}); pick other hosts from the new list, then run the update again; "
                "nothing was pinned")
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
