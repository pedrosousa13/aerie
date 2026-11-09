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

Three more things are judged:

* every measurement is tied to a renderer by pid: the renderer whose CPU time grew during the
  hot loop is the one that hosts the page, and it must be the one started without the
  optimizing compilers for the site with the JIT off (and a different one for the other site),
  so a pass does not rest on process counts alone;
* the rule is also written the way the Site settings "Add site" dialog writes it
  (``setCategoryPermissionForPattern`` with a host pattern), in a profile of its own;
* a rule for a subdomain only is visited twice, leaving the page (about:blank) in between. A real
  build ran the JIT off on both visits, because the rule is read when the navigation starts; the
  check asserts that and fails if either visit runs with the JIT on.

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
SUB_PARENT = "b"  # the site that has no rule
SUB_SITE = "s.b"  # a subdomain of SUB_PARENT: the only host the subdomain rule names
JAVASCRIPT_OPTIMIZER_ALLOWED = 1  # JavascriptOptimizerSetting::kAllowed
MEASURE_TIMEOUT = 90.0

SET_JS = """((origin, value) => {
  chrome.send('setOriginPermissions', [origin, 'javascript-optimizer', value]);
  return true;
})(%s, %s)"""


def slowdown(slow: dict, fast: dict) -> float:
    """How many times slower ``slow`` ran than ``fast`` (a measurement is {"ms": ...})."""
    return slow["ms"] / max(fast["ms"], 0.1)


ADD_SITE_JS = """((pattern, value) => {
  chrome.send('setCategoryPermissionForPattern', [pattern, '', 'javascript-optimizer', value, false]);
  return true;
})(%s, %s)"""


def measure(session: Session, name: str, child: str | None = None) -> dict:
    """Run the timing page on a fixture site, optionally with another site embedded in it."""
    query = f"?child={child}.localhost" if child else ""
    nav = session.navigate_url(f"{site(session, name)}/hardening/jit.html{query}")
    if nav["error"]:
        return {"error": nav["error"]}
    got = page_value(session, "window.__result", timeout=MEASURE_TIMEOUT)
    return got if isinstance(got, dict) else {"error": f"no result: {got!r}"}


def measure_pinned(session: Session, name: str) -> dict:
    """``measure`` plus the pid of the renderer that hosted the page and the renderer table.

    The pid is the renderer whose CPU time grew most while the page ran its loop. Both are None
    where /proc is not available, and the pid is None when no renderer clearly did the work.
    """
    before = procmem.renderer_cpu(session.browser_pid) if session.browser_pid else None
    got = measure(session, name)
    if "error" in got:
        return got
    after = procmem.renderer_cpu(session.browser_pid) if session.browser_pid else None
    return {**got, "pid": procmem.busiest(before, after), "renderers": renderers(session)}


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


def add_site_rule(session: Session, pattern: str, value: str = "block") -> dict:
    """Write a javascript-optimizer rule the way the Site settings "Add site" dialog does."""
    nav = session.navigate_url(SETTINGS_URL)
    if nav["error"]:
        return {"ok": False, "error": nav["error"]}
    if not session.wait_for("typeof chrome !== 'undefined' && typeof chrome.send === 'function'", timeout=20):
        return {"ok": False, "error": "chrome.send is not available on the settings page"}
    session.evaluate(ADD_SITE_JS % (json.dumps(pattern), json.dumps(value)))
    time.sleep(1.0)
    return {"ok": True, "pattern": pattern, "value": value}


def renderers(session: Session) -> dict | None:
    return procmem.renderer_modes(session.browser_pid) if session.browser_pid else None


