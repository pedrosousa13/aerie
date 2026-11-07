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
re-keyed with the same old-to-new map, as Helium's own pass does. The pass reads pristine bytes and
returns the new content; aerie_build.layers writes it.
"""

from __future__ import annotations

import hashlib
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


GRD_EXTENSIONS = (".grd", ".grdp")
XTB_EXTENSIONS = (".xtb",)
_XTB_ID_RE = re.compile(r'<translation\b[^>]*?\bid="(\d+)"')

Reader = Callable[[str], "bytes | None"]  # tree path -> pristine bytes (None: no such file)


def candidates(tree: Path, extensions: tuple[str, ...]) -> Iterator[str]:
    """Tree-relative paths (posix) of the files the pass may rewrite."""
    for root, dirs, files in os.walk(tree):
        if Path(root) == tree:
            dirs[:] = [d for d in dirs if d != "out"]
        dirs[:] = [d for d in dirs if d not in IGNORE_DIRS[1:]]
        for name in files:
            if not name.startswith(".") and name.lower().endswith(extensions):
                yield (Path(root) / name).relative_to(tree).as_posix()


def _scan_grd(args: tuple[Reader, str, Fingerprint]) -> tuple[str, str | None, dict[str, str], list[str]]:
    """Read-only: rewritten text, fingerprint changes and kept message names of one string file."""
    read, rel, fingerprint = args
    data = read(rel)
    text = data.decode("utf-8") if data is not None else ""
    if "Helium" not in text:
        return rel, None, {}, []
    new_text, fp_map = substitute_grd_text(text, fingerprint)
    return rel, new_text, fp_map, kept_messages(text)


def _do_xtb(args: tuple[Reader, str, dict[str, str]]) -> tuple[str, str | None]:
    read, rel, fp_map = args
    data = read(rel)
    text = data.decode("utf-8") if data is not None else ""
    # Most translation files need nothing; skip parsing them.
    if "Helium" not in text and not any(i in fp_map for i in _XTB_ID_RE.findall(text)):
        return rel, None
    return rel, substitute_xtb_text(text, fp_map)


def substitute_files(
    read: Reader,
    grd_paths: list[str],
    xtb_paths: list[str],
    fingerprint: Fingerprint,
    workers: int | None = None,
) -> tuple[dict[str, bytes], Stats]:
    """Work out the rewritten content of every string and translation file. Writes nothing.

    Reads come from `read` (pristine bytes), so the result is the same on every
    run. String files are scanned first to get the full old-to-new fingerprint
    map; the translation files are re-keyed with it. Returns {path: new bytes}
    for the files that change, plus statistics.
    """
    stats = Stats()
    out: dict[str, bytes] = {}
    fp_map: dict[str, str] = {}
    with ProcessPoolExecutor(max_workers=workers or os.cpu_count()) if (workers or 2) > 1 else _Inline() as pool:
        for rel, new_text, found, kept in pool.map(_scan_grd, [(read, p, fingerprint) for p in grd_paths]):
            fp_map.update(found)
            stats.kept |= frozenset(kept)
            if new_text is not None:
                out[rel] = new_text.encode("utf-8")
                stats.grd_files += 1
        if fp_map:
            for rel, new_text in pool.map(_do_xtb, [(read, p, fp_map) for p in xtb_paths]):
                if new_text is not None:
                    out[rel] = new_text.encode("utf-8")
                    stats.xtb_files += 1
    return out, stats


class _Inline:
    """Stands in for a process pool when workers=1 (and for unpicklable test fakes)."""

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False

    @staticmethod
    def map(fn, items):
        return map(fn, items)


class SubstitutionLayer:
    """The pass as a tree layer: pristine string files in, rewritten string files out."""

    name = "helium-to-aerie substitution"

    def __init__(self, fingerprint_factory: Callable[[], Fingerprint], workers: int | None = None,
                 log: Callable[[str], None] = lambda _m: None):
        self._factory = fingerprint_factory
        self._workers = workers
        self._log = log
        self.stats = Stats()

    def apply(self, ws) -> None:
        def paths(extensions):
            found = set(candidates(ws.tree, extensions))
            found |= {p for p in ws.known_paths() if p.lower().endswith(extensions)}
            return sorted(p for p in found if ws.read(p) is not None)

        out, self.stats = substitute_files(
            ws.reader, paths(GRD_EXTENSIONS), paths(XTB_EXTENSIONS), self._factory(), self._workers)
        for rel, data in out.items():
            ws.put(rel, data)
        self._log(f"Helium->Aerie: rewrites {self.stats.grd_files} string file(s), "
                  f"{self.stats.xtb_files} translation file(s); "
                  f"{len(self.stats.kept)} Helium-services message(s) left as Helium")


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
