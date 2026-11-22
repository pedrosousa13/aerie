"""The privacy report (issue #253): proof, for one release, that Aerie never phones home.

``./aerie bench --report <dir>`` runs ``zero_traffic``, ``adblock_score``, the fingerprinting checks and the
hardening checks in one Bench run, then turns the recorded observations (``results.json`` data) into
``report.html`` and ``report.json``.

``build`` is a pure function over the recorded run: no browser, no network, no clock. It also decides the
release gate (``report["gate"]``). The gate fails when:

* a request to a non-loopback host is not explained by the request policy's host table in
  ``docs/network-endpoints.md`` (``aerie_netpolicy``: the same table that the browser enforces). A request that the
  policy refused (net error -22 on the last hop) never left the machine: it is counted, not a stray;
* a required check is missing from the run or did not pass (``zero_traffic``, ``adblock_score``, every
  fingerprinting and hardening check the run was asked for), the crash step of ``zero_traffic`` did not pass,
  or its idle phase was shorter than 5 minutes;
* the run itself failed.

Redaction: the report is built from an allowlist of fields. A request is reduced to scheme-less host and URL
path (no query, fragment, user info or port). Free text from a check goes through ``scrub_text``. Before
anything is written, ``check_clean`` scans the finished text for local paths, query strings and the strings the
caller names, and refuses to write on a match.
"""

from __future__ import annotations

import html
import json
from pathlib import Path
from urllib.parse import urlsplit

from aerie_netpolicy import model as policy_model

from . import comparison as comparison_mod
from . import netlog
from .scrub import _ANY_PATH_RE, _LEAK_RE, _PATH_RE, scrub_text  # noqa: F401  (scrub_text is re-exported)

SCHEMA = 1
PHASES = ("first_launch", "idle", "browse")
PHASE_TITLES = {"first_launch": "First launch", "idle": "5 minutes idle", "browse": "Browse script"}
POLICY_NET_ERROR = -22
MIN_IDLE_SECONDS = 300.0
REPO = Path(__file__).resolve().parents[2]


class ReportError(Exception):
    """The report would hold something it must not, or cannot be built."""


def check_local_path(text: str) -> None:
    """Refuse text that still holds an absolute path (checked on fields where a local path could appear)."""
    m = _ANY_PATH_RE.search(text)
    if m:
        raise ReportError(f"the report would hold a local path ({m.group(0)!r}); not written")


def _safe_field(value: object) -> str:
    out = scrub_text(value)
    check_local_path(out)
    return out


def check_clean(text: str, forbidden: tuple[str, ...]) -> None:
    m = _LEAK_RE.search(text)
    if m:
        raise ReportError(f"the report would hold a local path, query string or cookie ({m.group(0)!r}); not written")
    for bad in forbidden:
        if bad and bad in text:
            raise ReportError("the report would hold a local path or profile string; not written")


def load_allowlist(doc_text: str) -> policy_model.PolicyTable:
    """The request policy tables: the hosts a browser request may reach, each with its reason."""
    return policy_model.parse_doc(doc_text)


def load_default_allowlist() -> policy_model.PolicyTable:
    return load_allowlist((REPO / policy_model.DOC_PATH).read_text(encoding="utf-8"))


def _explain(host: str, table: policy_model.PolicyTable) -> str | None:
    found = table.explain(host)
    return None if found is None else f"{found[1]} (request policy, annotation {found[0]})"


def _safe_path(url: str) -> str:
    # A URL path is expected in the report; only the known local roots are hidden from it.
    return _PATH_RE.sub("<path>", urlsplit(url).path or "/")


def _phase_bounds(zt: dict, total: int) -> list[tuple[str, int, int]]:
    ends = (zt.get("data") or {}).get("phases") or {}
    out, start = [], 0
    for name in PHASES:
        end = ends.get(name)
        end = total if end is None else min(int(end), total)
        out.append((name, start, max(start, end)))
        start = max(start, end)
    return out


