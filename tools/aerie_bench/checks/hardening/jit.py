"""Per-site JavaScript JIT: the upstream javascript-optimizer content setting, turned off for one site.

The setting is Chromium's own (ContentSettingsType::JAVASCRIPT_OPTIMIZER). With it blocked for a
site, that site's renderer starts with ``--js-flags=--disable-optimizing-compilers`` (TurboFan and
Maglev off). The mode is part of the SiteInfo and ``RenderProcessHostImpl::IsSuitableHost`` refuses
to hand a site a process whose mode differs, so the two kinds of site never share a process. See
the ``js_optimizer`` entry of tools/aerie_hardening/controls.json for the code.

The check turns the JIT off through the real Site settings handler (the call the site details page
makes), then measures a hot loop on that site and on another one:

* before the change both sites are fast;
* after it only the chosen site is slow, also when one page embeds the other (so the two modes
  live in one tab, in two processes), and the renderer command lines show a process without the
  optimizing compilers;
* after a clean restart on the same profile it is still so;
* a second browser that never changed the setting shows no difference between the sites.

Timing is the detector and the process flags are a second witness. The judge needs a slowdown of
at least ``MIN_SLOWDOWN`` between the modes; the default mode on both sites must stay below it.
"""

from __future__ import annotations

import json
import time

from ... import procmem
from ...session import Session
from .. import register
from .common import DEEP_JS, HardeningCheck, Verdict, page_value, site

SETTINGS_URL = "chrome://settings/"
V8_PAGE_URL = "chrome://settings/content/v8"
OFF_SITE = "a"
ON_SITE = "b"
MIN_SLOWDOWN = 2.0  # off must be at least this many times slower than on; on must stay below it
JAVASCRIPT_OPTIMIZER_ALLOWED = 1  # JavascriptOptimizerSetting::kAllowed
MEASURE_TIMEOUT = 90.0

SET_JS = """((origin, value) => {
  chrome.send('setOriginPermissions', [origin, 'javascript-optimizer', value]);
  return true;
})(%s, %s)"""


def slowdown(slow: dict, fast: dict) -> float:
    """How many times slower ``slow`` ran than ``fast`` (a measurement is {"ms": ...})."""
    return slow["ms"] / max(fast["ms"], 0.1)


def measure(session: Session, name: str, child: str | None = None) -> dict:
    """Run the timing page on a fixture site, optionally with another site embedded in it."""
    query = f"?child={child}.localhost" if child else ""
    nav = session.navigate_url(f"{site(session, name)}/hardening/jit.html{query}")
    if nav["error"]:
        return {"error": nav["error"]}
    got = page_value(session, "window.__result", timeout=MEASURE_TIMEOUT)
    return got if isinstance(got, dict) else {"error": f"no result: {got!r}"}


def set_site_setting(session: Session, name: str, value: str) -> dict:
    """Set the javascript-optimizer content setting for one fixture site, as Site settings does."""
    nav = session.navigate_url(SETTINGS_URL)
    if nav["error"]:
        return {"ok": False, "error": nav["error"]}
    if not session.wait_for("typeof chrome !== 'undefined' && typeof chrome.send === 'function'", timeout=20):
        return {"ok": False, "error": "chrome.send is not available on the settings page"}
    # No port: the browser looks the setting up by the site URL, which has none (scheme and host),
    # so an exception for http://a.localhost:PORT would never match. Real sites on 80/443 have
    # no port in the rule either.
    origin = f"http://{name}.localhost"
    session.evaluate(SET_JS % (json.dumps(origin), json.dumps(value)))
    time.sleep(1.0)  # the handler runs on the UI thread; the next navigation reads the new rule
    return {"ok": True, "origin": origin, "value": value}


def renderers(session: Session) -> dict | None:
    return procmem.renderer_modes(session.browser_pid) if session.browser_pid else None


def snapshot(session: Session) -> dict:
    """What the two sites measure right now, in one page and across a frame, plus the renderers."""
    return {"off_site": measure(session, OFF_SITE), "on_site": measure(session, ON_SITE),
            "off_top": measure(session, OFF_SITE, child=ON_SITE),
            "on_top": measure(session, ON_SITE, child=OFF_SITE),
            "renderers": renderers(session)}


def settings_defaults(session: Session) -> dict:
    """The JavaScript optimization page loads and its default is on (read through settingsPrivate)."""
    nav = session.navigate_url(V8_PAGE_URL)
    if nav["error"]:
        return {"error": nav["error"]}
    session.wait_for("typeof chrome !== 'undefined' && !!chrome.settingsPrivate", timeout=20)
    page = bool(session.wait_for(DEEP_JS + "!!deepQuery(document, 'settings-v8-page')", timeout=20))
    pref = session.evaluate(
        "new Promise((res) => chrome.settingsPrivate.getPref('generated.javascript_optimizer', (p) => res(p)))",
        await_promise=True) or {}
    return {"page": page, "pref_value": pref.get("value")}


def _frames(m: dict) -> tuple[dict, dict]:
    """(top, framed) measurements of a page that embeds another site."""
    return {"ms": m["ms"]}, m["child"]


