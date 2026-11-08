"""Read Chromium NetLog JSON (written by --log-net-log) into request records.

The NetLog sees every URL request the browser makes, including the ones CDP's
Network domain cannot: component updater, services calls, and so on. The file
is only complete after the browser exits cleanly, so parsing tolerates a
truncated tail.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from urllib.parse import urlsplit

LOOPBACK = {"127.0.0.1", "127.0.0.2", "localhost", "[::1]", "::1"}
# The Bench resolves *.localhost and *.bench.test to its own loopback fixture server.
LOOPBACK_SUFFIXES = (".localhost", ".bench.test")


def is_loopback(host: str) -> bool:
    return host in LOOPBACK or host.endswith(LOOPBACK_SUFFIXES)


def _events(text: str) -> tuple[dict, list[dict], bool]:
    """Constants, events, and whether the file was complete, valid JSON."""
    dec = json.JSONDecoder()
    try:
        doc = json.loads(text)
        return doc.get("constants", {}), doc.get("events", []), True
    except json.JSONDecodeError:
        pass
    # Truncated or damaged: decode what is there, event by event.
    start = text.find('"constants"')
    constants: dict = {}
    if start >= 0:
        colon = text.index(":", start) + 1
        try:
            constants, _ = dec.raw_decode(text, colon + _skip_ws(text, colon))
        except json.JSONDecodeError:
            constants = {}
    events: list[dict] = []
    marker = text.find('"events"')
    if marker < 0:
        return constants, events, False
    pos = text.find("[", marker) + 1
    while pos > 0 and pos < len(text):
        while pos < len(text) and text[pos] in " \t\r\n,":
            pos += 1
        if pos >= len(text) or text[pos] != "{":
            break
        try:
            ev, pos = dec.raw_decode(text, pos)
        except json.JSONDecodeError:
            break
        events.append(ev)
    return constants, events, False


def _skip_ws(text: str, pos: int) -> int:
    n = 0
    while pos + n < len(text) and text[pos + n] in " \t\r\n":
        n += 1
    return n


def _external(url: str) -> bool:
    parts = urlsplit(url)
    return parts.scheme in ("http", "https", "ws", "wss", "ftp") and not is_loopback(parts.hostname or "")


def parse(text: str) -> list[dict]:
    """Return one record per URL request, in order of first appearance.

    A request that redirects is one NetLog source with a START_JOB event for each hop (and a
    URL_REDIRECTED event with the location). ``url`` is the first URL; ``urls`` has every hop in
    order, so a hop that reached the network stack is not hidden behind the first URL.
    ``external`` is true if any hop went to a non-loopback host.
    """
    constants, events, _ = _events(text)
    ev_names = {v: k for k, v in constants.get("logEventTypes", {}).items()}
    src_names = {v: k for k, v in constants.get("logSourceType", {}).items()}
    records: dict[int, dict] = {}
    order: list[int] = []
    edges: dict[int, set[int]] = {}      # source id -> source ids it depends on (request, job, controller)
    socket_ids: set[int] = set()
    for ev in events:
        src = ev.get("source", {})
        dep = (ev.get("params") or {}).get("source_dependency")
        src_name = src_names.get(src.get("type"))
        if dep and src_name in _WALK_SOURCES:
            dep_name = src_names.get(dep.get("type"))
            if dep_name == "SOCKET":
                socket_ids.add(dep["id"])
                edges.setdefault(src["id"], set()).add(dep["id"])
            elif dep_name in _WALK_SOURCES and dep_name != "URL_REQUEST":
                edges.setdefault(src["id"], set()).add(dep["id"])
        if src_name != "URL_REQUEST":
            continue
        sid = src.get("id")
        params = ev.get("params") or {}
        kind = ev_names.get(ev.get("type"))
        if kind == "URL_REQUEST_START_JOB":
            url = params.get("url", "")
            if sid not in records:
                parts = urlsplit(url)
                initiator = params.get("initiator")
                records[sid] = {
                    "source_id": sid, "time": ev.get("time"), "url": url, "urls": [url],
                    "method": params.get("method"), "scheme": parts.scheme, "host": parts.hostname or "",
                    "initiator": None if initiator in (None, "", "not an origin") else initiator,
                    "net_error": None, "external": _external(url),
                }
                order.append(sid)
            elif url not in records[sid]["urls"]:
                records[sid]["urls"].append(url)
                records[sid]["external"] = records[sid]["external"] or _external(url)
        elif kind == "URL_REQUEST_REDIRECTED" and sid in records:
            location = params.get("location", "")
            if location and location not in records[sid]["urls"]:
                records[sid]["urls"].append(location)
                records[sid]["external"] = records[sid]["external"] or _external(location)
        if sid in records and params.get("net_error"):
            records[sid]["net_error"] = params["net_error"]
    for sid in order:
        records[sid]["sockets"] = sorted(_sockets_of(sid, edges, socket_ids))
    return [records[i] for i in order]


# Sources a request's connection hangs off. The walk goes request -> stream job -> socket and stops
# at a socket: a socket points back at the job that uses it, which would lead to other requests.
_WALK_SOURCES = ("URL_REQUEST", "HTTP_STREAM_JOB", "HTTP_STREAM_JOB_CONTROLLER")


def _sockets_of(start: int, edges: dict[int, set[int]], socket_ids: set[int]) -> set[int]:
    found: set[int] = set()
    seen = {start}
    todo = [start]
    while todo:
        for dep in edges.get(todo.pop(), ()):
            if dep in socket_ids:
                found.add(dep)
            elif dep not in seen:
                seen.add(dep)
                todo.append(dep)
    return found


def all_urls(records: list[dict]) -> set[str]:
    """Every URL any request (any hop) reached the network stack with."""
    return {u for r in records for u in r.get("urls", [r["url"]])}


_LOOKUP_KINDS = ("HOST_RESOLVER", "DNS", "SOCKET", "CONNECT_JOB", "QUIC")
_LOOKUP_KEYS = ("host", "hostname", "group_id")


def _host_of(value: object) -> str | None:
    if not isinstance(value, str) or not value:
        return None
    try:
        return urlsplit(value if "://" in value else "//" + value).hostname
    except ValueError:
        return None


@dataclass
class Parsed:
    records: list[dict] = field(default_factory=list)   # URL requests, as ``parse`` returns them
    lookups: list[dict] = field(default_factory=list)   # host names seen in DNS, socket and connect events
    complete: bool = False                              # the file was whole, valid JSON


def load(text: str) -> Parsed:
    """URL requests, host names from resolver and socket events, and whether the log is complete.

    A browser that is killed leaves a truncated file. The tail may hold the traffic that matters,
    so callers that prove absence must check ``complete``."""
    constants, events, complete = _events(text)
    ev_names = {v: k for k, v in constants.get("logEventTypes", {}).items()}
    src_names = {v: k for k, v in constants.get("logSourceType", {}).items()}
    seen: set[str] = set()
    lookups: list[dict] = []
    for ev in events:
        src = src_names.get(ev.get("source", {}).get("type"), "")
        name = ev_names.get(ev.get("type"), "")
        if not any(k in src or k in name for k in _LOOKUP_KINDS):
            continue
        params = ev.get("params") or {}
        for key in _LOOKUP_KEYS:
            host = _host_of(params.get(key))
            if host and host.lower() not in seen:
                seen.add(host.lower())
                lookups.append({"host": host.lower(), "source_type": src, "event": name, "time": ev.get("time")})
    return Parsed(parse(text), lookups, complete)


def load_file(path) -> Parsed:
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return load(f.read())
    except FileNotFoundError:
        return Parsed()


def parse_file(path) -> list[dict]:
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return parse(f.read())
    except FileNotFoundError:
        return []


def external(records: list[dict]) -> list[dict]:
    """Requests to anything that is not loopback: the traffic the Bench refuses and records."""
    return [r for r in records if r["external"]]
