"""Phishing and malware protection: a listed domain gets the interstitial, in every kind of navigation.

The Bench cannot use the real list (its hosts are real phishing sites, and the Bench resolves nothing
but loopback). It starts a second browser (a ``Session.variant``) with
``--aerie-test-protection-list=<file>``, which adds fixture domains to the list the build ships. The
fixture names are ``*.localhost`` (exempt from HTTPS-only mode, resolved to the fixture server). The
first browser has no such list and is the negative control: the same listed names load there.

Steps, each on its own listed host so that one cannot change another (the "Continue anyway" choice is
kept per tab, per host):

* main frame: ``phish.localhost`` shows the interstitial. It is Aerie's page, not the site's: the
  site's script did not run (``window.blockedPageRan`` is unset), its green style is not applied, and
  the fixture server never saw the request. "Go back" is the primary button and "Continue anyway" is a
  link in the details section, closed at first. A subdomain (``www.phish.localhost``) is covered too.
* Go back returns to the previous page.
* Continue anyway (``cont.localhost``) loads the page, and the choice lasts for that tab only: the same
  tab loads the host again without a warning, a new tab gets the interstitial.
* subframe (``sub.localhost``): an iframe to a listed host is blocked and the request never goes out.
* redirect chains, in the main frame (``chain.localhost``) and in a subframe (``chainsub.localhost``):
  a clean first URL that answers 302 to a listed host is stopped at the hop, and the listed URL never
  reaches the fixture server.
* the browser's own signals: ``Aerie.Protection.ListLoadSuccess`` is true, and
  ``Aerie.Protection.NavigationBlocked`` has at least one true sample per stopped navigation, so the
  stop was the protection service's and not another feature's.

``observe`` drives the browsers and returns plain data; ``judge`` is a pure function of that data, run
by the tests on good and bad observations.
"""

from __future__ import annotations

import tempfile
import time
from pathlib import Path
from urllib.parse import quote

from ..session import CheckResult, Session
from . import register
from .ad_blocking import read_boolean

LIST_SWITCH = "--aerie-test-protection-list"
LISTED = {
    "main": "phish.localhost", "subdomain": "www.phish.localhost", "back": "back.localhost",
    "continue": "cont.localhost", "subframe": "sub.localhost", "chain": "chain.localhost",
    "chain_subframe": "chainsub.localhost",
}
# The names the test list holds: the subdomain case is covered by listing its parent.
LIST_NAMES = ["phish.localhost", "back.localhost", "cont.localhost", "sub.localhost", "chain.localhost",
              "chainsub.localhost"]
CLEAN = "clean.localhost"
LANDING = "/protection/landing.html"
LANDING_TITLE = "Fixture protected landing"
INTERSTITIAL_TITLE = "Dangerous site blocked"
LOAD_HISTOGRAM = "Aerie.Protection.ListLoadSuccess"
BLOCKED_HISTOGRAM = "Aerie.Protection.NavigationBlocked"
PROCEED_HISTOGRAM = "Aerie.Protection.UserProceeded"

STATE_JS = """(() => {
  const q = (s) => document.querySelector(s);
  const primary = q('#primary-button');
  const details = q('#details');
  return {href: location.href, title: document.title,
          interstitial: !!q('.interstitial-wrapper'),
          primary_text: primary ? primary.textContent.trim() : null,
          proceed_link: !!q('#proceed-link'),
          details_closed: details ? details.classList.contains('hidden') : null,
          body_class: document.body ? document.body.className : null,
          page_script_ran: window.blockedPageRan === true,
          body_background: document.body ? getComputedStyle(document.body).backgroundColor : null};
})()"""


def origin(session: Session, host: str) -> str:
    return f"http://{host}:{session.fixtures.port}"


def hits(session: Session, host: str, path: str) -> int:
    """Requests the fixture server saw for one Host header value and path (query ignored)."""
    want = f"{host}:{session.fixtures.port}"
    return sum(1 for r in session.fixtures.snapshot()
               if r["host"] == want and r["path"].split("?", 1)[0] == path)


def frames(session: Session) -> list[dict]:
    """The frame tree flattened: [{url, unreachable}] with the top frame first."""
    out: list[dict] = []

    def walk(node: dict) -> None:
        frame = node.get("frame", {})
        out.append({"url": frame.get("url"), "unreachable": frame.get("unreachableUrl")})
        for child in node.get("childFrames", []):
            walk(child)

    walk(session.cdp("Page.getFrameTree")["frameTree"])
    return out


