"""Fingerprinting protection, graphics card (issue #112): WebGL reports a vendor family, WebGPU nothing.

With protection on, UNMASKED_VENDOR_WEBGL and UNMASKED_RENDERER_WEBGL are one of the family strings
(Chrome's format, for example ``Google Inc. (AMD)``), with no digit beyond the GL ES version, in the page
and in a worker. WebGPU adapter info is empty, or there is no adapter (the result says which). The strings
come from components/aerie_gpu_info/gpu_info.cc; a unit test keeps ``MASKED`` equal to that file.

Negative controls, each a second browser: ``--disable-features=HeliumNoiseWebGL`` must show the real
strings, and so must protection turned off for the whole browser or for one site. The site that still has
protection on in the per-site browser must stay masked. Workers are not judged in those browsers: a worker
has no tokens yet (issue #113), so it masks even when its site has protection off.
"""

from __future__ import annotations

import re

from ...session import Session
from .. import register
from .common import HardeningCheck, Verdict, page_value, site

# (vendor, renderer) per family, the same strings as MaskedVendor and MaskedRenderer in gpu_info.cc.
MASKED = {
    "AMD": ("Google Inc. (AMD)", "ANGLE (AMD, AMD Radeon Graphics, OpenGL ES 3.2)"),
    "Intel": ("Google Inc. (Intel)", "ANGLE (Intel, Mesa Intel(R) Graphics, OpenGL ES 3.2)"),
    "NVIDIA": ("Google Inc. (NVIDIA)", "ANGLE (NVIDIA, NVIDIA GeForce Graphics, OpenGL ES 3.2)"),
    "Mesa": ("Google Inc. (Mesa)", "ANGLE (Mesa, llvmpipe, OpenGL ES 3.2)"),
}
GL_VERSION = "OpenGL ES 3.2"
WEBGPU_FIELDS = ("vendor", "architecture", "device", "description")
FEATURE_FLAG = "--disable-features=HeliumNoiseWebGL"


def site_off_prefs(origin: str) -> dict:
    """Preferences that turn fingerprinting protection off for one origin.

    Helium's per-site switch is the HELIUM_NOISE content setting, registered as "helium-noise" with the
    top-origin-only scope. Chromium stores it in Preferences with an underscore, as
    content_settings.exceptions.helium_noise (a hyphenated key is ignored). An exception key is
    "<primary pattern>,<secondary pattern>": the origin and "*". CONTENT_SETTING_BLOCK is 2, and Helium reads
    BLOCK as "noise off for this site" (GetHeliumNoiseState gives kDisabledForSite).
    """
    return {"profile": {"content_settings": {"exceptions": {"helium_noise": {f"{origin},*": {"setting": 2}}}}}}


def read_gpu(session: Session, name: str) -> dict:
    session.navigate_url(f"{site(session, name)}/hardening/gpu.html")
    return page_value(session, "window.__result")


def family_of(pair: dict | None) -> str | None:
    """The family whose masked strings equal this (vendor, renderer) pair, or None."""
    if not pair or "error" in pair:
        return None
    for family, (vendor, renderer) in MASKED.items():
        if pair.get("vendor") == vendor and pair.get("renderer") == renderer:
            return family
    return None


def digits_beyond_version(text: str) -> bool:
    return bool(re.search(r"\d", text.replace(GL_VERSION, "")))


def webgpu_state(info: dict) -> str:
    """'empty', 'no adapter', 'real' or 'error' for the WebGPU part of one reading."""
    gpu = info.get("webgpu") or {}
    if "error" in gpu:
        return "error"
    if not gpu.get("adapter"):
        return "no adapter"
    return "empty" if all(gpu.get(f, "") == "" for f in WEBGPU_FIELDS) else "real"


