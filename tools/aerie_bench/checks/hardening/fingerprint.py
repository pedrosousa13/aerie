"""Fingerprinting protection: canvas and audio differ per site and per session.

The rule (issue #38): each hash must be stable for one site in one session, so a tracker cannot
average the noise away by reading again, and must differ across sites and across sessions and from
a no-noise control. The page renders the audio graph twice (`audio`, `audio_again`); a result with
no second render fails. Helium 0.22's AudioReadbackNoise seeds audio noise from the site and session token and the samples read
(no per-render counter); the `audio_noise_stable` source gate guards that.

The page also reads the same graph through an AudioWorklet (issue #157): the `worklet` hash and the
`worklet_samples` it posted. A worklet sees one 128-frame block at a time, so its noise is seeded from the
samples of the block and cannot equal that of the rendered buffer; it follows the same rules instead. The
hash must be stable for a site, differ across sites, equal the no-noise control with protection off for the
site, and every sample must stay within the 0.01% jitter bound of the control's (the `audio_worklet_noise`
source gate guards that the noise is there).
"""


from __future__ import annotations

from ...session import Session
from .. import register
from .common import HardeningCheck, Verdict, page_value, site
from .fingerprint_site import BLOCK, SITE_PATTERN, seeded_exceptions

KINDS = ("canvas", "audio")
JITTER_BOUND = 1e-4  # Helium's largest audio jitter, as a fraction of the sample (kMaxAudioJitterSteps * kAudioJitterStep)


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
        with session.variant((), seeded_exceptions({SITE_PATTERN: BLOCK})) as v:
            site_off = {"a": fingerprint(v, "a")}
        return {"a1": a1, "a2": a2, "b1": b1, "next_session": next_session, "control": control,
                "site_off": site_off}

    @staticmethod
    def judge_worklet(obs: dict) -> Verdict | None:
        """The worklet tap. None when it all holds."""
        for key in ("a1", "a2", "b1"):
            if "worklet" not in obs[key] or not obs[key].get("worklet_samples"):
                return "fail", f"the page made no worklet read ({key} has no worklet samples)"
        control = obs["control"]
        if control["a"].get("worklet") != control["b"].get("worklet"):
            return "fail", (f"negative control failed: with noise disabled the worklet hash still differed between "
                            f"sites ({control['a'].get('worklet')} vs {control['b'].get('worklet')})")
        if obs["a1"]["worklet"] != obs["a2"]["worklet"]:
            return "fail", "worklet audio changed between two loads of one site in one session"
        if obs["a1"]["worklet"] == obs["b1"]["worklet"]:
            return "fail", f"worklet audio is the same on two different sites ({obs['a1']['worklet']})"
        if obs["a1"]["worklet"] == control["a"]["worklet"]:
            return "fail", "the worklet reads the same samples as the no-noise control, so the worklet is not noised"
        if obs["site_off"]["a"].get("worklet") != control["a"]["worklet"]:
            return "fail", "with protection off for a site the worklet still reads noised samples"
        real = control["a"]["worklet_samples"]
        for key in ("a1", "b1"):
            samples = obs[key]["worklet_samples"]
            if len(samples) != len(real):
                return "fail", f"the worklet read {len(samples)} samples in {key} and {len(real)} in the control"
            for i, (got, want) in enumerate(zip(samples, real)):
                if abs(got - want) > abs(want) * JITTER_BOUND * 1.001 + 1e-12:
                    return "fail", f"worklet sample {i} in {key} is outside the 0.01% jitter bound ({got} vs {want})"
        return None

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
        worklet = Fingerprinting.judge_worklet(obs)
        if worklet:
            return worklet
        return "pass", ("canvas and audio are stable within a site and session, and differ across sites and "
                        "across sessions; an audio worklet reads noised samples that follow the same rules and stay "
                        "within the jitter bound")
