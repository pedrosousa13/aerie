"""Device memory cap: navigator.deviceMemory and Sec-CH-Device-Memory are at most 8 (issue #115).

Aerie patch hardening/0012 lowers the desktop maximum from 32 to 8, behind the HeliumNoise feature
only. The cap does not follow the fingerprinting pref or the per-site switch, so the check runs
once with the pref off and once with a per-site HELIUM_NOISE exception for the fixture origin. A machine with 8 GB or less reports its real bucket either way, so
the check cannot tell the cap from the machine: it is inconclusive there, never a pass.
"""

from __future__ import annotations

from ...session import Session
from .. import register
from .common import HardeningCheck, Verdict, fixture_hits, page_value, site

# The cap in GB. This is the ONE place the Python side states it. It must equal `kMaxMemory = 8.0f` in
# ApproximatedDeviceMemory::CalculateAndSetApproximatedDeviceMemory
# (third_party/blink/common/device_memory/approximated_device_memory.cc), set by patch hardening/0012.
CAP = 8.0
PREF_OFF = {"helium": {"noise": {"enabled": False}}}
CONTENT_SETTING_BLOCK = 2


def site_off_prefs(host_port: str) -> dict:
    """Profile prefs with fingerprinting protection switched off for one origin: a BLOCK exception in the
    HELIUM_NOISE content setting (pref name "helium-noise", registered in content_settings_registry.cc by
    helium-chromium core/noise/core.patch; the primary pattern is the origin, the secondary is "*")."""
    return {"profile": {"content_settings": {"exceptions": {
        "helium-noise": {f"http://{host_port},*": {"setting": CONTENT_SETTING_BLOCK}}}}}}


def parse_hint(value: str | None) -> float | None:
    try:
        return float(value) if value else None
    except ValueError:
        return None


def read(session: Session, tag: str) -> dict:
    """Load the page twice: the first load sees the Accept-CH opt-in, the second sends the hint."""
    base = site(session, "a")
    url = f"{base}/hardening/device-memory.html?v={tag}"
    session.navigate_url(url)
    session.navigate_url(url)
    got = page_value(session, "window.__result")
    if "error" in got:
        return got
    probe = f"/hardening/device-memory-probe.txt?v={tag}"
    hits = fixture_hits(session, f"a.localhost:{session.fixtures.port}", probe)
    got["header"] = (hits[-1]["sec_ch_device_memory"] or None) if hits else None
    got["legacy_header"] = (hits[-1]["device_memory"] or None) if hits else None
    return got


@register
class DeviceMemory(HardeningCheck):
    name = "hardening_device_memory"
    control = "fingerprinting"
    negative_control = ("A second browser with the noise feature disabled reports the machine's own bucket in the "
                        "window, in a worker and in the Sec-CH-Device-Memory header, so a value of 8 or less in the "
                        "first browser really comes from the cap.")

    def observe(self, session: Session) -> dict:
        main = read(session, "main")
        with session.variant((), PREF_OFF) as v:
            pref_off = read(v, "pref_off")
        with session.variant((), site_off_prefs(f"a.localhost:{session.fixtures.port}")) as v:
            site_off = read(v, "site_off")
        with session.variant(("--disable-features=HeliumNoise",)) as v:
            control = read(v, "control")
        return {"main": main, "pref_off": pref_off, "site_off": site_off, "control": control}

    @staticmethod
    def _problem(label: str, side: dict) -> str | None:
        window, worker = side.get("window"), side.get("worker")
        header, legacy = parse_hint(side.get("header")), parse_hint(side.get("legacy_header"))
        if window is None or window > CAP:
            return f"{label}: navigator.deviceMemory is {window}, not at most {CAP:g}"
        if worker is None or worker > CAP:
            return f"{label}: the worker saw navigator.deviceMemory {worker}, not at most {CAP:g}"
        if header is None:
            return f"{label}: the server saw no Sec-CH-Device-Memory header after the Accept-CH opt-in"
        if header > CAP:
            return f"{label}: the server saw Sec-CH-Device-Memory {header:g}, not at most {CAP:g}"
        if legacy is None:
            return f"{label}: the server saw no Device-Memory header after the Accept-CH opt-in"
        if legacy > CAP:
            return f"{label}: the server saw Device-Memory header {legacy:g}, not at most {CAP:g}"
        return None

    @staticmethod
    def judge(obs: dict) -> Verdict:
        for key in ("main", "pref_off", "site_off", "control"):
            if "error" in obs[key]:
                return "fail", f"the device memory page failed ({key}): {obs[key]['error']}"
        control = obs["control"]
        if control.get("window") is None:
            return "fail", ("negative control failed: navigator.deviceMemory is missing with the cap off, "
                            "so the API did not work")
        if control["window"] <= CAP:
            return "error", (f"inconclusive: this machine reports {control['window']} GB with the cap off, "
                             f"so it cannot show the cap (it needs more than {CAP:g} GB of memory)")
        header, legacy = parse_hint(control.get("header")), parse_hint(control.get("legacy_header"))
        if header is None or legacy is None:
            return "fail", ("negative control failed: with the cap off the server saw no "
                            "Sec-CH-Device-Memory or Device-Memory header, so the Accept-CH opt-in did not work")
        if header <= CAP or legacy <= CAP or (control.get("worker") or 0) <= CAP:
            return "fail", (f"negative control failed: with the cap off the headers were {header:g} and {legacy:g} "
                            f"and the worker saw {control.get('worker')}, not the machine's {control['window']:g}")
        for key, label in (("main", "protection on"), ("pref_off", "fingerprinting pref off"),
                           ("site_off", "site switched off")):
            problem = DeviceMemory._problem(label, obs[key])
            if problem:
                return "fail", problem
        return "pass", (f"with the cap off this machine reports {control['window']:g} GB; with the cap it reports at "
                        f"most {CAP:g} in the window, in a worker, in Sec-CH-Device-Memory and in Device-Memory, "
                        "also with the fingerprinting pref off and with the site switched off")