@register
class FingerprintingGpu(HardeningCheck):
    name = "hardening_fingerprinting_gpu"
    control = "fingerprinting"
    negative_control = ("Three more browsers read the same page: one with --disable-features=HeliumNoiseWebGL, one with "
                        "fingerprinting protection off for the whole browser and one with it off for the site a.localhost. "
                        "Each must show the real graphics strings, and b.localhost in the last one must stay masked.")

    def observe(self, session: Session) -> dict:
        a = read_gpu(session, "a")
        b = read_gpu(session, "b")
        with session.variant((FEATURE_FLAG,)) as v:
            feature_off = {"a": read_gpu(v, "a")}
        with session.variant((), prefs={"helium": {"noise": {"enabled": False}}}) as v:
            global_off = {"a": read_gpu(v, "a")}
        origin = site(session, "a")
        site_prefs = site_off_prefs(origin)
        with session.variant((), prefs=site_prefs) as v:
            site_off = {"a": read_gpu(v, "a"), "b": read_gpu(v, "b")}
        return {"a": a, "b": b, "feature_off": feature_off, "global_off": global_off, "site_off": site_off}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        readings = {"a": obs["a"], "b": obs["b"], "feature_off": obs["feature_off"]["a"],
                    "global_off": obs["global_off"]["a"], "site_off_a": obs["site_off"]["a"],
                    "site_off_b": obs["site_off"]["b"]}
        for key, reading in readings.items():
            if "error" in reading:
                return "fail", f"the GPU page failed ({key}): {reading['error']}"
            if not reading.get("webgl"):
                return "fail", f"no WebGL with WEBGL_debug_renderer_info here ({key}): the check cannot run"
        # The negative controls first: with protection off the strings must be the real ones.
        control = readings["feature_off"]["webgl"]
        if family_of(control):
            return "fail", ("negative control failed: with HeliumNoiseWebGL disabled the strings are still the family "
                            f"strings ({control['vendor']} / {control['renderer']})")
        for key in ("global_off", "site_off_a"):
            got = readings[key]["webgl"]
            if got != control:
                return "fail", (f"with protection off ({key}) WebGL did not give the real strings: "
                                f"{got['vendor']} / {got['renderer']} instead of {control['vendor']} / {control['renderer']}")
        # Protection on.
        families = {}
        for key in ("a", "b", "site_off_b"):
            webgl = readings[key]["webgl"]
            family = family_of(webgl)
            if not family:
                return "fail", (f"WebGL is not masked on {key}: {webgl['vendor']} / {webgl['renderer']} is not a family string"
                                if key != "site_off_b" else
                                f"b.localhost lost protection when only a.localhost was turned off: "
                                f"{webgl['vendor']} / {webgl['renderer']}")
            for text in webgl.values():
                if digits_beyond_version(text):
                    return "fail", f"a masked WebGL string holds digits beyond the GL version: {text}"
            families[key] = family
        if len(set(families.values())) != 1:
            return "fail", f"the family differs between pages of one browser: {families}"
        for key in ("a", "b"):
            worker = readings[key].get("worker")
            if family_of(worker) != families[key]:
                return "fail", f"a worker on {key} did not report the family strings: {worker}"
        # WebGPU.
        states = {key: webgpu_state(readings[key]) for key in readings}
        for key in ("a", "b", "site_off_b"):
            if states[key] == "real":
                return "fail", f"WebGPU adapter info is not empty on {key}: {readings[key]['webgpu']}"
            if states[key] == "error":
                return "fail", f"the WebGPU read failed on {key}: {readings[key]['webgpu']['error']}"
        if states["a"] == "empty" and states["feature_off"] != "real":
            return "fail", ("negative control failed: with HeliumNoiseWebGL disabled the WebGPU adapter info is still empty "
                            "(it must show the real values)")
        if states["a"] == "empty":
            for key in ("global_off", "site_off_a"):
                if states[key] != "real":
                    return "fail", f"with protection off ({key}) WebGPU adapter info is still empty"
        webgpu = ("WebGPU adapter info empty" if states["a"] == "empty"
                  else "WebGPU gave no adapter here, so its info was not tested")
        return "pass", (f"WebGL reports the {families['a']} family strings, on the page and in a worker; {webgpu}; "
                        "the real strings come back with the feature, the whole browser or the site turned off")