def snapshot(session: Session) -> dict:
    """What the two sites measure right now, in one page and across a frame, plus the renderers."""
    return {"off_site": measure_pinned(session, OFF_SITE), "on_site": measure_pinned(session, ON_SITE),
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
    for where in ("before", "after", "restarted", "control", "add_site", "subdomain"):
        for key, m in obs.get(where, {}).items():
            if key != "renderers" and isinstance(m, dict) and "error" in m:
                errors.append(f"{where}.{key}: {m['error']}")
            elif key in ("off_top", "on_top") and isinstance(m, dict) and "error" not in m \
                    and not (isinstance(m.get("child"), dict) and "ms" in m["child"]):
                errors.append(f"{where}.{key}: the framed site did not report")
    return errors


def pinned_problem(label: str, m: dict, want_off: bool) -> str | None:
    """None if the renderer that hosted measurement ``m`` has the JIT in the wanted state.

    ``m["pid"]`` is the renderer that ran the page and ``m["renderers"]["pids"]`` lists the
    renderers started with (``noopt``) and without (``default``) the optimizing compilers. A
    measurement without a renderer table (no /proc) is not judged here.
    """
    procs = m.get("renderers")
    if procs is None:
        return None
    pid = m.get("pid")
    noopt, default = procs["pids"]["noopt"], procs["pids"]["default"]
    if pid is None:
        return f"{label}: could not tell which renderer hosted the page"
    if pid not in noopt and pid not in default:
        return f"{label}: the renderer that hosted the page (pid {pid}) is not in the process table"
    if want_off and pid not in noopt:
        return (f"{label}: the renderer that hosted the site with the JIT off (pid {pid}) still has the "
                f"optimizing compilers on; the renderers without them are {noopt or 'none'}")
    if not want_off and pid in noopt:
        return f"{label}: the renderer that hosted the site (pid {pid}) runs without the optimizing compilers"
    return None


def site_verdict(label: str, snap: dict) -> str | None:
    """None if this snapshot shows the JIT off for the chosen site only, by speed and by renderer pid."""
    off, on = snap["off_site"], snap["on_site"]
    ratio = slowdown(off, on)
    if ratio < MIN_SLOWDOWN:
        return f"{label}: the site with the JIT off ran only {ratio:.1f}x slower than the other (need {MIN_SLOWDOWN}x)"
    return pinned_problem(f"{label}, site {OFF_SITE}", off, True) or pinned_problem(f"{label}, site {ON_SITE}", on, False)


def mode_verdict(label: str, snap: dict) -> str | None:
    """None if this snapshot shows the JIT off on the chosen site only, else why not."""
    why = site_verdict(label, snap)
    if why:
        return why
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


def subdomain_verdict(sub: dict) -> str | None:
    """None if a subdomain-only rule behaved as documented: the JIT is off on both visits, and site b stays on."""
    first, second, parent = sub["first"], sub["second"], sub["parent"]
    for label, m in (("first", first), ("second", second)):
        if slowdown(m, parent) < MIN_SLOWDOWN:
            return (f"subdomain rule: the {label} visit to {SUB_SITE} ran only {slowdown(m, parent):.1f}x slower "
                    f"than {SUB_PARENT} (need {MIN_SLOWDOWN}x), so the rule did not apply")
    return (pinned_problem(f"subdomain rule, first visit to {SUB_SITE}", first, True)
            or pinned_problem(f"subdomain rule, second visit to {SUB_SITE}", second, True)
            or pinned_problem(f"subdomain rule, site {SUB_PARENT}", parent, False))


@register
class JsOptimizer(HardeningCheck):
    name = "hardening_js_optimizer"
    control = "js_optimizer"
    negative_control = ("A second browser that never changes the setting measures the same two sites; with the JIT "
                        "on for both they must run at the same speed, so the difference in the main run comes "
                        "from the setting and not from the sites.")

    def observe(self, session: Session) -> dict:
        defaults = settings_defaults(session)
        with session.new_profile() as p:
            with session.variant(profile=p) as first:
                before = snapshot(first)
                change = set_site_setting(first, OFF_SITE, "block")
                after = snapshot(first)
            with session.variant(profile=p) as second:
                restarted = snapshot(second)
        with session.new_profile() as p2:
            with session.variant(profile=p2) as third:
                add_rule = add_site_rule(third, f"[*.]{OFF_SITE}.localhost")
                add_site = snapshot(third)
                sub_rule = add_site_rule(third, f"{SUB_SITE}.localhost")
                subdomain = {"rule": sub_rule, "first": measure_pinned(third, SUB_SITE)}
                third.navigate_url("about:blank")  # leave the page before the second visit
                subdomain["second"] = measure_pinned(third, SUB_SITE)
                subdomain["parent"] = measure_pinned(third, SUB_PARENT)
        with session.variant() as v:
            control = {"off_site": measure(v, OFF_SITE), "on_site": measure(v, ON_SITE), "renderers": renderers(v)}
        return {"defaults": defaults, "change": change, "before": before, "after": after,
                "restarted": restarted, "control": control,
                "add_rule": add_rule, "add_site": add_site, "subdomain": subdomain}

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
        # Observations recorded before the Add site and subdomain subcases existed lack these keys.
        if "add_site" in obs:
            if not obs["add_rule"].get("ok") or not obs["subdomain"]["rule"].get("ok"):
                return "fail", "could not add a rule through the Add site call: " + str(
                    obs["add_rule"].get("error") or obs["subdomain"]["rule"].get("error"))
            why = mode_verdict("after a rule added like the Add site dialog does", obs["add_site"])
            if why:
                return "fail", why
            why = subdomain_verdict(obs["subdomain"])
            if why:
                return "fail", why
        # The other site must not have been slowed by the change.
        if slowdown(obs["after"]["on_site"], before["on_site"]) >= MIN_SLOWDOWN:
            return "fail", f"site {ON_SITE} got slower after the setting changed for site {OFF_SITE}"
        a, r = obs["after"]["off_site"], obs["restarted"]["off_site"]
        extra = ""
        if "add_site" in obs:
            extra = ("a rule added like the Add site dialog does it gave the same result; "
                     "a subdomain-only rule: JIT off at the first visit and at the second; ")
        return "pass", (f"JIT off for site {OFF_SITE} only: {slowdown(a, obs['after']['on_site']):.1f}x slower than "
                        f"site {ON_SITE}, {slowdown(r, obs['restarted']['on_site']):.1f}x after a restart; framed in "
                        "one page the two sites ran at different speeds, so in different processes; "
                        f"{extra}control {slowdown(c['off_site'], c['on_site']):.1f}x")
