"""The Aerie Patch Layer: series parsing, header lint, and the apply check.

Aerie keeps its own quilt-style patch directory (patches/series). It is
applied after Helium's core and Linux series, in the same build step, with
the same GNU patch flags Helium's utils/patches.py uses. The one difference
is that Aerie's layer applies with fuzz off and rejects a hunk that lands at a
shifted line, so any context drift is an error.

The check applies all three series to a sparse tree (only the files the
series touch, extracted from the pinned Chromium tarball) and reports every
patch that fails, instead of stopping at the first.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import tarfile
import zipfile
import zlib
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Callable, Iterable, Sequence

REQUIRED_FIELDS = ("Aerie-Purpose", "Aerie-Upstream-Area")
ALLOWED_KINDS = frozenset({"hook", "tracer", "branding", "backport"})

_FIELD_RE = re.compile(r"^(Aerie-[A-Za-z-]+):[ \t]*(.*)$")
_DIFF_START_RE = re.compile(r"^(--- |diff --git |Index: )")
_PATCH_FLAGS = ("-p1", "--ignore-whitespace", "--no-backup-if-mismatch")
_DRIFT_RE = re.compile(r"succeeded at \d+ \((?:offset -?\d+ lines?|fuzz)|with fuzz \d+|\(offset -?\d+ lines?\)")


@dataclass(frozen=True)
class Problem:
    patch: str
    message: str


@dataclass(frozen=True)
class Layer:
    """One patch directory: Helium core, helium-linux, or Aerie."""

    label: str
    directory: Path
    fuzz: bool


class PatchStateError(RuntimeError):
    """The build cannot apply Aerie's layer to this tree."""


class PatchLayer:
    """One patch series as a tree layer (see aerie_build.layers).

    The patches run in a staging directory that holds only the files they
    touch, copied from pristine content. The tree is not changed here.
    """

    def __init__(self, layer: Layer, log: Callable[[str], None] = lambda _m: None):
        self.layer = layer
        self.name = f"{layer.label} patches"
        self._log = log

    def apply(self, ws) -> None:
        entries = layer_entries(self.layer)
        files: list[str] = []
        for name, path in entries:
            if not path.is_file():
                raise PatchStateError(f"{name}: patch file does not exist")
            text = path.read_text(encoding="utf-8", errors="replace")
            if _RENAME_RE.search(text):
                raise PatchStateError(f"{name}: rename/copy headers are not supported")
            files += [f for f in touched_files(text) if f not in files]
        ws.materialize(files)
        # Every patch is judged, so a failure report names all of them.
        results = apply_layers(ws.stage_dir, [self.layer])
        report, ok = format_report(results)
        if not ok:
            raise PatchStateError(report)
        for rel in files:
            staged = ws.stage_dir / rel
            ws.put(rel, staged.read_bytes() if staged.is_file() else None)
        self._log(f"{self.layer.label}: {len(entries)} patch(es) over {len(files)} file(s)")


@dataclass(frozen=True)
class PatchResult:
    patch: str  # "<layer label>/<series entry>"
    status: str  # ok | failed | skipped
    detail: str


def parse_series(series_path: Path) -> list[str]:
    """Same rules as Helium's utils/_common.parse_series."""
    entries = []
    for line in series_path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        entries.append(line.strip().split(" #")[0])
    return entries


def parse_header(text: str) -> dict[str, str]:
    """Aerie-* fields in the preamble before the first diff line.

    GNU patch ignores that preamble, so it is free text for humans and tools.
    An indented line continues the previous field.
    """
    fields: dict[str, str] = {}
    current: str | None = None
    for line in text.splitlines():
        if _DIFF_START_RE.match(line):
            break
        match = _FIELD_RE.match(line)
        if match:
            current = match.group(1)
            fields[current] = match.group(2).strip()
        elif current and line[:1] in (" ", "\t") and line.strip():
            fields[current] = (fields[current] + " " + line.strip()).strip()
        else:
            current = None
    return fields


