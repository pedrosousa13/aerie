"""Ad blocking: the Filter Engine blocks known ad and tracker requests and leaves first-party content alone.

The fixture page (`fixtures/blocking/index.html`) asks for real ad and tracker
URLs of several types and for two first-party files. `fixtures/blocking/
expected.json` says which is which. The Bench resolves nothing but loopback,
so a request that is not blocked fails with ``net::ERR_NAME_NOT_RESOLVED`` and
shows up in the NetLog, while a blocked one fails with
``net::ERR_BLOCKED_BY_CLIENT`` and never reaches the network stack. That
difference is what the check reads:

* each expected-blocked URL: DevTools reports ``loadingFailed`` with
  ``net::ERR_BLOCKED_BY_CLIENT``, and the NetLog has no request for it;
* each expected-allowed path: DevTools reports a 200 response, and the fixture
  server saw the request;
* redirects: ``expected.json`` also lists first-party URLs that redirect. The page asks for
  ``/blocking/go-tracker`` (a first-party URL that the fixture server redirects to a listed
  tracker) and ``/blocking/go-ok.js`` (redirects to first-party content). The first URL
  answers 302 and the redirect target of the first must be blocked at the hop
  (``ERR_BLOCKED_BY_CLIENT``, never in the NetLog); the second must be followed and load;
* the first-party script ran (``window.firstPartyScriptLoaded``);
* the blocking is the Aerie Filter Engine's. Helium still ships uBlock Origin,
  which blocks the same URLs with the same error, so a pass must also show the
  engine's own signals: ``Aerie.Adblock.EngineLoadSuccess`` has a true sample
  and no false one, and ``Aerie.Adblock.RequestBlocked`` has at least as many
  true samples as the page has expected-blocked URLs. A request that uBlock
  Origin stops never reaches the engine (the extension's proxy comes first), so
  uBlock Origin alone cannot make these true. A third counter,
  ``Aerie.Adblock.RedirectHopBlocked``, is recorded only for redirect hop verdicts, and the
  page needs at least one true sample per expected blocked redirect.

  Limit: histograms carry no URLs. The proof that THIS hop was the engine's is the
  combination of the hop's own ``ERR_BLOCKED_BY_CLIENT`` in DevTools, the target never
  reaching the NetLog (every hop of every request is read), and the engine having
  blocked at least that many hops. Another blocker that also stops hops could in principle
  cover for an engine that missed one hop only if the counters were satisfied by other
  hops; the fixture has one blocked chain, so each counter maps to it.

It also records, with no threshold, the page timings, the memory of the
browser's process tree and the engine load time (the histogram
``Aerie.Adblock.EngineLoadTime``), so later work can set limits.

The decision logic is in plain functions (``collect_outcomes``, ``judge``) that
the tests run against recorded DevTools events.
"""

from __future__ import annotations

import json
import time
from pathlib import Path

from .. import netlog, procmem
from ..fixtures import FIXTURE_DIR
from ..session import CheckResult, Session
from . import register

BLOCKED_ERROR = "net::ERR_BLOCKED_BY_CLIENT"
EXPECTED_FILE = FIXTURE_DIR / "blocking" / "expected.json"
SETTLE_SECONDS = 15.0
ENGINE_HISTOGRAM = "Aerie.Adblock.EngineLoadTime"
LOAD_SUCCESS_HISTOGRAM = "Aerie.Adblock.EngineLoadSuccess"
BLOCKED_HISTOGRAM = "Aerie.Adblock.RequestBlocked"
HOP_HISTOGRAM = "Aerie.Adblock.RedirectHopBlocked"
RECORDING_NAME = "ad_blocking-recording.json"


def load_expected(path: Path = EXPECTED_FILE) -> dict:
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    if not data.get("blocked") or not data.get("allowed") or not data.get("page"):
        raise ValueError(f"{path}: needs 'page', 'blocked' and 'allowed'")
    return data


