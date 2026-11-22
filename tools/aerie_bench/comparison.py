"""Other browsers' network traffic, measured with the same checks as Aerie's (issue #254).

``./aerie bench --measure --binary <chromium-family browser>`` runs ``zero_traffic`` and ``adblock_score`` on a fresh
profile with default settings. ``measure`` turns that recorded run into a measurement document, and ``render_html``
turns Aerie's measurement and the others into the comparison table of the privacy report. Method:
``docs/browser-comparison.md``.

Every non-loopback request (every hop of a redirect) falls in exactly one class, tested in this order:

* ``vendor``: a host of the browser's maker (``VENDORS``; for Helium and Aerie, the ``helium`` group of the
  denylist). A browser that is not in ``VENDORS`` has no vendor class: its maker's hosts count as ``other``, and
  the measurement says so (``vendor_known``).
* ``google``: the ``google`` group of the denylist (``denylist/google-helium.txt``).
* ``tracker``: a host of the ad and tracker test list that ``adblock_score`` pins (the Bench's existing list, not a
  new one). Without that list (the check was skipped) the class cannot be told, and ``tracker_list`` is null.
* ``other``: anything else.

A measurement holds only counts, host names, the browser's name and version, the date and the settings. It never
holds a local path. Results about other browsers are public claims: nothing here is a recorded number.
"""

from __future__ import annotations

import html
import re
from pathlib import Path
from urllib.parse import urlsplit

from . import denylist as denylist_mod
from . import netlog

SCHEMA = 1
CLASSES = ("vendor", "google", "tracker", "other")
POLICY_NET_ERROR = -22
METHOD_DOC = "docs/browser-comparison.md"
DEFAULT_IDLE, DEFAULT_SETTLE = 300.0, 60.0

# The maker's own hosts, for the browsers measured so far. Add a browser only with hosts seen in a real NetLog
# or named by its maker.
VENDORS: dict[str, tuple[str, ...]] = {
    "brave": ("brave.com", "bravesoftware.com", "brave.software"),
    "vivaldi": ("vivaldi.com", "vivaldi.net"),
    "chrome": (), "chromium": (),   # the maker is Google: those requests are the google class
}
# Browsers whose maker's hosts are the denylist's ``helium`` group.
HELIUM_LINEAGE = ("helium", "aerie")
NAMES = {"brave": "Brave", "chrome": "Chrome", "chromium": "Chromium", "helium": "Helium", "aerie": "Aerie",
         "vivaldi": "Vivaldi"}
# Matched in this order against the lower-cased version line, then the binary name.
_MARKERS = (("brave", "brave"), ("google chrome", "chrome"), ("chromium", "chromium"), ("helium", "helium"),
            ("aerie", "aerie"), ("vivaldi", "vivaldi"))
_VERSION = re.compile(r"\d+(?:\.\d+)+")

# Browsers the Bench cannot measure, and why. The wording of Zen is fixed: the reason is the capture, nothing more.
NOT_MEASURED = (
    {"id": "zen", "name": "Zen", "reason": "Gecko-based; the Bench captures Chromium NetLogs only",
     "slot": True},
    {"id": "dia", "name": "Dia", "reason": "Mac-only; it cannot run on the Linux machine of the Bench"},
    {"id": "arc", "name": "Arc", "reason": "Mac-only; it cannot run on the Linux machine of the Bench"},
)

_denylist: denylist_mod.Denylist | None = None


def _deny() -> denylist_mod.Denylist:
    global _denylist
    if _denylist is None:
        _denylist = denylist_mod.load_default()
    return _denylist


def identify(version_text: str | None, binary_name: str = "") -> dict:
    """The browser from its ``--version`` line (first) or binary name: ``{id, name, version}``."""
    text = (version_text or "").strip()
    for source in (text.lower(), binary_name.lower()):
        for marker, bid in _MARKERS:
            if marker in source:
                found = _VERSION.search(text)
                return {"id": bid, "name": NAMES[bid], "version": found.group(0) if found else "unknown"}
    found = _VERSION.search(text)
    name = text[:found.start()].strip() if found else text
    return {"id": "unknown", "name": name or binary_name or "unknown", "version": found.group(0) if found else "unknown"}


def _under(host: str, domains) -> bool:
    return any(host == d or host.endswith("." + d) for d in domains)


