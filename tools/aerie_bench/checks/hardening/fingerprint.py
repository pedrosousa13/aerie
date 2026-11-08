"""Fingerprinting randomization: canvas and audio differ per site and per session.

The rule (issue #38): each hash must be stable for one site in one session, so a tracker cannot
average the noise away by reading again, and must differ across sites and across sessions and from
a no-noise control. The page renders the audio graph twice (`audio`, `audio_again`); a result with
no second render fails. Aerie patch hardening/0009 seeds audio noise from the site and session alone.
"""


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
            if kind == "audio":  # the page renders the graph a second time
                for key in ("a1", "a2", "b1"):
                    if "audio_again" not in obs[key]:
                        return "fail", f"the page made no second audio render ({key} has no audio_again)"
                    if obs[key]["audio_again"] != obs[key]["audio"]:
                        return "fail", "audio changed between two renders in one page"
            if obs["a1"][kind] == obs["b1"][kind]:
                return "fail", f"{kind} is the same on two different sites ({obs['a1'][kind]})"
            if obs["a1"][kind] == obs["next_session"]["a"].get(kind):
                return "fail", f"{kind} is the same in a new browser session ({obs['a1'][kind]})"
        for kind in KINDS:
            if obs["control"]["a"][kind] in (obs["a1"][kind], obs["a2"][kind]):
                return "fail", f"{kind} equals the no-noise control, so the noise did nothing"
        return "pass", ("canvas and audio are stable within a site and session, and differ across sites and "
                        "across sessions")
