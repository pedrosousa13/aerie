"""./aerie bump: move the Helium pins to the newest Helium release.

The only source of releases is the pinned upstream repositories below. A release
is a helium-linux tag `A.B.C.D` and the helium (core) tag `A.B.C` that goes with
it. Each tag is read from upstream with `git ls-remote`, fetched from that same
URL into the submodule, and the submodule is only moved if the fetched tag is the
commit that ls-remote reported. `.gitmodules` must point at the same URLs.

Digests for the new Chromium tarballs come from the SHA256SUMS file of the
chromium-tarballs release, the digests of the other downloads from Helium's
deps.ini, and the fallback digest from the .hashes file next to the official
tarball: the same sources the README names for a manual bump.
"""

from __future__ import annotations

import configparser
import os
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Mapping, Sequence

from . import fallback, helium, helium_deps, lock

CORE_UPSTREAM = "https://github.com/imputnet/helium.git"
PLATFORM_UPSTREAM = "https://github.com/imputnet/helium-linux.git"
SUBMODULES = {"helium-chromium": CORE_UPSTREAM, "helium-linux": PLATFORM_UPSTREAM}
TARBALL_SUMS_URL = "https://github.com/imputnet/chromium-tarballs/releases/download/chromium-{version}/SHA256SUMS"

# Exit statuses of ./aerie bump.
OK, ERROR, PATCHES_FAIL, UPDATE_AVAILABLE = 0, 1, 3, 20

_PLATFORM_TAG_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$")
_CORE_TAG_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
_CHROMIUM_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$")
_COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")


class BumpError(RuntimeError):
    """The bump cannot go on; nothing is half done (the submodules are put back)."""


@dataclass(frozen=True)
class Release:
    platform_tag: str
    platform_commit: str
    core_tag: str
    core_commit: str


def _key(tag: str) -> tuple[int, ...]:
    return tuple(int(n) for n in tag.split("."))


def is_newer(candidate: str, current: str) -> bool:
    return _key(candidate) > _key(current)


def core_tag_for(platform_tag: str) -> str:
    return ".".join(platform_tag.split(".")[:3])


def parse_ls_remote(text: str) -> dict[str, str]:
    """tag -> commit from `git ls-remote --tags` output. An annotated tag's peeled commit (`^{}`) wins."""
    tags: dict[str, str] = {}
    peeled: dict[str, str] = {}
    for line in text.splitlines():
        parts = line.split("\t")
        if len(parts) != 2 or not parts[1].startswith("refs/tags/"):
            continue
        commit, ref = parts
        name = ref[len("refs/tags/"):]
        if name.endswith("^{}"):
            peeled[name[:-3]] = commit
        else:
            tags[name] = commit
    tags.update(peeled)
    return tags


def latest_release(platform_tags: Mapping[str, str]) -> str:
    releases = [t for t in platform_tags if _PLATFORM_TAG_RE.match(t)]
    if not releases:
        raise BumpError("upstream has no release tag of the form A.B.C.D")
    return max(releases, key=_key)


def resolve_release(platform_tags: Mapping[str, str], core_tags: Mapping[str, str], to: str | None = None) -> Release:
    tag = to if to is not None else latest_release(platform_tags)
    if not _PLATFORM_TAG_RE.match(tag):
        raise BumpError(f"{tag!r} is not a Helium release tag (A.B.C.D)")
    if tag not in platform_tags:
        raise BumpError(f"{PLATFORM_UPSTREAM} has no tag {tag}")
    core = core_tag_for(tag)
    if core not in core_tags:
        raise BumpError(f"{CORE_UPSTREAM} has no tag {core}, which release {tag} needs")
    for commit in (platform_tags[tag], core_tags[core]):
        if not _COMMIT_RE.match(commit):
            raise BumpError(f"upstream reported a bad commit id {commit!r}")
    return Release(tag, platform_tags[tag], core, core_tags[core])


def _git(cwd: Path | None, *args: str) -> str:
    cmd = ["git", *(["-C", str(cwd)] if cwd else []), *args]
    proc = subprocess.run(cmd, capture_output=True, text=True, stdin=subprocess.DEVNULL, check=False,
                          env={**os.environ, "GIT_TERMINAL_PROMPT": "0"})
    if proc.returncode != 0:
        raise BumpError(f"git {' '.join(args[:2])} failed: {(proc.stderr or proc.stdout).strip()}")
    return proc.stdout.strip()


def remote_tags(url: str) -> dict[str, str]:
    return parse_ls_remote(_git(None, "ls-remote", "--tags", url))