def classify_host(host: str, browser_id: str, trackers: set[str] | frozenset[str]) -> str:
    match = _deny().match_host(host)
    if browser_id in HELIUM_LINEAGE and match and match.group == "helium":
        return "vendor"
    if _under(host, VENDORS.get(browser_id, ())):
        return "vendor"
    if match and match.group == "google":
        return "google"
    if _under(host, trackers):
        return "tracker"
    return "other"


def classify(records: list[dict], browser_id: str, trackers) -> dict:
    """``{counts, total, hosts}`` over the requests that left loopback. A request that the request policy refused
    (net error -22 on the last hop) never left the machine and is not counted."""
    counts = {c: 0 for c in CLASSES}
    hosts: dict[str, dict[str, int]] = {c: {} for c in CLASSES}
    for r in records:
        hops = list(r.get("urls") or [r.get("url", "")])
        if r.get("net_error") == POLICY_NET_ERROR and r.get("external"):
            hops = hops[:-1]
        for url in hops:
            parts = urlsplit(url)
            host = (parts.hostname or "").lower()
            if parts.scheme not in ("http", "https", "ws", "wss", "ftp") or not host or netlog.is_loopback(host):
                continue
            cls = classify_host(host, browser_id, trackers)
            counts[cls] += 1
            hosts[cls][host] = hosts[cls].get(host, 0) + 1
    return {"counts": counts, "total": sum(counts.values()), "hosts": hosts}


def _check(run: dict, name: str) -> dict:
    return next((c for c in run.get("checks", []) if c.get("name") == name), {})


def measure(run: dict, command: str) -> dict:
    """The measurement document of one recorded Bench run (``results.json`` data)."""
    from .privacy_report import scrub_text  # a free-text field never carries a local path
    binary = run.get("binary") or {}
    name = str(binary.get("path") or "").rsplit("/", 1)[-1]
    browser = identify(binary.get("version"), name)
    zt = _check(run, "zero_traffic").get("data") or {}
    score = ((_check(run, "adblock_score").get("data") or {}).get("score")) or {}
    trackers = None
    blocking = None
    if score.get("overall"):
        trackers = {*score.get("blocked_hosts", []), *score.get("through_hosts", []), *score.get("unjudged", [])}
        ov = score["overall"]
        total = int(ov.get("total") or 0)
        blocking = {"blocked": int(ov.get("blocked") or 0), "total": total,
                    "percent": round(100 * int(ov.get("blocked") or 0) / total, 1) if total else 0.0}
    c = classify((run.get("network") or {}).get("requests", []), browser["id"], trackers or set())
    idle = zt.get("idle_seconds")
    settle = zt.get("settle_seconds")
    return {
        "schema": SCHEMA,
        "browser": {k: scrub_text(v) for k, v in browser.items()},
        "date": str(run.get("started") or "")[:10] or "unknown",
        "settings": {"profile": "fresh", "settings": "defaults", "idle_seconds": idle, "settle_seconds": settle,
                     "defaults": idle == DEFAULT_IDLE and settle == DEFAULT_SETTLE},
        "counts": c["counts"], "total": c["total"], "hosts": c["hosts"],
        "vendor_known": browser["id"] in VENDORS or browser["id"] in HELIUM_LINEAGE,
        "tracker_list": (None if trackers is None else
                         {"sha256": (_check(run, "adblock_score").get("data") or {}).get("hostlist", {}).get("sha256"),
                          "date": (_check(run, "adblock_score").get("data") or {}).get("hostlist", {}).get("date")}),
        "blocking": blocking,
        "command": scrub_text(command),
    }


class MeasurementError(Exception):
    """A measurement file that cannot be trusted in the report."""


def load_measurement(path) -> dict:
    """Read a ``measurement.json`` that ``./aerie bench --measure`` wrote, and refuse anything of another shape."""
    import json
    from .privacy_report import scrub_text
    try:
        doc = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise MeasurementError(f"cannot read the measurement {path}: {exc}") from exc
    try:
        if doc["schema"] != SCHEMA:
            raise MeasurementError(f"{path}: schema {doc['schema']!r}, expected {SCHEMA}")
        counts = {c: doc["counts"][c] for c in CLASSES}
        if not all(type(v) is int and v >= 0 for v in counts.values()):
            raise MeasurementError(f"{path}: the counts must be whole numbers")
        browser = {k: scrub_text(doc["browser"][k]) for k in ("id", "name", "version")}
        out = {**doc, "browser": browser, "counts": counts, "total": sum(counts.values()),
               "date": scrub_text(doc["date"]), "command": scrub_text(doc.get("command", ""))}
        out["settings"] = dict(doc["settings"])
    except (KeyError, TypeError, AttributeError) as exc:
        raise MeasurementError(f"{path} is not a measurement document ({type(exc).__name__}: {exc})") from exc
    return out