def problems_in(obs: dict) -> list[str]:
    errors = []
    for where in ("before", "after", "restarted", "control"):
        for key, m in obs.get(where, {}).items():
            if key != "renderers" and isinstance(m, dict) and "error" in m:
                errors.append(f"{where}.{key}: {m['error']}")
            elif key in ("off_top", "on_top") and isinstance(m, dict) and "error" not in m \
                    and not (isinstance(m.get("child"), dict) and "ms" in m["child"]):
                errors.append(f"{where}.{key}: the framed site did not report")
    return errors


def mode_verdict(label: str, snap: dict) -> str | None:
    """None if this snapshot shows the JIT off on the chosen site only, else why not."""
    off, on = snap["off_site"], snap["on_site"]
    ratio = slowdown(off, on)
    if ratio < MIN_SLOWDOWN:
        return f"{label}: the site with the JIT off ran only {ratio:.1f}x slower than the other (need {MIN_SLOWDOWN}x)"
    top, framed = _frames(snap["off_top"])
    if slowdown(top, framed) < MIN_SLOWDOWN:
        return (f"{label}: with the JIT-on site framed in the JIT-off page both ran at the same speed "
                f"({top['ms']} ms vs {framed['ms']} ms), so they may share a process")
    top, framed = _frames(snap["on_top"])
    if slowdown(framed, top) < MIN_SLOWDOWN:
        return (f"{label}: with the JIT-off site framed in the JIT-on page both ran at the same speed "
                f"({top['ms']} ms vs {framed['ms']} ms), so they may share a process")
    procs = snap.get("renderers")
    if procs is not None:
        if procs["noopt"] < 1:
            return f"{label}: no renderer was started with --disable-optimizing-compilers"
        if procs["total"] - procs["noopt"] < 1:
            return f"{label}: every renderer runs without the optimizing compilers"
    return None


@register
class JsOptimizer(HardeningCheck):
    name = "hardening_js_optimizer"
    control = "js_optimizer"
    negative_control = ("A second browser that never changes the setting measures the same two sites; with the JIT "
                        "on for both they must run at the same speed, so the difference in the main run comes "
                        "from the setting and not from the sites.")

    def observe(self, session: Session) -> dict:
        defaults = settings_defaults(session)
        with session.restartable() as box:
            before = snapshot(box.session)
            change = set_site_setting(box.session, OFF_SITE, "block")
            after = snapshot(box.session)
            box.restart()
            restarted = snapshot(box.session)
        with session.variant() as v:
            control = {"off_site": measure(v, OFF_SITE), "on_site": measure(v, ON_SITE), "renderers": renderers(v)}
        return {"defaults": defaults, "change": change, "before": before, "after": after,
                "restarted": restarted, "control": control}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        errors = problems_in(obs)
        if errors:
            return "fail", "a measurement failed: " + "; ".join(errors)
        d = obs["defaults"]
        if "error" in d or not d.get("page"):
            return "fail", f"the JavaScript optimization settings page did not load ({d.get('error')})"
        if d.get("pref_value") != JAVASCRIPT_OPTIMIZER_ALLOWED:
            return "fail", f"the JIT is not on by default: generated.javascript_optimizer is {d.get('pref_value')!r}"
        if not obs["change"].get("ok"):
            return "fail", f"could not change the setting for a site: {obs['change'].get('error')}"
        c = obs["control"]
        if slowdown(c["off_site"], c["on_site"]) >= MIN_SLOWDOWN:
            return "fail", (f"negative control failed: with the setting untouched site {OFF_SITE} still ran "
                            f"{slowdown(c['off_site'], c['on_site']):.1f}x slower than site {ON_SITE}")
        if c.get("renderers") and c["renderers"]["noopt"]:
            return "fail", "negative control failed: a renderer lacked the optimizing compilers with the setting untouched"
        before = obs["before"]
        if slowdown(before["off_site"], before["on_site"]) >= MIN_SLOWDOWN:
            return "fail", "the JIT was not on for both sites before the change"
        if before.get("renderers") and before["renderers"]["noopt"]:
            return "fail", "a renderer lacked the optimizing compilers before the change"
        for label, key in (("after the change", "after"), ("after a restart", "restarted")):
            why = mode_verdict(label, obs[key])
            if why:
                return "fail", why
        # The other site must not have been slowed by the change.
        if slowdown(obs["after"]["on_site"], before["on_site"]) >= MIN_SLOWDOWN:
            return "fail", f"site {ON_SITE} got slower after the setting changed for site {OFF_SITE}"
        a, r = obs["after"]["off_site"], obs["restarted"]["off_site"]
        return "pass", (f"JIT off for site {OFF_SITE} only: {slowdown(a, obs['after']['on_site']):.1f}x slower than "
                        f"site {ON_SITE}, {slowdown(r, obs['restarted']['on_site']):.1f}x after a restart; framed in "
                        "one page the two sites ran at different speeds, so in different processes; "
                        f"control {slowdown(c['off_site'], c['on_site']):.1f}x")