def state(session: Session) -> dict:
    try:
        return session.evaluate(STATE_JS) or {}
    except Exception as exc:  # an error page that refuses script is still data
        return {"script_error": str(exc)}


def visit(session: Session, host: str, path: str = LANDING) -> dict:
    """Navigate the top frame; describe where it ended up."""
    url = origin(session, host) + path
    nav = session.navigate_url(url, wait=10)
    return {"url": url, "nav_error": nav["error"], "state": state(session), "frames": frames(session),
            "landing_hits": hits(session, host, path)}


def wait_title(session: Session, title: str, timeout: float = 10.0) -> bool:
    return bool(session.wait_for(f"document.title === {title!r}", timeout=timeout))


def new_tab_title(session: Session, url: str, timeout: float = 10.0) -> str | None:
    """Open a new tab and return its title once the page has one (an error page's title is its own)."""
    target = session.client.send("Target.createTarget", {"url": url}, timeout=session.timeout)["targetId"]
    deadline = time.monotonic() + timeout
    title = None
    while time.monotonic() < deadline:
        for info in session.targets():
            if info.get("targetId") == target and info.get("title") and info["title"] != url:
                title = info["title"]
        if title in (INTERSTITIAL_TITLE, LANDING_TITLE):
            break
        time.sleep(0.25)
    session.client.send("Target.closeTarget", {"targetId": target}, timeout=session.timeout)
    return title


def observe_listed_browser(session: Session) -> dict:
    obs: dict = {}
    clean = visit(session, CLEAN)
    obs["clean"] = {"title": clean["state"].get("title"), "landing_hits": clean["landing_hits"]}

    obs["main"] = visit(session, LISTED["main"])
    obs["subdomain"] = visit(session, LISTED["subdomain"])

    # Go back: a page first, then the listed site, then the primary button.
    visit(session, CLEAN)
    back = visit(session, LISTED["back"])
    clicked = bool(back["state"].get("interstitial")) and bool(session.evaluate(
        "(() => { const b = document.querySelector('#primary-button'); if (!b) return false; b.click(); return true; })()"))
    came_back = bool(clicked and session.wait_for(f"location.hostname === {CLEAN!r}", timeout=10))
    obs["go_back"] = {"interstitial": back["state"].get("interstitial"), "clicked": clicked,
                      "back_on_clean": came_back, "landing_hits": back["landing_hits"]}

    # Continue anyway: open the details, click the link, expect the real page; then the same tab again.
    cont = visit(session, LISTED["continue"])
    opened = bool(session.evaluate(
        "(() => { const b = document.querySelector('#details-button'); if (!b) return false; b.click(); return true; })()"))
    details_open = bool(opened and session.evaluate(
        "!document.querySelector('#details').classList.contains('hidden')"))
    link = bool(session.evaluate(
        "(() => { const a = document.querySelector('#proceed-link'); if (!a) return false; a.click(); return true; })()"))
    loaded = bool(link and wait_title(session, LANDING_TITLE))
    after = {"title": state(session).get("title"), "href": state(session).get("href")}
    again = visit(session, LISTED["continue"])
    new_tab = new_tab_title(session, origin(session, LISTED["continue"]) + LANDING)
    obs["continue"] = {"first": {"interstitial": cont["state"].get("interstitial"),
                                 "details_closed": cont["state"].get("details_closed")},
                       "details_opened": details_open, "clicked_link": link, "loaded": loaded, "after": after,
                       "same_tab_again": {"title": again["state"].get("title"),
                                          "interstitial": again["state"].get("interstitial")},
                       "new_tab_title": new_tab,
                       "landing_hits": hits(session, LISTED["continue"], LANDING)}

    # Subframe: a clean page that embeds a listed host.
    frame_url = origin(session, LISTED["subframe"]) + LANDING
    host_page = visit(session, CLEAN, "/protection/frame.html?to=" + quote(frame_url, safe=""))
    time.sleep(1.0)
    obs["subframe"] = {"frames": frames(session), "frame_url": frame_url,
                       "listed_hits": hits(session, LISTED["subframe"], LANDING),
                       "host_title": host_page["state"].get("title")}

    # Redirect chains: clean URL -> 302 -> listed URL.
    chain_url = origin(session, LISTED["chain"]) + LANDING
    hop = "/protection/hop?to=" + quote(chain_url, safe="")
    main_chain = visit(session, CLEAN, hop)
    obs["redirect_main"] = {"state": main_chain["state"], "frames": main_chain["frames"], "target": chain_url,
                            "hop_hits": hits(session, CLEAN, "/protection/hop"),
                            "listed_hits": hits(session, LISTED["chain"], LANDING)}
    sub_url = origin(session, LISTED["chain_subframe"]) + LANDING
    sub_hop = origin(session, CLEAN) + "/protection/hop?to=" + quote(sub_url, safe="")
    visit(session, CLEAN, "/protection/frame.html?to=" + quote(sub_hop, safe=""))
    time.sleep(1.0)
    obs["redirect_subframe"] = {"frames": frames(session), "target": sub_url,
                                "hop_hits": hits(session, CLEAN, "/protection/hop"),
                                "listed_hits": hits(session, LISTED["chain_subframe"], LANDING)}

    obs["histograms"] = {"list_load": read_boolean(session, LOAD_HISTOGRAM),
                         "blocked": read_boolean(session, BLOCKED_HISTOGRAM),
                         "proceeded": read_boolean(session, PROCEED_HISTOGRAM)}
    return obs


