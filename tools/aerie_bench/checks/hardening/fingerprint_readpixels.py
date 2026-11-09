"""Fingerprinting protection, WebGL readPixels (issue #119): the default framebuffer is noised, nothing else.

The page draws one scene and reads it back with ``readPixels`` from the default framebuffer (twice) and,
after drawing the same scene into a framebuffer object, from that object. With protection on, the default
framebuffer read is stable for one site in one session, differs across sites and across sessions, and differs
from the read with noise off. The framebuffer-object read must equal the same read with noise off, byte for
byte (compared as a hash): GPGPU, TF.js and video readback need exact values. A noise that reached every
framebuffer would make that read differ from the control, and the check fails then.

Negative controls: a second browser with ``--disable-features=HeliumNoiseCanvas`` gives one hash on both
sites, and so does one with protection off for the whole browser or for ``a.localhost`` (the real value comes
back; ``b.localhost`` stays noised). Aerie patch hardening/0019 adds the noise.
"""

from __future__ import annotations

from ...session import Session
from .. import register
from .common import HardeningCheck, Verdict, page_value, site
from .fingerprint_site import BLOCK, SITE_PATTERN, seeded_exceptions  # noqa: F401

FEATURE_FLAG = "--disable-features=HeliumNoiseCanvas"


def read(session: Session, name: str) -> dict:
    session.navigate_url(f"{site(session, name)}/hardening/fingerprint_readpixels.html")
    return page_value(session, "window.__result")


@register
class FingerprintingReadPixels(HardeningCheck):
    name = "hardening_fingerprinting_readpixels"
    control = "fingerprinting"
    negative_control = ("Three more browsers read the same page: one with --disable-features=HeliumNoiseCanvas, one with "
                        "fingerprinting protection off for the whole browser and one with it off for a.localhost. Each "
                        "must give the same default framebuffer hash on both sites, and the framebuffer object read "
                        "must equal that control hash, so noise applied to every framebuffer fails the check.")

    def observe(self, session: Session) -> dict:
        a1 = read(session, "a")
        a2 = read(session, "a")  # a fresh load in the same session
        b1 = read(session, "b")
        with session.variant(()) as v:
            next_session = {"a": read(v, "a")}
        with session.variant((FEATURE_FLAG,)) as v:
            control = {"a": read(v, "a"), "b": read(v, "b")}
        with session.variant((), prefs={"helium": {"noise": {"enabled": False}}}) as v:
            global_off = {"a": read(v, "a")}
        with session.variant((), prefs=seeded_exceptions({SITE_PATTERN: BLOCK})) as v:
            site_off = {"a": read(v, "a"), "b": read(v, "b")}
        return {"a1": a1, "a2": a2, "b1": b1, "next_session": next_session, "control": control,
                "global_off": global_off, "site_off": site_off}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        readings = {"a1": obs["a1"], "a2": obs["a2"], "b1": obs["b1"], "next_session": obs["next_session"]["a"],
                    "control_a": obs["control"]["a"], "control_b": obs["control"]["b"],
                    "global_off": obs["global_off"]["a"], "site_off_a": obs["site_off"]["a"],
                    "site_off_b": obs["site_off"]["b"]}
        for key, reading in readings.items():
            if "error" in reading:
                return "fail", f"the readPixels page failed ({key}): {reading['error']}"
        ca, cb = readings["control_a"], readings["control_b"]
        if not ca.get("lit"):
            return "fail", "the scene drew nothing (every byte read back was zero), so the check cannot run"
        if ca["default"] != ca["default_again"] or ca["default"] != cb["default"]:
            return "fail", ("negative control failed: with the noise feature disabled the default framebuffer read "
                            f"was not one stable value ({ca['default']}, {ca['default_again']}, {cb['default']})")
        real = ca["default"]
        real_fbo = ca["fbo"]
        # The noise: stable per site and session, different across sites and sessions, and not the real value.
        a1, a2, b1 = readings["a1"], readings["a2"], readings["b1"]
        if a1["default"] != a1["default_again"]:
            return "fail", "readPixels changed between two reads in one page"
        if a1["default"] != a2["default"]:
            return "fail", "readPixels changed between two loads of one site in one session"
        if a1["default"] == b1["default"]:
            return "fail", f"readPixels is the same on two different sites ({a1['default']})"
        if a1["default"] == readings["next_session"]["default"]:
            return "fail", f"readPixels is the same in a new browser session ({a1['default']})"
        if real in (a1["default"], a2["default"], b1["default"]):
            return "fail", "the default framebuffer read equals the no-noise control, so the noise did nothing"
        # Only the default framebuffer: a framebuffer object read is exact.
        for key in ("a1", "a2", "b1"):
            if readings[key]["fbo"] != real_fbo:
                return "fail", (f"a framebuffer object read on {key} differs from the no-noise control "
                                f"({readings[key]['fbo']} instead of {real_fbo}), so noise reached a framebuffer object")
        # Turning protection off restores the real value, and only for what was turned off.
        if readings["global_off"]["default"] != real:
            return "fail", "protection turned off for the whole browser did not restore the real readPixels value"
        if readings["site_off_a"]["default"] != real:
            return "fail", "protection turned off for the site did not restore the real readPixels value"
        if readings["site_off_b"]["default"] == real:
            return "fail", "the other site lost its noise when protection was turned off for a.localhost"
        return "pass", ("readPixels from the default framebuffer is stable within a site and session, differs across sites "
                        "and sessions, the framebuffer object read stays exact, and the real value returns with the "
                        "feature, the whole browser or the site turned off")
