"""Parse the request policy tables of docs/network-endpoints.md and render the C++ include.

The doc holds two marked tables:

* hosts: ``| `annotation` | `host` | why |``. A browser request with that traffic annotation may reach
  that host or a subdomain of it.
* directed: ``| `annotation` | why |``. Traffic that a page or the user directs, for any host.

Everything else a browser-process request does is refused (services/network/aerie, issue #257).
"""

from __future__ import annotations

import re
from dataclasses import dataclass

GENERATED_PATH = "overlay/services/network/aerie/request_allowlist.inc"
DOC_PATH = "docs/network-endpoints.md"

ANNOTATION_RE = re.compile(r"^[a-z0-9_]+$")
# A DNS name with at least one dot, lower case, no wildcard, no port, no path. IP literals and
# single labels (localhost) are not accepted: the allowlist is about public names.
HOST_RE = re.compile(r"^(?=.{1,253}$)([a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?\.)+[a-z]([a-z0-9-]{0,61}[a-z0-9])?$")


class PolicyDocError(ValueError):
    pass


@dataclass
class PolicyTable:
    hosts: list[tuple[str, str]]
    directed: list[str]


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


def parse_doc(text: str) -> PolicyTable:
    hosts: list[tuple[str, str]] = []
    for line in _block(text, "hosts"):
        cells = _cells(line)
        if cells is None or cells[0] == "Annotation":
            continue
        if len(cells) != 3:
            raise PolicyDocError(f"hosts row needs 3 cells: {line!r}")
        annotation, host = _code(cells[0], "annotation"), _code(cells[1], "host")
        if not ANNOTATION_RE.match(annotation):
            raise PolicyDocError(f"bad annotation name {annotation!r}")
        if not HOST_RE.match(host):
            raise PolicyDocError(f"bad host {host!r}: lower case DNS name with a dot, no scheme, wildcard, port or path")
        if not cells[2]:
            raise PolicyDocError(f"row for {host!r} needs a reason")
        if (annotation, host) in hosts:
            raise PolicyDocError(f"duplicate row {annotation} / {host}")
        hosts.append((annotation, host))
    directed: list[str] = []
    for line in _block(text, "directed"):
        cells = _cells(line)
        if cells is None or cells[0] == "Annotation":
            continue
        if len(cells) != 2:
            raise PolicyDocError(f"directed row needs 2 cells: {line!r}")
        annotation = _code(cells[0], "annotation")
        if not ANNOTATION_RE.match(annotation):
            raise PolicyDocError(f"bad annotation name {annotation!r}")
        if not cells[1]:
            raise PolicyDocError(f"row for {annotation!r} needs a reason")
        if annotation in directed:
            raise PolicyDocError(f"duplicate annotation {annotation}")
        directed.append(annotation)
    if not hosts:
        raise PolicyDocError("the hosts table is empty")
    if not directed:
        raise PolicyDocError("the directed table is empty")
    return PolicyTable(hosts, directed)


def render(table: PolicyTable) -> str:
    out = [
        "// Generated from docs/network-endpoints.md by: ./aerie netpolicy generate",
        "// Do not edit by hand. Add a reviewed row to the doc and regenerate; a test fails when they differ.",
        "// Included inside namespace aerie_network of request_policy.cc.",
        "",
        "// A browser request with this traffic annotation may reach this host or a subdomain of it.",
        "constexpr AllowedHost kAllowedHosts[] = {",
    ]
    out += [f'    {{"{a}", "{h}"}},' for a, h in table.hosts]
    out += [
        "};",
        "",
        "// Traffic that a page or the user directs: allowed for any host.",
        "constexpr DirectedAnnotation kDirectedAnnotations[] = {",
    ]
    out += [f'    {{"{a}"}},' for a in table.directed]
    out += ["};", ""]
    return "\n".join(out)
