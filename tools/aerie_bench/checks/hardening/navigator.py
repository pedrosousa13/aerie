"""Fingerprinting protection, navigator surfaces (issue #120): languages, battery and speech voices.

The Bench records every value the issue names (``navigator.languages``, the speech voice list, the device
count before permission, ``getBattery()``, ``plugins``/``mimeTypes`` and ``keyboard.getLayoutMap()``) and judges
the three that are pinned:

* ``navigator.languages`` is the first language only, the one the reduced Accept-Language header carries
  (Helium turns ``ReduceAcceptLanguage`` on; no Aerie patch is needed, and this check fails if it is lost).
* ``getBattery()`` reports charging, level 1, charging time 0 and discharging time Infinity (hardening/0021).
* ``speechSynthesis.getVoices()`` lists at most one voice, a voice the system has (hardening/0022).

The device count, plugins, mime types and layout map are recorded and never judged: plugins are the same five
PDF entries in every Chrome, and the other two are follow-up issues (see docs/hardening.md).

Negative controls: a browser with ``--disable-features=HeliumNoiseNavigator,ReduceAcceptLanguage`` and a
profile with three accepted languages shows the real values; protection off for the whole browser or for one
site restores the real battery and voices, and the other site stays pinned. A machine with no battery reports
the pinned battery already, and one with at most one voice cannot show the voice limit; the result then says
"not distinguishable" for that surface instead of claiming a control. A control with only one language is
inconclusive (status ``error``), because the language pin could not be told from the real list.
"""

from __future__ import annotations

from ...session import Session
from .. import register
from .common import HardeningCheck, Verdict, page_value, site
from .gpu import site_off_prefs

# Must equal the constants in components/aerie_navigator_pins/navigator_pins.h (a unit test checks them).
# dischargingTime is Infinity, which the fixture page reports as the string "Infinity".
PINNED_BATTERY = {"charging": True, "level": 1, "chargingTime": 0, "dischargingTime": "Infinity"}
ACCEPT_LANGUAGES = "en-US,en,fr"
FEATURE_FLAG = "--disable-features=HeliumNoiseNavigator,ReduceAcceptLanguage"
PREFS_LANGUAGES = {"intl": {"accept_languages": ACCEPT_LANGUAGES}}
PREFS_OFF = {"intl": {"accept_languages": ACCEPT_LANGUAGES}, "helium": {"noise": {"enabled": False}}}


def prefs_site_off(origin: str) -> dict:
    prefs = site_off_prefs(origin)
    prefs["intl"] = {"accept_languages": ACCEPT_LANGUAGES}
    return prefs


def read_navigator(session: Session, name: str) -> dict:
    session.navigate_url(f"{site(session, name)}/hardening/navigator.html")
    return page_value(session, "window.__result", timeout=20.0)


def battery_problem(label: str, r: dict) -> str | None:
    b = r["battery"]
    if "error" in b:
        return f"{label}: getBattery() failed: {b['error']}"
    if b != PINNED_BATTERY:
        return f"{label}: battery is {b}, not the pinned {PINNED_BATTERY}"
    return None


def voices_problem(label: str, r: dict, system_voices: list[str]) -> str | None:
    v = r["voices"]
    if v["count"] > 1 or len(v["names"]) > 1:
        return f"{label}: voices lists {v['count']} voices ({', '.join(v['names'])}), not at most one"
    for name in v["names"]:
        if system_voices and name not in system_voices:
            return f"{label}: voices lists {name!r}, which is not a voice of the system ({', '.join(system_voices)})"
    return None


@register
class FingerprintingNavigator(HardeningCheck):
    name = "hardening_fingerprinting_navigator"
    control = "fingerprinting"
    negative_control = ("Three more browsers with three accepted languages: one with "
                        "--disable-features=HeliumNoiseNavigator,ReduceAcceptLanguage, one with fingerprinting protection "
                        "off for the whole browser and one with it off for a.localhost. Each must show the real battery, "
                        "voices and (the first) languages, and b.localhost in the last one must stay pinned.")

    def observe(self, session: Session) -> dict:
        origin = site(session, "a")
        with session.variant((), prefs=PREFS_LANGUAGES) as v:
            on = {"a": read_navigator(v, "a"), "b": read_navigator(v, "b")}
        with session.variant((FEATURE_FLAG,), prefs=PREFS_LANGUAGES) as v:
            feature_off = {"a": read_navigator(v, "a")}
        with session.variant((), prefs=PREFS_OFF) as v:
            global_off = {"a": read_navigator(v, "a")}
        with session.variant((), prefs=prefs_site_off(origin)) as v:
            site_off = {"a": read_navigator(v, "a"), "b": read_navigator(v, "b")}
        return {"on": on, "feature_off": feature_off, "global_off": global_off, "site_off": site_off}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        readings = {"on_a": obs["on"]["a"], "on_b": obs["on"]["b"], "feature_off": obs["feature_off"]["a"],
                    "global_off": obs["global_off"]["a"], "site_off_a": obs["site_off"]["a"],
                    "site_off_b": obs["site_off"]["b"]}
        for key, reading in readings.items():
            if "error" in reading:
                return "fail", f"the navigator page failed ({key}): {reading['error']}"
        control = readings["feature_off"]
        if len(control["languages"]) < 2:
            return "error", ("inconclusive: with the pins off the browser still reports "
                             f"{len(control['languages'])} language ({', '.join(control['languages'])}) although three were "
                             "accepted, so the check cannot tell the language pin from the real list")
        system_voices = list(control["voices"]["names"])
        battery_distinct = control["battery"] != PINNED_BATTERY
        voices_distinct = control["voices"]["count"] > 1
        pinned = {"on_a": "protection on (a)", "on_b": "protection on (b)",
                  "site_off_b": "b.localhost with only a.localhost switched off"}
        for key, label in pinned.items():
            r = readings[key]
            if r["languages"] != control["languages"][:1]:
                return "fail", (f"{label}: languages is {r['languages']}, not only the first language "
                                f"{control['languages'][:1]}")
            problem = battery_problem(label, r) or voices_problem(label, r, system_voices)
            if problem:
                return "fail", problem
        for key in ("global_off", "site_off_a"):
            r = readings[key]
            if "error" in r["battery"]:
                return "fail", f"with protection off ({key}) getBattery() failed: {r['battery']['error']}"
            if battery_distinct and r["battery"] != control["battery"]:
                return "fail", (f"with protection off ({key}) the battery is not the real one: {r['battery']}, "
                                f"not {control['battery']}")
            if voices_distinct and (r["voices"]["count"] != control["voices"]["count"]):
                return "fail", (f"with protection off ({key}) the voices are not the real list: "
                                f"{r['voices']['count']} voices, not {control['voices']['count']}")
        skipped = []
        if not battery_distinct:
            skipped.append("battery not distinguishable here (the real battery equals the pinned one)")
        if not voices_distinct:
            skipped.append(f"voices not distinguishable here (the system lists {control['voices']['count']})")
        on = readings["on_a"]
        text = (f"languages {on['languages']} (real {control['languages']}), battery {on['battery']['level']} charging, "
                f"voices {on['voices']['count']} of {control['voices']['count']}; the real values come back with the "
                "feature, the whole browser or the site turned off")
        if skipped:
            text += "; " + "; ".join(skipped)
        recorded = (f"; recorded, not pinned: devices {on['devices']}, plugins {len(on['plugins'])}, "
                    f"mimeTypes {len(on['mimeTypes'])}, keyboard layout map {on['layoutMap']}")
        return "pass", text + recorded
