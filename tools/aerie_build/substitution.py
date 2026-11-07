"""The Helium -> Aerie name substitution pass.

Helium renames Chrome and Chromium to Helium in every GRIT string file
(.grd, .grdp) and translation file (.xtb) after its patches are applied.
Aerie runs one more pass over the same files that turns Helium into Aerie, so
the browser's own strings carry Aerie's name without hand-patching each one.
It runs after Helium's name substitution and translation step, never before:
Helium's i18n step looks messages up by the fingerprint of the Helium text.

Scope: the text users read. Message descriptions, the helium:// URL scheme,
and the "The Helium Authors" copyright holder stay as they are. Messages that
describe Helium's own services, servers, proxying, updates, packages, crash
reporting or partner program also stay as they are (KEEP_MESSAGES): Aerie does
not run any of those, so text saying "Aerie services" would be false.
Only the capitalised word Helium is rewritten. HELIUM in identifiers or
environment-style names is not user text and is deliberately left alone.

Rewriting a message changes its GRIT fingerprint, so translation files are
re-keyed with the same old-to-new map, as Helium's own pass does. The map is
saved before any file is rewritten and every write is atomic, so a run that
dies half way is repaired by running again.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import sys
import xml.etree.ElementTree as ET
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterator

# "out" is skipped at the tree top level only; the rest at any depth.
IGNORE_DIRS = ("out", ".pc", "chromeos", "remoting", "ash", "testdata")
_DOLLAR = "&#36;"
_DOLLAR_TOKEN = "!!dollar-sign-literal!!"
_KEEP = "_aeriekeep"

# Applied in order; the protected spans are restored in the last rule.
RULES: list[tuple[re.Pattern[str], str]] = [
    (re.compile(r"\bHelium Authors"), f"Helium{_KEEP} Authors"),
    (re.compile(r"(?<!\w)Helium(?!\w)"), "Aerie"),
    (re.compile(_KEEP), ""),
]

# Messages (by name) that describe Helium's own services or servers. Aerie runs
# none of them, so text saying "Aerie services" would be false. Issue #6
# removes or rewrites the services UI; until then these stay Helium.
# Everything else, update and crash wording included, becomes Aerie.
KEEP_MESSAGES: list[tuple[re.Pattern[str], str]] = [
    (re.compile(r"^IDS_.*HELIUM_SERVICES"),
     "Helium services: settings, menu, tooltip, status and download messages"),
    (re.compile(r"^IDS_SETTINGS_HELIUM_SCHEMA_"),
     "notice about changes in Helium services"),
    (re.compile(r"^IDS_SETTINGS_LANGUAGES_DICTIONARY_DOWNLOAD_FAILED_HELP_HELIUM$"),
     "spell-check dictionary downloads from Helium services and servers"),
    (re.compile(r"^IDS_HELIUM_ONBOARDING_SERVICES_"),
     "onboarding page for Helium services"),
    (re.compile(r"^IDS_HELIUM_ONBOARDING_PARTNER_"),
     "Helium's partner offers, tied to Helium's own services"),
    (re.compile(r"^IDS_HELIUM_ONBOARDING_WELCOME_FOOTER$"),
     "says Helium services will be enabled by default"),
]

Fingerprint = Callable[[ET.Element], str]


@dataclass
class Stats:
    grd_files: int = 0
    xtb_files: int = 0
    kept: frozenset[str] = frozenset()  # messages left saying Helium by KEEP_MESSAGES


def replace_text(text: str) -> tuple[str, bool]:
    """Apply the rules. The flag says whether a name was actually replaced."""
    changed = False
    for regex, replacement in RULES:
        if replacement == "Aerie" and regex.search(text):
            changed = True
        text = regex.sub(replacement, text)
    return text, changed


def keep_reason(name: str | None) -> str | None:
    """Why a message keeps its Helium wording, or None when it is rewritten."""
    for pattern, reason in KEEP_MESSAGES:
        if name and pattern.search(name):
            return reason
    return None


def kept_messages(text: str) -> list[str]:
    """Names of messages in a .grd/.grdp text that still say Helium because KEEP_MESSAGES says so."""
    if "Helium" not in text:
        return []
    root = ET.fromstring(text.replace(_DOLLAR, _DOLLAR_TOKEN), _parser())
    return sorted({
        m.get("name") or ""
        for m in root.findall(".//message")
        if keep_reason(m.get("name")) and "Helium" in "".join(m.itertext())
    })


def _parser() -> ET.XMLParser:
    return ET.XMLParser(target=ET.TreeBuilder(insert_comments=True))


def _replace_in_element(element: ET.Element) -> bool:
    """Rewrite text, tail and children of a message or translation."""
    changed = False
    if element.text:
        element.text, hit = replace_text(element.text)
        changed |= hit
    if element.tail:
        element.tail, hit = replace_text(element.tail)
        changed |= hit
    for child in element:
        changed |= _replace_in_element(child)
    return changed


def _serialise(root: ET.Element) -> str:
    return ET.tostring(root, encoding="unicode", xml_declaration=True).replace(_DOLLAR_TOKEN, _DOLLAR)


def substitute_grd_text(text: str, fingerprint: Fingerprint) -> tuple[str | None, dict[str, str]]:
    """Rewrite a .grd/.grdp file. Returns (None, {}) when nothing changed."""
    root = ET.fromstring(text.replace(_DOLLAR, _DOLLAR_TOKEN), _parser())
    fp_map: dict[str, str] = {}
    changed = False
    for message in root.findall(".//message"):
        if keep_reason(message.get("name")):
            continue
        old = fingerprint(message)
        if _replace_in_element(message):
            changed = True
            new = fingerprint(message)
            if old != new:
                fp_map[old] = new
    return (_serialise(root) if changed else None), fp_map


def substitute_xtb_text(text: str, fp_map: dict[str, str]) -> str | None:
    """Rewrite a .xtb file: new names in the text, new fingerprints as ids."""
    root = ET.fromstring(text.replace(_DOLLAR, _DOLLAR_TOKEN), _parser())
    changed = False
    seen: set[str] = set()
    for translation in root.findall(".//translation"):
        changed |= _replace_in_element(translation)
        old_id = translation.get("id") or ""
        new_id = fp_map.get(old_id, old_id)
        if new_id != old_id:
            changed = True
        # Two strings that now read the same would share an id and break the build.
        while new_id in seen and new_id.isdigit():
            new_id = str(int(new_id) + 1)
            changed = True
        translation.set("id", new_id)
        seen.add(new_id)
    return _serialise(root) if changed else None


def _candidates(tree: Path, extensions: tuple[str, ...]) -> Iterator[Path]:
    for root, dirs, files in os.walk(tree):
        if Path(root) == tree:
            dirs[:] = [d for d in dirs if d != "out"]
        dirs[:] = [d for d in dirs if d not in IGNORE_DIRS[1:]]
        for name in files:
            if not name.startswith(".") and name.lower().endswith(extensions):
                yield Path(root) / name


def _write_atomic(path: Path, text: str) -> None:
    """Write via a temp file in the same directory, so a crash never leaves half a file."""
    tmp = path.with_name(path.name + ".aerie-tmp")
    try:
        tmp.write_text(text, encoding="utf-8")
        os.replace(tmp, path)
    finally:
        tmp.unlink(missing_ok=True)


def _scan_grd(args: tuple[Path, Fingerprint]) -> tuple[Path, str | None, dict[str, str], list[str]]:
    """Read-only: rewritten text, fingerprint changes and kept message names of one string file."""
    path, fingerprint = args
    text = path.read_text(encoding="utf-8")
    if "Helium" not in text:
        return path, None, {}, []
    new_text, fp_map = substitute_grd_text(text, fingerprint)
    return path, new_text, fp_map, kept_messages(text)


def _do_xtb(args: tuple[Path, dict[str, str]]) -> bool:
    path, fp_map = args
    new_text = substitute_xtb_text(path.read_text(encoding="utf-8"), fp_map)
    if new_text is None:
        return False
    _write_atomic(path, new_text)
    return True


def _load_map(state_path: Path | None) -> dict[str, str]:
    if state_path and state_path.is_file():
        return json.loads(state_path.read_text())
    return {}


def substitute_tree(
    tree: Path,
    fingerprint: Fingerprint,
    workers: int | None = None,
    state_path: Path | None = None,
) -> Stats:
    """Run the pass over a source tree.

    Order matters for crash safety. First every string file is read and the
    full old-to-new fingerprint map is worked out and saved (state_path). Only
    then are string files rewritten, each atomically, and last the translation
    files. A string file that an earlier, interrupted run already rewrote adds
    nothing new, but its entries are still in the saved map, so a re-run
    re-keys the translations it missed.
    """
    stats = Stats()
    fp_map = _load_map(state_path)
    with ProcessPoolExecutor(max_workers=workers or os.cpu_count()) if (workers or 2) > 1 else _Inline() as pool:
        scanned = list(pool.map(_scan_grd, [(p, fingerprint) for p in _candidates(tree, (".grd", ".grdp"))]))
        for _, _, found, kept in scanned:
            fp_map.update(found)
            stats.kept |= frozenset(kept)
        if state_path and fp_map:
            _write_atomic(state_path, json.dumps(fp_map, indent=1) + "\n")
        for path, new_text, _, _ in scanned:
            if new_text is not None:
                _write_atomic(path, new_text)
                stats.grd_files += 1
        if fp_map:
            for changed in pool.map(_do_xtb, [(p, fp_map) for p in _candidates(tree, (".xtb",))]):
                stats.xtb_files += bool(changed)
    return stats


class _Inline:
    """Stands in for a process pool when workers=1 (and for unpicklable test fakes)."""

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False

    @staticmethod
    def map(fn, items):
        return map(fn, items)


def rules_digest() -> str:
    """Identifies the rule set, so a tree substituted with other rules is noticed."""
    text = "\n".join(f"{regex.pattern}=>{replacement}" for regex, replacement in RULES)
    text += "\n" + "\n".join(f"keep:{pattern.pattern}" for pattern, _ in KEEP_MESSAGES)
    return hashlib.sha256(text.encode()).hexdigest()


def helium_fingerprint(core_utils: Path, tree: Path) -> Fingerprint:
    """GRIT message fingerprints, computed with Helium's helper and the tree's grit."""
    sys.path.insert(0, str(core_utils))
    import name_substitution_utils as util  # Helium's utils/name_substitution_utils.py

    util.add_grit_to_path(tree)
    return util.compute_fp
