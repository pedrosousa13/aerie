"""Media devices: enumerateDevices() before any permission lists the same three entries (issue #202).

Aerie patch hardening/0028 gives a page that holds no camera or microphone permission, while protection is on for
its site, exactly one ``audioinput``, one ``audiooutput`` and one ``videoinput``, each with an empty ``deviceId``,
``label`` and ``groupId``, whatever the machine has. Chromium already trims a list without permission to one
entry per kind with empty fields, so a machine that has no camera shows no ``videoinput`` and no entry of any kind
it lacks: that is what the patch adds. The browser decides it from the pref and the per-site setting.

Runs, each a browser with its own profile:

* ``main``: protection on, no permission. Exactly the pinned list.
* ``pref_off`` and ``site_off``: protection off for the whole browser, and for the fixture site. Both give what the
  browser gives with no protection (``control``), so an always-on pin fails.
* ``control``: ``--disable-features=HeliumNoise``, no permission. The real list.
* ``granted`` and ``granted_control``: fake camera and microphone and a permission that is granted
  (``--use-fake-device-for-media-stream``, ``--use-fake-ui-for-media-stream``), the page calls ``getUserMedia``
  first, protection on and ``HeliumNoise`` off. After a grant the protected page must see the real devices with
  their labels, the same ones the control sees.

The check cannot tell the pin from the machine when the real list before permission is already one entry of each
kind, which is every machine that has a camera, a microphone and a speaker: it is then inconclusive, never a pass.
The box that the check was written on has no camera.
"""

from __future__ import annotations

from ...session import Session
from .. import register
from .common import HardeningCheck, Verdict, page_value, site
from .gpu import site_off_prefs

KINDS = ("audioinput", "audiooutput", "videoinput")
PREF_OFF = {"helium": {"noise": {"enabled": False}}}
# A camera and a microphone that exist, and a permission that is granted, without a prompt.
GRANT_FLAGS = ("--use-fake-device-for-media-stream", "--use-fake-ui-for-media-stream")


def read(session: Session, grant: bool = False) -> dict:
    session.navigate_url(f"{site(session, 'a')}/hardening/media-devices.html" + ("?gum=1" if grant else ""))
    return page_value(session, "window.__result")


def blank(entry: dict) -> bool:
    return not (entry.get("deviceId") or entry.get("label") or entry.get("groupId"))


def shape(devices: list[dict]) -> list[tuple]:
    """The list as a sorted list of (kind, deviceId, label, groupId), so two lists compare without an order."""
    return sorted((d.get("kind"), d.get("deviceId"), d.get("label"), d.get("groupId")) for d in devices)


def counts(devices: list[dict]) -> dict:
    return {kind: sum(1 for d in devices if d.get("kind") == kind) for kind in KINDS}


def pinned_problem(label: str, devices: list[dict]) -> str | None:
    got = counts(devices)
    if len(devices) != len(KINDS) or any(n != 1 for n in got.values()):
        return (f"{label}: enumerateDevices() listed {len(devices)} entries "
                f"({', '.join(f'{n} {k}' for k, n in got.items())}), not one audioinput, one audiooutput and one "
                f"videoinput")
    if not all(blank(d) for d in devices):
        return f"{label}: an entry of the pinned list has a deviceId, a label or a groupId"
    return None


def observe_labels(devices: list[dict]) -> list[str]:
    return sorted(d.get("label") or "" for d in devices)


@register
class MediaDevices(HardeningCheck):
    name = "hardening_fingerprinting_media_devices"
    control = "fingerprinting"
    negative_control = ("A second browser with the noise feature disabled lists the machine's own devices before "
                        "permission, so a list of one entry per kind in the first browser really comes from the "
                        "pin, and the pref-off and site-off browsers are compared with that real list. A third and "
                        "fourth browser with a fake camera, a fake microphone and a granted permission must list "
                        "the same labelled devices with protection on and off.")

    def observe(self, session: Session) -> dict:
        main = read(session)
        with session.variant((), PREF_OFF) as v:
            pref_off = read(v)
        with session.variant((), site_off_prefs(site(session, "a"))) as v:
            site_off = read(v)
        with session.variant(("--disable-features=HeliumNoise",)) as v:
            control = read(v)
        with session.variant(GRANT_FLAGS) as v:
            granted = read(v, grant=True)
        with session.variant(GRANT_FLAGS + ("--disable-features=HeliumNoise",)) as v:
            granted_control = read(v, grant=True)
        return {"main": main, "pref_off": pref_off, "site_off": site_off, "control": control,
                "granted": granted, "granted_control": granted_control}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        for key in ("main", "pref_off", "site_off", "control", "granted", "granted_control"):
            if "error" in obs[key]:
                return "fail", f"the media devices page failed ({key}): {obs[key]['error']}"
        main, control = obs["main"]["devices"], obs["control"]["devices"]
        if not all(blank(d) for d in control):
            return "fail", ("negative control failed: with the noise feature off and no permission the list "
                            "still showed an id or a label, so the page did not run without a permission")
        # The grant runs first: they tell whether the fixture can show a grant at all.
        granted_control = obs["granted_control"]["devices"]
        if not granted_control or not any(d.get("label") for d in granted_control):
            return "fail", ("negative control failed: with a fake camera, a fake microphone and a granted "
                            "permission the list showed no label, so the grant did not work")
        problem = pinned_problem("protection on, no permission", main)
        if problem:
            return "fail", problem
        if shape(control) == shape(main):
            return "error", (f"inconclusive: this machine already lists one empty entry of each kind with the "
                             f"feature off ({', '.join(f'{n} {k}' for k, n in counts(control).items())}), so it "
                             f"cannot show the pin (it needs a machine that lacks a camera, a microphone or a "
                             f"speaker)")
        for key, label in (("pref_off", "fingerprinting pref off"), ("site_off", "site switched off")):
            if shape(obs[key]["devices"]) != shape(control):
                got = counts(obs[key]["devices"])
                return "fail", (f"{label}: enumerateDevices() listed {', '.join(f'{n} {k}' for k, n in got.items())} "
                                f"and not the real list "
                                f"({', '.join(f'{n} {k}' for k, n in counts(control).items())})")
        granted = obs["granted"]["devices"]
        if counts(granted) != counts(granted_control) or observe_labels(granted) != observe_labels(granted_control):
            return "fail", (f"after a grant, with protection on, the page saw {counts(granted)} with labels "
                            f"{observe_labels(granted)}, not the real {counts(granted_control)} with labels "
                            f"{observe_labels(granted_control)}")
        if any(not d.get("deviceId") for d in granted):
            return "fail", "after a grant, with protection on, a device still has an empty deviceId"
        return "pass", (f"with the feature off a page without permission sees "
                        f"{', '.join(f'{n} {k}' for k, n in counts(control).items())}; with protection on it sees "
                        f"one audioinput, one audiooutput and one videoinput, all empty; with the pref off and with "
                        f"the site switched off it sees the real list; after a grant it sees the real "
                        f"{len(granted)} devices with their labels")
