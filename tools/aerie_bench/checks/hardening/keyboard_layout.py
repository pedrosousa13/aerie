"""Keyboard layout map: getLayoutMap() is refused only while protection is on (issue #203).

Aerie patch hardening/0029 makes ``navigator.keyboard.getLayoutMap()`` return a promise rejected with
``NotSupportedError`` while fingerprinting protection is on for the page's site, the outcome in a browser without the
API. The layout gives away the user's region. ``navigator.keyboard.lock()`` and ``unlock()`` stay as they are, so a
remote-desktop client keeps working. The browser decides it from the pref and the per-site setting.

Runs, each a browser with its own profile:

* ``main``: protection on. The promise rejects with ``NotSupportedError`` and ``lock``/``unlock`` are functions.
* ``pref_off`` and ``site_off``: protection off for the whole browser, and for the fixture site. Both give the real
  map (``control``), so an always-on refusal fails.
* ``control``: ``--disable-features=HeliumNoise``. The real map, with entries.
"""

from __future__ import annotations

from ...session import Session
from .. import register
from .common import HardeningCheck, Verdict, page_value, site
from .gpu import site_off_prefs

PREF_OFF = {"helium": {"noise": {"enabled": False}}}
KEYS = ("main", "pref_off", "site_off", "control")


def read(session: Session) -> dict:
    session.navigate_url(f"{site(session, 'a')}/hardening/keyboard-layout.html")
    return page_value(session, "window.__result")


def real_map_problem(label: str, result: dict, real_size: int | None) -> str | None:
    if result.get("rejection"):
        return f"{label}: getLayoutMap() rejected with {result['rejection']} and not the real map"
    size = result.get("size") or 0
    if size <= 0 or (real_size is not None and size != real_size):
        return f"{label}: getLayoutMap() gave {size} entries, not the real {real_size}"
    return None


@register
class KeyboardLayout(HardeningCheck):
    name = "hardening_fingerprinting_keyboard_layout"
    control = "fingerprinting"
    negative_control = ("A second browser with the noise feature disabled must return the real layout map with "
                        "entries, so a rejection in the first browser really comes from the patch, and the "
                        "pref-off and site-off browsers are compared with that real map.")

    def observe(self, session: Session) -> dict:
        main = read(session)
        with session.variant((), PREF_OFF) as v:
            pref_off = read(v)
        with session.variant((), site_off_prefs(site(session, "a"))) as v:
            site_off = read(v)
        with session.variant(("--disable-features=HeliumNoise",)) as v:
            control = read(v)
        return {"main": main, "pref_off": pref_off, "site_off": site_off, "control": control}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        for key in KEYS:
            if "error" in obs[key]:
                return "fail", f"the keyboard layout page failed ({key}): {obs[key]['error']}"
        control = obs["control"]
        if control.get("rejection") or not control.get("size"):
            return "fail", ("negative control failed: with the noise feature off getLayoutMap() did not return "
                            "a map with entries, so the page cannot show a real map "
                            f"(rejection: {control.get('rejection')}, entries: {control.get('size')})")
        real = control["size"]
        main = obs["main"]
        if main.get("rejection") != "NotSupportedError":
            got = main.get("rejection") or f"a map of {main.get('size')} entries"
            return "fail", f"protection on: getLayoutMap() ended with {got}, not a NotSupportedError rejection"
        for key, label in (("main", "protection on"), ("pref_off", "fingerprinting pref off"),
                           ("site_off", "site switched off"), ("control", "feature off")):
            r = obs[key]
            if r.get("lockType") != "function" or r.get("unlockType") != "function":
                return "fail", (f"{label}: navigator.keyboard.lock is {r.get('lockType')} and unlock is "
                                f"{r.get('unlockType')}, not functions")
        for key, label in (("pref_off", "fingerprinting pref off"), ("site_off", "site switched off")):
            problem = real_map_problem(label, obs[key], real)
            if problem:
                return "fail", problem
        return "pass", (f"with the feature off getLayoutMap() returns the real map of {real} entries; with "
                        f"protection on it rejects with NotSupportedError and keyboard.lock and unlock are "
                        f"functions; with the pref off and with the site switched off it returns the real map")