def touched_files(patch_text: str) -> list[str]:
    """Paths (with the -p1 prefix removed) a patch edits, creates or deletes."""
    files: list[str] = []
    for line in patch_text.splitlines():
        if not line.startswith(("--- ", "+++ ")):
            continue
        name = line[4:].split("\t")[0].strip()
        if name == "/dev/null":
            continue
        if name.startswith(("a/", "b/")):
            name = name[2:]
        if name not in files:
            files.append(name)
    return files


def _unsafe(path: str) -> bool:
    pure = PurePosixPath(path)
    return pure.is_absolute() or ".." in pure.parts


@dataclass(frozen=True)
class SubstitutionRules:
    """What Helium's domain and name substitution rewrite, for linting added lines.

    Aerie's patches apply after those steps, so a line a patch adds is never
    rewritten by them. A line that they would rewrite has to be written in its
    final form, or the patch is wrong.
    """

    domain_patterns: tuple[re.Pattern[str], ...] = ()
    domain_files: frozenset[str] = frozenset()
    # Files helium_resources.txt copies over after the patches; a patch edit there is lost.
    resource_destinations: frozenset[str] = frozenset()


_NAME_RE = re.compile(r"Chrom(?:e|ium)(?!\w)")
_GRD_SUFFIXES = (".grd", ".grdp")
_RENAME_RE = re.compile(r"^(rename|copy) (from|to) ", re.MULTILINE)


def load_substitution_rules(core: Path) -> SubstitutionRules:
    """Read helium-chromium's domain_regex.list and domain_substitution.list (missing: no rules)."""
    regexes, files = [], frozenset()
    regex_list, file_list = core / "domain_regex.list", core / "domain_substitution.list"
    if regex_list.is_file():
        for line in regex_list.read_text(encoding="utf-8").splitlines():
            if line:
                regexes.append(re.compile(line.split("#")[0]))
    if file_list.is_file():
        files = frozenset(line.strip() for line in file_list.read_text(encoding="utf-8").splitlines() if line.strip())
    resources = core / "resources" / "helium_resources.txt"
    destinations: set[str] = set()
    if resources.is_file():
        for line in resources.read_text(encoding="utf-8").splitlines():
            parts = line.split()
            if len(parts) == 2 and not line.lstrip().startswith("#"):
                destinations.add(parts[1])
    return SubstitutionRules(tuple(regexes), files, frozenset(destinations))


_HUNK_RE = re.compile(r"^@@ -\d+(?:,(\d+))? \+\d+(?:,(\d+))? @@")


def added_lines(patch_text: str) -> list[tuple[str, str]]:
    """(file, line) for every line a patch adds.

    Inside a hunk the line counts from the @@ header say what is content, so an
    added line that reads "++ x" is not mistaken for a "+++ " file header.
    """
    out: list[tuple[str, str]] = []
    current = ""
    old_left = new_left = 0
    for line in patch_text.splitlines():
        if old_left > 0 or new_left > 0:
            if line.startswith("+"):
                out.append((current, line[1:]))
                new_left -= 1
            elif line.startswith("-"):
                old_left -= 1
            elif line.startswith("\\"):
                pass  # "\ No newline at end of file"
            else:
                old_left -= 1
                new_left -= 1
            continue
        match = _HUNK_RE.match(line)
        if match:
            old_left = int(match.group(1) if match.group(1) is not None else 1)
            new_left = int(match.group(2) if match.group(2) is not None else 1)
        elif line.startswith("+++ "):
            name = line[4:].split("\t")[0].strip()
            current = name[2:] if name.startswith("b/") else name
    return out


def lint_patch_dir(directory: Path, rules: SubstitutionRules | None = None) -> list[Problem]:
    """Series hygiene and header checks for the Aerie-owned patch directory."""
    series_path = directory / "series"
    if not series_path.is_file():
        return [Problem("series", "series file is missing")]
    problems: list[Problem] = []
    entries = parse_series(series_path)
    seen: set[str] = set()
    root = directory.resolve()
    for entry in entries:
        if entry in seen:
            problems.append(Problem(entry, "listed more than once in series"))
            continue
        seen.add(entry)
        path = (directory / entry).resolve()
        if not path.is_relative_to(root):
            problems.append(Problem(entry, "series entry points outside the patch directory"))
            continue
        if not path.is_file():
            problems.append(Problem(entry, "listed in series but the file does not exist"))
            continue
        problems += _lint_patch(entry, path.read_text(encoding="utf-8", errors="replace"), rules)
    for path in sorted(directory.rglob("*.patch")):
        name = path.relative_to(directory).as_posix()
        if name not in seen:
            problems.append(Problem(name, "patch file is not listed in series"))
    return problems