def _check_group(name: str) -> str | None:
    if name.startswith("hardening_fingerprinting") or name == "hardening_device_memory":
        return "fingerprinting"
    if name.startswith("hardening_"):
        return "hardening"
    return None


def _classify_requests(records: list[dict], zt: dict, table: policy_model.PolicyTable) -> tuple[list[dict], list[dict]]:
    """(requests that left loopback, per-phase counts). A request the policy refused is counted, not listed."""
    bounds = _phase_bounds(zt, len(records))

    def phase_of(i: int) -> str:
        for name, lo, hi in bounds:
            if lo <= i < hi:
                return name
        return bounds[-1][0]

    requests = []
    phases = {n: {"name": n, "title": PHASE_TITLES[n], "requests": 0, "non_loopback": 0, "refused": 0} for n in PHASES}
    for i, r in enumerate(records):
        ph = phase_of(i)
        phases[ph]["requests"] += 1
        hops = list(r.get("urls") or [r.get("url", "")])
        if r.get("net_error") == POLICY_NET_ERROR and r.get("external"):
            phases[ph]["refused"] += 1
            hops = hops[:-1]
        made = False
        for url in hops:
            parts = urlsplit(url)
            host = (parts.hostname or "").lower()
            if parts.scheme not in ("http", "https", "ws", "wss", "ftp") or not host or netlog.is_loopback(host):
                continue
            made = True
            reason = _explain(host, table)
            requests.append({"host": host, "path": _safe_path(url), "method": str(r.get("method") or ""),
                             "phase": ph, "allowed": reason is not None,
                             "reason": reason or "NOT EXPLAINED by docs/network-endpoints.md"})
        if made:
            phases[ph]["non_loopback"] += 1
    return requests, list(phases.values())


def _blocking(check: dict) -> dict:
    data = check.get("data") or {}
    score = data.get("score") or {}
    ov = score.get("overall") or {}
    total = int(ov.get("total") or 0)
    blocked = int(ov.get("blocked") or 0)
    hostlist = data.get("hostlist") or {}
    return {"blocked": blocked, "total": total, "percent": round(100 * blocked / total, 1) if total else 0.0,
            "categories": {scrub_text(k): {"blocked": int(v.get("blocked", 0)), "total": int(v.get("total", 0))}
                           for k, v in (score.get("categories") or {}).items()},
            "hostlist_sha256": scrub_text(hostlist.get("sha256") or ""),
            "hostlist_date": scrub_text(hostlist.get("date") or "")}


def _unknown(value: object) -> bool:
    return not value or str(value).strip().lower() == "unknown"


def _gate(run: dict, checks: dict, requests: list[dict], version: object, commit: object,
          required: tuple[str, ...]) -> list[str]:
    failures = [f"unexplained request to {q['host']} ({PHASE_TITLES[q['phase']]})"
                for q in requests if not q["allowed"]]
    if _unknown(commit):
        failures.append("the commit is unknown; a release report must name its commit")
    if _unknown(version):
        failures.append("the browser version is unknown; a release report must name its version")
    for name in required:
        c = checks.get(name)
        if c is None:
            failures.append(f"required check {name} did not run")
        elif c.get("status") != "pass":
            failures.append(f"check {name}: {c.get('status')}")
    if run.get("status") != "pass":
        failures.append(f"the Bench run did not pass ({run.get('status')})")
    ztd = (checks.get("zero_traffic") or {}).get("data") or {}
    if "zero_traffic" in checks:
        if float(ztd.get("idle_seconds") or 0) < MIN_IDLE_SECONDS:
            failures.append(f"the idle phase lasted {ztd.get('idle_seconds')} s; a release needs {MIN_IDLE_SECONDS:g} s")
        if (ztd.get("crash_step") or {}).get("status") != "pass":
            failures.append("the crash step of zero_traffic did not pass or did not run")
        if ztd.get("refusal_counter") is None:
            failures.append("the request policy's refusal counter could not be read, so refusals are not proven")
    if (run.get("exit") or {}).get("code") != 0:
        failures.append("the browser did not exit cleanly")
    return failures


