"""Third-party cookies and partitioned storage."""

from __future__ import annotations

from ...session import Session
from .. import register
from .common import HardeningCheck, Verdict, page_value, site

ALLOW_THIRD_PARTY_PREFS = {"profile": {"cookie_controls_mode": 2}}  # 2: only blocked in Incognito
STORAGE_VALUE = "a-value"


def probe_cookies(session: Session) -> dict:
    """First party sets a cookie; then a frame from another site tries to read it."""
    session.navigate_url(f"{site(session, 'b')}/hardening/cookie_top.html")
    first = page_value(session, "window.__result")
    session.navigate_url(f"{site(session, 'a')}/hardening/embed.html?site=b.localhost"
                         "&frame=/hardening/cookie_frame.html")
    third = page_value(session, "window.__result")
    return {"first_party": first, "third_party": third}


def cookie_verdict(probe: dict) -> str:
    """blocked, allowed or inconclusive."""
    first, third = probe.get("first_party") or {}, probe.get("third_party") or {}
    if "tp=1" not in str(first.get("cookie", "")):
        return "inconclusive"
    if "error" in third and "cookie" not in third:
        return "inconclusive"
    seen = any(tag in str(third.get(key, "")) for tag in ("tp=1", "tps=1") for key in ("cookie", "echo"))
    return "allowed" if seen else "blocked"


@register
class ThirdPartyCookies(HardeningCheck):
    name = "hardening_third_party_cookies"
    control = "third_party_cookies"
    negative_control = ("A second browser with third-party cookies allowed in its profile lets the same frame "
                        "read the cookie, so an empty read means blocking, not a broken probe.")

    def observe(self, session: Session) -> dict:
        main = probe_cookies(session)
        with session.variant((), ALLOW_THIRD_PARTY_PREFS) as v:
            control = probe_cookies(v)
        return {"main": main, "control": control}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        main, control = cookie_verdict(obs["main"]), cookie_verdict(obs["control"])
        if main == "inconclusive":
            return "fail", f"the probe could not set a first-party cookie on the fixture site: {obs['main']['first_party']}"
        if main == "allowed":
            return "fail", f"a frame from another site could read a cookie: {obs['main']['third_party']}"
        if control != "allowed":
            return "fail", (f"negative control failed: with third-party cookies allowed the probe saw {control}, "
                            "so it cannot tell blocking from a broken probe")
        return "pass", "a cross-site frame could not read or send the cookie; with blocking off the same probe could"


def probe_storage(session: Session, with_write: bool = True) -> dict:
    """Write storage in a frame under top site a, read it under a again and under c."""
    def frame(top: str, mode: str) -> object:
        session.navigate_url(f"{site(session, top)}/hardening/embed.html?site=b.localhost"
                             f"&frame=/hardening/storage_frame.html&mode={mode}&value={STORAGE_VALUE}")
        res = page_value(session, "window.__result") or {}
        return res.get("result", res.get("error"))

    out = {"write": frame("a", "write") if with_write else None}
    out["read_same_top"] = frame("a", "read")
    out["read_other_top"] = frame("c", "read")
    return out


@register
class StoragePartitioning(HardeningCheck):
    name = "hardening_storage_partitioning"
    control = "storage_partitioning"
    negative_control = ("A second browser with partitioning disabled (and third-party cookies allowed, so storage "
                        "is reachable) shows the value to the other top-level site.")

    def observe(self, session: Session) -> dict:
        default_profile = probe_storage(session)
        with session.variant((), ALLOW_THIRD_PARTY_PREFS) as v:
            partitioned = probe_storage(v)
        with session.variant(("--disable-features=ThirdPartyStoragePartitioning",), ALLOW_THIRD_PARTY_PREFS) as v:
            control = probe_storage(v)
        return {"default_profile": default_profile, "partitioned": partitioned, "control": control}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        part, ctl = obs["partitioned"], obs["control"]
        if part["write"] != "ok" or part["read_same_top"] != STORAGE_VALUE:
            return "fail", f"storage did not work under its own top-level site, so the probe is inconclusive: {part}"
        if part["read_other_top"] == STORAGE_VALUE:
            return "fail", "a frame under another top-level site read the stored value: storage is shared"
        if obs["default_profile"]["read_other_top"] == STORAGE_VALUE:
            return "fail", "with the default profile a frame under another top-level site read the stored value"
        if ctl["read_other_top"] != STORAGE_VALUE:
            return "fail", (f"negative control failed: with partitioning disabled the other site read "
                            f"{ctl['read_other_top']!r}, so the probe cannot see shared storage")
        return "pass", "the same frame saw its value under site a and none under site c; with partitioning off it saw it under both"
