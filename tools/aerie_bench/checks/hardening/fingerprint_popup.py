"""A window opened with window.open() carries its opener's fingerprinting noise (issue #156).

The initial empty document of a popup is same-origin with its opener, so the opener can script it at once.
Before #156 that document had no noise tokens until its first navigation committed, and every protected
surface read "no token" as "protection off": one ``window.open('')`` after a click gave a page the real
canvas, graphics card, device memory and screen size.

The fixture page reads four surfaces (a canvas read-back, the WebGL unmasked renderer, ``deviceMemory`` and
the screen size) in its own window and through the window it opens. In one browser per case, all at the same
window size, so the screen buckets of the page and of the popup are the same:

* the regular browser: the empty popup of ``a.localhost`` reads what ``a`` reads, the empty popup of
  ``b.localhost`` reads what ``b`` reads, and the two differ. A popup that goes to ``b.localhost``, opened
  directly or after being empty, reads ``b``'s own values and not the opener's (the first commit replaces the
  tokens);
* protection off for ``http://[*.]a.localhost``: the popup of ``a`` reads the real values, and a popup that
  goes to ``b`` is still protected;
* protection off for the whole browser: the popup reads the real values;
* the negative control: ``--disable-features=HeliumNoise`` gives the real values, the same in the page and in
  the popup, so a difference really is noise and the values "off" must restore are known.

The canvas and the renderer string must differ from the real ones when protection is on (that is what shows
the page is protected). Device memory and screen are only compared for equality: a machine whose real values
equal the protected ones (8 GB or less, or a screen of a common size) cannot show a difference there.
"""

from __future__ import annotations

from ...session import Session
from .. import register
from .common import HardeningCheck, Verdict, page_value, site
from .fingerprint_site import BLOCK, SITE_PATTERN, seeded_exceptions

FIELDS = ("canvas", "renderer", "memory", "screen")
DIFFERING = ("canvas", "renderer")  # must differ from the real value when protection is on
WINDOW_FLAG = "--window-size=1000,700"  # smaller than every common size: page and popup get the same bucket
PREF_OFF = {"helium": {"noise": {"enabled": False}}}
PAGE = "/hardening/fingerprint_popup.html"
OPEN_AT_B = "openAt(%s)"
BLANK_THEN_B = "blankThenNavigate(%s)"


def own_reading(session: Session, host: str) -> dict:
    session.navigate_url(f"{site(session, host)}{PAGE}")
    result = page_value(session, "window.__result")
    return result if "error" in result else result["self"]


def gesture(session: Session, expression: str):
    """Run ``expression`` in the page with a user gesture, so that window.open is allowed."""
    return session.evaluate(expression, await_promise=True, user_gesture=True)


def readings(session: Session, host: str, popups: tuple[str, ...]) -> dict:
    """The page's own reading on ``host`` and the readings through the popups it opens: ``blank`` (the empty
    popup), ``to_b`` (a popup opened on b.localhost) and ``blank_then_b`` (an empty popup sent to b.localhost)."""
    out = {"self": own_reading(session, host)}
    b_url = f"{site(session, 'b')}{PAGE}"
    if "blank" in popups:
        out["blank"] = gesture(session, "blank()")
    if "to_b" in popups:
        out["to_b"] = gesture(session, OPEN_AT_B % repr(b_url))
    if "blank_then_b" in popups:
        out["blank_then_b"] = gesture(session, BLANK_THEN_B % repr(b_url))
    return out


def differences(got: dict, want: dict) -> list[str]:
    return [f for f in FIELDS if got.get(f) != want.get(f)]