def _unreachable(frames_: list[dict], url: str, index: int | None = None) -> bool:
    """True if a frame (the top one when ``index`` is 0, a child when 1) failed to load ``url``."""
    pool = frames_ if index is None else frames_[index:index + 1]
    return any(f.get("unreachable") == url for f in pool)


def judge(obs: dict) -> tuple[str, str, dict]:
    """(status, detail, data). Every expectation that fails is named."""
    problems: list[str] = []
    control = obs.get("control", {})
    if control.get("title") != LANDING_TITLE or not control.get("landing_hits"):
        problems.append("negative control failed: without the test list the listed name did not load "
                        f"(title {control.get('title')!r}, {control.get('landing_hits')} request(s)), so "
                        "a blocked load proves nothing")
    listed = obs.get("listed", {})
    clean = listed.get("clean", {})
    if clean.get("title") != LANDING_TITLE or not clean.get("landing_hits"):
        problems.append(f"a clean host did not load in the browser with the test list ({clean.get('title')!r})")

    for key, label in (("main", "main frame"), ("subdomain", "subdomain of a listed domain")):
        visit_ = listed.get(key, {})
        st = visit_.get("state", {})
        if not st.get("interstitial") or st.get("title") != INTERSTITIAL_TITLE:
            problems.append(f"{label}: no interstitial (title {st.get('title')!r}, {st.get('script_error') or 'ok'})")
            continue
        if visit_.get("landing_hits"):
            problems.append(f"{label}: the request reached the fixture server ({visit_['landing_hits']})")
        if not _unreachable(visit_.get("frames", []), visit_.get("url", ""), 0):
            problems.append(f"{label}: the top frame is not an error page for {visit_.get('url')}")
        if st.get("page_script_ran"):
            problems.append(f"{label}: the blocked page's script ran")
        if st.get("body_background") == "rgb(0, 255, 0)":
            problems.append(f"{label}: the blocked page's style is applied to the interstitial")
        if key == "main":
            if st.get("primary_text") != "Go back":
                problems.append(f"the primary button says {st.get('primary_text')!r}, not 'Go back'")
            if not st.get("proceed_link"):
                problems.append("no 'Continue anyway' link")
            elif not st.get("details_closed"):
                problems.append("'Continue anyway' is not secondary: the details are open at first")

    back = listed.get("go_back", {})
    if not back.get("interstitial"):
        problems.append("go back: no interstitial to go back from")
    elif not back.get("back_on_clean"):
        problems.append(f"go back: the primary button did not return to the previous page (clicked={back.get('clicked')})")

    cont = listed.get("continue", {})
    if not cont.get("first", {}).get("interstitial"):
        problems.append("continue anyway: no interstitial first")
    elif not (cont.get("details_opened") and cont.get("clicked_link") and cont.get("loaded")):
        problems.append("continue anyway: the page did not load after the link "
                        f"(details opened={cont.get('details_opened')}, clicked={cont.get('clicked_link')}, "
                        f"loaded={cont.get('loaded')})")
    else:
        if not cont.get("landing_hits"):
            problems.append("continue anyway: the page loaded but the fixture server saw no request")
        again = cont.get("same_tab_again", {})
        if again.get("interstitial") or again.get("title") != LANDING_TITLE:
            problems.append("continue anyway: the choice was not remembered in the same tab")
        if cont.get("new_tab_title") != INTERSTITIAL_TITLE:
            problems.append("continue anyway: the choice is not limited to its tab: a new tab got "
                            f"{cont.get('new_tab_title')!r}")

    sub = listed.get("subframe", {})
    if not _unreachable(sub.get("frames", []), sub.get("frame_url", ""), 1):
        problems.append("subframe: the listed host's frame was not blocked "
                        f"(frames: {[f.get('unreachable') or f.get('url') for f in sub.get('frames', [])]})")
    if sub.get("listed_hits"):
        problems.append(f"subframe: the request reached the fixture server ({sub['listed_hits']})")

    chain = listed.get("redirect_main", {})
    if not chain.get("state", {}).get("interstitial"):
        problems.append("redirect chain (main frame): no interstitial for the listed hop")
    else:
        if not _unreachable(chain.get("frames", []), chain.get("target", ""), 0):
            problems.append("redirect chain (main frame): the error page is not for the listed target")
        if not chain.get("hop_hits"):
            problems.append("redirect chain (main frame): the clean first URL never answered 302, so no "
                            "redirect was tested")
        if chain.get("listed_hits"):
            problems.append("redirect chain (main frame): the listed target reached the fixture server")
    rsub = listed.get("redirect_subframe", {})
    if not _unreachable(rsub.get("frames", []), rsub.get("target", ""), 1):
        problems.append("redirect chain (subframe): the listed hop was not blocked")
    if rsub.get("hop_hits", 0) < 2:
        # The counter is cumulative: one 302 for the main frame chain and one for the subframe chain.
        problems.append("redirect chain (subframe): the clean first URL never answered 302 for the frame")
    if rsub.get("listed_hits"):
        problems.append("redirect chain (subframe): the listed target reached the fixture server")

    hist = listed.get("histograms", {})
    load, blocked = hist.get("list_load"), hist.get("blocked")
    if load is None:
        problems.append(f"no {LOAD_HISTOGRAM} histogram: this browser has no protection service")
    elif load["true"] < 1 or load["false"] > 0:
        problems.append(f"the protection list did not load cleanly ({LOAD_HISTOGRAM}: {load})")
    # Stops: main, subdomain, back, continue (first visit), the new tab, subframe, two redirect chains = 8.
    need = 8
    if blocked is None:
        problems.append(f"no {BLOCKED_HISTOGRAM} histogram: nothing shows the protection service judged anything")
    elif blocked["true"] < need:
        problems.append(f"the protection service stopped {blocked['true']} navigation(s), the check needs "
                        f"{need} ({BLOCKED_HISTOGRAM}): something else stopped the rest")
    proceeded = hist.get("proceeded")
    if proceeded is None or proceeded["true"] < 1 or proceeded["false"] < 1:
        problems.append(f"the interstitial buttons left no trace in {PROCEED_HISTOGRAM}: {proceeded}")

    data = {"observations": obs}
    if problems:
        return "fail", f"{len(problems)} problem(s): " + "; ".join(problems), data
    return "pass", ("a listed domain got the interstitial (Go back primary, Continue anyway secondary, "
                    "the site's own script and style kept out); Go back returned, Continue anyway loaded the "
                    "page for that tab only; a listed subframe and two redirect chains were stopped before the "
                    "listed request went out; the same name loads without the list"), data


@register
class PhishingProtection:
    name = "phishing_protection"
    seeds_helium_services = False

    def run(self, session: Session) -> CheckResult:
        control = visit(session, LISTED["main"])  # negative control: no test list in this browser
        with tempfile.TemporaryDirectory() as tmp:
            list_file = Path(tmp) / "test-protection-list.txt"
            list_file.write_text("# Aerie Bench test list\n" + "\n".join(LIST_NAMES) + "\n")
            with session.variant((f"{LIST_SWITCH}={list_file}",)) as v:
                listed = observe_listed_browser(v)
        obs = {"control": {"title": control["state"].get("title"), "landing_hits": control["landing_hits"]},
               "listed": listed}
        status, detail, data = judge(obs)
        return CheckResult(self.name, status, detail, data=data)
