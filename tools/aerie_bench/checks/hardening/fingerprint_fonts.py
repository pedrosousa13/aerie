"""Fonts restriction (issue #117): with "Restrict fonts" on, a page matches only the allowlist.

The restriction is opt-in (the pref ``aerie.fonts.restrict``, off by default). The Bench profile has two test
fonts in its fontconfig directory (``fixtures/fonts``, made by ``make_fonts.py``): "Aerie Bench Probe", which is
installed but not on the allowlist, and "Droid Sans Mono", a name that is on it. A page finds a font three ways,
and the fixture (``hardening/fonts.html``) uses all three: it measures text in ``'name', generic`` against the
generic alone (canvas ``measureText``), it loads ``@font-face { src: local(name) }``, and it measures in a
dedicated worker with ``OffscreenCanvas``.

* With the pref on, the probe font is found by none of the three, the allowlisted font is found by all three, a
  generic family has a width, and text in other scripts has the width it has without the restriction (the
  glyphs come from system fallback, which the restriction does not touch).
* With the pref off, which is the default, the probe font is found by all three. That also proves the page can
  see the test font at all.

Negative controls, each a browser with the pref on: ``--disable-features=HeliumNoiseFonts`` must find the probe
font, and so must fingerprinting protection turned off for the whole browser or for one site. The other site in
the last browser stays restricted.
"""

from __future__ import annotations

from ...session import Session
from .. import register
from .common import HardeningCheck, Verdict, page_value, site
from .gpu import site_off_prefs

PROBE_FAMILY = "Aerie Bench Probe"   # installed, not on the allowlist
ALLOWED_FAMILY = "Droid Sans Mono"   # on the allowlist; the Bench installs a test font of this name
PREF_ON = {"aerie": {"fonts": {"restrict": True}}}
PREF_NOISE_OFF = {"helium": {"noise": {"enabled": False}}}
FEATURE_FLAG = "--disable-features=HeliumNoiseFonts"
FIELDS = ("probe", "probe_local", "probe_worker", "allowed", "allowed_local", "allowed_worker", "generic",
          "nonlatin", "nonlatin_base")
PROBE_CHANNELS = ("probe", "probe_local", "probe_worker")
ALLOWED_CHANNELS = ("allowed", "allowed_local", "allowed_worker")


def merge(*parts: dict) -> dict:
    out: dict = {}
    for part in parts:
        for key, value in part.items():
            if isinstance(value, dict) and isinstance(out.get(key), dict):
                out[key] = merge(out[key], value)
            else:
                out[key] = value
    return out


def read_fonts(session: Session, name: str) -> dict:
    session.navigate_url(f"{site(session, name)}/hardening/fonts.html")
    return page_value(session, "window.__result", timeout=20)


def how_found(reading: dict) -> str:
    return ", ".join(c for c in PROBE_CHANNELS if reading[c]) or "nowhere"


@register
class FingerprintingFonts(HardeningCheck):
    name = "hardening_fingerprinting_fonts"
    control = "font_restriction"
    negative_control = ("With the restriction off (the default) the page must find the test font that is not on the allowlist, "
                        "which shows it can see it; and three more browsers with the restriction on, one with "
                        "--disable-features=HeliumNoiseFonts, one with fingerprinting protection off for the whole browser and one "
                        "with it off for the site a.localhost, must each find it, while b.localhost in the last one stays restricted.")

    def observe(self, session: Session) -> dict:
        origin = site(session, "a")
        default = {"a": read_fonts(session, "a")}  # the main Bench browser: the pref has its default, off
        with session.variant((), prefs=PREF_ON) as v:
            on = {"a": read_fonts(v, "a"), "b": read_fonts(v, "b")}
        with session.variant((FEATURE_FLAG,), prefs=PREF_ON) as v:
            feature_off = {"a": read_fonts(v, "a")}
        with session.variant((), prefs=merge(PREF_ON, PREF_NOISE_OFF)) as v:
            global_off = {"a": read_fonts(v, "a")}
        with session.variant((), prefs=merge(PREF_ON, site_off_prefs(origin))) as v:
            site_off = {"a": read_fonts(v, "a"), "b": read_fonts(v, "b")}
        return {"default": default, "on": on, "feature_off": feature_off, "global_off": global_off, "site_off": site_off}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        readings = {"default": obs["default"]["a"], "on_a": obs["on"]["a"], "on_b": obs["on"]["b"],
                    "feature_off": obs["feature_off"]["a"], "global_off": obs["global_off"]["a"],
                    "site_off_a": obs["site_off"]["a"], "site_off_b": obs["site_off"]["b"]}
        for key, reading in readings.items():
            if "error" in reading:
                return "fail", f"the fonts page failed ({key}): {reading['error']}"
        base = readings["default"]
        missing = [c for c in PROBE_CHANNELS if not base[c]]
        if missing:
            return "fail", (f"detector failed: with the restriction off (the default) the page does not find {PROBE_FAMILY} "
                            f"({', '.join(missing)}), so the test font is not installed in the Bench profile or the probe is wrong")
        for key, label in (("feature_off", "--disable-features=HeliumNoiseFonts"),
                           ("global_off", "fingerprinting protection off for the whole browser"),
                           ("site_off_a", "fingerprinting protection off for the site")):
            got = readings[key]
            if not all(got[c] for c in PROBE_CHANNELS):
                return "fail", (f"negative control failed: with {label} and the restriction on, {PROBE_FAMILY} is not found "
                                f"everywhere it is without the restriction (found: {how_found(got)}), so that switch does not lift it")
        for key, label in (("on_a", "restriction on (a)"), ("on_b", "restriction on (b)"),
                           ("site_off_b", "b.localhost with only a.localhost switched off")):
            got = readings[key]
            if any(got[c] for c in PROBE_CHANNELS):
                return "fail", f"{label}: {PROBE_FAMILY} is not on the list but the page finds it ({how_found(got)})"
            if not all(got[c] for c in ALLOWED_CHANNELS):
                return "fail", f"{label}: the allowlisted font {ALLOWED_FAMILY} is not found by every channel ({[c for c in ALLOWED_CHANNELS if not got[c]]})"
            if got["generic"] <= 0:
                return "fail", f"{label}: a generic family has no width"
            if got["nonlatin"] <= 0 or got["nonlatin"] != got["nonlatin_base"] or got["nonlatin"] != base["nonlatin_base"]:
                return "fail", (f"{label}: text in another script is {got['nonlatin']} wide, not the {base['nonlatin_base']} "
                                "it is without the restriction")
        return "pass", (f"with the restriction on, {PROBE_FAMILY} (installed, not on the list) is found by no channel and "
                        f"{ALLOWED_FAMILY} (on the list) by all three, a generic family and other scripts still work; "
                        f"with it off, with the feature, the whole browser or the site switched off, {PROBE_FAMILY} is found "
                        "by all three")
