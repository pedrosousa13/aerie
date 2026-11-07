"""Strict site isolation: a cross-site frame lives in its own process (an out-of-process iframe)."""

from __future__ import annotations

from urllib.parse import urlsplit

from ...session import Session
from .. import register
from .common import HardeningCheck, Verdict, page_value, site

FRAME_HOST = "b.localhost"


def observe_frame(session: Session) -> dict:
    session.navigate_url(f"{site(session, 'a')}/hardening/embed.html?site={FRAME_HOST}"
                         "&frame=/hardening/idle_frame.html")
    res = page_value(session, "window.__result") or {}
    targets = session.targets()
    return {"frame_loaded": bool(res.get("ready")),
            "iframe_targets": [t["url"] for t in targets if t.get("type") == "iframe"]}


def has_cross_site_target(obs: dict) -> bool:
    return any(urlsplit(u).hostname == FRAME_HOST for u in obs["iframe_targets"])


@register
class SiteIsolation(HardeningCheck):
    name = "hardening_site_isolation"
    control = "site_isolation"
    negative_control = ("A second browser started with --disable-site-isolation-trials keeps the same "
                        "cross-site frame in the page's process, so no iframe target appears for it.")

    def observe(self, session: Session) -> dict:
        main = observe_frame(session)
        with session.variant(("--disable-site-isolation-trials",)) as v:
            control = observe_frame(v)
        return {"main": main, "control": control}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        main, control = obs["main"], obs["control"]
        if not main["frame_loaded"]:
            return "fail", "the cross-site frame did not load, so site isolation could not be observed"
        if not has_cross_site_target(main):
            return "fail", f"the cross-site frame has no process of its own (iframe targets: {main['iframe_targets']})"
        if not control["frame_loaded"] or has_cross_site_target(control):
            return "fail", (f"negative control failed: with site isolation disabled the frame loaded="
                            f"{control['frame_loaded']} and iframe targets were {control['iframe_targets']}")
        return "pass", "the cross-site frame ran in its own process; with isolation disabled it did not"
