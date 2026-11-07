"""HTTPS-only mode: strict by default, localhost exempt, per-site exceptions listed in settings."""

from __future__ import annotations

import time

from ...session import Session
from .. import register
from .common import (DEEP_JS, FIXTURE_TITLE, HardeningCheck, Verdict, fixture_hits, navigate_state,
                     plain_site)

SETTINGS_URL = "chrome://settings/privacy/hardening"
CLICKJACK_WAIT = 0.8  # the warning page ignores clicks for 500 ms after it loads

# Each check uses its own names: an exception that one check creates must not change another's result.
STRICT_HOST = "strict.bench.test"
EXCEPTION_HOST = "exc.bench.test"
OTHER_HOST = "other.bench.test"
SERVED_HOSTS = ("localhost", "dev.localhost", "127.0.0.1")
# Inside the exempt ranges and nothing listens there: loading fails with a connection error. An upgrade
# would end in the HTTPS-only warning page or an SSL error, and the judge rejects both.
UNSERVED_HOSTS = ("127.0.0.2", "[::1]")
HTTPS_ATTEMPT_ERRORS = ("ERR_SSL_PROTOCOL_ERROR", "ERR_SSL_VERSION_OR_CIPHER_MISMATCH", "ERR_CONNECTION_CLOSED",
                        "ERR_CERT_", "ERR_SSL_", "ERR_BAD_SSL_CLIENT_AUTH_CERT")

CLICK_PROCEED_JS = """(() => {
  const b = document.querySelector('#proceed-button');
  if (!b || b.classList.contains('hidden')) return false;
  b.click();
  return true;
})()"""


def read_exceptions(session: Session) -> list[str] | None:
    """Hosts listed under 'Sites allowed over HTTP' on the Security hardening page."""
    nav = session.navigate_url(SETTINGS_URL)
    if nav["error"]:
        return None
    ready = session.wait_for(DEEP_JS + "!!deepQuery(document, '#httpExceptions')", timeout=20)
    if not ready:
        return None
    time.sleep(0.5)  # the list is fetched after the page renders
    return session.evaluate(DEEP_JS + "deepQueryAll(document, '.http-exception').map(e => e.dataset.host)")


def remove_exception(session: Session, host: str) -> bool:
    return bool(session.evaluate(
        DEEP_JS + f"(() => {{ const b = deepQueryAll(document, '.remove-http-exception')"
                  f".find(e => e.dataset.host === {host!r}); if (!b) return false; b.click(); return true; }})()"))


@register
class HttpsOnly(HardeningCheck):
    name = "hardening_https_only"
    control = "https_only"
    negative_control = ("After the user clicks Continue to site, the same http page loads and the fixture "
                        "server sees the request, so a blocked load really is the browser's doing.")

    def observe(self, session: Session) -> dict:
        host = f"plain.bench.test:{session.fixtures.port}"
        first = navigate_state(session, f"http://{host}/index.html")
        before = len(fixture_hits(session, host, "/index.html"))
        time.sleep(CLICKJACK_WAIT)
        clicked = False
        loaded = False
        if first["outcome"] == "interstitial":
            clicked = bool(session.evaluate(CLICK_PROCEED_JS))
            loaded = bool(clicked and session.wait_for(f"document.title === {FIXTURE_TITLE!r}", timeout=10))
        return {"host": host, "first": first, "hits_before_click": before,
                "click": {"clicked": clicked, "loaded": loaded},
                "hits_after_click": len(fixture_hits(session, host, "/index.html"))}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        first = obs["first"]
        if first["outcome"] == "loaded":
            return "fail", (f"strict HTTPS-only mode is not on: {first['url']} loaded over plain http "
                            f"({obs['hits_before_click']} request(s) reached the fixture server)")
        if first["outcome"] != "interstitial":
            return "fail", f"expected the HTTPS-only warning page, got {first['outcome']} ({first.get('error')})"
        state = first["state"]
        # A certificate-error page also has a Continue button, so the type must be read and must match.
        if state.get("type") != "HTTPS_ONLY":
            return "fail", f"warning page has type {state.get('type')!r}, expected HTTPS_ONLY"
        if not state.get("proceed_visible"):
            return "fail", "the warning page shows no Continue to site button, so an http exception takes more than one click"
        if obs["hits_before_click"]:
            return "fail", f"{obs['hits_before_click']} http request(s) reached the server before any click"
        click = obs["click"]
        if not (click["clicked"] and click["loaded"] and obs["hits_after_click"] >= 1):
            return "fail", ("negative control failed: after Continue to site the page did not load over http "
                            f"(clicked={click['clicked']}, loaded={click['loaded']}, "
                            f"hits={obs['hits_after_click']}), so the check cannot tell blocking from failure")
        return "pass", (f"{first['url']} got the HTTPS-only warning page with a Continue to site button and "
                        f"no http request; Continue to site then loaded it ({obs['hits_after_click']} request)")


