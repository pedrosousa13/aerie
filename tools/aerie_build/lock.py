"""The source lock (helium.lock): pinned Helium version and download digests.

Helium's own downloader checks each tarball against the digest file its
publisher hosts next to it. Aerie also checks every download against the
sha256 pinned in this repo, so a changed upstream file or a new, unpinned
download stops the build. Every failure path raises; nothing is skipped.
"""

from __future__ import annotations

import configparser
import hashlib
import re
import tomllib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

from . import minisign

_SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")


class VerificationError(Exception):
    """A download or the lock itself failed verification."""


@dataclass(frozen=True)
class Lock:
    helium_version: str
    chromium_version: str
    core_tag: str
    core_commit: str
    platform_tag: str
    platform_commit: str
    downloads: dict[str, str] = field(default_factory=dict)
    tools: dict[str, dict] = field(default_factory=dict)
    # Official Chromium tarball used when Helium's tarball host is down.
    fallback: dict[str, str] = field(default_factory=dict)


def _check_digest(name: str, digest: str) -> str:
    if not isinstance(digest, str) or not _SHA256_RE.match(digest):
        raise VerificationError(f"lock entry {name!r} is not a sha256 hex digest: {digest!r}")
    return digest.lower()


def load_lock(path: Path) -> Lock:
    with open(path, "rb") as fh:
        data = tomllib.load(fh)
    helium = data["helium"]
    downloads = {
        name: _check_digest(name, digest) for name, digest in data.get("downloads", {}).items()
    }
    tools = data.get("tools", {})
    for name, tool in tools.items():
        tool["minisign_key"] = _load_tool_key(name, tool, Path(path).parent)
    fallback = dict(data.get("fallback", {}).get("chromium", {}))
    if fallback:
        fallback["sha256"] = _check_digest("fallback.chromium", fallback.get("sha256"))
        for key in ("url", "hashes_url"):
            if not str(fallback.get(key, "")).startswith("https://"):
                raise VerificationError(f"fallback.chromium.{key} must be an https URL")
    return Lock(
        helium_version=helium["version"],
        chromium_version=helium["chromium_version"],
        core_tag=helium["core_tag"],
        core_commit=helium["core_commit"],
        platform_tag=helium["platform_tag"],
        platform_commit=helium["platform_commit"],
        downloads=downloads,
        tools=tools,
        fallback=fallback,
    )


def _load_tool_key(name: str, tool: dict, base: Path) -> str:
    """Read and check the publisher's minisign public key a tool entry points to."""
    key_file = tool.get("minisign_key_file")
    if not key_file:
        raise VerificationError(
            f"tools.{name} has no minisign_key_file; tools must be verified against "
            "their publisher's signature"
        )
    try:
        text = (base / key_file).read_text(encoding="utf-8")
        minisign.parse_public_key(text)
    except (OSError, minisign.SignatureError) as exc:
        raise VerificationError(f"tools.{name}: bad minisign key file {key_file}: {exc}") from exc
    return text


def verify_tool_archive(name: str, tool: dict, archive: Path, minisig: str) -> None:
    """Check a tool download against its publisher's minisign signature.

    On any failure the archive is renamed to <name>.rejected and
    VerificationError is raised.
    """
    try:
        minisign.verify_file(archive, tool["minisign_key"], minisig)
    except minisign.SignatureError as exc:
        archive.replace(archive.with_name(archive.name + ".rejected"))
        raise VerificationError(f"{name}: {archive.name} failed signature check: {exc}") from exc


def sha256_file(path: Path, chunk: int = 1 << 20) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as fh:
        while block := fh.read(chunk):
            digest.update(block)
    return digest.hexdigest()


def verify_file(path: Path, expected: str) -> None:
    actual = sha256_file(path)
    if actual != expected.lower():
        raise VerificationError(
            f"sha256 mismatch for {path.name}: expected {expected.lower()}, got {actual}"
        )


def verify_downloads(lock: Lock, cache_dir: Path, names: Iterable[str]) -> None:
    """Check each named file in cache_dir against its pinned digest.

    A file that fails is renamed to <name>.rejected so the next run downloads
    it again instead of trusting it.
    """
    names = list(names)
    if not names:
        raise VerificationError("no downloads to verify; refusing to continue")
    for name in names:
        expected = lock.downloads.get(name)
        if expected is None:
            raise VerificationError(
                f"no pinned sha256 for {name!r} in helium.lock; add it before building"
            )
        path = cache_dir / name
        if not path.is_file():
            raise VerificationError(f"download {name!r} is missing from {cache_dir}")
        try:
            verify_file(path, expected)
        except VerificationError:
            path.replace(path.with_name(path.name + ".rejected"))
            raise


def required_downloads(ini_paths: Iterable[Path], chromium_version: str) -> list[str]:
    """List the download_filename of every section in Helium's downloads INI files."""
    names: list[str] = []
    for ini in ini_paths:
        parser = configparser.ConfigParser(defaults={"_chromium_version": chromium_version})
        with open(ini, encoding="utf-8") as fh:
            parser.read_file(fh, source=str(ini))
        for section in parser.sections():
            names.append(parser.get(section, "download_filename"))
    return names