@register
class FingerprintingPopup(HardeningCheck):
    name = "hardening_fingerprinting_popup"
    control = "fingerprinting"
    negative_control = ("A second browser with the noise feature disabled gives the same real canvas, graphics card, "
                        "device memory and screen in the page and in the empty popup, so a difference really comes from "
                        "the noise and these are the values that turning protection off must restore in a popup.")

    def observe(self, session: Session) -> dict:
        obs: dict = {}
        with session.variant((WINDOW_FLAG,)) as v:
            obs["regular"] = {"a": readings(v, "a", ("blank", "to_b", "blank_then_b")), "b": readings(v, "b", ("blank",))}
        with session.variant((WINDOW_FLAG,), seeded_exceptions({SITE_PATTERN: BLOCK})) as v:
            obs["site_off"] = {"a": readings(v, "a", ("blank", "to_b")), "b": readings(v, "b", ())}
        with session.variant((WINDOW_FLAG,), prefs=PREF_OFF) as v:
            obs["global_off"] = {"a": readings(v, "a", ("blank",))}
        with session.variant((WINDOW_FLAG, "--disable-features=HeliumNoise")) as v:
            obs["control"] = {"a": readings(v, "a", ("blank",))}
        return obs

    @staticmethod
    def judge(obs: dict) -> Verdict:
        for group, per_host in obs.items():
            for host, got in per_host.items():
                for what, reading in got.items():
                    if not isinstance(reading, dict) or "error" in reading:
                        why = reading.get("error") if isinstance(reading, dict) else "no reading"
                        return "fail", f"no reading ({group}.{host}.{what}): {why}"
        control = obs["control"]["a"]
        real = control["self"]
        # The negative control: with noise disabled the popup reads the same real values as the page.
        wrong = differences(control["blank"], real)
        if wrong:
            return "fail", (f"negative control failed: with noise disabled the empty popup differs from the page in "
                            f"{', '.join(wrong)} ({control['blank']} vs {real})")
        regular = obs["regular"]
        a, b = regular["a"], regular["b"]
        for kind in DIFFERING:
            if a["self"][kind] == real[kind]:
                return "fail", f"the {kind} on a.localhost equals the no-noise control, so the page is not protected"
        wrong = differences(a["blank"], a["self"])
        if wrong:
            return "fail", (f"the empty popup of a.localhost reads different {', '.join(wrong)} from its opener "
                            f"({a['blank']} vs {a['self']}); real values: {real}")
        wrong = differences(b["blank"], b["self"])
        if wrong:
            return "fail", (f"the empty popup of b.localhost reads different {', '.join(wrong)} from its opener "
                            f"({b['blank']} vs {b['self']})")
        if a["blank"]["canvas"] == b["blank"]["canvas"]:
            return "fail", f"the empty popups of a.localhost and b.localhost read the same canvas ({a['blank']['canvas']})"
        for what in ("to_b", "blank_then_b"):
            if a[what]["canvas"] == a["self"]["canvas"]:
                return "fail", (f"a popup of a.localhost that went to b.localhost ({what}) still reads the opener's "
                                f"canvas ({a[what]['canvas']})")
            wrong = differences(a[what], b["self"])
            if wrong:
                return "fail", (f"a popup of a.localhost that went to b.localhost ({what}) reads different "
                                f"{', '.join(wrong)} from b.localhost itself ({a[what]} vs {b['self']})")
        # The per-site switch reaches the popup, and only for its site.
        off = obs["site_off"]
        wrong = differences(off["a"]["blank"], real)
        if wrong:
            return "fail", (f"with protection off for {SITE_PATTERN} the empty popup of a.localhost still differs from "
                            f"the real values in {', '.join(wrong)} ({off['a']['blank']} vs {real})")
        if off["b"]["self"]["canvas"] == real["canvas"]:
            return "fail", f"turning protection off for {SITE_PATTERN} turned off another site (b.localhost)"
        wrong = differences(off["a"]["to_b"], off["b"]["self"])
        if wrong:
            return "fail", (f"with protection off for {SITE_PATTERN} a popup that went to b.localhost reads different "
                            f"{', '.join(wrong)} from b.localhost itself ({off['a']['to_b']} vs {off['b']['self']})")
        # The global switch reaches the popup.
        wrong = differences(obs["global_off"]["a"]["blank"], real)
        if wrong:
            return "fail", (f"with protection off for the whole browser the empty popup still differs from the real "
                            f"values in {', '.join(wrong)} ({obs['global_off']['a']['blank']} vs {real})")
        return "pass", ("the empty popup of a site reads the same canvas, graphics card, device memory and screen as its "
                        "opener, differs across sites, and takes the new site's values when it goes to another site; "
                        "turning protection off for the site or for the whole browser restores the real values in it")
