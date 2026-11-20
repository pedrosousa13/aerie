"""Parse the request policy tables of docs/network-endpoints.md and render the C++ include.

The doc holds three marked tables:

* hosts: ``| `annotation` | `host` | why |``. A browser request with that traffic annotation may reach
  that host or a subdomain of it.
* user: ``| `annotation` | why |``. The user typed or chose the destination. Any host.
* site: ``| `annotation` | why |``. The destination is set by the site being visited, or by an extension
  the user installed, never by Google or Chromium code ("trusted by annotation"). Any host.

One annotation is in exactly one table. Everything else a browser-process request does is refused
(services/network/aerie, issue #257).
"""

from __future__ import annotations

import os
import re
from dataclasses import dataclass
from pathlib import Path

GENERATED_PATH = "overlay/services/network/aerie/request_allowlist.inc"
DOC_PATH = "docs/network-endpoints.md"
# The list of every traffic annotation in the pinned Chromium source.
ANNOTATIONS_XML = "tools/traffic_annotation/summary/annotations.xml"

ANNOTATION_RE = re.compile(r"^[a-z0-9_]+$")
# A DNS name with at least one dot, lower case, no wildcard, no port, no path. IP literals and
# single labels (localhost) are not accepted: the allowlist is about public names.
HOST_RE = re.compile(r"^(?=.{1,253}$)([a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?\.)+[a-z]([a-z0-9-]{0,61}[a-z0-9])?$")


class PolicyDocError(ValueError):
    pass


@dataclass
class PolicyTable:
    hosts: list[tuple[str, str]]
    user: list[str]
    site: list[str]


def _block(text: str, name: str) -> list[str]:
    begin, end = f"<!-- request-policy:{name}:begin -->", f"<!-- request-policy:{name}:end -->"
    if text.count(begin) != 1 or text.count(end) != 1 or text.index(begin) > text.index(end):
        raise PolicyDocError(f"docs/network-endpoints.md needs exactly one {begin} ... {end} block")
    return text.split(begin, 1)[1].split(end, 1)[0].splitlines()


def _cells(line: str) -> list[str] | None:
    line = line.strip()
    if not line.startswith("|"):
        return None
    cells = [c.strip() for c in line.strip("|").split("|")]
    if all(set(c) <= set("-: ") for c in cells):
        return None  # the separator row
    return cells


def _code(cell: str, what: str) -> str:
    if len(cell) < 3 or not (cell.startswith("`") and cell.endswith("`")) or "`" in cell[1:-1]:
        raise PolicyDocError(f"{what} must be one `code span`, got {cell!r}")
    return cell[1:-1]


def _annotation(cell: str) -> str:
    name = _code(cell, "annotation")
    if not ANNOTATION_RE.match(name):
        raise PolicyDocError(f"bad annotation name {name!r}")
    return name


def _directed(text: str, name: str) -> list[str]:
    out: list[str] = []
    for line in _block(text, name):
        cells = _cells(line)
        if cells is None or cells[0] == "Annotation":
            continue
        if len(cells) != 2:
            raise PolicyDocError(f"{name} row needs 2 cells: {line!r}")
        annotation = _annotation(cells[0])
        if not cells[1]:
            raise PolicyDocError(f"row for {annotation!r} needs a reason")
        if annotation in out:
            raise PolicyDocError(f"duplicate annotation {annotation}")
        out.append(annotation)
    if not out:
        raise PolicyDocError(f"the {name} table is empty")
    return out


def parse_doc(text: str) -> PolicyTable:
    hosts: list[tuple[str, str]] = []
    for line in _block(text, "hosts"):
        cells = _cells(line)
        if cells is None or cells[0] == "Annotation":
            continue
        if len(cells) != 3:
            raise PolicyDocError(f"hosts row needs 3 cells: {line!r}")
        annotation, host = _annotation(cells[0]), _code(cells[1], "host")
        if not HOST_RE.match(host):
            raise PolicyDocError(f"bad host {host!r}: lower case DNS name with a dot, no scheme, wildcard, port or path")
        if not cells[2]:
            raise PolicyDocError(f"row for {host!r} needs a reason")
        if (annotation, host) in hosts:
            raise PolicyDocError(f"duplicate row {annotation} / {host}")
        hosts.append((annotation, host))
    if not hosts:
        raise PolicyDocError("the hosts table is empty")
    user, site = _directed(text, "user"), _directed(text, "site")
    seen: dict[str, str] = {}
    for group, names in (("hosts", sorted({a for a, _ in hosts})), ("user", user), ("site", site)):
        for name in names:
            if name in seen:
                raise PolicyDocError(f"annotation {name!r} is in both the {seen[name]} and the {group} table")
            seen[name] = group
    return PolicyTable(hosts, user, site)


def render(table: PolicyTable) -> str:
    out = [
        "// Generated from docs/network-endpoints.md by: ./aerie netpolicy generate",
        "// Do not edit by hand. Add a reviewed row to the doc and regenerate; a test fails when they differ.",
        "// Included inside the anonymous namespace of request_policy.cc.",
        "",
        "// A browser request with this traffic annotation may reach this host or a",
        "// subdomain of it.",
        "constexpr AllowedHost kAllowedHosts[] = {",
    ]
    out += [f'    {{"{a}", "{h}"}},' for a, h in table.hosts]
    out += [
        "};",
        "",
        "// The user typed or chose the destination: allowed for any host.",
        "constexpr DirectedAnnotation kUserDirectedAnnotations[] = {",
    ]
    out += [f'    {{"{a}"}},' for a in table.user]
    out += [
        "};",
        "",
        "// A site being visited, or an extension the user installed, sets the",
        "// destination (trusted by annotation): allowed for any host.",
        "constexpr DirectedAnnotation kSiteDirectedAnnotations[] = {",
    ]
    out += [f'    {{"{a}"}},' for a in table.site]
    out += ["};", ""]
    return "\n".join(out)


def table_annotations(table: PolicyTable) -> list[str]:
    return sorted({a for a, _ in table.hosts} | set(table.user) | set(table.site))


def overlay_annotation_text(repo: Path) -> str:
    """The text of Aerie's own C++ files that define annotations (their names are not in annotations.xml)."""
    parts = []
    for path in sorted(Path(repo, "overlay").rglob("*.cc")):
        text = path.read_text(encoding="utf-8", errors="replace")
        if "DefineNetworkTrafficAnnotation" in text:
            parts.append(text)
    return "\n".join(parts)


def unknown_annotations(table: PolicyTable, annotations_xml: str, overlay_text: str) -> list[str]:
    """Names in the tables that no annotation in the pinned source or in Aerie's overlay defines."""
    chromium = set(re.findall(r'<item id="([^"]+)"', annotations_xml))
    aerie = set(re.findall(r'DefineNetworkTrafficAnnotation\(\s*"([^"]+)"', overlay_text))
    return [n for n in table_annotations(table) if n not in chromium and n not in aerie]


def default_tree() -> Path | None:
    """The prepared source tree of the default build root, or None. Used by tests that skip without one."""
    root = Path(os.environ.get("AERIE_BUILD_ROOT") or "/mnt/linux2/aerie-build")
    src = root / "src"
    if not src.is_dir():
        return None
    trees = sorted(p for p in src.iterdir() if (p / ANNOTATIONS_XML).is_file())
    return trees[-1] if trees else None