def collect_outcomes(events: list[dict]) -> dict[str, dict]:
    """Fold CDP Network events into one outcome per request URL (the last request for a URL wins).

    Outcome: ``state`` is "blocked" (loadingFailed with ERR_BLOCKED_BY_CLIENT), "failed" (any other
    loadingFailed), "loaded" (a response arrived), "redirected" (a URL that answered with a redirect:
    DevTools sends the next hop as another requestWillBeSent with the same requestId and a
    ``redirectResponse``) or "pending"; ``error``, ``status``, ``type``.
    """
    current: dict[str, str] = {}  # requestId -> the URL of its latest hop
    outcomes: dict[str, dict] = {}
    for event in events:
        params = event.get("params", {})
        rid = params.get("requestId")
        method = event.get("method")
        if rid is None:
            continue
        if method == "Network.requestWillBeSent":
            url = params.get("request", {}).get("url", "")
            redirect = params.get("redirectResponse")
            if redirect and rid in current and current[rid] in outcomes:
                hop = outcomes[current[rid]]
                hop["state"], hop["status"] = "redirected", redirect.get("status")
            current[rid] = url
            outcomes[url] = {"url": url, "type": params.get("type"), "state": "pending", "error": None,
                             "status": None}
        elif rid in current and current[rid] in outcomes:
            entry = outcomes[current[rid]]
            if method == "Network.responseReceived":
                entry["status"] = params.get("response", {}).get("status")
                entry["state"] = "loaded"
            elif method == "Network.loadingFailed":
                entry["error"] = params.get("errorText")
                entry["state"] = "blocked" if entry["error"] == BLOCKED_ERROR else "failed"
    return outcomes


def is_settled(expected: dict, outcomes: dict[str, dict]) -> bool:
    """True once every expected request has a final outcome (not pending, not unseen)."""
    wanted = [b["url"] for b in expected["blocked"]]
    for url in wanted:
        if outcomes.get(url, {}).get("state", "pending") == "pending":
            return False
    for item in expected["allowed"]:
        if not any(u.endswith(item["path"]) and o["state"] != "pending" for u, o in outcomes.items()):
            return False
    redirects = expected.get("redirects", {})
    for item in redirects.get("blocked", []):
        if outcomes.get(item["target"], {}).get("state", "pending") == "pending":
            return False
    for item in redirects.get("allowed", []):
        if not any(u.endswith(item["final"]) and o["state"] not in ("pending", "redirected")
                   for u, o in outcomes.items()):
            return False
    return True


def boolean_counts(hist: dict) -> dict:
    """{"true": n, "false": m} from a DevTools histogram of a boolean metric."""
    counts = {"true": 0, "false": 0}
    for bucket in hist.get("buckets", []):
        counts["true" if bucket.get("low") == 1 else "false"] += bucket.get("count", 0)
    return counts


def read_boolean(session: Session, name: str) -> dict | None:
    """Counts of a boolean histogram, or None if the browser has no such histogram."""
    try:
        return boolean_counts(session.client.send("Browser.getHistogram", {"name": name},
                                                  timeout=session.timeout)["histogram"])
    except Exception:
        return None


def judge_engine(expected: dict, engine: dict | None) -> tuple[list[str], dict]:
    """Problems with the claim "the Aerie engine did the blocking", and what was read."""
    engine = engine or {}
    load, blocked = engine.get("load_success"), engine.get("requests")
    hops = engine.get("redirect_hops")
    problems = []
    if load is None:
        problems.append(f"no {LOAD_SUCCESS_HISTOGRAM} histogram: this browser has no Aerie Filter Engine")
    elif load["true"] < 1 or load["false"] > 0:
        problems.append(f"the Aerie Filter Engine did not load cleanly ({LOAD_SUCCESS_HISTOGRAM}: "
                        f"{load['true']} true, {load['false']} false)")
    need = len(expected["blocked"]) + len(expected.get("redirects", {}).get("blocked", []))
    if blocked is None:
        problems.append(f"no {BLOCKED_HISTOGRAM} histogram: nothing shows that the engine judged any request")
    elif blocked["true"] < need:
        problems.append(
            f"the Aerie Filter Engine blocked {blocked['true']} request(s), the page needs {need} "
            f"({BLOCKED_HISTOGRAM}); the rest were blocked by something else (Helium's bundled "
            "uBlock Origin?), which does not count")
    hops_needed = len(expected.get("redirects", {}).get("blocked", []))
    if hops_needed:
        if hops is None:
            problems.append(f"no {HOP_HISTOGRAM} histogram: nothing shows that the engine judged a redirect hop")
        elif hops["true"] < hops_needed:
            problems.append(f"the Aerie Filter Engine blocked {hops['true']} redirect hop(s), the page needs "
                            f"{hops_needed} ({HOP_HISTOGRAM})")
    return problems, {"load_success": load, "requests": blocked, "redirect_hops": hops}


