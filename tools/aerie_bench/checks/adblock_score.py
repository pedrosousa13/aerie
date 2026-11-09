"""Ad-block score: how many of the hosts in a pinned ad and tracker test list the browser blocks (issue #155).

The test is the one of https://adblock.turtlecute.org (its "Host" checks): one request per ad, analytics,
tracker and device-maker host, and a host counts as blocked when the request fails. This check measures that
offline, so a train can record the number and later work (#103 to #110) can be compared with it.

* The list is not in the repo (CC BY-NC-SA 4.0). ``adblock_hosts.lock.json`` pins its URL, sha256, size
  and date. The Bench downloads and verifies it, or uses the verified copy in ``<build root>/downloads``
  (``aerie_bench.adblock_hosts``). With neither, the check skips and says why.
* Every listed host is mapped to the loopback fixture server (``--host-resolver-rules``). The page
  ``/adblock/index.html`` sends one HEAD ``fetch`` (``mode: 'no-cors'``) per host, as the source test does.
  Nothing leaves the machine: a blocked request fails with ``net::ERR_BLOCKED_BY_CLIENT`` and never reaches the
  network stack (no NetLog request and no host lookup for it), and a request that gets through only reaches the
  fixture server.
* A host is blocked only when DevTools reports ``ERR_BLOCKED_BY_CLIENT``, the NetLog has no request for it and the
  fixture server never saw it. It got through when its response arrived or the fixture server saw it. Any other
  outcome (never requested, another error) cannot be judged and fails the check, so a broken run is never a score.
* The score is blocked / total, overall and per category. ``--adblock-baseline FILE`` compares against a stored
  score (the ``adblock_score.json`` an earlier run wrote): a drop of more than ``DROP_THRESHOLD_POINTS``
  percentage points, or any host that the baseline blocked and that now gets through, fails. Without a baseline the
  check passes and records the score.
* ``--adblock-online SEC`` also loads the live test page in a second browser and records its own score
  (``aerie_bench.adblock_online``). That run is informational and never fails the check.

The offline score measures host blocking only. The live page's cosmetic and script checks depend on its own
domain being in filter lists, so they are not reproduced here.

The decision logic is in plain functions (``judge``, ``compare``, ``score_document``) that the tests run on made-up
outcomes. The first real run in a train has to confirm the request shape against a real browser.
"""

from __future__ import annotations

import json
import time
from pathlib import Path
from urllib.parse import urlsplit

from .. import adblock_hosts, adblock_online, netlog
from ..session import CheckResult, Session
from . import register
from .ad_blocking import BLOCKED_ERROR, _event_rank, collect_outcomes

PAGE = "/adblock/index.html"
PROBE_PATH = "/adblock/fakepage.html"
ARTIFACT = "adblock_score.json"
SETTLE_SECONDS = 30.0
DROP_THRESHOLD_POINTS = 2.0
SCHEMA = 1
ONLINE_FLAGS = ("--host-resolver-rules=MAP *.bench.test 127.0.0.1",)


class BaselineError(Exception):
    """The baseline file is missing or is not an adblock_score document."""


def probe_url(host: str, port: int) -> str:
    return f"http://{host}:{port}{PROBE_PATH}"


def _percent(blocked: int, total: int) -> float:
    return round(100 * blocked / total, 1) if total else 0.0


def load_baseline(path) -> dict:
    try:
        doc = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise BaselineError(f"cannot read the adblock baseline {path}: {exc}") from exc
    overall = doc.get("overall") if isinstance(doc, dict) else None
    if not isinstance(overall, dict) or "blocked" not in overall or "total" not in overall \
            or not isinstance(doc.get("blocked_hosts"), list):
        raise BaselineError(f"{path} is not an adblock_score document (use the {ARTIFACT} of an earlier run)")
    return doc


def score_document(data: dict) -> dict:
    """What a run writes as ``adblock_score.json``, and what ``--adblock-baseline`` reads."""
    score = data["score"]
    return {"schema": SCHEMA, "hostlist_sha256": data.get("hostlist_sha256"),
            "overall": {k: score["overall"][k] for k in ("blocked", "total")},
            "categories": {c: {k: v[k] for k in ("blocked", "total")} for c, v in score["categories"].items()},
            "blocked_hosts": score["blocked_hosts"], "through_hosts": score["through_hosts"]}


