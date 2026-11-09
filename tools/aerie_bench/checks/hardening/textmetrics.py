"""Fingerprinting protection, measureText (issue #118): canvas text widths are stable per site and session.

With protection on, ``CanvasRenderingContext2D.measureText`` scales every metric by a factor that comes from the
site's text-metrics token, so two loads of one site read the same widths and two sites read different ones. The
factor is at most ``MAX_DEVIATION`` away from 1. The factor range comes from
components/aerie_text_metrics/text_metrics_noise.h; a unit test keeps ``MAX_DEVIATION`` equal to that file.

The fixture also lays out a sticky header and a virtual list and reads the row geometry with
``getBoundingClientRect``: the noise must not touch layout, and client-rect noise stays off, so the rectangles
are exact.

Negative controls, each a browser of its own: ``--disable-features=HeliumNoiseTextMetrics`` must show the real
widths (the same on both sites, so a difference elsewhere is noise), and so must protection turned off for the
whole browser or for one site. The site that still has protection on in the per-site browser must stay noised.
The equal-loads step is the one a per-document factor (the Bromite patch's ``base::RandDouble``) fails.
"""

from __future__ import annotations

from ...session import Session
from .. import register
from .common import HardeningCheck, Verdict, page_value, site
from .gpu import site_off_prefs

# Must equal kMaxDeviation in components/aerie_text_metrics/text_metrics_noise.h (a unit test checks it).
MAX_DEVIATION = 1.5e-6
# The widths are doubles that a scale of 1+-1.5e-6 changes by about 1e-4, so a little slack covers rounding.
SLACK = 2e-7
FEATURE_FLAG = "--disable-features=HeliumNoiseTextMetrics"
PREF_OFF = {"helium": {"noise": {"enabled": False}}}
# What the fixture's layout must read, whatever the noise does (see fixtures/hardening/textmetrics.html).
LAYOUT = {"stickyTop": 0, "firstRow": 400, "rowHeight": 24, "rects_exact": True}


def read_metrics(session: Session, name: str) -> dict:
    session.navigate_url(f"{site(session, name)}/hardening/textmetrics.html")
    return page_value(session, "window.__result")


def deviation(real: list, got: list) -> float:
    return max(abs(g / r - 1) for r, g in zip(real, got) if r)


@register
class FingerprintingTextMetrics(HardeningCheck):
    name = "hardening_fingerprinting_textmetrics"
    control = "fingerprinting"
    negative_control = ("Three more browsers: one with --disable-features=HeliumNoiseTextMetrics, one with fingerprinting protection "
                        "off for the whole browser and one with it off for the site a.localhost. Each must show the real widths, "
                        "the feature-off browser the same ones on both sites, and b.localhost in the last one must stay noised.")

    def observe(self, session: Session) -> dict:
        origin = site(session, "a")
        on = {"a1": read_metrics(session, "a"), "b": read_metrics(session, "b")}
        on["a2"] = read_metrics(session, "a")  # a second load of a.localhost, after another site
        with session.variant((FEATURE_FLAG,)) as v:
            feature_off = {"a": read_metrics(v, "a"), "b": read_metrics(v, "b")}
        with session.variant((), prefs=PREF_OFF) as v:
            global_off = {"a": read_metrics(v, "a")}
        with session.variant((), prefs=site_off_prefs(origin)) as v:
            site_off = {"a": read_metrics(v, "a"), "b": read_metrics(v, "b")}
        return {"on": on, "feature_off": feature_off, "global_off": global_off, "site_off": site_off}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        readings = {"on_a1": obs["on"]["a1"], "on_a2": obs["on"]["a2"], "on_b": obs["on"]["b"],
                    "feature_off_a": obs["feature_off"]["a"], "feature_off_b": obs["feature_off"]["b"],
                    "global_off_a": obs["global_off"]["a"], "site_off_a": obs["site_off"]["a"],
                    "site_off_b": obs["site_off"]["b"]}
        for key, r in readings.items():
            if "error" in r:
                return "fail", f"the measureText page failed ({key}): {r['error']}"
        for key, r in readings.items():
            if r["layout"] != LAYOUT:
                return "fail", f"layout changed ({key}): {r['layout']} instead of {LAYOUT}"
        real = readings["feature_off_a"]["widths"]
        if not real or not any(real):
            return "error", "inconclusive: the real widths are empty, so the check cannot tell noise from none"
        if readings["feature_off_b"]["widths"] != real:
            return "fail", ("negative control failed: with the feature off the widths differ between a.localhost and "
                            "b.localhost, so a difference between sites does not show noise")
        for key in ("global_off_a", "site_off_a"):
            if readings[key]["widths"] != real:
                return "fail", f"with protection off ({key}) the widths are not the real widths"
        a1, a2, b = (readings[k]["widths"] for k in ("on_a1", "on_a2", "on_b"))
        if a1 != a2:
            return "fail", ("two loads of the same site gave different widths: the factor is not stable per site and "
                            "session (a per-document factor does this)")
        if a1 == b:
            return "fail", "the same widths on a.localhost and b.localhost: the noise is not keyed on the site"
        if a1 == real or b == real:
            return "fail", "with protection on the widths are the unnoised ones"
        limit = MAX_DEVIATION + SLACK
        for key, widths in (("a", a1), ("b", b)):
            worst = deviation(real, widths)
            if worst > limit:
                return "fail", f"the noise on {key}.localhost moves a width by {worst:.2e}, more than {MAX_DEVIATION:.1e}"
        other = readings["site_off_b"]["widths"]
        if other == real:
            return "fail", "b.localhost lost its noise when only a.localhost was switched off"
        if deviation(real, other) > limit:
            return "fail", "b.localhost moves a width by more than the noise range when only a.localhost is switched off"
        return "pass", (f"two loads of a.localhost read the same widths and b.localhost reads other ones, at most "
                        f"{max(deviation(real, a1), deviation(real, b)):.1e} from the real widths; the real widths come back "
                        "with the feature, the whole browser or the site turned off, and the sticky header and virtual list lay out the same")
