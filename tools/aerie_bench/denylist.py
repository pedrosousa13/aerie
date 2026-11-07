"""Host denylist: Google-owned and Helium-owned names the browser must never contact.

File format (``denylist/google-helium.txt``), one rule per line:

    # comment                  ignored; so are blank lines and ``  # trailing`` comments
    @group google              names the group of the rules that follow (every rule needs one)
    gstatic.com                the domain and every subdomain (label boundary, never substring)
    *.doubleclick.net          same as ``doubleclick.net``
    google.*                   ``google`` followed by a country or generic TLD: google.de,
                               www.google.co.uk, google.com.br. Not google.example.com.
    google                     a bare label is a TLD rule: dns.google, but not google.com
    url host/path-prefix       a URL rule: that exact host and a path starting with the prefix (case-insensitive)

IP literals never match: the Bench's resolver rules refuse everything except
loopback, and an address says nothing about who owns the name. A request made
straight to a Google address is therefore not caught; see docs/network-denylist.md.

``.qjz9zk`` is not a rule. It is the dummy TLD that ungoogled-chromium's domain
substitution rewrites Google hosts into. Requests to it are refused inside the
network stack, so they are reported separately (see ``is_dummy_host``).
"""

from __future__ import annotations

import ipaddress
import re
from dataclasses import dataclass, field
from pathlib import Path
from urllib.parse import urlsplit

DEFAULT_FILE = Path(__file__).resolve().parent / "denylist" / "google-helium.txt"
DUMMY_TLD = "qjz9zk"

# Two-label public suffixes a country code can hide behind: google.co.uk, google.com.br.
_SLD = frozenset({"co", "com", "org", "net", "gov", "edu", "ac", "or", "ne", "go", "gob", "mil"})
_LABEL = re.compile(r"^[a-z0-9_]([a-z0-9_-]{0,61}[a-z0-9_])?$")


class DenylistError(ValueError):
    """A denylist file line is not valid."""


@dataclass(frozen=True)
class Match:
    group: str
    pattern: str
    host: str
    url: str | None = None


@dataclass(frozen=True)
class Entry:
    group: str
    pattern: str
    kind: str  # suffix | tld_wildcard | url
    labels: tuple[str, ...] = ()
    path: str = ""


def normalise_host(host: str) -> str | None:
    """Lower-cased ASCII host without a trailing dot; None for IPs, empties and non-hosts."""
    host = (host or "").strip()
    if host.startswith("[") and host.endswith("]"):
        host = host[1:-1]
    host = host.rstrip(".").lower()
    if not host or "/" in host or " " in host or "@" in host:
        return None
    try:
        ipaddress.ip_address(host)
        return None
    except ValueError:
        pass
    try:
        host = host.encode("idna").decode("ascii").lower()
    except UnicodeError:
        return None
    labels = host.split(".")
    if any(not _LABEL.match(label) for label in labels):
        return None
    return host


def is_dummy_host(host: str) -> bool:
    h = normalise_host(host)
    return bool(h) and h.split(".")[-1] == DUMMY_TLD


def _tld_tail_ok(tail: list[str]) -> bool:
    """The labels after ``google`` look like a TLD: ``de``, ``dev``, or ``co.uk``."""
    if len(tail) == 1:
        return tail[0].isalpha() and 2 <= len(tail[0]) <= 24
    if len(tail) == 2:
        return tail[0] in _SLD and tail[1].isalpha() and len(tail[1]) == 2
    return False


@dataclass
class Denylist:
    entries: list[Entry] = field(default_factory=list)

    def match_host(self, host: str) -> Match | None:
        h = normalise_host(host)
        if h is None:
            return None
        labels = h.split(".")
        for e in self.entries:
            if e.kind == "suffix":
                n = len(e.labels)
                if len(labels) >= n and tuple(labels[-n:]) == e.labels:
                    return Match(e.group, e.pattern, h)
            elif e.kind == "tld_wildcard":
                n = len(e.labels)
                for i in range(len(labels) - n + 1):
                    if tuple(labels[i:i + n]) == e.labels and _tld_tail_ok(labels[i + n:]):
                        return Match(e.group, e.pattern, h)
        return None

    def match_url(self, url: str) -> Match | None:
        try:
            parts = urlsplit(url)
            host = parts.hostname or ""
        except ValueError:
            return None
        h = normalise_host(host)
        if h is None:
            return None
        for e in self.entries:
            if e.kind == "url" and h == e.labels[0] and (parts.path or "/").lower().startswith(e.path):
                return Match(e.group, e.pattern, h, url)
        return self.match_host(h)


def parse(text: str) -> Denylist:
    entries: list[Entry] = []
    group: str | None = None
    for no, raw in enumerate(text.splitlines(), start=1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue

        def bad(why: str) -> DenylistError:
            return DenylistError(f"line {no}: {why}: {raw!r}")

        if line.startswith("@"):
            parts = line.split()
            if len(parts) != 2 or parts[0] != "@group" or not re.fullmatch(r"[a-z][a-z0-9_-]*", parts[1]):
                raise bad("expected '@group <name>'")
            group = parts[1]
            continue
        if group is None:
            raise bad("rule before any '@group'")
        if line.startswith("url "):
            target = line[4:].strip()
            host, sep, path = target.partition("/")
            if not sep or " " in target or normalise_host(host) != host:
                raise bad("url rule needs 'host/path-prefix'")
            entries.append(Entry(group, target, "url", (host,), "/" + path.lower()))
            continue
        if " " in line or "/" in line or ":" in line:
            raise bad("not a host pattern")
        pattern = line[2:] if line.startswith("*.") else line
        kind = "suffix"
        if pattern.endswith(".*"):
            kind, pattern = "tld_wildcard", pattern[:-2]
        labels = pattern.lower().split(".")
        if not all(_LABEL.match(label) for label in labels):
            raise bad("not a host pattern")
        entries.append(Entry(group, line, kind, tuple(labels)))
    return Denylist(entries)


def load(path: Path | str) -> Denylist:
    return parse(Path(path).read_text(encoding="utf-8"))


def load_default() -> Denylist:
    return load(DEFAULT_FILE)
