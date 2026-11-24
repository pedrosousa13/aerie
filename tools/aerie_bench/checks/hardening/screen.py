"""Fingerprinting randomization, screen size (issue #116): window.screen reports a common size.

The browser runs at a fixed window size (``WINDOW``). With protection on, ``screen.width`` and ``screen.height``
are the smallest of the common sizes in ``BUCKETS`` that fits the window's outer size; the available size
equals the screen size, ``availLeft``/``availTop`` and ``screenX``/``screenY`` are 0, the colour depth is 24,
``isExtended`` is false, ``matchMedia('(device-width: Npx)')`` and the height query agree with ``screen``,
``screen.width >= outerWidth`` and ``devicePixelRatio`` is not changed. The sizes come from
components/aerie_screen/screen_size.cc; a unit test keeps ``BUCKETS`` equal to that file.

Event step (issue #159): after the page is read the check dispatches a mouse move at ``EVENT_POINT`` with CDP
``Input.dispatchMouseEvent`` and a touch with ``Input.dispatchTouchEvent``, and the page records ``screenX - clientX`` and
``screenY - clientY`` of the ``mousemove``, the ``pointermove`` and the ``touchstart`` it gets. On a protected page both must equal the offset the page itself
reports (``outerWidth - innerWidth``, ``outerHeight - innerHeight``, from window position 0,0), and must not change
when the window is started at another ``--window-position`` (``on_moved``) or the page is zoomed (``on_zoom``).
With protection off the offset must equal the feature-off browser's, which is the real one, and that must move with the
window position (``feature_off_moved``), or the check is inconclusive.

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
# Where the window starts (issue #159). The event step runs at two positions, so a protected page whose events
# still carry the real position gives a different offset in each and the check can fail.
POSITION_A = (10, 10)
POSITION_B = (240, 180)
WINDOW_FLAGS = (WINDOW_FLAG, f"--window-position={POSITION_A[0]},{POSITION_A[1]}")
MOVED_FLAGS = (WINDOW_FLAG, f"--window-position={POSITION_B[0]},{POSITION_B[1]}")
# A page zoom (issue #159): the profile's default zoom level, 3 steps of 1.2 (a layout zoom of 1.728), so clientX is the
# frame point divided by 1.728 and an offset worked out from the frame point would show the zoom and the pointer.
# (--force-device-scale-factor is no zoom: it leaves innerWidth and the CSS pixel alone.)
ZOOM_LEVEL = 3.0
ZOOM_PREFS = {"partition": {"default_zoom_level": {"x": ZOOM_LEVEL}}}
# Where the mouse event is dispatched, in client coordinates.
EVENT_POINT = (50, 60)
FEATURE_FLAG = "--disable-features=HeliumNoiseScreen"
PREF_OFF = {"helium": {"noise": {"enabled": False}}}
# What "the real screen" is compared on. The window position is left out: a new headless browser does not
# always put its window at the same place (0,0 in one browser and 10,10 in the next, in one train run), so
# a browser with protection off cannot be expected to repeat the control's position.
FIELDS = ("width", "height", "availWidth", "availHeight", "availLeft", "availTop", "colorDepth", "pixelDepth",
          "isExtended", "devicePixelRatio")


def bucket_for(outer_width: int, outer_height: int) -> tuple[int, int]:
    """The smallest common size at least as wide and tall as the window (a larger window keeps its size)."""
    w, h = max(outer_width, 0), max(outer_height, 0)
    for bucket in BUCKETS:
        if bucket[0] >= w and bucket[1] >= h:
            return bucket
    return max(BUCKETS[-1][0], w), max(BUCKETS[-1][1], h)


def read_screen(session: Session, name: str) -> dict:
    session.navigate_url(f"{site(session, name)}/hardening/screen.html")
    reading = page_value(session, "window.__result")
    if "error" not in reading:
        reading["mouse"] = read_event_offsets(session)
    return reading


def read_event_offsets(session: Session) -> dict:
    """Dispatch a mouse move and return screenX - clientX and screenY - clientY of the events the page got."""
    x, y = EVENT_POINT
    # The first move after a load can be dropped before the renderer has a hit target; send a few.
    for _ in range(5):
        session.cdp("Input.dispatchMouseEvent", {"type": "mouseMoved", "x": x, "y": y})
        if session.wait_for("window.__events.mouse && window.__events.pointer", timeout=1.0):
            break
        x, y = x + 1, y + 1
    # A touch (Touch is built on its own path): touch emulation, then a touch start and end at the same point.
    session.cdp("Emulation.setTouchEmulationEnabled", {"enabled": True, "maxTouchPoints": 1})
    for _ in range(5):
        point = {"x": x, "y": y}
        session.cdp("Input.dispatchTouchEvent", {"type": "touchStart", "touchPoints": [point]})
        session.cdp("Input.dispatchTouchEvent", {"type": "touchEnd", "touchPoints": []})
        if session.wait_for("window.__events.touch", timeout=1.0):
            break
    got = session.evaluate("window.__events")
    for kind in ("mouse", "pointer", "touch"):
        if not got.get(kind):
            return {"error": f"the page got no {kind} event"}
    m, p, t = got["mouse"], got["pointer"], got["touch"]
    return {"dx": m["screenX"] - m["clientX"], "dy": m["screenY"] - m["clientY"],
            "pdx": p["screenX"] - p["clientX"], "pdy": p["screenY"] - p["clientY"],
            "tdx": t["screenX"] - t["clientX"], "tdy": t["screenY"] - t["clientY"]}


def reported_offset(r: dict) -> tuple[int, int]:
    """The event offset a page can predict from what the window reports: the browser chrome around the page."""
    return r["outerWidth"] - r["innerWidth"], r["outerHeight"] - r["innerHeight"]


def event_offsets(r: dict) -> tuple:
    m = r["mouse"]
    return m["dx"], m["dy"], m["pdx"], m["pdy"], m["tdx"], m["tdy"]


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
    want_x, want_y = reported_offset(r)
    if event_offsets(r) != (want_x, want_y) * 3:
        return (f"{label}: an event's screen position minus its client position is {event_offsets(r)} (mouse x,y, "
                f"pointer x,y, touch x,y), not the {want_x},{want_y} that outerWidth and innerWidth report, so events show "
                "the window position")
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
        with session.variant(WINDOW_FLAGS) as v:
            on = {"a": read_screen(v, "a"), "b": read_screen(v, "b")}
        with session.variant(MOVED_FLAGS) as v:
            on_moved = {"a": read_screen(v, "a")}
        with session.variant(WINDOW_FLAGS, prefs=ZOOM_PREFS) as v:
            on_zoom = {"a": read_screen(v, "a")}
        with session.variant((*WINDOW_FLAGS, FEATURE_FLAG)) as v:
            feature_off = {"a": read_screen(v, "a")}
        with session.variant((*MOVED_FLAGS, FEATURE_FLAG)) as v:
            feature_off_moved = {"a": read_screen(v, "a")}
        with session.variant(WINDOW_FLAGS, prefs=PREF_OFF) as v:
            global_off = {"a": read_screen(v, "a")}
        with session.variant(WINDOW_FLAGS, prefs=site_off_prefs(origin)) as v:
            site_off = {"a": read_screen(v, "a"), "b": read_screen(v, "b")}
        return {"on": on, "on_moved": on_moved, "on_zoom": on_zoom, "feature_off": feature_off, "feature_off_moved": feature_off_moved,
                "global_off": global_off, "site_off": site_off}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        readings = {"on_a": obs["on"]["a"], "on_b": obs["on"]["b"], "feature_off": obs["feature_off"]["a"],
                    "global_off": obs["global_off"]["a"], "site_off_a": obs["site_off"]["a"],
                    "site_off_b": obs["site_off"]["b"], "on_moved": obs["on_moved"]["a"],
                    "on_zoom": obs["on_zoom"]["a"],
                    "feature_off_moved": obs["feature_off_moved"]["a"]}
        for key, reading in readings.items():
            if "error" in reading:
                return "fail", f"the screen page failed ({key}): {reading['error']}"
            if "error" in reading["mouse"]:
                return "fail", f"the event step failed ({key}): {reading['mouse']['error']}"
        control = readings["feature_off"]
        want = bucket_for(control["outerWidth"], control["outerHeight"])
        if (control["width"], control["height"]) == want:
            return "error", (f"inconclusive: with the feature off this machine already reports {want[0]}x{want[1]}, "
                             f"the common size for a {control['outerWidth']}x{control['outerHeight']} window, so "
                             "the check cannot tell the protection from the real screen")
        if not control["mqWidth"] or not control["mqHeight"]:
            return "fail", "negative control failed: with the feature off the device-width media feature disagrees with screen.width"
        if readings["on_zoom"]["innerWidth"] * 1.2 > readings["on_a"]["innerWidth"]:
            return "error", ("inconclusive: the zoom variant's innerWidth is "
                             f"{readings['on_zoom']['innerWidth']}, not smaller than the unzoomed {readings['on_a']['innerWidth']}, "
                             "so the page was not zoomed")
        if event_offsets(readings["feature_off_moved"]) == event_offsets(control):
            return "error", (f"inconclusive: with the feature off an event's screen position minus its client position "
                             f"is {event_offsets(control)} with the window at {POSITION_A} and with it at {POSITION_B}, "
                             "so --window-position does not move the events and the check cannot tell masked events "
                             "from real ones")
        for key in ("global_off", "site_off_a"):
            got = readings[key]
            if real_screen(got) != real_screen(control):
                return "fail", (f"with protection off ({key}) the screen is not the real one: "
                                f"{got['width']}x{got['height']} instead of {control['width']}x{control['height']}")
            if not got["mqWidth"] or not got["mqHeight"]:
                return "fail", f"with protection off ({key}) the device-width media feature disagrees with screen.width"
            if event_offsets(got) != event_offsets(control):
                return "fail", (f"with protection off ({key}) an event's screen position minus its client position is "
                                f"{event_offsets(got)}, not the real {event_offsets(control)} that the feature-off "
                                f"browser shows at the same window position")
        for key, label in (("on_a", "protection on (a)"), ("on_b", "protection on (b)"),
                           ("on_moved", "protection on, window moved"),
                           ("on_zoom", "protection on, page zoomed"),
                           ("site_off_b", "b.localhost with only a.localhost switched off")):
            problem = protected_problem(label, readings[key])
            if problem:
                return "fail", problem
            # A zoomed page legitimately has another devicePixelRatio.
            if key != "on_zoom" and readings[key]["devicePixelRatio"] != control["devicePixelRatio"]:
                return "fail", f"{label}: devicePixelRatio changed from {control['devicePixelRatio']} to {readings[key]['devicePixelRatio']}"
        if event_offsets(readings["on_moved"]) != event_offsets(readings["on_a"]):
            return "fail", (f"the event offset follows the window position: {event_offsets(readings['on_a'])} with the "
                            f"window at {POSITION_A}, {event_offsets(readings['on_moved'])} at {POSITION_B}")
        return "pass", (f"in a {readings['on_a']['outerWidth']}x{readings['on_a']['outerHeight']} window screen reports "
                        f"{readings['on_a']['width']}x{readings['on_a']['height']} (real {control['width']}x{control['height']}), "
                        "with the available area, position, colour depth and media features to match; the real screen comes back "
                        "with the feature, the whole browser or the site turned off")
