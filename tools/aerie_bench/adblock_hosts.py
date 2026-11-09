"""The host list of the ``adblock_score`` check: pinned, downloaded at run time, never committed.

The list is the Turtlecute adblock test's host list (https://adblock.turtlecute.org, source
https://github.com/Turtlecute33/adblocktest), licensed CC BY-NC-SA 4.0. Aerie does not copy it into the
repo. Only the pin is committed (``adblock_hosts.lock.json``: URL, sha256, size, date). The URL names a
commit of the source repository, so the bytes behind it do not change the way the live site's list does.

Same pattern as ``./aerie lists update`` (``aerie_build.filter_lists``): https only, no redirect to http,
the bytes must match the pinned size and sha256. The verified download is cached in
``<build root>/downloads/`` so a train without a network can still run the check. With neither a network
nor a verified cache the check skips, with the reason. It never passes silently.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import urllib.parse
from dataclasses import dataclass
from datetime import date as _date
from pathlib import Path
from typing import Callable

from aerie_build import fallback, filter_lists

from . import launcher

LOCK_FILE = Path(__file__).with_name("adblock_hosts.lock.json")
SCHEMA = 1
SOURCE_REPO = "Turtlecute33/adblocktest"
SOURCE_PATH = "src/d3host.txt"
LICENCE = "CC BY-NC-SA 4.0 (downloaded at run time, never committed)"
MAX_BYTES = 1024 * 1024
MAX_HOSTS = 2000
_SHA256 = re.compile(r"^[0-9a-f]{64}$")
_COMMIT = re.compile(r"^[0-9a-f]{40}$")
_DATE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
_HOST = re.compile(r"^(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)+[a-z][a-z0-9-]{0,62}$")
_CATEGORY = re.compile(r"^#\s*=+\s*(.+?)\s*=+\s*$")
_ENTRY = re.compile(r"^(?:0\.0\.0\.0|127\.0\.0\.1)\s+(\S+)\s*(?:#.*)?$")
DEFAULT_CATEGORY = "Uncategorised"


class HostListError(Exception):
    """The lock, the download or the list is bad. The check reports an error: a pin is never trusted blindly."""


class HostListUnavailable(Exception):
    """No network and no verified cached copy. The check skips and says why."""


@dataclass(frozen=True)
class Pin:
    url: str
    sha256: str
    size: int
    date: str


@dataclass(frozen=True)
class HostList:
    text: str
    source: str  # "download" or "cache"
    note: str = ""


def load_pin(path: Path = LOCK_FILE) -> Pin:
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise HostListError(f"cannot read the host list lock {path}: {exc}") from exc
    if data.get("schema") != SCHEMA:
        raise HostListError(f"{path}: unsupported schema {data.get('schema')!r}")
    try:
        pin = Pin(str(data["url"]), str(data["sha256"]), int(data["size"]), str(data["date"]))
    except (KeyError, TypeError, ValueError) as exc:
        raise HostListError(f"{path}: bad lock: {exc!r}") from exc
    _check_pin(pin, str(path))
    return pin


def _check_pin(pin: Pin, where: str) -> None:
    if urllib.parse.urlsplit(pin.url).scheme != "https":
        raise HostListError(f"{where}: the url must be https ({pin.url})")
    if not _SHA256.match(pin.sha256):
        raise HostListError(f"{where}: sha256 must be 64 lowercase hex digits")
    if not 0 < pin.size <= MAX_BYTES:
        raise HostListError(f"{where}: size {pin.size} is out of range")
    if not _DATE.match(pin.date):
        raise HostListError(f"{where}: date must be YYYY-MM-DD, got {pin.date!r}")


def verify(pin: Pin, data: bytes) -> None:
    """Raise HostListError unless ``data`` has the pinned size and sha256."""
    actual = hashlib.sha256(data).hexdigest()
    problems = []
    if len(data) != pin.size:
        problems.append(f"size {len(data)}, the lock pins {pin.size}")
    if actual != pin.sha256:
        problems.append(f"sha256 {actual}, the lock pins {pin.sha256}")
    if problems:
        raise HostListError("the host list has " + " and ".join(problems)
                            + ". A pin changes only through a reviewed commit: ./aerie bench --adblock-update-pin")


def cache_name(pin: Pin) -> str:
    return f"adblock-hosts-{pin.sha256}.txt"


def cache_path(build_root: Path | None, pin: Pin) -> Path | None:
    return None if build_root is None else Path(build_root) / "downloads" / cache_name(pin)


def _read_cache(path: Path | None, pin: Pin) -> tuple[bytes | None, str]:
    """(verified bytes, "") or (None, why not)."""
    if path is None:
        return None, "no cache directory"
    try:
        data = path.read_bytes()
    except OSError:
        return None, f"no cached copy at {path}"
    try:
        verify(pin, data)
    except HostListError as exc:
        return None, f"the cached copy at {path} does not match the lock ({exc})"
    return data, ""


def _write_cache(path: Path | None, data: bytes) -> None:
    if path is None:
        return
    try:
        filter_lists._write_atomic(path, data)
    except OSError:
        pass  # a cache that cannot be written only costs the offline fallback


def obtain(pin: Pin, build_root: Path | None, opener: fallback.Opener = filter_lists.list_opener) -> HostList:
    """The verified list: from the pinned URL, else from the verified cache.

    Raises HostListError for a refused answer (a redirect to http, a hash that does not match with
    no good cache) and HostListUnavailable when the network is down and the cache is absent or bad."""
    _check_pin(pin, "the host list pin")
    cache = cache_path(build_root, pin)
    try:
        data, _final = filter_lists._download(pin.url, opener)
    except filter_lists.DownloadError as exc:
        cached, why = _read_cache(cache, pin)
        if cached is None:
            raise HostListUnavailable(f"cannot download the pinned host list ({exc}) and have no usable cache: {why}") from exc
        return HostList(cached.decode("utf-8"), "cache", f"download failed ({exc}); used the verified cache")
    except filter_lists.ListError as exc:
        raise HostListError(str(exc)) from exc
    try:
        verify(pin, data)
    except HostListError as exc:
        cached, _why = _read_cache(cache, pin)
        if cached is None:
            raise
        return HostList(cached.decode("utf-8"), "cache", f"the download was refused ({exc}); used the verified cache")
    _write_cache(cache, data)
    return HostList(data.decode("utf-8"), "download")


def parse(text: str) -> dict[str, list[str]]:
    """Category name -> hosts, in order, lower case, each host once. Anything that is not a plain host
    name is an error, because the names end up in a resolver rule."""
    categories: dict[str, list[str]] = {}
    seen: set[str] = set()
    current = DEFAULT_CATEGORY
    for number, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        header = _CATEGORY.match(line)
        if header:
            current = header.group(1)
            continue
        if not line or line.startswith("#"):
            continue
        entry = _ENTRY.match(line)
        host = entry.group(1).lower() if entry else ""
        if not _HOST.match(host) or host.endswith((".localhost", ".test")):
            raise HostListError(f"host list line {number}: not a plain host name: {line!r}")
        if host in seen:
            continue
        seen.add(host)
        categories.setdefault(current, []).append(host)
    if not seen:
        raise HostListError("the host list has no hosts")
    if len(seen) > MAX_HOSTS:
        raise HostListError(f"the host list has {len(seen)} hosts, more than {MAX_HOSTS}")
    return categories


def resolver_flag(hosts: list[str]) -> str:
    """The browser's resolver rules with every listed host mapped to the loopback fixture server.
    The Bench's own rules follow, so everything else still fails to resolve."""
    maps = [f"MAP {h} 127.0.0.1" for h in hosts]
    return "--host-resolver-rules=" + " , ".join([*maps, *launcher.RESOLVER_RULES])


