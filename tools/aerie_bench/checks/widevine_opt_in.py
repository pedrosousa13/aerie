"""Widevine is opt-in: absent on a fresh profile, present once the user opts in (#7).

* Fresh profile: ``requestMediaKeySystemAccess('com.widevine.alpha')`` must be refused, the
  Widevine component must not be registered (chrome://components does not list it), and the NetLog
  must hold no request that could fetch it.
* Negative control: a second browser whose Local State has ``aerie.widevine.enabled`` must list the
  Widevine component. That proves the page reader can see Widevine, so its absence above means
  something. This works offline: the component is registered even if its download is refused.
* ``online_seconds`` > 0 (``--widevine-online SEC``) also waits up to that long for an opted-in
  browser to download Widevine, restarts it on the same profile (Linux registers a newly installed
  Widevine at the next start) and asks for the key system. It uses ``Session.new_profile`` and
  ``variant(profile=...)``, the Bench's restart-on-one-profile support. It needs a network that reaches the
  component update service, so it is off by default and the default Bench keeps its zero-traffic guarantee.
"""

from __future__ import annotations

import time

from .. import netlog
from ..denylist import DUMMY_TLD
from ..profile import LOCAL_STATE_KEY
from ..session import CheckResult, Session
from . import register

HINT_FILE = "latest-component-updated-widevine-cdm"  # written once the component is installed
SETTLE_SECONDS = 1.0  # the components list fills in after the page shell
OPT_IN_PREFS = {LOCAL_STATE_KEY: {"aerie": {"widevine": {"enabled": True}}}}

# The query asks for VP9 in WebM first: it needs no proprietary codec in the build. H.264 in MP4
# is the second try. "Available" means one of them was granted; `config` says which.
KEY_SYSTEM_JS = """(async () => {
  const configs = [
    {initDataTypes: ['webm'], videoCapabilities: [{contentType: 'video/webm; codecs="vp9"'}]},
    {initDataTypes: ['cenc'], videoCapabilities: [{contentType: 'video/mp4; codecs="avc1.42E01E"'}]},
  ];
  let last = null;
  for (const config of configs) {
    try {
      await navigator.requestMediaKeySystemAccess('com.widevine.alpha', [config]);
      return {available: true, config: config.videoCapabilities[0].contentType};
    } catch (e) {
      last = String((e && e.name) || e);
    }
  }
  return {available: false, error: last};
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
        detail += ", and, after the download and a restart, available to a DRM page"
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
        with session.variant((), OPT_IN_PREFS) as opted_in:
            control = {"listed": components_list_widevine(opted_in)}
        obs = {"main": main, "control": control}
        if self.online_seconds > 0:
            obs.update(self._online(session))
        return obs

    def _online(self, session: Session) -> dict:
        """Opt in, let the browser download Widevine from Google, restart it, ask for the key system.

        Two things only a real run showed. The Bench's default resolver rules refuse every
        non-loopback host, so these browsers keep only the fixture-name mapping. And on Linux a
        newly installed Widevine is registered at the next start (the installer only writes the
        hint file), so the proof restarts the browser on the same profile after the install.
        Each launch has its own NetLog (``variant`` parses it after the browser exits), which says
        which hosts it contacted.
        """
        deadline = time.monotonic() + self.online_seconds
        flags = ("--host-resolver-rules=MAP *.bench.test 127.0.0.1",)
        with session.new_profile() as prof:
            hint = prof.path / "WidevineCdm" / HINT_FILE
            with session.variant(flags, OPT_IN_PREFS, profile=prof) as first:
                while not hint.exists() and time.monotonic() < deadline:
                    time.sleep(2.0)
                downloaded = hint.exists()
            with session.variant(flags, None, profile=prof) as restarted:
                answer = (self._wait_available(restarted, deadline) if downloaded
                          else {"available": False, "error": "Widevine was not downloaded in time"})
        hosts = sorted({r["host"] for v in (first, restarted) for r in netlog.external(v.netlog) if r.get("host")})
        return {"online": answer, "online_downloaded": downloaded, "online_hosts": hosts}

    def _wait_available(self, session: Session, deadline: float) -> dict:
        while True:
            answer = ask_key_system(session)
            if answer.get("available") or time.monotonic() >= deadline:
                return answer
            time.sleep(3.0)

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
