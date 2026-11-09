"""Fingerprinting randomization, screen size (issue #116): window.screen reports a common size.

The browser runs at a fixed window size (``WINDOW``). With protection on, ``screen.width`` and ``screen.height``
are the smallest of the common sizes in ``BUCKETS`` that fits the window's outer size; the available size
equals the screen size, ``availLeft``/``availTop`` and ``screenX``/``screenY`` are 0, the colour depth is 24,
``isExtended`` is false, ``matchMedia('(device-width: Npx)')`` and the height query agree with ``screen``,
``screen.width >= outerWidth`` and ``devicePixelRatio`` is not changed. The sizes come from
components/aerie_screen/screen_size.cc; a unit test keeps ``BUCKETS`` equal to that file.

Negative controls, each a second browser at the same window size: ``--disable-features=HeliumNoiseScreen``
must show the real screen, and so must protection turned off for the whole browser or for one site. The
site that still has protection on in the per-site browser must stay protected. The real screen must not be
one of the common sizes at this window size, or the check cannot tell the two apart (it is then
inconclusive, never a pass).
"""

from __future__ import annotations

from ...session import Session
from .. import register
from .common import HardeningCheck, Verdict, page_value, site
from .gpu import site_off_prefs

# Must equal kBuckets in components/aerie_screen/screen_size.cc (a unit test checks it).
BUCKETS = ((1280, 800), (1366, 768), (1440, 900), (1680, 1050), (1920, 1080), (2560, 1440), (3840, 2160))
# The fixed window: between two sizes, so the expected bucket (1680x1050) is not a size a window of this
# size is likely to have as its real screen.
WINDOW = (1500, 900)
WINDOW_FLAG = f"--window-size={WINDOW[0]},{WINDOW[1]}"
FEATURE_FLAG = "--disable-features=HeliumNoiseScreen"
PREF_OFF = {"helium": {"noise": {"enabled": False}}}
FIELDS = ("width", "height", "availWidth", "availHeight", "availLeft", "availTop", "colorDepth", "pixelDepth",
          "isExtended", "screenX", "screenY", "devicePixelRatio")


def bucket_for(outer_width: int, outer_height: int) -> tuple[int, int]:
    """The smallest common size at least as wide and tall as the window (a larger window keeps its size)."""
    w, h = max(outer_width, 0), max(outer_height, 0)
    for bucket in BUCKETS:
        if bucket[0] >= w and bucket[1] >= h:
            return bucket
    return max(BUCKETS[-1][0], w), max(BUCKETS[-1][1], h)


def read_screen(session: Session, name: str) -> dict:
    session.navigate_url(f"{site(session, name)}/hardening/screen.html")
    return page_value(session, "window.__result")


def real_screen(reading: dict) -> tuple:
    return tuple(reading[f] for f in FIELDS)


def protected_problem(label: str, r: dict) -> str | None:
    """What is wrong with a reading that must be protected, or None."""
    want = bucket_for(r["outerWidth"], r["outerHeight"])
    if (r["width"], r["height"]) != want:
        return (f"{label}: screen is {r['width']}x{r['height']}, not the {want[0]}x{want[1]} common size for a "
                f"{r['outerWidth']}x{r['outerHeight']} window")
    if (r["availWidth"], r["availHeight"]) != want or (r["availLeft"], r["availTop"]) != (0, 0):
        return f"{label}: the available area is not the whole screen: {r['availWidth']}x{r['availHeight']} at {r['availLeft']},{r['availTop']}"
    if (r["screenX"], r["screenY"]) != (0, 0):
        return f"{label}: the window position is {r['screenX']},{r['screenY']}, not 0,0"
    if r["colorDepth"] != 24 or r["pixelDepth"] != 24:
        return f"{label}: the colour depth is {r['colorDepth']}/{r['pixelDepth']}, not 24"
    if r["isExtended"]:
        return f"{label}: isExtended is true"
    if not r["mqWidth"] or not r["mqHeight"]:
        return f"{label}: the device-width or device-height media feature disagrees with screen.width/height"
    if r["width"] < r["outerWidth"] or r["height"] < r["outerHeight"]:
        return f"{label}: the screen is smaller than the window"
    return None


@register
class FingerprintingScreen(HardeningCheck):
    name = "hardening_fingerprinting_screen"
    control = "fingerprinting"
    negative_control = ("Three more browsers at the same window size: one with --disable-features=HeliumNoiseScreen, one with "
                        "fingerprinting protection off for the whole browser and one with it off for the site a.localhost. "
                        "Each must show the real screen, and b.localhost in the last one must stay protected.")

    def observe(self, session: Session) -> dict:
        origin = site(session, "a")
        with session.variant((WINDOW_FLAG,)) as v:
            on = {"a": read_screen(v, "a"), "b": read_screen(v, "b")}
        with session.variant((WINDOW_FLAG, FEATURE_FLAG)) as v:
            feature_off = {"a": read_screen(v, "a")}
        with session.variant((WINDOW_FLAG,), prefs=PREF_OFF) as v:
            global_off = {"a": read_screen(v, "a")}
        with session.variant((WINDOW_FLAG,), prefs=site_off_prefs(origin)) as v:
            site_off = {"a": read_screen(v, "a"), "b": read_screen(v, "b")}
        return {"on": on, "feature_off": feature_off, "global_off": global_off, "site_off": site_off}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        readings = {"on_a": obs["on"]["a"], "on_b": obs["on"]["b"], "feature_off": obs["feature_off"]["a"],
                    "global_off": obs["global_off"]["a"], "site_off_a": obs["site_off"]["a"],
                    "site_off_b": obs["site_off"]["b"]}
        for key, reading in readings.items():
            if "error" in reading:
                return "fail", f"the screen page failed ({key}): {reading['error']}"
        control = readings["feature_off"]
        want = bucket_for(control["outerWidth"], control["outerHeight"])
        if (control["width"], control["height"]) == want:
            return "error", (f"inconclusive: with the feature off this machine already reports {want[0]}x{want[1]}, "
                             f"the common size for a {control['outerWidth']}x{control['outerHeight']} window, so "
                             "the check cannot tell the protection from the real screen")
        if not control["mqWidth"] or not control["mqHeight"]:
            return "fail", "negative control failed: with the feature off the device-width media feature disagrees with screen.width"
        for key in ("global_off", "site_off_a"):
            got = readings[key]
            if real_screen(got) != real_screen(control):
                return "fail", (f"with protection off ({key}) the screen is not the real one: "
                                f"{got['width']}x{got['height']} instead of {control['width']}x{control['height']}")
            if not got["mqWidth"] or not got["mqHeight"]:
                return "fail", f"with protection off ({key}) the device-width media feature disagrees with screen.width"
        for key, label in (("on_a", "protection on (a)"), ("on_b", "protection on (b)"),
                           ("site_off_b", "b.localhost with only a.localhost switched off")):
            problem = protected_problem(label, readings[key])
            if problem:
                return "fail", problem
            if readings[key]["devicePixelRatio"] != control["devicePixelRatio"]:
                return "fail", f"{label}: devicePixelRatio changed from {control['devicePixelRatio']} to {readings[key]['devicePixelRatio']}"
        return "pass", (f"in a {readings['on_a']['outerWidth']}x{readings['on_a']['outerHeight']} window screen reports "
                        f"{readings['on_a']['width']}x{readings['on_a']['height']} (real {control['width']}x{control['height']}), "
                        "with the available area, position, colour depth and media features to match; the real screen comes back "
                        "with the feature, the whole browser or the site turned off")
