"""Fingerprinting randomization: canvas and audio differ per site and per session, stable within one."""

from __future__ import annotations

from ...session import Session
from .. import register
from .common import HardeningCheck, Verdict, page_value, site

KINDS = ("canvas", "audio")


def fingerprint(session: Session, name: str) -> dict:
    session.navigate_url(f"{site(session, name)}/hardening/fingerprint.html")
    return page_value(session, "window.__result")



@register
class Fingerprinting(HardeningCheck):
    name = "hardening_fingerprinting"
    control = "fingerprinting"
    negative_control = ("A second browser with the noise feature disabled gives the same hashes on both sites, "
                        "so a difference between sites really comes from the noise.")

    def observe(self, session: Session) -> dict:
        a1 = fingerprint(session, "a")
        a2 = fingerprint(session, "a")  # a fresh load in the same session
        b1 = fingerprint(session, "b")
        with session.variant(()) as v:
            next_session = {"a": fingerprint(v, "a")}
        with session.variant(("--disable-features=HeliumNoise",)) as v:
            control = {"a": fingerprint(v, "a"), "b": fingerprint(v, "b")}
        return {"a1": a1, "a2": a2, "b1": b1, "next_session": next_session, "control": control}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        for key in ("a1", "a2", "b1"):
            if "error" in obs[key]:
                return "fail", f"the fingerprint page failed ({key}): {obs[key]['error']}"
        for kind in KINDS:
            c = obs["control"]
            if c["a"].get(kind) != c["b"].get(kind) or "error" in c["a"]:
                return "fail", (f"negative control failed: with noise disabled the {kind} hash still differed "
                                f"between sites ({c['a'].get(kind)} vs {c['b'].get(kind)})")
        for kind in KINDS:
            if obs["a1"][kind] != obs["a2"][kind]:
                return "fail", f"{kind} changed between two loads of one site in one session"
            if obs["a1"][kind] == obs["b1"][kind]:
                return "fail", f"{kind} is the same on two different sites ({obs['a1'][kind]})"
            if obs["a1"][kind] == obs["next_session"]["a"].get(kind):
                return "fail", f"{kind} is the same in a new browser session ({obs['a1'][kind]})"
        return "pass", "canvas and audio: stable within a site and session, different across sites and across sessions"
