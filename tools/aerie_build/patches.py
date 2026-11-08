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

import hashlib
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
    # Series entries that are skipped (patches/helium-drop.txt). Only Helium's layers have any.
    dropped: frozenset = frozenset()
    # Series entries whose patch is replaced, at the same place in the series, by an Aerie file
    # (patches/helium-adapt.txt): ((entry, replacement path), ...).
    replaced: tuple = ()


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
        if name.startswith(ADAPT_DIR + "/"):
            continue  # replacements of Helium patches, listed in helium-adapt.txt and linted there
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


def layer_series(layer: Layer) -> list[str]:
    """The layer's series entries without the dropped ones, in order."""
    return [e for e in parse_series(layer.directory / "series") if e not in layer.dropped]


def layer_patch_path(layer: Layer, entry: str) -> Path:
    """The file to apply for a series entry: Helium's, or Aerie's replacement."""
    for name, path in layer.replaced:
        if name == entry:
            return path
    return layer.directory / entry


def layer_entries(layer: Layer) -> list[tuple[str, Path]]:
    return [(f"{layer.label}/{entry}", layer_patch_path(layer, entry)) for entry in layer_series(layer)]


# --- Dropping Helium patches ---------------------------------------------------

DROP_FILE = "helium-drop.txt"


@dataclass(frozen=True)
class Drop:
    entry: str   # as in Helium's series, e.g. helium/core/ublock-setup-sources.patch
    reason: str
    sha256: str | None = None  # helium-adapt.txt only: the Helium patch that was adapted


def parse_drops(path: Path) -> tuple[list[Drop], list[Problem]]:
    """Read patches/helium-drop.txt: one Helium series entry per line, then ``# reason``.

    The reason is required. A missing file means no drops.
    """
    return parse_drops_file(path, DROP_FILE)


_SHA_RE = re.compile(r"^sha256:([0-9a-f]{64})$")


