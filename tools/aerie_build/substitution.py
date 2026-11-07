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
import xml.parsers.expat as expat
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
    """Rewrite text, tail and children of a message (parsed copy, used for fingerprints)."""
    changed = False
    if element.text and isinstance(element.tag, str):
        element.text, hit = replace_text(element.text)
        changed |= hit
    if element.tail:
        element.tail, hit = replace_text(element.tail)
        changed |= hit
    for child in element:
        changed |= _replace_in_element(child)
    return changed


# Markup that is not message text: comments, processing instructions, CDATA
# markers, and tags (attribute values may hold a ">").
_MARKUP_RE = re.compile(
    rb"(<!--.*?-->|<\?.*?\?>|<!\[CDATA\[|\]\]>|<(?:[^>\"']|\"[^\"]*\"|'[^']*')*>)", re.DOTALL)
_ATTR_RE = re.compile(rb"""([^\s=/<>]+)\s*=\s*("[^"]*"|'[^']*')""")


@dataclass
class _Span:
    """Where one <message>/<translation> sits in the original bytes."""
    name: str            # message name, or translation id
    id_span: tuple[int, int] | None  # offsets of the id attribute value (inside the quotes)
    start: int           # first byte of the content (after the start tag)
    end: int             # first byte of the end tag


def _tag_end(data: bytes, start: int) -> int:
    """Offset just past the '>' of the start tag that begins at `start`."""
    quote = None
    i = start
    while True:
        c = data[i:i + 1]
        if quote:
            if c == quote:
                quote = None
        elif c in (b'"', b"'"):
            quote = c
        elif c == b">":
            return i + 1
        i += 1


def _spans(data: bytes, tag: str) -> list[_Span]:
    """Locate every outermost <tag> element in the original bytes (the parser reads, nothing is rewritten)."""
    found: list[_Span] = []
    stack: list[tuple[str, int, int, dict]] = []
    depth = 0
    parser = expat.ParserCreate("utf-8")  # offsets are bytes, whatever the declaration says

    def start(name, attrs):
        nonlocal depth
        if name == tag:
            if depth == 0:
                at = parser.CurrentByteIndex
                stack.append((name, at, _tag_end(data, at), attrs))
            depth += 1

    def end(name):
        nonlocal depth
        if name == tag:
            depth -= 1
            if depth == 0:
                _, at, content, attrs = stack.pop()
                close = parser.CurrentByteIndex
                if close < content:  # <tag/>: no content
                    return
                id_span = None
                for m in _ATTR_RE.finditer(data, at, content):
                    if m.group(1) == b"id":
                        id_span = (m.start(2) + 1, m.end(2) - 1)
                found.append(_Span(attrs.get("name" if tag == "message" else "id", ""), id_span, content, close))

    parser.StartElementHandler = start
    parser.EndElementHandler = end
    parser.Parse(data, True)
    return found


def _replace_in_markup(raw: bytes) -> bytes:
    """Apply the rules to the text runs of `raw`, leaving tags and comments untouched."""
    parts = _MARKUP_RE.split(raw)
    for i in range(0, len(parts), 2):  # even parts are text
        if parts[i]:
            parts[i] = replace_text(parts[i].decode("utf-8"))[0].encode("utf-8")
    return b"".join(parts)


def _apply(data: bytes, edits: list[tuple[int, int, bytes]]) -> bytes:
    """Splice (start, end, replacement) edits into data."""
    for start, end, new in sorted(edits, reverse=True):
        data = data[:start] + new + data[end:]
    return data


def substitute_grd_text(text: str, fingerprint: Fingerprint) -> tuple[str | None, dict[str, str]]:
    """Rewrite a .grd/.grdp file. Returns (None, {}) when nothing changed.

    Only the text inside messages changes; every other byte of the input is kept.
    """
    data = text.encode("utf-8")
    edits: list[tuple[int, int, bytes]] = []
    for span in _spans(data, "message"):
        if keep_reason(span.name):
            continue
        inner = data[span.start:span.end]
        new = _replace_in_markup(inner)
        if new != inner:
            edits.append((span.start, span.end, new))
    if not edits:
        return None, {}
    # The parsed copy exists only to compute fingerprints of the old and new text.
    root = ET.fromstring(text.replace(_DOLLAR, _DOLLAR_TOKEN), _parser())
    fp_map: dict[str, str] = {}
    for message in root.findall(".//message"):
        if keep_reason(message.get("name")):
            continue
        old = fingerprint(message)
        if _replace_in_element(message):
            new_fp = fingerprint(message)
            if old != new_fp:
                fp_map[old] = new_fp
    return _apply(data, edits).decode("utf-8"), fp_map


def substitute_xtb_text(text: str, fp_map: dict[str, str]) -> str | None:
    """Rewrite a .xtb file: new names in the text, new fingerprints as ids.

    Only translation text and id attribute values change.
    """
    data = text.encode("utf-8")
    edits: list[tuple[int, int, bytes]] = []
    seen: set[str] = set()
    for span in _spans(data, "translation"):
        inner = data[span.start:span.end]
        new = _replace_in_markup(inner)
        if new != inner:
            edits.append((span.start, span.end, new))
        old_id = span.name
        new_id = fp_map.get(old_id, old_id)
        # Two strings that now read the same would share an id and break the build.
        while new_id in seen and new_id.isdigit():
            new_id = str(int(new_id) + 1)
        if new_id != old_id and span.id_span:
            edits.append((*span.id_span, new_id.encode("utf-8")))
        seen.add(new_id)
    return _apply(data, edits).decode("utf-8") if edits else None


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
