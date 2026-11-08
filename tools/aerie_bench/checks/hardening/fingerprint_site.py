"""Fingerprinting noise is keyed on the site, not the origin (issue #114).

``x.a.localhost`` and ``y.a.localhost`` are one site (scheme plus eTLD+1, and ``localhost`` is an
unknown registry, so the registrable domain is ``a.localhost``). They must show the same canvas,
audio and core count; ``a.localhost`` and ``b.localhost`` are two sites and must differ. The seeds
are held in memory per browser context (Helium's ``HeliumNoiseTokenData`` is user data of the
``BrowserContext``, filled with ``base::RandUint64()``), so an incognito context (its own browser
context, made with CDP ``Target.createBrowserContext``) has fresh seeds. The switch pattern carries
the scheme, as the key does.

The per-site switch of an off-the-record profile is not driven here. The only driver-free way to set
it is the Site settings handler, and ``chrome://settings`` cannot open in an off-the-record profile
(``SettingsUI`` dereferences a null ``ChromeSigninClientFactory::GetForProfile`` there, and upstream
never runs it for such a profile), so the step crashed the browser. That rule (set in an
off-the-record ``HostContentSettingsMap``: covers the site, stays in memory, never reaches the
regular profile or the preferences) is tested in ``aerie_fingerprint_unittests``
(``noise_switch_unittest.cc``).

The check measures, on one browser unless noted:

* the regular profile on ``x.a``, ``y.a``, ``a`` and ``b``;
* two incognito contexts, one after the other, on ``a`` (S3);
* the regular profile again after the incognito contexts: its values are unchanged;
* a browser where protection is turned off for ``http://[*.]a.localhost`` through the Site settings
  handler of its regular profile: both hosts of the site show the real values and ``b`` does not;
* a browser that starts with an old origin-scoped exception for ``x.a.localhost``: it covers the
  whole site (the migration of #114);
* a browser that starts with an ``http://[*.]a.localhost`` exception;
* the negative control: a browser with ``--disable-features=HeliumNoise``. It gives the real values
  that "off" must restore, and the same values on both sites, so a difference really is noise.

Core count has only three possible noised values, so it is checked for equality inside a site and
for equality with the real value when protection is off, never for difference.
"""

from __future__ import annotations

import json

from ...session import Session
from .. import register
from .common import HardeningCheck, Verdict, page_value, site

KINDS = ("canvas", "audio", "cores")
DIFFERING_KINDS = ("canvas", "audio")  # a hash that must differ between sites and contexts
SETTINGS_URL = "chrome://settings/"
SITE_PATTERN = "http://[*.]a.localhost"  # scheme-specific, like the key: the fixture serves plain http
BLOCK = 2  # CONTENT_SETTING_BLOCK in a profile's content_settings exceptions
SET_OFF_JS = """((pattern) => {
  chrome.send('setCategoryPermissionForPattern', [pattern, '', 'helium-noise', 'block', false]);
  return true;
})(%s)"""


def fingerprint(session: Session, host: str) -> dict:
    session.navigate_url(f"{site(session, host)}/hardening/fingerprint.html")
    return page_value(session, "window.__result")


def seeded_exceptions(patterns: dict[str, int]) -> dict:
    """Preferences that hold helium-noise exceptions, as a profile stores them: under the pref name
    ``helium_noise`` (the content type name with '-' replaced by '_'), keyed "primary,secondary"."""
    return {"profile": {"content_settings": {"exceptions": {
        "helium_noise": {f"{pattern},*": {"setting": setting} for pattern, setting in patterns.items()}}}}}


def turn_off_for_site(session: Session, pattern: str = SITE_PATTERN) -> bool:
    """Block the noise for a site pattern through the settings page's own handler (the call its
    'add a site' dialog makes), in the profile that page belongs to. Regular profiles only:
    chrome://settings does not open off the record."""
    nav = session.navigate_url(SETTINGS_URL)
    if nav["error"]:
        return False
    return bool(session.evaluate(SET_OFF_JS % json.dumps(pattern)))


def hosts(session: Session, names: tuple[str, ...]) -> dict:
    return {name: fingerprint(session, name) for name in names}


def describe(host: str) -> str:
    return f"{host}.localhost"