def parse_drops_file(path: Path, label: str, pinned: bool = False) -> tuple[list[Drop], list[Problem]]:
    """pinned: each line is ``entry sha256:<64 hex>  # reason`` (helium-adapt.txt)."""
    drops: list[Drop] = []
    problems: list[Problem] = []
    if not Path(path).is_file():
        return drops, problems
    seen: set[str] = set()
    for number, raw in enumerate(Path(path).read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        entry, sep, reason = line.partition("#")
        entry, reason = entry.strip(), reason.strip()
        where = f"{label}:{number}"
        sha = None
        if pinned:
            parts = entry.split()
            entry = parts[0] if parts else ""
            match = _SHA_RE.match(parts[1]) if len(parts) == 2 else None
            if not match:
                problems.append(Problem(entry or where, f"{where}: needs 'sha256:<64 hex digits>' after the entry, the "
                                                          "Helium patch that was adapted"))
                continue
            sha = match.group(1)
        if not entry or _unsafe(entry) or " " in entry:
            problems.append(Problem(where, f"not a Helium series entry: {raw!r}"))
        elif not sep or not reason:
            problems.append(Problem(entry, f"{where}: an entry needs a reason after '#'"))
        elif entry in seen:
            problems.append(Problem(entry, f"{where}: listed more than once"))
        else:
            seen.add(entry)
            drops.append(Drop(entry, reason, sha))
    return drops, problems


def check_drops(drops: Sequence[Drop], helium_layers: Sequence[Layer]) -> list[Problem]:
    """Every dropped entry must still be in a Helium series (and exist), so a bump surfaces a rename."""
    present: dict[str, Layer] = {}
    for layer in helium_layers:
        series = layer.directory / "series"
        for entry in parse_series(series) if series.is_file() else []:
            present.setdefault(entry, layer)
    problems = []
    for drop in drops:
        layer = present.get(drop.entry)
        if layer is None:
            problems.append(Problem(
                drop.entry, f"{DROP_FILE} drops it, but it is no longer in Helium's series "
                            "(renamed or removed in a Helium bump?): update or remove the entry and its ledger row"))
        elif not (layer.directory / drop.entry).is_file():
            problems.append(Problem(drop.entry, "listed in Helium's series but the patch file is missing"))
    return problems


def with_drops(layer: Layer, drops: Sequence[Drop], adapts: Sequence["Adapt"] = ()) -> Layer:
    """The layer with those drops and replacements that belong to its series."""
    series = layer.directory / "series"
    entries = set(parse_series(series)) if series.is_file() else set()
    return Layer(layer.label, layer.directory, layer.fuzz,
                 frozenset(d.entry for d in drops if d.entry in entries),
                 tuple((a.entry, a.path) for a in adapts if a.entry in entries))


# --- Adapting Helium patches ---------------------------------------------------

ADAPT_FILE = "helium-adapt.txt"
ADAPT_DIR = "helium-adapted"


@dataclass(frozen=True)
class Adapt:
    entry: str   # as in Helium's series
    reason: str
    path: Path   # patches/helium-adapted/<entry>
    original_sha256: str = ""  # of Helium's patch when it was adapted


def parse_adapts(patches_dir: Path) -> tuple[list[Adapt], list[Problem]]:
    """Read patches/helium-adapt.txt (same format as helium-drop.txt) and find the replacement files.

    A Helium patch that only applies on top of a patch Aerie drops is replaced, at the same
    place in the series, by patches/helium-adapted/<entry>: Helium's patch with the hunks
    that need the dropped code re-cut. The replacement needs the Aerie header fields.
    """
    drops, problems = parse_drops_file(Path(patches_dir) / ADAPT_FILE, ADAPT_FILE, pinned=True)
    adapts: list[Adapt] = []
    for drop in drops:
        path = Path(patches_dir) / ADAPT_DIR / drop.entry
        if not path.is_file():
            problems.append(Problem(drop.entry, f"{ADAPT_FILE} lists it but {ADAPT_DIR}/{drop.entry} does not exist"))
            continue
        problems += _lint_patch(f"{ADAPT_DIR}/{drop.entry}", path.read_text(encoding="utf-8", errors="replace"))
        adapts.append(Adapt(drop.entry, drop.reason, path, drop.sha256 or ""))
    return adapts, problems


_UBO_RE = re.compile(r"ublock|\bubo\b", re.I)


def _changed_lines(patch_text: str) -> list[list[str]]:
    """The added and removed lines of each hunk (file headers and context excluded)."""
    hunks: list[list[str]] = []
    for line in patch_text.splitlines():
        if line.startswith("@@"):
            hunks.append([])
        elif hunks and line[:1] in "+-" and not line.startswith(("+++ ", "--- ")):
            hunks[-1].append(line)
    return hunks


def recut_problems(original: str, recut: str) -> list[str]:
    """Compare a re-cut Helium patch with the original: only the placement of hunks may differ.

    Every added or removed line of the re-cut must be one of the original's (nothing invented);
    every line of the original's hunks that do not mention uBlock Origin must still be in the
    re-cut (nothing lost); and the re-cut must not mention uBlock Origin in a changed line.
    """
    from collections import Counter

    orig_hunks = _changed_lines(original)
    have = Counter(l for h in _changed_lines(recut) for l in h)
    all_orig = Counter(l for h in orig_hunks for l in h)
    kept = Counter(l for h in orig_hunks if not any(_UBO_RE.search(l) for l in h) for l in h)
    problems = []
    for line, count in (have - all_orig).items():
        problems.append(f"re-cut line is not in Helium's patch: {line!r}")
    for line, count in (kept - have).items():
        problems.append(f"line of Helium's patch is missing from the re-cut: {line!r}")
    for line in have:
        if _UBO_RE.search(line):
            problems.append(f"the re-cut changes a line that mentions uBlock Origin: {line!r}")
    return problems


def check_adapts(adapts: Sequence[Adapt], drops: Sequence[Drop], helium_layers: Sequence[Layer]) -> list[Problem]:
    """Entries exist in Helium; Helium's patch is still the one that was adapted; the re-cut is faithful."""
    problems = check_drops([Drop(a.entry, a.reason) for a in adapts], helium_layers)
    dropped = {d.entry for d in drops}
    present: dict[str, Path] = {}
    for layer in helium_layers:
        series = layer.directory / "series"
        for entry in parse_series(series) if series.is_file() else []:
            present.setdefault(entry, layer.directory / entry)
    for adapt in adapts:
        if adapt.entry in dropped:
            problems.append(Problem(adapt.entry, f"in both {DROP_FILE} and {ADAPT_FILE}"))
        original = present.get(adapt.entry)
        if original is None or not original.is_file():
            continue  # check_drops reported it
        data = original.read_bytes()
        actual = hashlib.sha256(data).hexdigest()
        if adapt.original_sha256 and actual != adapt.original_sha256:
            problems.append(Problem(
                adapt.entry,
                f"Helium's patch changed since it was adapted (pinned {adapt.original_sha256}, now {actual}): "
                f"re-cut {ADAPT_DIR}/{adapt.entry} from the new patch (method in CONTRIBUTING.md) and update the pin"))
            continue
        for message in recut_problems(data.decode("utf-8", errors="replace"),
                                      adapt.path.read_text(encoding="utf-8", errors="replace")):
            problems.append(Problem(adapt.entry, message))
    return problems


def filtered_series_dir(layer: Layer, dest: Path) -> Path:
    """A patch directory Helium's own patches.py can apply: the layer's series without the drops,
    with Aerie's replacements in place of the adapted entries.

    Patch files are symlinked from the real directory. Returns dest.
    """
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    kept = layer_series(layer)
    for entry in kept:
        target = dest / entry
        target.parent.mkdir(parents=True, exist_ok=True)
        target.symlink_to(layer_patch_path(layer, entry).resolve())
    (dest / "series").write_text("".join(f"{e}\n" for e in kept), encoding="utf-8")
    return dest


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


_FILE_LINE_RE = re.compile(r"^(?:checking|patching) file (.+)$")
_HUNK_FAILED_RE = re.compile(r"^Hunk #(\d+) FAILED at (\d+)\.?$")
_MAX_HUNK_LINES = 40


def _hunks_by_file(patch_text: str) -> dict[str, list[list[str]]]:
    """Each file's hunks, in order, as lists of lines (from "@@" on), keyed by the path after -p1."""
    files: dict[str, list[list[str]]] = {}
    current: list[list[str]] | None = None
    lines = patch_text.splitlines()
    for i, line in enumerate(lines):
        in_hunk = bool(current) and current[-1] is not None
        if line.startswith("--- ") and i + 1 < len(lines) and lines[i + 1].startswith("+++ "):
            path = lines[i + 1][4:].split("\t")[0].strip()
            path = path.split("/", 1)[1] if "/" in path else path
            current = files.setdefault(path, [])
        elif line.startswith("+++ ") and i > 0 and lines[i - 1].startswith("--- "):
            continue
        elif line.startswith("@@") and current is not None:
            current.append([line])
        elif in_hunk:
            current[-1].append(line)
    return files


def failed_hunks(patch_text: str, output: str) -> str:
    """The hunks GNU patch said FAILED, with their file, so a failure shows what did not apply.

    output is patch's output; hunk numbers restart in each file. Empty if no hunk failed.
    """
    hunks = _hunks_by_file(patch_text)
    shown: list[str] = []
    current_file = ""
    for line in output.splitlines():
        line = line.strip()
        file_match = _FILE_LINE_RE.match(line)
        if file_match:
            current_file = file_match.group(1)
            continue
        failed = _HUNK_FAILED_RE.match(line)
        if not failed:
            continue
        number = int(failed.group(1))
        file_hunks = hunks.get(current_file, [])
        if not 1 <= number <= len(file_hunks):
            continue
        body = file_hunks[number - 1]
        if len(body) > _MAX_HUNK_LINES:
            extra = len(body) - _MAX_HUNK_LINES
            body = body[:_MAX_HUNK_LINES] + [f"... ({extra} more lines)"]
        shown.append(f"failed hunk in {current_file} (hunk #{number}, patch said line {failed.group(2)}):")
        shown += body
    return "\n".join(shown)


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
                hunks = failed_hunks(path.read_text(encoding="utf-8", errors="replace"), output)
                results.append(PatchResult(name, "failed", output + ("\n" + hunks if hunks else "")))
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