def _lint_patch(name: str, text: str, rules: SubstitutionRules | None = None) -> list[Problem]:
    problems = []
    header = parse_header(text)
    for field in REQUIRED_FIELDS:
        if not header.get(field):
            problems.append(Problem(name, f"header field {field} is missing or empty"))
    kind = header.get("Aerie-Kind")
    if kind is not None and kind not in ALLOWED_KINDS:
        problems.append(
            Problem(name, f"header field Aerie-Kind is {kind!r}; allowed: {', '.join(sorted(ALLOWED_KINDS))}")
        )
    for path in touched_files(text):
        if _unsafe(path):
            problems.append(Problem(name, f"unsafe path in diff header: {path}"))
        if rules is not None and path in rules.resource_destinations:
            problems.append(Problem(
                name, f"edits {path}, which helium_resources.txt overwrites after the patches apply"))
        if path.lower().endswith(".xtb"):
            problems.append(Problem(
                name, f"edits translation file {path}; Helium's i18n step and Aerie's substitution own .xtb files"))
    if _RENAME_RE.search(text):
        problems.append(Problem(name, "rename/copy headers are not supported; delete and create the file instead"))
    if rules is not None:
        problems += _lint_added_lines(name, text, rules)
    return problems


def _lint_added_lines(name: str, text: str, rules: SubstitutionRules) -> list[Problem]:
    problems: list[Problem] = []
    flagged: set[tuple[str, str]] = set()
    for path, line in added_lines(text):
        if path in rules.domain_files and any(p.search(line) for p in rules.domain_patterns):
            if (path, "domain") not in flagged:
                flagged.add((path, "domain"))
                problems.append(Problem(
                    name, f"an added line in {path} matches helium-chromium/domain_regex.list, but Aerie "
                          f"patches apply after domain substitution and are not rewritten: {line.strip()[:80]}"))
        if path.endswith(_GRD_SUFFIXES) and _NAME_RE.search(line):
            if (path, "name") not in flagged:
                flagged.add((path, "name"))
                problems.append(Problem(
                    name, f"an added line in {path} says Chrome or Chromium, but Aerie patches apply after "
                          f"name substitution and are not rewritten; write the final name: {line.strip()[:80]}"))
    return problems


def layer_entries(layer: Layer) -> list[tuple[str, Path]]:
    return [
        (f"{layer.label}/{entry}", layer.directory / entry)
        for entry in parse_series(layer.directory / "series")
    ]


def wanted_files(layers: Sequence[Layer]) -> list[str]:
    """Every file any patch in any layer touches, sorted."""
    wanted: set[str] = set()
    for layer in layers:
        for _, path in layer_entries(layer):
            wanted.update(touched_files(path.read_text(encoding="utf-8", errors="replace")))
    return sorted(wanted)


def _patch_cmd(tree: Path, patch_file: Path, fuzz: bool, dry_run: bool) -> list[str]:
    cmd = ["patch", *_PATCH_FLAGS, "--forward", "-i", str(patch_file), "-d", str(tree)]
    if not fuzz:
        cmd.append("--fuzz=0")
    if dry_run:
        cmd.append("--dry-run")
    return cmd