def update_pin(lock_path: Path, build_root: Path | None, commit: str | None = None,
               opener: fallback.Opener = filter_lists.list_opener, today: str | None = None,
               log: Callable[[str], None] = print) -> Pin:
    """Pin the list at ``commit`` (default: the newest commit that changed it), write the lock and
    cache the bytes. A deliberate act: review the diff of the lock and commit it."""
    if commit is None:
        api = f"https://api.github.com/repos/{SOURCE_REPO}/commits?path={SOURCE_PATH}&per_page=1"
        try:
            data, _ = filter_lists._download(api, opener)
            commit = json.loads(data)[0]["sha"]
        except (filter_lists.ListError, ValueError, LookupError, TypeError) as exc:
            raise HostListError(f"cannot find the newest commit of {SOURCE_PATH}: {exc}") from exc
    if not isinstance(commit, str) or not _COMMIT.match(commit):
        raise HostListError(f"the commit must be 40 lowercase hex digits, got {commit!r}")
    url = f"https://raw.githubusercontent.com/{SOURCE_REPO}/{commit}/{SOURCE_PATH}"
    log(f"downloading {url}")
    try:
        data, _ = filter_lists._download(url, opener)
    except filter_lists.ListError as exc:
        raise HostListError(str(exc)) from exc
    if not 0 < len(data) <= MAX_BYTES:
        raise HostListError(f"the host list has {len(data)} bytes; refusing it")
    parse(data.decode("utf-8", errors="strict"))  # refuse a list the check could not use
    pin = Pin(url, hashlib.sha256(data).hexdigest(), len(data), today or _date.today().isoformat())
    document = {"schema": SCHEMA, "name": "Turtlecute adblock test host list",
                "source": f"https://github.com/{SOURCE_REPO}/blob/{commit}/{SOURCE_PATH}",
                "licence": LICENCE, "url": pin.url, "sha256": pin.sha256, "size": pin.size, "date": pin.date}
    lock_path = Path(lock_path)
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    tmp = lock_path.with_name(lock_path.name + ".part")
    tmp.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    os.replace(tmp, lock_path)
    _write_cache(cache_path(build_root, pin), data)
    log(f"pinned {pin.url} ({pin.size} bytes, sha256 {pin.sha256}); review the diff of {lock_path} and commit it")
    return pin
