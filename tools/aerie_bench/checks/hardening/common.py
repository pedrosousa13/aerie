"""Shared pieces of the hardening checks.

Every check splits into ``observe(session)`` (drive the browser, return plain JSON data) and
``judge(obs)`` (a pure function from that data to a verdict). The unit tests feed ``judge``
recorded observations: a passing one, a failing one and one where the negative control
failed. The negative control is what proves a check can fail at all, so each check runs it
for real in the same Bench run (in the same browser, or in a ``Session.variant``).
"""

from __future__ import annotations

from typing import Any

from ...session import CheckResult, Session

FIXTURE_TITLE = "Aerie Bench Fixture"
Verdict = tuple[str, str]  # (status, detail)


class HardeningCheck:
    """Base class. Subclasses set ``name``, ``control``, ``negative_control`` and implement
    ``observe`` and ``judge``."""

    name = ""
    control = ""           # id in tools/aerie_hardening/controls.json, or "all"
    negative_control = ""  # one sentence: how this check proves it can fail
    seeds_helium_services = False

    def observe(self, session: Session) -> dict:
        raise NotImplementedError

    @staticmethod
    def judge(obs: dict) -> Verdict:
        raise NotImplementedError

    def run(self, session: Session) -> CheckResult:
        obs = self.observe(session)
        status, detail = self.judge(obs)
        return CheckResult(self.name, status, detail, data=obs)


def site(session: Session, name: str) -> str:
    """Origin of a fixture site that is its own site and is exempt from HTTPS-only mode."""
    return f"http://{name}.localhost:{session.fixtures.port}"


def plain_site(session: Session, name: str) -> str:
    """Origin of a fixture site that HTTPS-only mode does NOT exempt."""
    return f"http://{name}.bench.test:{session.fixtures.port}"


def fixture_hits(session: Session, host: str, path_prefix: str = "") -> list[dict]:
    """Requests the fixture server saw for one Host header value (name:port)."""
    return [r for r in session.fixtures.snapshot()
            if r["host"] == host and r["path"].startswith(path_prefix)]


def page_value(session: Session, expression: str, timeout: float = 15.0) -> Any:
    """Wait until ``window.__done`` is set, then return ``expression``."""
    if not session.wait_for("window.__done === true", timeout=timeout):
        return {"error": "timeout waiting for the fixture page"}
    return session.evaluate(expression)


# Looks inside every shadow root. Prepended to the settings page probes.
DEEP_JS = """
function deepQuery(root, sel) {
  const hit = root.querySelector(sel);
  if (hit) return hit;
  for (const el of root.querySelectorAll('*')) {
    if (el.shadowRoot) {
      const inner = deepQuery(el.shadowRoot, sel);
      if (inner) return inner;
    }
  }
  return null;
}
function deepQueryAll(root, sel, out) {
  out = out || [];
  out.push(...root.querySelectorAll(sel));
  for (const el of root.querySelectorAll('*')) {
    if (el.shadowRoot) deepQueryAll(el.shadowRoot, sel, out);
  }
  return out;
}
"""

INTERSTITIAL_JS = """(() => {
  const proceed = document.querySelector('#proceed-button');
  const wrapper = document.querySelector('.interstitial-wrapper');
  let type = null;
  try { type = (typeof loadTimeData !== 'undefined') ? loadTimeData.getString('type') : null; } catch (e) {}
  return {title: document.title, href: location.href,
          interstitial: !!(proceed && wrapper), type: type,
          proceed_visible: !!(proceed && !proceed.classList.contains('hidden'))};
})()"""


def classify(nav_error: str | None, state: dict | None) -> str:
    """One word for what a navigation ended in: interstitial, loaded, net_error or other."""
    state = state or {}
    if state.get("interstitial"):
        return "interstitial"
    if nav_error:
        return "net_error"
    if state.get("title") == FIXTURE_TITLE:
        return "loaded"
    return "other"


def navigate_state(session: Session, url: str) -> dict:
    """Navigate and describe the result: {url, outcome, error, state}."""
    nav = session.navigate_url(url)
    try:
        state = session.evaluate(INTERSTITIAL_JS)
    except Exception as exc:  # an error page that refuses script is still data
        state = {"title": None, "interstitial": False, "script_error": str(exc)}
    return {"url": url, "outcome": classify(nav["error"], state), "error": nav["error"], "state": state}
