"""Zero Google and Helium traffic: the M1 promise, proven on a real browser.

One browser run, three phases, one verdict:

* ``first_launch``: a fresh profile, the first fixture page, then ``settle_seconds`` of quiet
  (default 60). Startup requests land here.
* ``idle``: ``idle_seconds`` with nothing happening (default 300, the issue's 5 minutes). Timers
  fire here: component updater, extension updater, uBlock Origin, crash and update checks.
* ``browse``: the non-Google fixture script: three local pages, a click, and a nudge to the
  extension updater through ``chrome://extensions``.

The verdict is computed after the browser has exited, from the NetLog (the browser's own record,
which also holds browser-initiated and extension requests, and the host names of DNS, preconnect and
socket events). It is INCONCLUSIVE, and fails, unless the NetLog is whole and the browser exited with
code 0: a crash or kill loses the tail of the log, which may hold the traffic that matters. It fails if any request matches the
denylist (``denylist/google-helium.txt``, documented in ``docs/network-denylist.md``), or if the
capture is empty: a run that saw not even its own fixture pages proves nothing.

The profile is seeded as the worst case for a Helium-lineage browser: the user consented to
Helium services and switched every service on. Unmodified Helium then calls its services proxy
and fails this check. Aerie must stay silent anyway, because its code no longer has an origin
to call, whatever the prefs say. Run the same check on an unmodified Helium binary (or any
browser that talks to Google) with ``--baseline`` to see it fail.

The network service's request policy (issue #257, ``docs/network-endpoints.md``) refuses a request that the
browser makes on its own unless it is on a reviewed allowlist, with net error -22 (``ERR_BLOCKED_BY_ADMINISTRATOR``)
and a count in the ``Aerie.Network.RequestPolicy.Refused`` histogram. The check names every such attempt
(host only) from the NetLog and reads the counter at the end of the run. A refused attempt never left the
machine, so it does not fail the run, but it shows that the browser or a later upstream version tried: it
fails in strict mode, and the report lists it. If the policy refuses something the browser needs, that is a bug.

Requests to the ``.qjz9zk`` dummy TLD (and ``trk:`` URLs) are refused inside the network stack
before any DNS query. They are listed as ``blocked`` and fail the run only in strict mode.
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field

from .. import crash_step
from .. import denylist as denylist_mod
from .. import netlog
from ..cdp import CdpError
from ..session import CheckResult, Session
from . import register

PHASES = ("first_launch", "idle", "browse")
# The request policy's refusal: net::ERR_BLOCKED_BY_ADMINISTRATOR, which nothing else in the browser returns
# for a network request, and its counter (services/network/aerie/request_policy.h).
POLICY_NET_ERROR = -22
REFUSAL_HISTOGRAM = "Aerie.Network.RequestPolicy.Refused"
BROWSE_PAGES = ("/browse-a.html", "/browse-b.html")


@dataclass
class Verdict:
    status: str
    reason: str
    hits: list[dict] = field(default_factory=list)
    blocked: list[dict] = field(default_factory=list)
    other_hosts: list[str] = field(default_factory=list)
    fixture_requests: int = 0
    # Attempts the request policy refused, by host: {host, phase, net_error, group}. Never a path or query.
    refused: list[dict] = field(default_factory=list)
    # The policy's counter as read from the browser; None when it could not be read.
    refusal_count: int | None = None


def _phase_of(index: int, phase_ends: dict[str, int] | None) -> str | None:
    if not phase_ends:
        return None
    last = None
    for name, end in phase_ends.items():
        last = name
        if index < end:
            return name
    return last


def evaluate(requests: list[dict], dl: denylist_mod.Denylist, strict: bool = False,
             phase_ends: dict[str, int] | None = None, lookups: list[dict] | None = None,
             refusal_count: int | None = None) -> Verdict:
    """Judge parsed NetLog requests. ``phase_ends`` maps phase name to the cumulative request count
    at its end, in order; requests are assigned to the first phase whose end is past them."""
    hits: list[dict] = []
    blocked: list[dict] = []
    refused: list[dict] = []
    others: set[str] = set()
    fixture_requests = 0
    for i, r in enumerate(requests):
        phase = _phase_of(i, phase_ends)
        base = {"url": r["url"], "host": r["host"], "phase": phase, "net_error": r.get("net_error")}
        if r.get("scheme") == "trk":
            blocked.append(base)
            continue
        if r["scheme"] in ("http", "https") and not r["external"]:
            fixture_requests += 1
            continue
        if not r["external"]:
            continue
        if r.get("net_error") == POLICY_NET_ERROR:
            # The last hop was refused by the request policy before any DNS query: it left nothing. The hops
            # before it were made, and count like any other request.
            urls = r.get("urls", [r["url"]])
            for u in urls[:-1]:
                if (match := dl.match_url(u)):
                    hits.append({**base, "url": u, "host": match.host, "group": match.group, "pattern": match.pattern})
            last = dl.match_url(urls[-1])
            refused.append({"host": r["host"], "phase": phase, "net_error": POLICY_NET_ERROR,
                            "group": last.group if last else None})
            continue
        # Every hop of a redirect counts, not just the first URL.
        matches = [(u, m) for u in r.get("urls", [r["url"]]) if (m := dl.match_url(u))]
        if matches:
            for u, match in matches:
                hits.append({**base, "url": u, "host": match.host, "group": match.group, "pattern": match.pattern})
        elif denylist_mod.is_dummy_host(r["host"]):
            blocked.append(base)
        else:
            others.add(r["host"])
    # Host names the browser resolved, preconnected or sent to a DoH server, without a URL request.
    requested = {h["host"] for h in hits}
    for lk in lookups or []:
        match = dl.match_host(lk["host"])
        if match and match.host not in requested:
            requested.add(match.host)
            hits.append({"url": f"dns://{match.host}", "host": match.host, "phase": None, "net_error": None,
                         "group": match.group, "pattern": match.pattern, "via": "dns", "event": lk.get("event")})
        elif (not match and denylist_mod.normalise_host(lk["host"]) not in (None, "localhost")
              and not denylist_mod.is_dummy_host(lk["host"]) and lk["host"] not in {r["host"] for r in requests}):
            others.add(lk["host"])
    other_hosts = sorted(others)

    def verdict(status: str, reason: str) -> Verdict:
        return Verdict(status, reason, hits, blocked, other_hosts, fixture_requests, refused, refusal_count)

    if hits:
        shown = ", ".join(f"{h['host']} ({h['group']}, {h['phase'] or 'run'})" for h in hits[:5])
        more = f" and {len(hits) - 5} more" if len(hits) > 5 else ""
        return verdict("fail", f"{len(hits)} request(s) to denylisted hosts: {shown}{more}")
    if fixture_requests == 0:
        return verdict("fail", "no fixture page request in the NetLog: the capture saw nothing, "
                       "so a pass would prove nothing")
    names = sorted({r["host"] for r in refused})
    unnamed = max(0, (refusal_count or 0) - len(refused))
    refusal_note = ""
    if refused:
        refusal_note += f"; {len(refused)} attempt(s) refused by the request policy: {', '.join(names)}"
    if unnamed:
        refusal_note += f"; {refusal_count} refusal(s) counted, {len(refused)} named"
    if strict and (refused or unnamed):
        return verdict("fail", f"strict: the request policy refused {max(len(refused), refusal_count or 0)} attempt(s)"
                       + (f": {', '.join(names)}" if names else " (not named in the NetLog)"))
    if strict and blocked:
        return verdict("fail", f"strict: {len(blocked)} blocked attempt(s) to dummy or trk: hosts, "
                       f"e.g. {blocked[0]['url']}")
    note = f"; {len(blocked)} blocked attempt(s) to dummy hosts" if blocked else ""
    return verdict("pass", f"no request to a denylisted host in {len(requests)} URL request(s){note}{refusal_note}")


def worst_case_prefs() -> dict:
    """A profile whose user said yes to every Helium service. Note: no origin override, so the
    only origin a Helium build can call is the one compiled into it."""
    return {"helium": {"services": {
        "enabled": True, "user_consented": True, "schema_version": 1, "disable_schema_alerts": True,
        "ext_proxy": True, "bangs": True, "browser_updates": True, "spellcheck_files": True,
        "ublock_assets": True}}}


@register
class ZeroTraffic:
    name = "zero_traffic"
    seeds_helium_services = False

    def __init__(self, idle_seconds: float = 300.0, settle_seconds: float = 60.0, strict: bool = False,
                 denylist_path: str | None = None, crash: bool = True):
        self.idle_seconds = float(idle_seconds)
        self.settle_seconds = float(settle_seconds)
        self.strict = strict
        self.denylist = (denylist_mod.load(denylist_path) if denylist_path else denylist_mod.load_default())
        self.phase_ends: dict[str, int] = {}
        self.probe_error: str | None = None
        self.refusal_count: int | None = None
        self.counter_error: str | None = None
        # The crash step (issue #262): crash a browser on purpose and start it again. It runs two more
        # browsers; ``crash=False`` (``--zero-traffic-no-crash``) leaves it out.
        self.crash = crash
        self.crash_verdict: tuple[str, str, dict] | None = None

    def profile_prefs(self, server) -> dict:
        return worst_case_prefs()

    def _count(self, session: Session) -> int:
        return len(netlog.parse_file(session.netlog_path)) if session.netlog_path else 0

    def run(self, session: Session) -> CheckResult:
        session.navigate("/index.html")
        time.sleep(self.settle_seconds)
        self.phase_ends["first_launch"] = self._count(session)
        time.sleep(self.idle_seconds)
        self.phase_ends["idle"] = self._count(session)
        session.navigate("/index.html")
        session.click("#go")
        for page in BROWSE_PAGES:
            session.navigate(page)
        self._poke_extension_updater(session)
        self.phase_ends["browse"] = self._count(session)
        self.refusal_count = self.read_refusal_counter(session)
        if self.crash:
            self.crash_verdict = crash_step.judge(
                crash_step.observe(session, settle_seconds=min(self.settle_seconds, 5.0)), self.denylist)
        return CheckResult(self.name, "pending")

    def read_refusal_counter(self, session: Session) -> int | None:
        """How many requests the request policy refused, from its histogram. 0 when the histogram was never
        recorded (DevTools says "not found"); None when the reading itself failed."""
        try:
            hist = session.client.send("Browser.getHistogram", {"name": REFUSAL_HISTOGRAM},
                                       timeout=session.timeout)["histogram"]
            return int(hist["count"])
        except CdpError as exc:
            self.counter_error = str(exc)
            msg = str(exc).lower()
            return 0 if ("not found" in msg or "cannot find" in msg) else None
        except (OSError, KeyError, TypeError, ValueError) as exc:
            self.counter_error = f"{type(exc).__name__}: {exc}"
            return None

    def _poke_extension_updater(self, session: Session) -> None:
        """Ask for an extension update check from chrome://extensions, so that path is exercised
        in the run. Best effort: the verdict never depends on it working."""
        try:
            sid = session._page()
            session.client.drain("Page.loadEventFired")
            session.client.send("Page.navigate", {"url": "chrome://extensions"}, session_id=sid,
                                timeout=session.timeout)
            session.client.wait_event("Page.loadEventFired", session_id=sid, timeout=session.timeout)
            session.client.send(
                "Runtime.evaluate",
                {"expression": "new Promise(r => chrome.developerPrivate.autoUpdate(() => r(true)))",
                 "awaitPromise": True, "returnByValue": True},
                session_id=sid, timeout=min(session.timeout, 15.0))
            time.sleep(2.0)
        except (CdpError, OSError, KeyError) as exc:
            self.probe_error = f"{type(exc).__name__}: {exc}"

    def finalize(self, session: Session, result: CheckResult) -> CheckResult:
        # The last phase also takes whatever the NetLog flushed after the final poll.
        ends = dict(self.phase_ends)
        if ends:
            ends[PHASES[-1]] = len(session.netlog) + 1
        verdict = evaluate(session.netlog, self.denylist, strict=self.strict, phase_ends=ends or None,
                           lookups=getattr(session, "netlog_lookups", None),
                           refusal_count=self.refusal_count)
        # A pass needs a log that is whole and a browser that exited cleanly. A truncated log or a crash
        # may have lost exactly the traffic we are looking for.
        problems = []
        if not getattr(session, "netlog_complete", False):
            problems.append("the NetLog is truncated or missing")
        if session.exit.get("code") != 0:
            problems.append(f"the browser did not exit cleanly (exit code {session.exit.get('code')})")
        inconclusive = bool(problems)
        status, reason = verdict.status, verdict.reason
        if inconclusive:
            status = "fail"
            reason = ("INCONCLUSIVE: " + " and ".join(problems) + "; a pass would prove nothing"
                      + (f". Seen so far: {verdict.reason}" if verdict.hits else ""))
        if self.crash_verdict is not None:
            crash_status, crash_reason, _ = self.crash_verdict
            reason = f"{reason}; crash step: {crash_reason}" if reason else f"crash step: {crash_reason}"
            if crash_status != "pass":
                status = "fail"
        data = {
            "phases": {name: ends.get(name) for name in PHASES} if ends else {},
            "idle_seconds": self.idle_seconds, "settle_seconds": self.settle_seconds,
            "strict": self.strict, "denylist_entries": len(self.denylist.entries),
            "hits": verdict.hits, "blocked": verdict.blocked, "refused": verdict.refused,
            "refusal_counter": verdict.refusal_count, "refusal_counter_error": self.counter_error, "other_hosts": verdict.other_hosts,
            "fixture_requests": verdict.fixture_requests, "extension_update_probe_error": self.probe_error,
            "inconclusive": inconclusive, "netlog_complete": bool(getattr(session, "netlog_complete", False)),
            "crash_step": ({"status": self.crash_verdict[0], "detail": self.crash_verdict[1], **self.crash_verdict[2]}
                           if self.crash_verdict is not None else {"status": "skipped"}),
        }
        return CheckResult(self.name, status, reason, data=data)