def _run_patch(tree: Path, patch_file: Path, fuzz: bool, dry_run: bool) -> tuple[bool, str]:
    """Run GNU patch. With fuzz off, a hunk applied at an offset also counts as a failure."""
    proc = subprocess.run(
        _patch_cmd(tree, patch_file, fuzz, dry_run),
        capture_output=True, text=True, errors="replace", stdin=subprocess.DEVNULL, check=False,
        # patch words its messages by locale; the offset check reads the English ones.
        env={**os.environ, "LC_ALL": "C"},
    )
    output = (proc.stdout + proc.stderr).strip()
    if proc.returncode == 0 and not fuzz and _DRIFT_RE.search(output):
        return False, output + "\nthe context has drifted (hunk applied at an offset); refresh the patch"
    return proc.returncode == 0, output


def apply_layers(tree: Path, layers: Sequence[Layer]) -> list[PatchResult]:
    """Apply every layer's series to tree in order and report each patch.

    A patch is dry-run first, so a failure never leaves a half-applied tree.
    A later patch that touches a file a failed patch touches is skipped
    (it cannot be judged), other patches are still applied and checked.
    """
    results: list[PatchResult] = []
    failed_files: dict[str, str] = {}  # file -> name of the patch that failed on it
    for layer in layers:
        for name, path in layer_entries(layer):
            if not path.is_file():
                results.append(PatchResult(name, "failed", "patch file does not exist"))
                continue
            files = touched_files(path.read_text(encoding="utf-8", errors="replace"))
            blockers = sorted({failed_files[f] for f in files if f in failed_files})
            if blockers:
                results.append(PatchResult(name, "skipped", "depends on failed " + ", ".join(blockers)))
                for f in files:  # what this patch would have changed is missing too
                    failed_files.setdefault(f, blockers[0])
                continue
            ok, output = _run_patch(tree, path, layer.fuzz, dry_run=True)
            if ok:
                ok, output = _run_patch(tree, path, layer.fuzz, dry_run=False)
            if ok:
                results.append(PatchResult(name, "ok", ""))
            else:
                results.append(PatchResult(name, "failed", output))
                for f in files:
                    failed_files.setdefault(f, name)
    return results


def format_report(results: Iterable[PatchResult]) -> tuple[str, bool]:
    results = list(results)
    lines = []
    for r in results:
        if r.status == "failed":
            lines.append(f"FAILED  {r.patch}")
            lines += [f"          {line}" for line in r.detail.splitlines()]
        elif r.status == "skipped":
            lines.append(f"SKIPPED {r.patch}: {r.detail}")
    failed = sum(r.status == "failed" for r in results)
    skipped = sum(r.status == "skipped" for r in results)
    applied = sum(r.status == "ok" for r in results)
    lines.append(f"{failed} failed, {skipped} skipped, {applied} applied")
    return "\n".join(lines), failed == 0 and skipped == 0


def extract_wanted(archive: Path, strip_prefix: str, wanted: Iterable[str], dest: Path) -> set[str]:
    """Extract only the wanted files from an archive into dest.

    wanted holds paths relative to dest; an entry that ends in "/" is a
    directory and brings everything under it. strip_prefix is the archive's
    leading directory (may be empty). Handles .tar.zst (streamed through GNU tar),
    .tar.gz and .zip. Returns the subset the archive contained. The caller
    must have verified the archive against helium.lock first.
    """
    wanted = sorted(set(wanted))
    dest.mkdir(parents=True, exist_ok=True)
    try:
        if archive.name.endswith(".zip"):
            return _extract_zip(archive, strip_prefix, wanted, dest)
        if archive.name.endswith((".tar.gz", ".tgz")):
            return _extract_tar_gz(archive, strip_prefix, wanted, dest)
        return _extract_tar_zst(archive, strip_prefix, wanted, dest)
    except (EOFError, zlib.error, tarfile.TarError, zipfile.BadZipFile) as exc:
        raise RuntimeError(f"{archive.name} is truncated or damaged: {exc}") from exc


def _member_key(name: str, strip_prefix: str) -> str | None:
    """Archive member name -> path relative to the output directory."""
    while name.startswith("./"):
        name = name[2:]
    if strip_prefix:
        if not name.startswith(strip_prefix + "/"):
            return None
        name = name[len(strip_prefix) + 1:]
    return name or None