def flagged(m: dict) -> int:
    """Requests to trackers, vendors or Google: what the headline counts."""
    return sum(m["counts"][k] for k in ("vendor", "google", "tracker"))


def _e(x: object) -> str:
    return html.escape(str(x), quote=True)


def _plural(n: int) -> str:
    return "1 request" if n == 1 else f"{n} requests"


def headline(m: dict) -> str:
    return f"{m['browser']['name']}: {_plural(flagged(m))} to trackers or vendors" \
        if flagged(m) == 0 else f"{m['browser']['name']}: {flagged(m)}"


def _settings(m: dict) -> str:
    s = m["settings"]
    base = "fresh profile, default settings"
    if s.get("defaults"):
        return f"{base}; {s['settle_seconds']:g} s settle, {s['idle_seconds']:g} s idle"
    idle, settle = s.get("idle_seconds"), s.get("settle_seconds")
    return f"{base}; NOT the full run: {settle} s settle, {idle} s idle"


def render_html(aerie: dict, others: list[dict]) -> str:
    """The comparison section of the report: a headline for each browser, one row per measurement, and the browsers
    that were not measured, each with its reason."""
    rows = []
    notes = []
    for m in [aerie, *others]:
        if m is not aerie and not m.get("vendor_known", True):
            notes.append(f"{m['browser']['name']}: the hosts of its maker are not listed, so they count as other.")
        if m.get("tracker_list") is None and m is not aerie:
            notes.append(f"{m['browser']['name']}: no tracker list was available, so the tracker column is not known.")
        c = m["counts"]
        bl = m.get("blocking")
        rows.append((m["browser"]["name"], m["browser"]["version"], m["date"], _settings(m), c["vendor"], c["google"],
                     c["tracker"] if m.get("tracker_list", True) is not None else "no tracker list", c["other"],
                     m["total"], f"{bl['blocked']}/{bl['total']} ({bl['percent']}%)" if bl else "not measured"))
    head = ("Browser", "Version", "Date", "Settings", "To the maker", "To Google", "To trackers", "To other hosts",
            "All requests", "Test hosts blocked")
    th = "".join(f"<th>{_e(h)}</th>" for h in head)
    body = "".join("<tr>" + "".join(f"<td>{_e(c)}</td>" for c in row) + "</tr>" for row in rows)
    lines = [headline(aerie) + "."] + [f"{headline(m)}." for m in others]
    seen = {m["browser"]["id"] for m in [aerie, *others]}
    missing = [n for n in NOT_MEASURED if n["id"] not in seen]
    nm = "".join(
        f"<tr><td>{_e(n['name'])}</td><td>{'not measured yet' if n.get('slot') else 'not measured'}</td>"
        f"<td>{_e(n['reason'])}</td></tr>" for n in missing)
    return (f'<h2>Other browsers</h2>\n<p class="mut">Same checks, same machine, a fresh profile and default settings for '
            f'each browser. Only what was measured is shown, with its date and version. Method: '
            f'<a href="{_e(METHOD_DOC)}">{_e(METHOD_DOC)}</a>. The counts to trackers or vendors add the requests to '
            f'the maker, to Google and to listed tracker hosts. Requests are attempts: the Bench resolves no outside name, '
            f'so none reaches its host. The blocking score is taken without a network, so a browser that downloads its '
            f'filter lists after install is scored without them.</p>\n'
            + "".join(f"<p><strong>{_e(line)}</strong></p>\n" for line in lines)
            + f"<table><thead><tr>{th}</tr></thead><tbody>{body}</tbody></table>\n"
            + "".join(f'<p class="mut">{_e(n)}</p>\n' for n in notes)
            + ("<h3>Not measured</h3>\n"
               f"<table><thead><tr><th>Browser</th><th>Status</th><th>Reason</th></tr></thead><tbody>{nm}</tbody></table>\n"
               if missing else ""))
