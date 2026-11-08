"""Widevine is opt-in: absent on a fresh profile, present once the user opts in (#7).

* Fresh profile: ``requestMediaKeySystemAccess('com.widevine.alpha')`` must be refused, the
  Widevine component must not be registered (chrome://components does not list it), and the NetLog
  must hold no request that could fetch it.
* Negative control: a second browser whose Local State has ``aerie.widevine.enabled`` must list the
  Widevine component. That proves the page reader can see Widevine, so its absence above means
  something. This works offline: the component is registered even if its download is refused.
* ``online_seconds`` > 0 (``--widevine-online SEC``) also waits that long, in the opted-in browser,
  for the key system to become available. It needs a network that reaches the component update
  service, so it is off by default and the default Bench keeps its zero-traffic guarantee.
"""

from __future__ import annotations

import time

from .. import netlog
from ..denylist import DUMMY_TLD
from ..profile import LOCAL_STATE_KEY
from ..session import CheckResult, Session
from . import register

SETTLE_SECONDS = 1.0  # the components list fills in after the page shell
OPT_IN_PREFS = {LOCAL_STATE_KEY: {"aerie": {"widevine": {"enabled": True}}}}

KEY_SYSTEM_JS = """(async () => {
  try {
    await navigator.requestMediaKeySystemAccess('com.widevine.alpha', [{
      initDataTypes: ['cenc'],
      videoCapabilities: [{contentType: 'video/mp4; codecs="avc1.42E01E"'}]
    }]);
    return {available: true};
  } catch (e) {
    return {available: false, error: String((e && e.name) || e)};
  }
})()"""

# All text of chrome://components, shadow roots included.
COMPONENTS_TEXT_JS = """(() => {
  let text = '';
  const walk = (node) => {
    if (node.shadowRoot) walk(node.shadowRoot);
    for (const child of node.children || []) walk(child);
    if (node.childNodes) for (const n of node.childNodes) if (n.nodeType === 3) text += n.textContent + ' ';
  };
  walk(document.documentElement);
  return text;
})()"""


def ask_key_system(session: Session) -> dict:
    session.navigate("/index.html")
    answer = session.evaluate(KEY_SYSTEM_JS, await_promise=True)
    return answer if isinstance(answer, dict) else {"error": f"unexpected answer {answer!r}"}


def components_list_widevine(session: Session) -> bool:
    session.navigate_url("chrome://components")
    session.wait_for(f"{COMPONENTS_TEXT_JS}.length > 200", timeout=15.0)
    time.sleep(SETTLE_SECONDS)
    return "widevine" in str(session.evaluate(COMPONENTS_TEXT_JS)).lower()


def escaped_requests(records: list[dict]) -> list[dict]:
    """Requests that could fetch Widevine: to a real host, or naming Widevine even at a dummy host."""
    out = []
    for r in netlog.external(records):
        urls = r.get("urls") or [r["url"]]
        if any("widevine" in u.lower() for u in urls) or not (r["host"] or "").endswith("." + DUMMY_TLD):
            out.append(r)
    return out


def judge(obs: dict) -> tuple[str, str]:
    main, control = obs["main"], obs["control"]
    answer = main["key_system"]
    if "available" not in answer:
        return "fail", f"the page did not answer the key system query: {answer.get('error')}"
    if answer["available"]:
        return "fail", "Widevine was available on a fresh profile"
    if main["listed"]:
        return "fail", "the Widevine component is registered (listed in chrome://components) on a fresh profile"
    if not control["listed"]:
        return "fail", ("negative control failed: with Widevine opted in, chrome://components still did not "
                        "list it, so its absence on the fresh profile proves nothing")
    online = obs.get("online")
    if online is not None and not online.get("available"):
        return "fail", (f"opted in, but Widevine did not become available ({online.get('error', 'no error')}); "
                        "needs a network that reaches the component update service")
    detail = ("Widevine absent on a fresh profile (key system refused, component not registered); "
              "present in the component list once opted in")
    if online is not None:
        detail += ", and available to a DRM page after the download"
        hosts = obs.get("online_hosts")
        if hosts is not None:
            detail += f" (the opted-in browser contacted: {', '.join(hosts) or 'no external host'})"
    return "pass", detail


@register
class WidevineOptIn:
    name = "widevine_opt_in"
    seeds_helium_services = False
    negative_control = ("A second browser with aerie.widevine.enabled in Local State must list the Widevine "
                        "component, so the reader that reports it absent on the fresh profile can see it.")

    def __init__(self, online_seconds: float = 0.0):
        self.online_seconds = online_seconds

    def observe(self, session: Session) -> dict:
        main = {"key_system": ask_key_system(session), "listed": components_list_widevine(session)}
        # Online mode must reach Google, so this one browser gets the real resolver (only the
        # fixture names stay mapped) and writes its own NetLog, to say which hosts it contacted.
        # The Bench's default rules refuse every non-loopback host, and Widevine could never download.
        flags: tuple[str, ...] = ()
        online_log = session.out_dir / "widevine-online-netlog.json" if self.online_seconds > 0 else None
        if online_log is not None:
            flags = ("--host-resolver-rules=MAP *.bench.test 127.0.0.1", f"--log-net-log={online_log}")
        with session.variant(flags, OPT_IN_PREFS) as opted_in:
            control = {"listed": components_list_widevine(opted_in)}
            obs = {"main": main, "control": control}
            if self.online_seconds > 0:
                obs["online"] = self._wait_available(opted_in)
        if self.online_seconds > 0:
            hosts = sorted({r["host"] for r in netlog.external(netlog.parse_file(online_log)) if r.get("host")})
            obs["online_hosts"] = hosts
        return obs

    def _wait_available(self, session: Session) -> dict:
        deadline = time.monotonic() + self.online_seconds
        while True:
            answer = ask_key_system(session)
            if answer.get("available") or time.monotonic() >= deadline:
                return answer
            time.sleep(5.0)

    def run(self, session: Session) -> CheckResult:
        obs = self.observe(session)
        status, detail = judge(obs)
        return CheckResult(self.name, status, detail, data=obs)

    def finalize(self, session: Session, result: CheckResult) -> CheckResult:
        if result.status != "pass":
            return result
        sent = escaped_requests(session.netlog)
        if sent:
            return CheckResult(self.name, "fail",
                               f"a request that could fetch Widevine was made on the fresh profile: {sent[0]['url']}",
                               data={**result.data, "requests": [r["url"] for r in sent]})
        return CheckResult(self.name, "pass", result.detail + "; no request could have fetched it",
                           data=result.data)
