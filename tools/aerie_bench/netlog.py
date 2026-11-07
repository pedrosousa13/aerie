"""Read Chromium NetLog JSON (written by --log-net-log) into request records.

The NetLog sees every URL request the browser makes, including the ones CDP's
Network domain cannot: component updater, services calls, and so on. The file
is only complete after the browser exits cleanly, so parsing tolerates a
truncated tail.
"""

from __future__ import annotations

import json
from urllib.parse import urlsplit

LOOPBACK = {"127.0.0.1", "localhost", "[::1]", "::1"}


def _events(text: str) -> tuple[dict, list[dict]]:
    dec = json.JSONDecoder()
    try:
        doc = json.loads(text)
        return doc.get("constants", {}), doc.get("events", [])
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
        return constants, events
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
    return constants, events


def _skip_ws(text: str, pos: int) -> int:
    n = 0
    while pos + n < len(text) and text[pos + n] in " \t\r\n":
        n += 1
    return n


def parse(text: str) -> list[dict]:
    """Return one record per URL request, in order of first appearance."""
    constants, events = _events(text)
    ev_names = {v: k for k, v in constants.get("logEventTypes", {}).items()}
    src_names = {v: k for k, v in constants.get("logSourceType", {}).items()}
    records: dict[int, dict] = {}
    order: list[int] = []
    for ev in events:
        src = ev.get("source", {})
        if src_names.get(src.get("type")) != "URL_REQUEST":
            continue
        sid = src.get("id")
        params = ev.get("params") or {}
        if ev_names.get(ev.get("type")) == "URL_REQUEST_START_JOB" and sid not in records:
            url = params.get("url", "")
            parts = urlsplit(url)
            host = parts.hostname or ""
            initiator = params.get("initiator")
            records[sid] = {
                "source_id": sid, "time": ev.get("time"), "url": url,
                "method": params.get("method"), "scheme": parts.scheme, "host": host,
                "initiator": None if initiator in (None, "", "not an origin") else initiator,
                "net_error": None,
                "external": parts.scheme in ("http", "https", "ws", "wss", "ftp") and host not in LOOPBACK,
            }
            order.append(sid)
        if sid in records and params.get("net_error"):
            records[sid]["net_error"] = params["net_error"]
    return [records[i] for i in order]


def parse_file(path) -> list[dict]:
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return parse(f.read())
    except FileNotFoundError:
        return []


def external(records: list[dict]) -> list[dict]:
    """Requests to anything that is not loopback: the traffic the Bench refuses and records."""
    return [r for r in records if r["external"]]