def judge_redirects(redirects: dict, outcomes: dict[str, dict], netlog_urls: set[str], fixture_paths: list[str],
                    fixture_redirects: list[str] | None, problems: list[str]) -> dict:
    """Check the redirect chains of expected.json; add what fails to ``problems``; return a report."""
    report: dict = {"blocked": [], "allowed": []}
    seen_302 = fixture_redirects if fixture_redirects is not None else None
    for item in redirects.get("blocked", []):
        source = next((o for u, o in outcomes.items() if u.endswith(item["path"])), None)
        target = outcomes.get(item["target"])
        report["blocked"].append({
            "path": item["path"], "target": item["target"],
            "source_state": source["state"] if source else "not requested",
            "state": target["state"] if target else "not requested",
            "error": target["error"] if target else None, "in_netlog": item["target"] in netlog_urls})
        if source is None or target is None:
            problems.append(f"redirect chain never requested by the page: {item['path']} -> {item['target']}")
        elif source["state"] != "redirected" or source["status"] != 302:
            problems.append(f"redirect source did not answer 302 ({source['state']}, {source['status']}): {item['path']}")
        elif target["state"] != "blocked":
            problems.append(f"redirect NOT BLOCKED at the hop ({target['state']}, {target['error']}): "
                            f"{item['path']} -> {item['target']}")
        elif item["target"] in netlog_urls:
            problems.append(f"redirect target blocked in DevTools but it reached the network stack: {item['target']}")
        elif seen_302 is not None and item["path"] not in seen_302:
            problems.append(f"the fixture server never saw the redirecting URL: {item['path']}")
    for item in redirects.get("allowed", []):
        source = next((o for u, o in outcomes.items() if u.endswith(item["path"])), None)
        final = next((o for u, o in outcomes.items() if u.endswith(item["final"])), None)
        report["allowed"].append({
            "path": item["path"], "final": item["final"],
            "source_state": source["state"] if source else "not requested",
            "state": final["state"] if final else "not requested",
            "status": final["status"] if final else None})
        if source is None or final is None:
            problems.append(f"redirect chain never requested by the page: {item['path']} -> {item['final']}")
        elif final["state"] != "loaded" or final["status"] != 200:
            problems.append(f"first-party redirect target did not load ({final['state']}, "
                            f"{final['error'] or final['status']}): {item['path']} -> {item['final']}")
        elif item["final"] not in fixture_paths:
            problems.append(f"redirect target loaded in DevTools but the fixture server never saw it: {item['final']}")
    return report


def judge(expected: dict, outcomes: dict[str, dict], netlog_urls: set[str], fixture_paths: list[str],
          first_party_script_ran: bool | None, engine: dict | None,
          fixture_redirects: list[str] | None = None) -> tuple[str, str, dict]:
    """Return (status, detail, data). Every expectation that fails is named."""
    problems: list[str] = []
    blocked_report, allowed_report = [], []
    for item in expected["blocked"]:
        url = item["url"]
        outcome = outcomes.get(url)
        row = {"url": url, "state": outcome["state"] if outcome else "not requested",
               "error": outcome["error"] if outcome else None, "in_netlog": url in netlog_urls}
        blocked_report.append(row)
        if outcome is None:
            problems.append(f"never requested by the page: {url}")
        elif outcome["state"] != "blocked":
            problems.append(f"NOT BLOCKED ({outcome['state']}, {outcome['error'] or outcome['status']}): {url}")
        elif url in netlog_urls:
            problems.append(f"blocked in DevTools but the request still reached the network stack: {url}")
    for item in expected["allowed"]:
        path = item["path"]
        matches = [o for u, o in outcomes.items() if u.endswith(path)]
        outcome = matches[-1] if matches else None
        row = {"path": path, "state": outcome["state"] if outcome else "not requested",
               "status": outcome["status"] if outcome else None, "fixture_saw_it": path in fixture_paths}
        allowed_report.append(row)
        if outcome is None:
            problems.append(f"first-party request never made: {path}")
        elif outcome["state"] != "loaded" or outcome["status"] != 200:
            problems.append(f"first-party content did not load ({outcome['state']}, {outcome['error'] or outcome['status']}): {path}")
        elif path not in fixture_paths:
            problems.append(f"DevTools says it loaded but the fixture server never saw it: {path}")
    redirect_report = judge_redirects(expected.get("redirects", {}), outcomes, netlog_urls, fixture_paths,
                                      fixture_redirects, problems)
    if first_party_script_ran is False:
        problems.append("the first-party script did not run (window.firstPartyScriptLoaded is not set)")
    engine_problems, engine_data = judge_engine(expected, engine)
    problems += engine_problems
    data = {"blocked": blocked_report, "allowed": allowed_report, "first_party_script_ran": first_party_script_ran,
            "engine": engine_data, "redirects": redirect_report}
    n_blocked = sum(1 for r in blocked_report if r["state"] == "blocked" and not r["in_netlog"])
    n_redirects = len(redirect_report["blocked"]) + len(redirect_report["allowed"])
    if problems:
        return "fail", f"{len(problems)} problem(s): " + "; ".join(problems), data
    return "pass", (f"{n_blocked}/{len(blocked_report)} ad and tracker requests blocked, "
                    f"{len(allowed_report)}/{len(allowed_report)} first-party requests loaded, "
                    f"{n_redirects} redirect chain(s) judged at the hop"), data