def compare(score: dict, baseline: dict, hostlist_sha256: str | None,
            threshold_points: float = DROP_THRESHOLD_POINTS) -> dict:
    """The baseline verdict: ``problems`` (what fails), ``newly_unblocked`` and a ``note``."""
    now = score["overall"]
    was = baseline["overall"]
    now_pct = 100 * now["blocked"] / now["total"] if now["total"] else 0.0
    was_pct = 100 * was["blocked"] / was["total"] if was["total"] else 0.0
    problems = []
    if was_pct - now_pct > threshold_points:
        problems.append(f"the score dropped from {_percent(was['blocked'], was['total'])}% "
                        f"({was['blocked']}/{was['total']}) to {_percent(now['blocked'], now['total'])}% "
                        f"({now['blocked']}/{now['total']}), more than the allowed {threshold_points:g} points")
    newly = sorted(set(baseline["blocked_hosts"]) & set(score["through_hosts"]))
    if newly:
        problems.append("newly unblocked (blocked in the baseline, got through now): " + ", ".join(newly))
    note = ""
    if baseline.get("hostlist_sha256") and hostlist_sha256 and baseline["hostlist_sha256"] != hostlist_sha256:
        note = ("the baseline was recorded with a different host list; the score compares the rates "
                "and only the hosts both lists have")
    return {"problems": problems, "newly_unblocked": newly, "note": note,
            "baseline": {"blocked": was["blocked"], "total": was["total"]}}


def judge(categories: dict[str, list[str]], outcomes: dict[str, dict], port: int, netlog_hosts: set[str],
          fixture_hosts: set[str], baseline: dict | None, hostlist_sha256: str | None = None,
          threshold_points: float = DROP_THRESHOLD_POINTS) -> tuple[str, str, dict]:
    """Return (status, detail, data). ``outcomes`` is ``ad_blocking.collect_outcomes`` of the page's requests.
    ``netlog_hosts`` are the hosts the network stack saw (requests and lookups), ``fixture_hosts`` the hosts the
    fixture server answered."""
    state: dict[str, str] = {}
    problems: list[str] = []
    unjudged: dict[str, str] = {}
    for hosts in categories.values():
        for host in hosts:
            oc = outcomes.get(probe_url(host, port))
            if oc is not None and oc["state"] == "blocked":
                if host in fixture_hosts:
                    problems.append(f"blocked in DevTools but the request reached the fixture server: {host}")
                elif host in netlog_hosts:
                    problems.append(f"blocked in DevTools but the NetLog has a request or lookup for it: {host}")
                state[host] = "blocked"
            elif (oc is not None and oc["state"] == "loaded") or host in fixture_hosts:
                state[host] = "through"
            else:
                why = "never requested" if oc is None else f"{oc['state']}: {oc['error'] or oc['status']}"
                unjudged[host] = why
                state[host] = "unjudged"
    if unjudged:
        problems.insert(0, f"{len(unjudged)} host(s) could not be judged (" + "; ".join(
            f"{h} {w}" for h, w in sorted(unjudged.items())) + ")")
    per_cat = {}
    for cat, hosts in categories.items():
        b = sum(1 for h in hosts if state[h] == "blocked")
        per_cat[cat] = {"blocked": b, "total": len(hosts), "percent": _percent(b, len(hosts))}
    total = sum(len(h) for h in categories.values())
    blocked_hosts = sorted(h for h, s in state.items() if s == "blocked")
    through = sorted(h for h, s in state.items() if s == "through")
    score = {"overall": {"blocked": len(blocked_hosts), "total": total,
                         "percent": _percent(len(blocked_hosts), total)},
             "categories": per_cat, "blocked_hosts": blocked_hosts, "through_hosts": through,
             "unjudged": sorted(unjudged)}
    base_report = None
    if baseline is not None:
        base_report = compare(score, baseline, hostlist_sha256, threshold_points)
        problems += base_report["problems"]
    data = {"hostlist_sha256": hostlist_sha256, "score": score, "baseline": base_report}
    o = score["overall"]
    summary = (f"{o['blocked']}/{o['total']} hosts blocked ({o['percent']}%); "
               + ", ".join(f"{c} {v['blocked']}/{v['total']} ({v['percent']}%)" for c, v in per_cat.items())
               + "; got through: " + (", ".join(through) if through else "none got through"))
    if base_report is not None and not base_report["problems"]:
        summary += f"; no worse than the baseline ({base_report['baseline']['blocked']}/{base_report['baseline']['total']})"
    if base_report is not None and base_report["note"]:
        summary += f" ({base_report['note']})"
    if problems:
        return "fail", f"{len(problems)} problem(s): " + "; ".join(problems) + ". Score: " + summary, data
    return "pass", summary, data


def _host_of(url: str) -> str:
    return (urlsplit(url).hostname or "").lower()