@register
class FingerprintingSite(HardeningCheck):
    name = "hardening_fingerprinting_site"
    control = "fingerprinting"
    negative_control = ("A second browser with the noise feature disabled gives the same real values on both sites, so "
                        "a difference between sites or contexts really comes from the noise, and its values are what "
                        "turning protection off must restore.")

    def observe(self, session: Session) -> dict:
        obs: dict = {}
        obs["regular"] = hosts(session, ("x.a", "y.a", "a", "b"))
        with session.incognito() as inc:
            obs["incognito_1"] = hosts(inc, ("a",))
        with session.incognito() as inc:
            obs["incognito_2"] = hosts(inc, ("a",))
        obs["regular_after"] = hosts(session, ("a",))
        port = session.fixtures.port
        legacy = seeded_exceptions({f"http://{describe('x.a')}:{port}": BLOCK})
        with session.variant((), legacy) as v:
            obs["legacy_exception"] = hosts(v, ("x.a", "y.a", "b"))
        with session.variant(()) as v:
            turned_off = turn_off_for_site(v)
            obs["handler_off"] = hosts(v, ("x.a", "y.a", "b")) if turned_off else {
                "x.a": {"error": "could not open the settings page"}}
        with session.variant((), seeded_exceptions({SITE_PATTERN: BLOCK})) as v:
            obs["site_off"] = hosts(v, ("x.a", "y.a", "b"))
        with session.variant(("--disable-features=HeliumNoise",)) as v:
            obs["control"] = hosts(v, ("a", "b"))
        return obs

    @staticmethod
    def judge(obs: dict) -> Verdict:
        for group, per_host in obs.items():
            for host, fp in per_host.items():
                if "error" in fp:
                    return "fail", f"the fingerprint page failed ({group}.{host}): {fp['error']}"
        control = obs["control"]
        for kind in KINDS:
            if control["a"][kind] != control["b"][kind]:
                return "fail", (f"negative control failed: with noise disabled the {kind} still differed between sites "
                                f"({control['a'][kind]} vs {control['b'][kind]})")
        real = control["a"]
        regular = obs["regular"]
        for group in ("regular", "incognito_1", "incognito_2", "handler_off",
                      "regular_after", "legacy_exception", "site_off"):
            for host, fp in obs[group].items():
                if fp.get("audio_again") != fp.get("audio"):
                    return "fail", f"audio changed between two renders in one page ({group} {describe(host)})"

        # The same site shows the same values; this is the step that fails on origin keying.
        for kind in KINDS:
            for other in ("y.a", "a"):
                if regular["x.a"][kind] != regular[other][kind]:
                    return "fail", (f"{kind} differs between {describe('x.a')} and {describe(other)}, which are "
                                    f"one site ({regular['x.a'][kind]} vs {regular[other][kind]})")
        for kind in DIFFERING_KINDS:
            if regular["a"][kind] == regular["b"][kind]:
                return "fail", f"{kind} is the same on {describe('a')} and {describe('b')}, two sites ({regular['a'][kind]})"
            if regular["a"][kind] == real[kind]:
                return "fail", f"{kind} equals the no-noise control, so the noise did nothing"

        # Fresh seeds in an incognito context (S3).
        for kind in DIFFERING_KINDS:
            first, second = obs["incognito_1"]["a"][kind], obs["incognito_2"]["a"][kind]
            if first == regular["a"][kind] or second == regular["a"][kind]:
                return "fail", f"{kind} in an incognito context is the same as in the regular profile ({first})"
            if first == second:
                return "fail", f"{kind} is the same in two incognito contexts ({first})"

        # A per-site switch set through the settings handler (S4, S6).
        off = obs["handler_off"]
        for host in ("x.a", "y.a"):
            for kind in KINDS:
                if off[host][kind] != real[kind]:
                    return "fail", (f"after turning protection off for {SITE_PATTERN}, "
                                    f"handler_off {describe(host)} still shows a protected {kind}")
        for kind in DIFFERING_KINDS:
            if off["b"][kind] == real[kind]:
                return "fail", f"turning protection off for {SITE_PATTERN} turned off another site ({describe('b')})"
        for kind in DIFFERING_KINDS:
            if obs["regular_after"]["a"][kind] == real[kind] or obs["regular_after"]["a"][kind] != regular["a"][kind]:
                return "fail", (f"the {kind} in the regular profile changed or lost protection after the incognito "
                                f"contexts ran for {describe('a')}")

        # Exceptions that are on disk when the browser starts.
        for group, why in (("legacy_exception", "an old origin exception for x.a.localhost"),
                           ("site_off", f"an exception for {SITE_PATTERN}")):
            for host in ("x.a", "y.a"):
                for kind in KINDS:
                    if obs[group][host][kind] != real[kind]:
                        return "fail", f"with {why}, {group} {describe(host)} still shows a protected {kind}"
            for kind in DIFFERING_KINDS:
                if obs[group]["b"][kind] == real[kind]:
                    return "fail", f"with {why}, {describe('b')} lost protection too: it turned off another site"
        return "pass", ("canvas, audio and cores agree inside one site and canvas and audio differ across sites and "
                        "across incognito contexts; turning protection off covers the whole site, and an old origin "
                        "exception and a site exception from the profile cover the site")