def _histogram_ms(session: Session) -> dict | None:
    try:
        hist = session.client.send("Browser.getHistogram", {"name": ENGINE_HISTOGRAM}, timeout=session.timeout)["histogram"]
    except Exception:
        return None
    count = hist.get("count", 0)
    return {"count": count, "sum_ms": hist.get("sum"), "mean_ms": (hist["sum"] / count) if count else None}


@register
class AdBlocking:
    name = "ad_blocking"
    seeds_helium_services = False

    def __init__(self, settle_seconds: float = SETTLE_SECONDS, expected_path: str | None = None):
        self.settle_seconds = settle_seconds
        self.expected = load_expected(Path(expected_path) if expected_path else EXPECTED_FILE)

    def run(self, session: Session) -> CheckResult:
        session.cdp("Network.enable")
        session.cdp("Performance.enable")
        session.navigate(self.expected["page"])
        events: list[dict] = []
        deadline = time.monotonic() + self.settle_seconds
        while True:
            for method in ("Network.requestWillBeSent", "Network.responseReceived", "Network.loadingFailed"):
                events += session.take_events(method)
            outcomes = collect_outcomes(sorted(events, key=_event_rank))
            if is_settled(self.expected, outcomes) or time.monotonic() > deadline:
                break
            time.sleep(0.2)
        ran = session.evaluate("window.firstPartyScriptLoaded === true")
        nav = session.evaluate(
            "(()=>{const n=performance.getEntriesByType('navigation')[0];"
            "return n?{dom_content_loaded_ms:n.domContentLoadedEventEnd,load_event_ms:n.loadEventEnd,"
            "response_end_ms:n.responseEnd,transfer_size:n.transferSize}:null})()")
        resources = session.evaluate("performance.getEntriesByType('resource').length")
        session.screenshot("ad_blocking.png")
        self._live = {
            "events": sorted(events, key=_event_rank),
            "outcomes": outcomes,
            "first_party_script_ran": bool(ran),
            "engine": {"load_success": read_boolean(session, LOAD_SUCCESS_HISTOGRAM),
                       "requests": read_boolean(session, BLOCKED_HISTOGRAM),
                       "redirect_hops": read_boolean(session, HOP_HISTOGRAM)},
            "metrics": {
                "page_timing": nav,
                "resource_entries": resources,
                "memory": procmem.tree_rss_kb(session.browser_pid) if session.browser_pid else None,
                "engine_load": _histogram_ms(session),
                "note": "recorded only; no threshold yet",
            },
        }
        return CheckResult(self.name, "pending")

    def finalize(self, session: Session, result: CheckResult) -> CheckResult:
        live = getattr(self, "_live", None)
        if live is None:
            return result  # run() failed; keep its error
        netlog_urls = netlog.all_urls(session.netlog)  # every hop of every request
        seen = session.fixtures.snapshot()
        fixture_paths = [r["path"].split("?", 1)[0] for r in seen if r["status"] == 200]
        fixture_redirects = [r["path"].split("?", 1)[0] for r in seen if r["status"] == 302]
        status, detail, data = judge(self.expected, live["outcomes"], netlog_urls, fixture_paths,
                                     live["first_party_script_ran"], live["engine"], fixture_redirects)
        data["metrics"] = live["metrics"]
        # Everything judge() saw, in the format the tests' recorded fixtures use
        # (tests/data/ad_blocking/). Copy a real run's file there to refresh them.
        recording = {"events": live["events"], "netlog_urls": sorted(netlog_urls),
                     "fixture_paths": fixture_paths, "fixture_redirects": fixture_redirects,
                     "first_party_script_ran": live["first_party_script_ran"], "engine": live["engine"]}
        (session.out_dir / RECORDING_NAME).write_text(json.dumps(recording, indent=1) + "\n")
        session.artifacts.append(RECORDING_NAME)
        return CheckResult(self.name, status, detail, data=data)


_RANK = {"Network.requestWillBeSent": 0, "Network.responseReceived": 1, "Network.loadingFailed": 2}


def _event_rank(event: dict) -> int:
    """Order events so a request's requestWillBeSent comes before its response or failure."""
    return _RANK.get(event.get("method"), 3)