def check_gitmodules(repo: Path, upstreams: Mapping[str, str] = SUBMODULES) -> None:
    """The submodules must point at the pinned upstreams, or a tag would be read from somewhere else."""
    for name, want in upstreams.items():
        got = _git(repo, "config", "-f", ".gitmodules", "--get", f"submodule.{name}.url")
        if got != want:
            raise BumpError(f".gitmodules points {name} at {got}, not at the pinned upstream {want}")


def checkout_tag(submodule: Path, url: str, tag: str, commit: str) -> None:
    """Fetch the tag from url and check out commit, but only if the tag is that commit."""
    _git(submodule, "fetch", "--no-tags", "-q", url, f"+refs/tags/{tag}:refs/tags/{tag}")
    fetched = _git(submodule, "rev-parse", f"refs/tags/{tag}^{{commit}}")
    if fetched != commit:
        raise BumpError(f"tag {tag} from {url} is commit {fetched}, but the tag list said {commit}; not moving")
    _git(submodule, "checkout", "--detach", "-q", commit)


def _sections(inis: Iterable[Path], chromium_version: str):
    for ini in inis:
        if not ini.is_file():
            continue
        parser = configparser.ConfigParser(defaults={"_chromium_version": chromium_version})
        with open(ini, encoding="utf-8") as fh:
            parser.read_file(fh, source=str(ini))
        for section in parser.sections():
            yield ini, section, parser[section]


def pin_downloads(inis: Sequence[Path], chromium_version: str, sums: str,
                  dropped: Iterable[str] = helium_deps.DROPPED_SECTIONS) -> dict[str, str]:
    """filename -> sha256 for every download Aerie fetches: from the ini's own sha256, else from sums."""
    dropped = set(dropped)
    by_name: dict[str, str] = {}
    for line in sums.splitlines():
        parts = line.split()
        if len(parts) == 2 and re.fullmatch(r"[0-9a-fA-F]{64}", parts[0]):
            by_name[parts[1].lstrip("*")] = parts[0].lower()
    pinned: dict[str, str] = {}
    for ini, section, values in _sections(inis, chromium_version):
        if ini.name == "deps.ini" and section in dropped:
            continue
        name = values["download_filename"]
        digest = values.get("sha256") or by_name.get(name)
        if not digest:
            raise BumpError(f"no sha256 for download {name} ({ini.name} [{section}]): not in the ini and not in SHA256SUMS")
        pinned[name] = digest.lower()
    return pinned


def rewrite_lock(text: str, release: Release, chromium_version: str, downloads: Mapping[str, str],
                 fallback_sha256: str) -> str:
    """helium.lock with the new release, digests and fallback; comments and other sections are kept."""
    old_chromium = re.search(r'^chromium_version = "([^"]+)"', text, re.M)
    old_platform = re.search(r'^platform_tag = "([^"]+)"', text, re.M)
    if not old_chromium or not old_platform:
        raise BumpError("helium.lock has no chromium_version or platform_tag line")
    values = {
        "helium": {"version": release.platform_tag, "chromium_version": chromium_version,
                   "core_tag": release.core_tag, "core_commit": release.core_commit,
                   "platform_tag": release.platform_tag, "platform_commit": release.platform_commit},
        "fallback.chromium": {"sha256": fallback_sha256},
    }
    out: list[str] = []
    section = ""
    wrote_downloads = False
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            section = stripped[1:-1]
        elif line.startswith("#"):
            line = line.replace(f"tag {old_platform[1]}.", f"tag {release.platform_tag}.")
            line = line.replace(old_chromium[1], chromium_version)
        elif section == "downloads" and stripped.startswith('"'):
            if not wrote_downloads:
                out += [f'"{name}" = "{digest}"' for name, digest in downloads.items()]
                wrote_downloads = True
            continue
        elif section in values and "=" in line:
            key = line.split("=", 1)[0].strip()
            if key in values[section]:
                line = f'{key} = "{values[section][key]}"'
            elif section == "fallback.chromium" and key in ("url", "hashes_url"):
                line = re.sub(r"chromium-[0-9.]+\.tar\.xz", f"chromium-{chromium_version}.tar.xz", line)
        out.append(line)
    if not wrote_downloads:
        raise BumpError("helium.lock has no [downloads] entries to replace")
    return "\n".join(out) + "\n"


def summary(old_helium: str, old_chromium: str, new_helium: str, new_chromium: str) -> str:
    return f"Helium {old_helium} -> {new_helium}\nChromium {old_chromium} -> {new_chromium}"


def _text(url: str, opener) -> str:
    return fallback._read(url, opener).decode("utf-8", "replace")


