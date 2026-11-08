"""Fingerprinting randomization: canvas and audio differ per site and per session.

Both must also be stable within one site and session, so a tracker cannot average the noise away
by reading again. Helium first salted audio noise with a process-wide render counter (train-1 run:
`352a1c50` then `8e2a574a` for one site in one session) and the rule was lifted for audio. Aerie
patch hardening/0009 (issue #38) seeds audio noise from the site and session alone, so the rule
is back.
"""


from __future__ import annotations

from ...session import Session
from .. import register
from .common import HardeningCheck, Verdict, page_value, site

KINDS = ("canvas", "audio")
STABLE_WITHIN_SESSION = ("canvas", "audio")


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
            if kind in STABLE_WITHIN_SESSION and obs["a1"][kind] != obs["a2"][kind]:
                return "fail", f"{kind} changed between two loads of one site in one session"
            for key in ("a1", "a2", "b1"):  # a second render in the same page, when the page makes one
                again = obs[key].get(f"{kind}_again")
                if kind in STABLE_WITHIN_SESSION and again is not None and again != obs[key][kind]:
                    return "fail", f"{kind} changed between two renders in one page"
            if obs["a1"][kind] == obs["b1"][kind]:
                return "fail", f"{kind} is the same on two different sites ({obs['a1'][kind]})"
            if obs["a1"][kind] == obs["next_session"]["a"].get(kind):
                return "fail", f"{kind} is the same in a new browser session ({obs['a1'][kind]})"
        for kind in KINDS:
            if obs["control"]["a"][kind] in (obs["a1"][kind], obs["a2"][kind]):
                return "fail", f"{kind} equals the no-noise control, so the noise did nothing"
        return "pass", ("canvas and audio are stable within a site and session, and differ across sites and "
                        "across sessions")
