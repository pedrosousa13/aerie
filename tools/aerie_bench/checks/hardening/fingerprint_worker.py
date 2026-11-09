"""Workers get the page's fingerprinting noise (issue #113).

A fingerprinter must not be able to move its code into a worker and read clean values. The fixture page
measures a canvas read-back (``getImageData``) and ``navigator.hardwareConcurrency`` in the page, in a dedicated
worker and in a shared worker. For one site the three must agree; ``a.localhost`` and ``b.localhost`` are two
sites, so a canvas hash must differ between them in every kind of context.

Four browsers:

* the regular browser on ``a`` and ``b``: the worker values equal the window's, and differ across sites;
* a browser with protection off for ``http://[*.]a.localhost``: every context of ``a`` shows the real
  values (the per-site switch reaches workers) and ``b`` stays protected;
* a browser with protection off for the whole browser (``helium.noise.enabled`` false): every context of
  both sites shows the real values;
* the negative control: ``--disable-features=HeliumNoise``. It gives the real values, the same in every
  context and on both sites, so a difference really is noise and the values "off" must restore are known.

Core count has only three possible noised values, so it is compared for equality inside a site and with
the real value when protection is off, never for difference between sites. The service worker path is
covered by the source gate ``worker_noise_tokens`` (``./aerie patches gates``), not by this check.
"""

from __future__ import annotations

from ...session import Session
from .. import register
from .common import HardeningCheck, Verdict, page_value, site
from .fingerprint_site import BLOCK, SITE_PATTERN, seeded_exceptions

CONTEXTS = ("window", "dedicated", "shared")
KINDS = ("canvas", "cores")
SHOWN = {"window": "window", "dedicated": "dedicated worker", "shared": "shared worker"}


def worker_fingerprint(session: Session, host: str) -> dict:
    session.navigate_url(f"{site(session, host)}/hardening/fingerprint_worker.html")
    return page_value(session, "window.__result")


def readings(session: Session, hosts: tuple[str, ...]) -> dict:
    return {host: worker_fingerprint(session, host) for host in hosts}


@register
class FingerprintingWorker(HardeningCheck):
    name = "hardening_fingerprinting_worker"
    control = "fingerprinting"
    negative_control = ("A second browser with the noise feature disabled gives the same real values in the page, in a "
                        "dedicated worker and in a shared worker, on both sites, so a difference really comes from the "
                        "noise and its values are what turning protection off must restore in workers.")

    def observe(self, session: Session) -> dict:
        obs: dict = {"regular": readings(session, ("a", "b"))}
        with session.variant((), seeded_exceptions({SITE_PATTERN: BLOCK})) as v:
            obs["site_off"] = readings(v, ("a", "b"))
        with session.variant((), prefs={"helium": {"noise": {"enabled": False}}}) as v:
            obs["global_off"] = readings(v, ("a", "b"))
        with session.variant(("--disable-features=HeliumNoise",)) as v:
            obs["control"] = readings(v, ("a", "b"))
        return obs

    @staticmethod
    def judge(obs: dict) -> Verdict:
        for group, per_host in obs.items():
            for host, fp in per_host.items():
                if "error" in fp:
                    return "fail", f"the worker fingerprint page failed ({group}.{host}): {fp['error']}"
                for ctx in CONTEXTS:
                    reading = fp.get(ctx)
                    if not isinstance(reading, dict) or "error" in reading:
                        why = reading.get("error") if isinstance(reading, dict) else "no reading"
                        return "fail", f"the {SHOWN[ctx]} gave no reading ({group}.{host}): {why}"
        # The negative control: with noise disabled every context and both sites show one real value.
        control = obs["control"]
        real = control["a"]["window"]
        for host in ("a", "b"):
            for ctx in CONTEXTS:
                for kind in KINDS:
                    if control[host][ctx][kind] != real[kind]:
                        return "fail", (f"negative control failed: with noise disabled the {kind} of the {SHOWN[ctx]} on "
                                        f"{host}.localhost is {control[host][ctx][kind]}, not {real[kind]}")
        regular = obs["regular"]
        for host in ("a", "b"):
            for ctx in ("dedicated", "shared"):
                for kind in KINDS:
                    if regular[host][ctx][kind] != regular[host]["window"][kind]:
                        return "fail", (f"the {kind} of the {SHOWN[ctx]} on {host}.localhost differs from the window's "
                                        f"({regular[host][ctx][kind]} vs {regular[host]['window'][kind]})")
        for ctx in CONTEXTS:
            if regular["a"][ctx]["canvas"] == real["canvas"]:
                return "fail", f"the canvas of the {SHOWN[ctx]} on a.localhost equals the no-noise control, so it is not noised"
            if regular["a"][ctx]["canvas"] == regular["b"][ctx]["canvas"]:
                return "fail", (f"the canvas of the {SHOWN[ctx]} is the same on a.localhost and b.localhost, two sites "
                                f"({regular['a'][ctx]['canvas']})")
        # The per-site switch reaches workers (S6), and only for its site.
        for ctx in CONTEXTS:
            for kind in KINDS:
                if obs["site_off"]["a"][ctx][kind] != real[kind]:
                    return "fail", (f"with protection off for {SITE_PATTERN} the {kind} of the {SHOWN[ctx]} on a.localhost "
                                    f"is still protected ({obs['site_off']['a'][ctx][kind]})")
            if obs["site_off"]["b"][ctx]["canvas"] == real["canvas"]:
                return "fail", f"turning protection off for {SITE_PATTERN} turned off another site (the {SHOWN[ctx]} on b.localhost)"
        # The global switch reaches workers (S6).
        for host in ("a", "b"):
            for ctx in CONTEXTS:
                for kind in KINDS:
                    if obs["global_off"][host][ctx][kind] != real[kind]:
                        return "fail", (f"with protection turned off for the whole browser the {kind} of the {SHOWN[ctx]} "
                                        f"on {host}.localhost is still protected ({obs['global_off'][host][ctx][kind]})")
        return "pass", ("canvas and core count in a dedicated worker and in a shared worker equal the window's on the same "
                        "site, and the canvas differs across sites; turning protection off for the site or for the whole "
                        "browser restores the real values in workers")
