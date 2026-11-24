"""Extensions cannot loosen the hardened prefs through chrome.privacy or allow cookies through chrome.contentSettings."""

from __future__ import annotations

from pathlib import Path

from ...session import Session
from .. import register
from .common import HardeningCheck, Verdict
from .cookies import ALLOW_MODE, ALLOW_THIRD_PARTY_PREFS, BLOCK_MODE, cookie_verdict, probe_cookies

# Fixed by the "key" in fixtures/hardening/extension/manifest.json (sha256 of the public key).
EXTENSION_ID = "knlpboobmlcbfihkhfbhepnojchckong"
CONTROLLED = "controlled_by_this_extension"
MODE_PREF = "profile.cookie_controls_mode"
# The user's own cookie allow, as the settings page stores it: site c, shown to any top-level site.
USER_ALLOW_PREFS = {"profile": {**ALLOW_THIRD_PARTY_PREFS["profile"],
                                "content_settings": {"exceptions": {"cookies": {"c.localhost,*": {"setting": 1}}}}}}
SET_MODE_JS = ("new Promise(r => chrome.settingsPrivate.setPref('%s', %d, '', ok => r(!!ok)))")


def flags_for(directory: Path) -> tuple[str, ...]:
    return (f"--load-extension={directory}",)


def extension_dir(session: Session) -> Path:
    return Path(session.fixtures.root) / "hardening" / "extension"


def probe_extension(session: Session) -> dict:
    nav = session.navigate_url(f"chrome-extension://{EXTENSION_ID}/probe.html")
    if nav["error"]:
        return {"loaded": False, "error": nav["error"], "results": []}
    results = session.evaluate("window.__probe()", await_promise=True)
    return {"loaded": True, "results": results or []}


def judge_switch(sw: dict | None) -> str | None:
    """The allow-then-block step (#166): a problem sentence, or None when the step is sound."""
    if not isinstance(sw, dict) or not all(k in sw for k in ("set", "cookies", "get")):
        return "the switch step is missing: no observation of a broad extension cookie allow set while third-party cookies were allowed"
    refused = {k: v for k, v in sw["set"].items() if v}
    if refused:
        return f"negative control failed: the extension's rules were refused while third-party cookies were allowed: {refused}"
    if not sw.get("mode_changed"):
        return "negative control failed: the cookie controls could not be switched from the settings page"
    cookies, get = sw["cookies"], sw["get"]
    allowed, blocked = cookies["allow_mode"], cookies["block_mode"]
    if allowed.get("broad") != "allowed" or allowed.get("user") != "allowed":
        return ("negative control failed: with third-party cookies allowed the broad extension allow and the user's allow "
                f"did not let a third-party cookie through ({allowed}), so the probe cannot see a rule take effect")
    if get["allow_mode"].get("broad_session") != "session_only":
        return f"negative control failed: with third-party cookies allowed the broad rule did not apply ({get['allow_mode']})"
    if blocked.get("none") != "blocked":
        return ("negative control failed: after the switch to block a third-party cookie from a site with no rule was "
                f"{blocked.get('none')}, so the switch did not block third-party cookies")
    if blocked.get("broad") == "allowed":
        return "a broad extension cookie allow set while third-party cookies were allowed still let a third-party cookie through after the switch to block"
    if blocked.get("broad") != "blocked":
        return f"the third-party cookie probe was inconclusive after the switch to block: {blocked}"
    if get["block_mode"].get("broad_session") != "allow":
        return f"a broad extension cookie rule is still in force after the switch to block ({get['block_mode']})"
    if blocked.get("user") != "allowed":
        return f"the user-created cookie allow stopped working after the switch to block ({blocked})"
    if get["block_mode"].get("first_party") != "session_only":
        return f"the one-host first-party allow set by the extension stopped working after the switch to block ({get['block_mode']})"
    if get["allow_again"].get("broad_session") != "session_only":
        return f"the broad rule did not come back when third-party cookies were allowed again ({get['allow_again']})"
    return None


def set_cookie_mode(session: Session, mode: int) -> bool:
    """Change the cookie controls the way the settings page does (chrome.settingsPrivate)."""
    session.navigate_url("chrome://settings/")
    session.wait_for("typeof chrome !== 'undefined' && !!chrome.settingsPrivate", timeout=15.0)
    return bool(session.evaluate(SET_MODE_JS % (MODE_PREF, mode), await_promise=True))