def apply(repo: Path, release: Release, opener=fallback.default_opener,
          log: Callable[[str], None] = print, upstreams: Mapping[str, str] = SUBMODULES) -> tuple[lock.Lock, lock.Lock]:
    """Move both submodules and helium.lock to release. Returns (old pins, new pins).

    If anything fails before helium.lock is written, the submodules go back where they were.
    """
    lock_path = repo / "helium.lock"
    old = lock.load_lock(lock_path)
    check_gitmodules(repo, upstreams)
    dirty = _git(repo, "status", "--porcelain", "--", "helium.lock", *SUBMODULES)
    if dirty:
        raise BumpError("helium.lock or a Helium submodule has uncommitted changes; commit or revert them first:\n" + dirty)
    core, platform = repo / "helium-chromium", repo / "helium-linux"
    before = {core: _git(core, "rev-parse", "HEAD"), platform: _git(platform, "rev-parse", "HEAD")}
    try:
        checkout_tag(core, upstreams["helium-chromium"], release.core_tag, release.core_commit)
        checkout_tag(platform, upstreams["helium-linux"], release.platform_tag, release.platform_commit)
        chromium = (core / "chromium_version.txt").read_text(encoding="utf-8").strip()
        if not _CHROMIUM_RE.match(chromium):
            raise BumpError(f"helium-chromium/chromium_version.txt holds {chromium!r}, not a Chromium version")
        built = helium.helium_version(core, platform)
        if built != release.platform_tag:
            raise BumpError(f"the checkouts build Helium {built}, not {release.platform_tag}; the tags do not match")
        log(f"fetching {TARBALL_SUMS_URL.format(version=chromium)}")
        sums = _text(TARBALL_SUMS_URL.format(version=chromium), opener)
        official = f"https://commondatastorage.googleapis.com/chromium-browser-official/chromium-{chromium}.tar.xz"
        log(f"fetching {official}.hashes")
        fallback_sha = fallback.parse_hashes(_text(official + ".hashes", opener), f"chromium-{chromium}.tar.xz")
        downloads = pin_downloads([core / "downloads.ini", platform / "downloads.ini", core / "deps.ini"], chromium, sums)
        text = rewrite_lock(lock_path.read_text(encoding="utf-8"), release, chromium, downloads, fallback_sha)
        old_text = lock_path.read_text(encoding="utf-8")
        lock_path.write_text(text, encoding="utf-8")
        try:
            new = lock.load_lock(lock_path)
        except BaseException:
            lock_path.write_text(old_text, encoding="utf-8")
            raise
    except BaseException:
        for sub, commit in before.items():
            subprocess.run(["git", "-C", str(sub), "checkout", "--detach", "-q", commit], check=False,
                           capture_output=True)
        raise
    return old, new


def sources(repo: Path, pins: lock.Lock) -> list[tuple[str, str]]:
    """(download_filename, url) of every archive Aerie fetches for the pinned release."""
    core, platform = repo / "helium-chromium", repo / "helium-linux"
    found = []
    for ini, section, values in _sections([core / "downloads.ini", platform / "downloads.ini", core / "deps.ini"],
                                          pins.chromium_version):
        if ini.name == "deps.ini" and section in helium_deps.DROPPED_SECTIONS:
            continue
        found.append((values["download_filename"], values["url"]))
    return found


def download_missing(repo: Path, pins: lock.Lock, downloads: Path, opener=fallback.default_opener,
                     log: Callable[[str], None] = print) -> None:
    """Fetch the pinned archives that are not in downloads yet, and check each against helium.lock."""
    downloads.mkdir(parents=True, exist_ok=True)
    for name, url in sources(repo, pins):
        if not (downloads / name).is_file():
            log(f"downloading {url}")
            fallback.download(url, downloads / name, opener)
        lock.verify_downloads(pins, downloads, [name])


def render_report(text: str, patch_lines: Sequence[str], patches_ok: bool | None) -> str:
    """Markdown for a bump pull request: the versions, then the patch check result with failing hunks."""
    out = ["## Helium bump", "", "```", text, "```", ""]
    if patches_ok is None:
        out.append("The patch check was not run.")
    elif patches_ok:
        out.append("The patch check passed: every Helium and Aerie patch applies and the source gates pass.")
    else:
        out += ["The patch check **failed**. Fix the patches below, then update "
                "`docs/helium-dependency-ledger.md` if a Helium patch it lists changed.", ""]
    if patch_lines:
        out += ["", "```", *patch_lines, "```"]
    return "\n".join(out) + "\n"
