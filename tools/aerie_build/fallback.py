"""Fallback source: the official Chromium release tarball.

Normally the build fetches Helium's prepared tarballs from tarballs.imput.net.
If that host is down, `./aerie build` fetches the official Chromium release
tarball for the same Chromium version from Google instead. Fetches fail
closed: the published .hashes file and the sha256 pinned in helium.lock must
agree, and the tarball must match both, or nothing is unpacked.

The official tarball is only the source tree. Helium's prepared tarballs also
carry the build toolchain (see TOOLCHAIN_PATHS); docs/helium-fallback.md says
how to provide it.
"""

from __future__ import annotations

import re
import shutil
import urllib.error
import urllib.request
from pathlib import Path
from typing import Callable

from . import lock

# Helium's prepared tarballs (helium-chromium/downloads.ini).
HELIUM_PROBE_URL = "https://tarballs.imput.net/chromium-{version}/chromium-{version}-base.tar.zst"

SOURCE_MODES = ("auto", "helium", "official")
ENV_SOURCE = "AERIE_SOURCE"
SOURCE_MARKER = ".aerie-source"

# Present in Helium's linux-x64 layer, absent from the official tarball.
TOOLCHAIN_PATHS = (
    "buildtools/linux64/gn",
    "third_party/siso/cipd/siso",
    "third_party/llvm-build/Release+Asserts/bin/clang",
    "third_party/rust-toolchain/bin/rustc",
    "third_party/node/linux/node-linux-x64/bin/node",
    "build/linux/debian_bullseye_amd64-sysroot/usr",
)

_HASH_LINE_RE = re.compile(r"^(?P<algo>[a-z0-9]+)\s+(?P<digest>[0-9a-fA-F]+)\s+\*?(?P<name>\S+)$")

# tarballs.imput.net answers 403 to urllib's default User-Agent (Helium's own
# downloader uses curl), so every request names Aerie instead.
USER_AGENT = "aerie-build (+https://github.com/pedrosousa13/aerie)"

Opener = Callable[[urllib.request.Request], object]


class FetchError(RuntimeError):
    """A fallback download failed (network, bad URL, unparsable checksum file)."""


def _request(url: str, method: str = "GET") -> urllib.request.Request:
    return urllib.request.Request(url, method=method, headers={"User-Agent": USER_AGENT})


def default_opener(request: urllib.request.Request):
    return urllib.request.urlopen(request, timeout=30)


def resolve_mode(flag: str | None, environ) -> str:
    mode = flag or environ.get(ENV_SOURCE) or "auto"
    if mode not in SOURCE_MODES:
        raise FetchError(f"source must be one of {', '.join(SOURCE_MODES)}; got {mode!r}")
    return mode


def helium_host_reachable(version: str, opener: Opener = default_opener) -> bool:
    """True only if Helium's tarball host answers a HEAD request for this version with 2xx."""
    request = _request(HELIUM_PROBE_URL.format(version=version), "HEAD")
    try:
        response = opener(request)
    except (urllib.error.URLError, OSError, TimeoutError):
        return False
    status = getattr(response, "status", 200)
    close = getattr(response, "close", None)
    if close:
        close()
    return 200 <= status < 300


def choose_source(mode: str, version: str, opener: Opener = default_opener) -> str:
    """Return "helium" or "official". Only "auto" probes the network."""
    if mode in ("helium", "official"):
        return mode
    return "helium" if helium_host_reachable(version, opener) else "official"


def parse_hashes(text: str, filename: str) -> str:
    """The sha256 that a Chromium .hashes file lists for filename.

    The file has one "<algorithm> <hex digest>  <file name>" line per
    algorithm. A missing, duplicated or malformed sha256 entry is an error.
    """
    found = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        match = _HASH_LINE_RE.match(line)
        if match and match["algo"] == "sha256" and match["name"] == filename:
            found.append(match["digest"])
    if len(found) != 1:
        raise lock.VerificationError(
            f"{filename}.hashes has {len(found)} sha256 entries for {filename}; expected exactly 1"
        )
    digest = found[0]
    if not re.fullmatch(r"[0-9a-fA-F]{64}", digest):
        raise lock.VerificationError(f"{filename}.hashes: malformed sha256 {digest!r}")
    return digest.lower()


def _require_https(url: str) -> None:
    if not url.startswith("https://"):
        raise FetchError(f"refusing non-https URL {url!r}")


def _read(url: str, opener: Opener) -> bytes:
    _require_https(url)
    try:
        response = opener(_request(url))
        try:
            return response.read()  # type: ignore[attr-defined]
        finally:
            response.close()  # type: ignore[attr-defined]
    except (urllib.error.URLError, OSError) as exc:
        raise FetchError(f"cannot fetch {url}: {exc}") from exc


def _download(url: str, dest: Path, opener: Opener) -> None:
    _require_https(url)
    partial = dest.with_name(dest.name + ".partial")
    try:
        response = opener(_request(url))
        try:
            with open(partial, "wb") as out:
                shutil.copyfileobj(response, out, 1 << 20)  # type: ignore[arg-type]
        finally:
            response.close()  # type: ignore[attr-defined]
    except (urllib.error.URLError, OSError) as exc:
        partial.unlink(missing_ok=True)
        raise FetchError(f"cannot fetch {url}: {exc}") from exc
    partial.replace(dest)


def fetch_official(
    pin: dict, downloads: Path, opener: Opener = default_opener, log: Callable[[str], None] = print
) -> Path:
    """Download the official tarball into downloads and verify it. Returns its path.

    pin is the [fallback.chromium] table of helium.lock (url, hashes_url, sha256).
    Order matters: the published checksum is read and compared with the pin
    first, so a re-uploaded or tampered tarball is never even downloaded.
    """
    url, hashes_url, pinned = pin["url"], pin["hashes_url"], pin["sha256"]
    filename = url.rsplit("/", 1)[-1]
    target = downloads / filename
    downloads.mkdir(parents=True, exist_ok=True)

    if target.is_file():
        log(f"{filename} already downloaded; verifying")
    else:
        log(f"fetching published checksum {hashes_url}")
        published = parse_hashes(_read(hashes_url, opener).decode("utf-8", "replace"), filename)
        if published != pinned:
            raise lock.VerificationError(
                f"published sha256 for {filename} is {published}, helium.lock pins {pinned}; "
                "not downloading. Check the release, then update helium.lock."
            )
        log(f"downloading {url}")
        _download(url, target, opener)
    try:
        lock.verify_file(target, pinned)
    except lock.VerificationError:
        target.replace(target.with_name(target.name + ".rejected"))
        raise
    log(f"{filename}: sha256 matches the published checksum and helium.lock")
    return target


def missing_toolchain(tree: Path) -> list[str]:
    return [p for p in TOOLCHAIN_PATHS if not (tree / p).exists()]


def toolchain_message(missing: list[str]) -> str:
    return (
        "the official Chromium tarball has no build toolchain; missing under the source tree: "
        + ", ".join(missing)
        + ". See docs/helium-fallback.md, 'Toolchain', then rerun ./aerie build."
    )
