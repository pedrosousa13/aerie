"""No prefetch, prerender, preconnect or speculation rules."""

from __future__ import annotations

import time

from ...session import Session
from .. import register
from .common import HardeningCheck, Verdict, site

HINT_PREFIX = "/hardening/hint/"
CONTROL_PATH = HINT_PREFIX + "control.txt"
PRELOADING_ON_PREFS = {"net": {"network_prediction_options": 0}}  # Standard preloading
OBSERVE_SECONDS = 5.0

FETCH_CONTROLS_JS = """(async (probe) => {
  await fetch('%s').catch(() => {});
  await fetch(location.protocol + '//127.0.0.1:' + probe, {mode: 'no-cors'}).catch(() => {});
  return true;
})(%d)"""


def observe_hints(session: Session) -> dict:
    with session.tcp_probe() as probe:
        host = f"a.localhost:{session.fixtures.port}"
        session.navigate_url(f"{site(session, 'a')}/hardening/prefetch.html?probe={probe.port}")
        time.sleep(OBSERVE_SECONDS)
        hits = [r for r in session.fixtures.snapshot()
                if r["host"] == host and r["path"].startswith(HINT_PREFIX) and not r["path"].startswith(CONTROL_PATH)]
        before = probe.connections
        session.evaluate(FETCH_CONTROLS_JS % (CONTROL_PATH, probe.port), await_promise=True)
        time.sleep(1.0)
        control_hits = [r for r in session.fixtures.snapshot() if r["path"].startswith(CONTROL_PATH)]
        return {"hint_hits": [f"{r['path']} purpose={r['sec_purpose'] or r['purpose'] or '-'}" for r in hits],
                "probe_connections": before, "control_hits": len(control_hits),
                "probe_after_control": probe.connections}


@register
class Prefetch(HardeningCheck):
    name = "hardening_prefetch"
    control = "prefetch"
    negative_control = ("The page then fetches a control URL and opens a control connection itself, so the "
                        "server and the TCP listener are proven able to see requests; a second browser with "
                        "preloading on must show at least one speculative load for the same hints.")

    def observe(self, session: Session) -> dict:
        main = observe_hints(session)
        with session.variant((), PRELOADING_ON_PREFS) as v:
            control = observe_hints(v)
        return {"main": main, "control": control}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        main, control = obs["main"], obs["control"]
        if main["control_hits"] < 1 or main["probe_after_control"] <= main["probe_connections"]:
            return "fail", ("detector failed: the control fetch or control connection was not seen "
                            f"(requests {main['control_hits']}, connections {main['probe_connections']} -> "
                            f"{main['probe_after_control']})")
        if main["hint_hits"] or main["probe_connections"]:
            return "fail", (f"speculative loads happened: {main['hint_hits']} and "
                            f"{main['probe_connections']} preconnect connection(s)")
        if not control["hint_hits"] and not control["probe_connections"]:
            return "fail", ("negative control failed: even with preloading on, the hints caused no request "
                            "and no connection, so the check proves nothing")
        return "pass", ("no hinted URL was requested and no preconnect arrived; with preloading on the same "
                        f"hints caused {len(control['hint_hits'])} request(s) and "
                        f"{control['probe_connections']} connection(s)")