def _comparison(measurements: list[dict] | None) -> dict:
    """Aerie's own measurement (``./aerie bench --measure`` on the Aerie binary) and the other browsers'. The
    report run is not used: it seeds prefs and runs the crash step, so it is not the same configuration."""
    ms = list(measurements or [])
    aerie = next((m for m in ms if m["browser"]["id"] == "aerie"), None)
    return {"aerie": aerie, "others": [m for m in ms if m is not aerie and m["browser"]["id"] != "aerie"]}


def build(run: dict, meta: dict, allowlist: policy_model.PolicyTable, *, required: tuple[str, ...] = (),
          forbidden: tuple[str, ...] = (), comparison: list[dict] | None = None) -> dict:
    """The report document for one recorded run. ``meta``: commit, binary_sha256, binary_name, command.
    ``comparison``: measurements of other browsers (``comparison.load_measurement``); the table never changes the gate."""
    checks = {c["name"]: c for c in run.get("checks", [])}
    ztd = (checks.get("zero_traffic") or {}).get("data") or {}
    requests, phases = _classify_requests((run.get("network") or {}).get("requests", []),
                                          checks.get("zero_traffic") or {}, allowlist)
    version = (run.get("binary") or {}).get("version")
    commit = meta.get("commit")
    failures = _gate(run, checks, requests, version, commit, required)

    def summary(group: str) -> list[dict]:
        return [{"name": _safe_field(n), "status": _safe_field(c.get("status")),
                 "detail": _safe_field(c.get("detail") or "")}
                for n, c in checks.items() if _check_group(n) == group]

    report = {
        "schema": SCHEMA,
        "title": "Aerie privacy report",
        "version": _safe_field(version or "unknown"),
        "commit": _safe_field(commit or "unknown"),
        "gate": {"passed": not failures, "failures": [_safe_field(f) for f in failures]},
        "requests": requests,
        "phases": phases,
        "idle_seconds": ztd.get("idle_seconds"),
        "blocking": _blocking(checks.get("adblock_score") or {}),
        "fingerprinting": summary("fingerprinting"),
        "hardening": summary("hardening"),
        "netpolicy": {"counter": ztd.get("refusal_counter"),
                      "counter_error": _safe_field(ztd.get("refusal_counter_error") or "") or None,
                      "refused": [{"host": scrub_text(str(x.get("host", "")).lower()), "phase": x.get("phase")}
                                  for x in ztd.get("refused") or []]},
        "comparison": _comparison(comparison),
        "rerun": {"command": _safe_field(meta.get("command") or "./aerie bench --report <dir>"),
                  "binary_name": _safe_field(meta.get("binary_name") or ""),
                  "binary_sha256": _safe_field(meta.get("binary_sha256") or "")},
    }
    check_clean(render_json(report), forbidden)
    check_clean(render_html(report), forbidden)
    return report


def render_json(report: dict) -> str:
    return json.dumps(report, indent=2) + "\n"


def _e(x: object) -> str:
    return html.escape(str(x), quote=True)


def _table(head: tuple[str, ...], rows: list[tuple]) -> str:
    th = "".join(f"<th>{_e(h)}</th>" for h in head)
    body = "".join("<tr>" + "".join(f"<td>{_e(c)}</td>" for c in row) + "</tr>" for row in rows)
    return f"<table><thead><tr>{th}</tr></thead><tbody>{body}</tbody></table>"


_CSS = """:root{--bg:#fff;--fg:#1b1b1f;--mut:#5c5c66;--ok:#0a7d33;--bad:#b3261e;--line:#d8d8de}
@media (prefers-color-scheme:dark){:root{--bg:#16161a;--fg:#ececf1;--mut:#a0a0ab;--ok:#4cd07d;--bad:#ff8a80;--line:#34343c}}
body{background:var(--bg);color:var(--fg);font:16px/1.5 system-ui,sans-serif;max-width:60rem;margin:0 auto;padding:1rem 16px}
table{border-collapse:collapse;width:100%;margin:.5rem 0 1.5rem;display:block;overflow-x:auto}
th,td{border-bottom:1px solid var(--line);padding:.3rem .6rem;text-align:left}
.ok{color:var(--ok)}.bad{color:var(--bad)}.mut{color:var(--mut)}code{word-break:break-all}"""


