"""WebRTC IP leak protection: no local address in any ICE candidate."""

from __future__ import annotations

import ipaddress

from ...session import Session
from .. import register
from .common import HardeningCheck, Verdict, page_value, site

LEAKY_PREFS = {"webrtc": {"ip_handling_policy": "default"}}
LEAKY_FLAGS = ("--disable-features=WebRtcHideLocalIpsWithMdns",)


def parse_candidate(line: str) -> dict:
    """'candidate:1 1 udp 2113937151 192.168.1.5 5000 typ host ...' -> {address, type}."""
    parts = line.removeprefix("a=").removeprefix("candidate:").split()
    out = {"address": parts[4] if len(parts) > 4 else "", "type": ""}
    if "typ" in parts and parts.index("typ") + 1 < len(parts):
        out["type"] = parts[parts.index("typ") + 1]
    return out


def is_local_address(address: str) -> bool:
    if address.endswith(".local"):
        return True  # an mDNS name stands in for a local address
    try:
        ip = ipaddress.ip_address(address)
    except ValueError:
        return False
    return ip.is_private or ip.is_link_local or ip.is_loopback


def is_leak(line: str) -> bool:
    """A candidate that exposes the device: a local address or an mDNS name. A host candidate with a public
    address is not a leak. A server-reflexive candidate on loopback comes from the Bench's own STUN server
    and only shows that gathering ran."""
    c = parse_candidate(line)
    if c["type"] == "srflx" and c["address"] == "127.0.0.1":
        return False
    return is_local_address(c["address"])


def leaks(candidates: list[str]) -> list[str]:
    return [line for line in candidates if is_leak(line)]


def raw_local_leaks(candidates: list[str]) -> list[str]:
    return [line for line in candidates
            if (c := parse_candidate(line))["type"] == "host" and not c["address"].endswith(".local")
            and is_local_address(c["address"])]


def gather(session: Session) -> dict:
    with session.stun_probe() as stun:
        session.navigate_url(f"{site(session, 'a')}/hardening/webrtc.html?stun={stun.port}")
        got = page_value(session, "({state: window.__state, candidates: window.__candidates})", timeout=20)
        if "state" not in got:
            return {"state": "error", "candidates": [], "error": got.get("error"), "stun_requests": stun.requests}
        return dict(got, stun_requests=stun.requests)


@register
class WebRtc(HardeningCheck):
    name = "hardening_webrtc"
    control = "webrtc_ip"
    negative_control = ("A second browser with the default policy and mDNS hiding off exposes a raw local "
                        "address to the same fixture, so zero candidates means protection, not a broken probe.")

    def observe(self, session: Session) -> dict:
        main = gather(session)
        with session.variant(LEAKY_FLAGS, LEAKY_PREFS) as v:
            control = gather(v)
        return {"main": main, "control": control}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        main, control = obs["main"], obs["control"]
        if main["state"] != "complete":
            return "fail", f"ICE gathering did not finish ({main['state']}), so the result is not trustworthy"
        found = leaks(main["candidates"])
        if found:
            return "fail", f"the page saw {len(found)} local candidate(s), e.g. {found[0]}"
        if not main["candidates"]:
            return "error", ("inconclusive: no candidate at all, not even one from the loopback STUN server, so "
                             "nothing shows that gathering ran")
        if not raw_local_leaks(control["candidates"]):
            return "fail", ("negative control failed: with the default policy and mDNS off the page still saw no "
                            "raw local address (is this machine offline?)")
        return "pass", (f"{len(main['candidates'])} candidate(s), none local; the control browser exposed "
                        f"{len(raw_local_leaks(control['candidates']))} raw local candidate(s)")