def probe_cookie_sites(session: Session) -> dict:
    """What a frame under site a reads: b is covered by the extension's broad allow, c by the user's
    allow, e by no rule."""
    return {name: cookie_verdict(probe_cookies(session, site_name))
            for name, site_name in (("broad", "b"), ("user", "c"), ("none", "e"))}


def probe_get(session: Session) -> dict:
    session.navigate_url(f"chrome-extension://{EXTENSION_ID}/probe.html")
    return session.evaluate("window.__probeGet()", await_promise=True) or {}


def probe_switch(session: Session) -> dict:
    """The extension sets a broad allow while third-party cookies are allowed; the user switches to block
    and back. ``session`` starts in allow mode with the user's allow already stored."""
    session.navigate_url(f"chrome-extension://{EXTENSION_ID}/probe.html")
    sw: dict = {"set": session.evaluate("window.__probeRules()", await_promise=True) or {},
                "cookies": {}, "get": {}}
    sw["cookies"]["allow_mode"] = probe_cookie_sites(session)
    sw["get"]["allow_mode"] = probe_get(session)
    sw["mode_changed"] = set_cookie_mode(session, BLOCK_MODE)
    sw["cookies"]["block_mode"] = probe_cookie_sites(session)
    sw["get"]["block_mode"] = probe_get(session)
    sw["mode_changed"] = sw["mode_changed"] and set_cookie_mode(session, ALLOW_MODE)
    sw["get"]["allow_again"] = probe_get(session)
    return sw


@register
class ExtensionPrefs(HardeningCheck):
    name = "hardening_extension_prefs"
    control = "extension_lock"
    negative_control = ("The same extension changes an unlocked setting (referrersEnabled) and blocks cookies "
                        "and allows one site's own cookies through contentSettings in the same run, so a refused write "
                        "to a hardened setting or a refused allow is the lock, not a broken extension. In a second browser the "
                        "broad allow is accepted and takes effect while third-party cookies are allowed, a third-party cookie from a "
                        "site with no rule is blocked after the switch to block (so the switch really blocks), and a rule set "
                        "through the same API for one site's own cookies and the user's own allow keep working.")

    def observe(self, session: Session) -> dict:
        with session.variant(flags_for(extension_dir(session))) as v:
            obs = probe_extension(v)
        if obs["loaded"]:
            with session.variant(flags_for(extension_dir(session)), USER_ALLOW_PREFS) as v:
                obs["switch"] = probe_switch(v)
        return obs

    @staticmethod
    def judge(obs: dict) -> Verdict:
        if not obs["loaded"]:
            return "fail", f"the probe extension did not load ({obs.get('error')}); see the browser stderr log"
        by_name = {r["name"]: r for r in obs["results"]}
        controls = [r for r in obs["results"] if r["role"] == "control"]
        if not controls or any(r["set_error"] or r["level"] != CONTROLLED for r in controls):
            return "fail", f"negative control failed: the unlocked setting could not be changed: {controls}"
        hardened = [r for r in obs["results"] if r["role"] == "hardened"]
        if len(hardened) < 3:
            return "fail", f"expected three hardened settings to be tried, got {sorted(by_name)}"
        for r in hardened:
            if not r["set_error"] or r["level"] == CONTROLLED:
                return "fail", f"the extension could change {r['name']} (error {r['set_error']}, level {r['level']})"
        cs_control = [r for r in obs["results"] if r["role"] == "content_setting_control"]
        cs_refused = [r for r in obs["results"] if r["role"] == "content_setting_refused"]
        cs_first_party = [r for r in obs["results"] if r["role"] == "content_setting_first_party"]
        if not cs_control or not cs_refused or not cs_first_party:
            return "fail", "the contentSettings cookie probes are missing from the results"
        if any(r["set_error"] for r in cs_control):
            return "fail", f"negative control failed: blocking cookies through contentSettings was refused: {cs_control}"
        for r in cs_first_party:
            if r["set_error"]:
                return "fail", f"a first-party cookie allow through contentSettings was refused ({r['name']}: {r['set_error']}); first-party cookie settings must keep working"
        for r in cs_refused:
            if not r["set_error"]:
                return "fail", f"the extension could allow cookies through contentSettings ({r['name']} returned no error)"
        problem = judge_switch(obs.get("switch"))
        if problem:
            return "fail", problem
        return "pass", ("the extension was refused for " + ", ".join(r["name"] for r in hardened)
                        + " and for allowing cookies for third-party contexts through contentSettings, and could still change referrersEnabled,"
                        + " block cookies and allow a site's own cookies through contentSettings; a broad cookie allow that it set while"
                        + " third-party cookies were allowed let nothing through after the switch to block, and came back when they were allowed again")