def render_html(report: dict) -> str:
    g = report["gate"]
    verdict = ('<p class="ok"><strong>PASS</strong>: every request is explained and every check passed.</p>'
               if g["passed"] else
               '<p class="bad"><strong>FAIL</strong>: this release must not be published.</p><ul>'
               + "".join(f"<li>{_e(f)}</li>" for f in g["failures"]) + "</ul>")
    reqs = (_table(("Host", "Path", "Method", "Phase", "Allowed", "Reason"),
                   [(q["host"], q["path"], q["method"], PHASE_TITLES.get(q["phase"], q["phase"]),
                     "yes" if q["allowed"] else "NO", q["reason"]) for q in report["requests"]])
            if report["requests"] else "<p>None. Every request in the recording went to this machine (loopback).</p>")
    b = report["blocking"]
    cats = _table(("Category", "Blocked", "Total"), [(c, v["blocked"], v["total"]) for c, v in b["categories"].items()])
    np = report["netpolicy"]
    counter = "unreadable" if np["counter"] is None else np["counter"]
    refused = (_table(("Host", "Phase"), [(x["host"], x["phase"]) for x in np["refused"]])
               if np["refused"] else "<p>No refused attempt was named in the NetLog.</p>")
    rr = report["rerun"]
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'">
<title>Aerie privacy report</title><style>{_CSS}</style></head><body>
<h1>Aerie privacy report</h1>
<p>Version <code>{_e(report['version'])}</code>, commit <code>{_e(report['commit'])}</code></p>
{verdict}
<h2>Requests that left loopback</h2>
<p class="mut">Every request to a host other than this machine, with the reason it is allowed (the request policy in
docs/network-endpoints.md). Only the host and URL path are kept: no query string, no cookie, no local path.</p>
{reqs}
<h2>Requests per phase</h2>
{_table(("Phase", "Requests", "To other hosts", "Refused by the policy"),
        [(p["title"], p["requests"], p["non_loopback"], p["refused"]) for p in report["phases"]])}
<p class="mut">Idle phase: {_e(report['idle_seconds'])} seconds. Counts include the Bench's own loopback fixture pages.</p>
<h2>Ad and tracker blocking</h2>
<p>{_e(b['blocked'])} of {_e(b['total'])} test hosts blocked ({_e(b['percent'])}%). Host list sha256
<code>{_e(b['hostlist_sha256'])}</code>, {_e(b['hostlist_date'])}.</p>
{cats}
{comparison_mod.render_html(report['comparison']['aerie'], report['comparison']['others'])}
<h2>Fingerprinting protection</h2>
{_table(("Check", "Result", "Detail"), [(c["name"], c["status"], c["detail"]) for c in report["fingerprinting"]])}
<h2>Hardening</h2>
{_table(("Check", "Result", "Detail"), [(c["name"], c["status"], c["detail"]) for c in report["hardening"]])}
<h2>Request policy refusals</h2>
<p>The network service refuses any browser request that is not on the reviewed list. Refusals counted: <strong>{_e(counter)}</strong>.</p>
{refused}
<h2>Rerun it yourself</h2>
<p>Run <code>{_e(rr['command'])}</code></p>
<p class="mut">Replace <code>&lt;dir&gt;</code> with an empty output directory. <code>--binary ./{_e(rr['binary_name'])}</code>
stands for the release binary of that name, wherever you unpacked it; with no <code>--binary</code>, the command tests
the binary that <code>./aerie build</code> makes from this commit. <code>&lt;file&gt;</code> stands for a file you supply.</p>
<p>Binary <code>{_e(rr['binary_name'])}</code> sha256 <code>{_e(rr['binary_sha256'])}</code></p>
</body></html>
"""


def write(out_dir: Path, report: dict) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "report.json").write_text(render_json(report), encoding="utf-8")
    (out_dir / "report.html").write_text(render_html(report), encoding="utf-8")