def _wanted_name(key: str | None, want: set[str]) -> str | None:
    """The wanted entry (a file, or a "dir/" prefix) this member belongs to."""
    if key is None:
        return None
    if key in want:
        return key
    return next((w for w in want if w.endswith("/") and key.startswith(w)), None)


def _write_member(dest: Path, key: str, data: bytes) -> None:
    target = dest / key
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)


def _extract_zip(archive: Path, strip_prefix: str, wanted: list[str], dest: Path) -> set[str]:
    want, found = set(wanted), set()
    with zipfile.ZipFile(archive) as zf:
        for info in zf.infolist():
            key = _member_key(info.filename, strip_prefix)
            hit = _wanted_name(key, want)
            if hit and not info.is_dir():  # only wanted names are written
                _write_member(dest, key, zf.read(info))
                found.add(hit)
    return found


def _extract_tar_gz(archive: Path, strip_prefix: str, wanted: list[str], dest: Path) -> set[str]:
    want, found = set(wanted), set()
    with tarfile.open(archive, "r:gz") as tf:
        for member in tf:
            key = _member_key(member.name, strip_prefix)
            hit = _wanted_name(key, want)
            if hit and member.isfile():
                _write_member(dest, key, tf.extractfile(member).read())
                found.add(hit)
    return found


def _extract_tar_zst(archive: Path, strip_prefix: str, wanted: list[str], dest: Path) -> set[str]:
    names = dest / ".extract-names"
    prefix = f"{strip_prefix}/" if strip_prefix else ""
    names.write_text("".join(f"{prefix}{w.rstrip('/')}\n" for w in wanted))
    tar = ["tar", "-x", "-f", "-", "-C", str(dest), "--no-same-owner", "--files-from", str(names)]
    if strip_prefix:
        tar.append("--strip-components=1")
    try:
        with open(archive, "rb") as fh:
            if archive.name.endswith(".zst"):
                # zstd reads stdin so a symlinked cache entry works too.
                zstd = subprocess.Popen(
                    ["zstd", "-dc"], stdin=fh, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
                )
                proc = subprocess.run(tar, stdin=zstd.stdout, capture_output=True, text=True, check=False)
                zstd.stdout.close()
                zstd_error = zstd.stderr.read()
                zstd.stderr.close()
                if zstd.wait() != 0:
                    raise RuntimeError(f"zstd could not decompress {archive.name}: {zstd_error.strip()}")
            else:
                proc = subprocess.run(tar, stdin=fh, capture_output=True, text=True, check=False)
    finally:
        names.unlink(missing_ok=True)
    return _checked_found(archive, wanted, dest, proc)


def _checked_found(archive: Path, wanted: list[str], dest: Path, proc: subprocess.CompletedProcess) -> set[str]:
    """Cross-check GNU tar's own report against the disk.

    tar names each member it did not find. Any other complaint (an unexpected
    end of archive, a bad header) means the archive is damaged. Every name tar
    did not report missing must exist on disk afterwards.
    """
    missing: set[str] = set()
    for line in proc.stderr.splitlines():
        if line.endswith(": Not found in archive"):
            missing.add(line[len("tar: "):-len(": Not found in archive")])
        elif line.strip() and "Exiting with failure status due to previous errors" not in line:
            raise RuntimeError(f"tar failed extracting from {archive.name}: {proc.stderr.strip()}")
    if proc.returncode not in (0, 2) or (proc.returncode == 2 and not missing):
        raise RuntimeError(f"tar failed extracting from {archive.name}: {proc.stderr.strip()}")
    found = set()
    for name in wanted:
        if _tar_name(name.rstrip("/"), archive, missing):
            continue
        if name.endswith("/"):
            if not (dest / name).is_dir():
                raise RuntimeError(f"{archive.name} is truncated or damaged: {name} was not extracted")
        elif not (dest / name).is_file():
            raise RuntimeError(f"{archive.name} is truncated or damaged: {name} was not extracted")
        found.add(name)
    return found


def _tar_name(name: str, archive: Path, missing: set[str]) -> bool:
    return any(m == name or m.endswith("/" + name) for m in missing)


def copy_tree(src: Path, dst: Path) -> None:
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst, symlinks=True)