@register
class AdblockScore:
    name = "adblock_score"
    seeds_helium_services = False

    def __init__(self, build_root: str | Path | None = None, baseline: str | None = None,
                 online_seconds: float = 0.0, settle_seconds: float = SETTLE_SECONDS,
                 pin_path: str | Path | None = None, opener=None):
        self.build_root = Path(build_root) if build_root else None
        self.baseline_path = baseline
        self.online_seconds = online_seconds
        self.settle_seconds = settle_seconds
        self.pin_path = Path(pin_path) if pin_path else adblock_hosts.LOCK_FILE
        self.opener = opener
        self.categories: dict[str, list[str]] = {}
        self.hostlist: adblock_hosts.HostList | None = None
        self.pin: adblock_hosts.Pin | None = None
        self._skip: str | None = None
        self._error: str | None = None
        self._port = 0
        self._online: dict | None = None
        self._live: dict | None = None

    def browser_flags(self, server) -> tuple[str, ...]:
        """Resolve the pinned list and map every host in it to the fixture server (asked once, before launch)."""
        self._port = server.port
        try:
            self.pin = adblock_hosts.load_pin(self.pin_path)
            kwargs = {"opener": self.opener} if self.opener else {}
            self.hostlist = adblock_hosts.obtain(self.pin, self.build_root, **kwargs)
            self.categories = adblock_hosts.parse(self.hostlist.text)
        except adblock_hosts.HostListUnavailable as exc:
            self._skip = str(exc)
            return ()
        except adblock_hosts.HostListError as exc:
            self._error = str(exc)
            return ()
        return (adblock_hosts.resolver_flag([h for hosts in self.categories.values() for h in hosts]),)

    def _baseline(self) -> dict | None:
        return load_baseline(self.baseline_path) if self.baseline_path else None

    def run(self, session: Session) -> CheckResult:
        if self.baseline_path:
            self._baseline()  # a baseline that cannot be read fails before the browser does any work
        if self.online_seconds > 0:
            self._online = self._run_online(session)
        if self._error:
            return CheckResult(self.name, "error", self._error)
        if self._skip:
            return CheckResult(self.name, "skip", self._skip)
        hosts = [h for hs in self.categories.values() for h in hs]
        session.cdp("Network.enable")
        session.navigate(PAGE)
        session.evaluate(f"window.runAdblockTest({json.dumps(hosts)}, {self._port})")
        events: list[dict] = []
        deadline = time.monotonic() + self.settle_seconds
        while True:
            for method in ("Network.requestWillBeSent", "Network.responseReceived", "Network.loadingFailed"):
                events += session.take_events(method)
            outcomes = collect_outcomes(sorted(events, key=_event_rank))
            done = bool(session.evaluate("window.adblockDone === true"))
            if (done and not any(o["state"] == "pending" for o in outcomes.values())) \
                    or time.monotonic() > deadline:
                break
            time.sleep(0.2)
        self._live = {"outcomes": outcomes}
        return CheckResult(self.name, "pending")

    def _run_online(self, session: Session) -> dict:
        """The live page in a second browser (the main one resolves nothing but loopback)."""
        try:
            if session.variant_launcher is None:
                return {"state": "error", "detail": "this session cannot start a second browser"}
            with session.variant(ONLINE_FLAGS, None) as live:
                return adblock_online.read_live(live, self.online_seconds)
        except Exception as exc:  # informational: never fails the train
            return {"state": "error", "detail": f"{type(exc).__name__}: {exc}"}

    def finalize(self, session: Session, result: CheckResult) -> CheckResult:
        online = self._online
        if self._live is None:  # skipped, or run() failed: keep its result, add the online run if there was one
            if online is not None:
                result.data = {**result.data, "online": online}
                result.detail = (result.detail + " | " if result.detail else "") + "online (informational): " + _online_line(online)
            return result
        netlog_hosts = {_host_of(u) for u in netlog.all_urls(session.netlog)}
        netlog_hosts |= {l["host"] for l in session.netlog_lookups}
        listed = {h for hs in self.categories.values() for h in hs}
        fixture_hosts = set()
        for r in session.fixtures.snapshot():
            if r["path"].split("?", 1)[0] == PROBE_PATH:
                fixture_hosts.add(r["host"].split(":", 1)[0].lower())
        try:
            baseline = self._baseline()
        except BaselineError as exc:
            return CheckResult(self.name, "error", str(exc), data=result.data)
        status, detail, data = judge(self.categories, self._live["outcomes"], self._port, netlog_hosts & listed,
                                     fixture_hosts, baseline, self.pin.sha256 if self.pin else None)
        data["hostlist"] = {"url": self.pin.url, "sha256": self.pin.sha256, "date": self.pin.date,
                            "source": self.hostlist.source, "note": self.hostlist.note, "hosts": len(listed)}
        data["other_external_hosts"] = sorted({r["host"] for r in netlog.external(session.netlog)
                                               if r.get("host") and r["host"] not in listed})
        if online is not None:
            data["online"] = online
            detail += " | online (informational): " + _online_line(online)
        (session.out_dir / ARTIFACT).write_text(json.dumps(score_document(data), indent=1) + "\n")
        session.artifacts.append(ARTIFACT)
        return CheckResult(self.name, status, detail, data=data)


def _online_line(online: dict) -> str:
    if online.get("state") == "finished":
        return f"{online['blocked']}/{online['total']} blocked ({online['percent']}%) on {online['url']}"
    return f"{online.get('state')}: {online.get('detail', '')}"