@register
class LocalhostDev(HardeningCheck):
    name = "hardening_localhost_dev"
    control = "https_only"
    negative_control = ("A non-exempt name (strict.bench.test) on the same server gets the warning page in the "
                        "same run, so loading localhost cannot be because HTTPS-only is off.")

    def observe(self, session: Session) -> dict:
        port = session.fixtures.port
        strict = navigate_state(session, f"http://{STRICT_HOST}:{port}/index.html")
        served = {}
        for name in SERVED_HOSTS:
            r = navigate_state(session, f"http://{name}:{port}/index.html")
            r["hit"] = bool(fixture_hits(session, f"{name}:{port}", "/index.html"))
            served[name] = r
        unserved = {name: navigate_state(session, f"http://{name}:{port}/index.html") for name in UNSERVED_HOSTS}
        return {"strict_control": {"outcome": strict["outcome"]}, "served": served, "unserved": unserved}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        if obs["strict_control"]["outcome"] != "interstitial":
            return "fail", (f"negative control failed: {STRICT_HOST} did not get the warning page "
                            f"({obs['strict_control']['outcome']}), so the exemption proves nothing")
        for name, r in obs["served"].items():
            if r["outcome"] == "interstitial":
                return "fail", f"http://{name}:<port>/ got the HTTPS-only warning page"
            if r["outcome"] != "loaded" or not r.get("hit"):
                return "fail", f"http://{name}:<port>/ did not load ({r['outcome']}, {r.get('error')})"
            if not str(r["state"].get("href", "")).startswith("http://"):
                return "fail", f"http://{name}:<port>/ ended on {r['state'].get('href')}"
        for name, r in obs["unserved"].items():
            err = r.get("error") or ""
            if r["outcome"] == "interstitial":
                return "fail", f"http://{name}:<port>/ got the HTTPS-only warning page"
            if any(tag in err for tag in HTTPS_ATTEMPT_ERRORS):
                return "fail", f"http://{name}:<port>/ was upgraded to https ({err})"
        return "pass", ("localhost, dev.localhost and 127.0.0.1 loaded over http with no warning page; "
                        f"127.0.0.2 and ::1 were not upgraded; {STRICT_HOST} got the warning page")


@register
class HttpException(HardeningCheck):
    name = "hardening_http_exception"
    control = "https_only"
    negative_control = ("Before the click the host is not on the exception list and gets the warning page; "
                        "a second host stays blocked after the first is allowed.")

    def observe(self, session: Session) -> dict:
        port = session.fixtures.port
        host, other = EXCEPTION_HOST, OTHER_HOST
        list_before = read_exceptions(session)
        first = navigate_state(session, f"http://{host}:{port}/index.html")
        time.sleep(CLICKJACK_WAIT)
        clicked = bool(session.evaluate(CLICK_PROCEED_JS)) if first["outcome"] == "interstitial" else False
        loaded = bool(clicked and session.wait_for(f"document.title === {FIXTURE_TITLE!r}", timeout=10))
        other_site = navigate_state(session, f"http://{other}:{port}/index.html")["outcome"]
        list_after_click = read_exceptions(session)
        removed = remove_exception(session, host) if list_after_click else False
        time.sleep(0.5)
        list_after_remove = session.evaluate(
            DEEP_JS + "deepQueryAll(document, '.http-exception').map(e => e.dataset.host)") if removed else None
        after_remove = navigate_state(session, f"http://{host}:{port}/index.html")["outcome"]
        return {"host": host, "other_host": other, "list_before": list_before, "first": first["outcome"],
                "clicked": clicked, "loaded_after_click": loaded, "other_site": other_site,
                "list_after_click": list_after_click, "removed": removed,
                "list_after_remove": list_after_remove, "after_remove": after_remove}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        host = obs["host"]
        if obs["list_before"] is None:
            return "fail", "the Security hardening page did not render the HTTP exceptions list"
        if host in obs["list_before"] or obs["first"] != "interstitial":
            return "fail", (f"negative control failed: before the click the list was {obs['list_before']} "
                            f"and the first load was {obs['first']}")
        if not (obs["clicked"] and obs["loaded_after_click"]):
            return "fail", "one click on Continue to site did not load the page over http"
        if obs["other_site"] != "interstitial":
            return "fail", f"allowing {host} also opened {obs['other_host']} ({obs['other_site']}); exceptions must be per site"
        if obs["list_after_click"] is None or host not in obs["list_after_click"]:
            return "fail", f"{host} is not listed in settings after the click (list: {obs['list_after_click']})"
        if not obs["removed"] or obs["list_after_remove"] is None or host in obs["list_after_remove"]:
            return "fail", f"Remove did not take {host} off the list (list: {obs['list_after_remove']})"
        if obs["after_remove"] != "interstitial":
            return "fail", f"after Remove, {host} loaded without the warning page ({obs['after_remove']})"
        return "pass", (f"one click allowed {host} over http, it was listed in settings, a second host stayed "
                        "blocked, and Remove restored the warning page")
