"""Measure the option 2 (partition) Workspace prototype (issue #72). Throwaway, train only.

Needs a browser built with prototypes/workspaces/partition (patches, overlay, test-overlay and
test-patches) and started with --aerie-test-workspaces, which serves chrome://workspace-test.
Uses the Bench's launcher and CDP client, so run it with the Aerie checkout's tools/ on
PYTHONPATH:

    PYTHONPATH=tools python3 prototypes/workspaces/measure/measure.py --binary out/Default/aerie --out DIR

The browser runs headed (the screenshots need a window and its tab strip) on DISPLAY, which
should be an Xvfb server. Writes DIR/results.json and DIR/*.png.
"""

from __future__ import annotations

import argparse
import http.server
import json
import os
import shutil
import statistics
import subprocess
import threading
import time
import urllib.parse
from pathlib import Path

from aerie_bench import launcher
from aerie_bench.runner import browser_env

WS_TEST = "chrome://workspace-test/"

# ---------------------------------------------------------------- fixture site


class Site:
    """The two-logins site: one origin, a login that sets a session cookie and page storage."""

    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.cached_hits: dict[str, int] = {}
        self.auth_seen: list[str | None] = []
        site = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a):  # quiet
                pass

            def _send(self, code: int, body: str, ctype: str = "text/html; charset=utf-8",
                      headers: dict | None = None) -> None:
                data = body.encode()
                self.send_response(code)
                self.send_header("Content-Type", ctype)
                self.send_header("Content-Length", str(len(data)))
                for k, v in (headers or {}).items():
                    self.send_header(k, v)
                self.end_headers()
                self.wfile.write(data)

            def do_GET(self):  # noqa: N802
                url = urllib.parse.urlsplit(self.path)
                q = dict(urllib.parse.parse_qsl(url.query))
                if url.path == "/login":
                    user = "".join(c for c in q.get("user", "") if c.isalnum())[:16]
                    # No Max-Age/Expires: a session cookie (G1).
                    self._send(200, PAGE % ("login " + user, LOGIN_JS % (user, user, user, user, user)),
                               headers={"Set-Cookie": f"user={user}; Path=/; SameSite=Lax"})
                elif url.path == "/whoami":
                    self._send(200, PAGE % ("whoami", WHOAMI_JS))
                elif url.path == "/blank":
                    self._send(200, PAGE % ("blank", ""))
                elif url.path == "/cached":
                    with site.lock:
                        site.cached_hits[q.get("id", "")] = site.cached_hits.get(q.get("id", ""), 0) + 1
                    self._send(200, "cached", "text/plain", {"Cache-Control": "max-age=3600"})
                elif url.path.startswith("/auth/"):
                    auth = self.headers.get("Authorization")
                    with site.lock:
                        site.auth_seen.append(auth)
                    if auth:
                        self._send(200, "authed", "text/plain")
                    elif "probe" in q:  # no challenge, so no prompt: only preemptive (cached) credentials count
                        self._send(403, "no credentials", "text/plain")
                    else:
                        self._send(401, "no", "text/plain", {"WWW-Authenticate": 'Basic realm="ws"'})
                elif url.path == "/sw.js":
                    self._send(200, "self.addEventListener('fetch', () => {});", "text/javascript")
                else:
                    self._send(404, "not found", "text/plain")

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.port = self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def url(self, path: str) -> str:
        return f"http://localhost:{self.port}{path}"


PAGE = """<!doctype html><meta charset="utf-8"><title>%s</title>
<body style="font: 28px sans-serif; margin: 40px"><div id="out">...</div>
<script>%s</script>"""

LOGIN_JS = """
localStorage.user = '%s'; sessionStorage.user = '%s';
const req = indexedDB.open('ws', 1);
req.onupgradeneeded = () => req.result.createObjectStore('s');
req.onsuccess = () => { const tx = req.result.transaction('s', 'readwrite');
  tx.objectStore('s').put('%s', 'user'); tx.oncomplete = async () => {
  const c = await caches.open('ws'); await c.put('/cache-user', new Response('%s'));
  window.loginDone = true; document.getElementById('out').textContent = 'logged in as %s'; }; };
"""

WHOAMI_JS = """
async function whoami() {
  const cookie = (document.cookie.match(/(?:^|; )user=([^;]*)/) || [])[1] || '';
  const idb = await new Promise(res => { const r = indexedDB.open('ws', 1);
    r.onupgradeneeded = () => r.result.createObjectStore('s');
    r.onsuccess = () => { const g = r.result.transaction('s').objectStore('s').get('user');
      g.onsuccess = () => res(g.result || ''); g.onerror = () => res(''); };
    r.onerror = () => res(''); });
  const c = await caches.open('ws'); const m = await c.match('/cache-user');
  const cache = m ? await m.text() : '';
  const who = {cookie, localStorage: localStorage.user || '', sessionStorage: sessionStorage.user || '',
               indexedDB: idb, cacheStorage: cache};
  window.who = who;
  document.title = 'whoami: ' + (cookie || 'nobody');
  document.getElementById('out').innerHTML = '<b style="font-size:64px">' + (cookie || 'nobody') +
      '</b><pre>' + JSON.stringify(who, null, 1) + '</pre>';
  return who;
}
window.whoReady = whoami();
"""

# ---------------------------------------------------------------- browser


class Browser:
    def __init__(self, binary: Path, profile: Path, netlog: Path, stderr: Path, display: str,
                 fresh_ws_page: bool = False) -> None:
        self.fresh_ws_page = fresh_ws_page
        argv = launcher.build_argv(binary, profile, netlog, headless=False,
                                   extra=("--aerie-test-workspaces", "--restore-last-session", "--ozone-platform=x11",
                                          "--window-size=1500,900", "--window-position=0,0"))
        # The Bench's allowlisted environment (HOME etc. inside the profile), on the Xvfb display.
        env = dict(browser_env(os.environ, profile), DISPLAY=display)
        env.pop("WAYLAND_DISPLAY", None)
        env.pop("XAUTHORITY", None)
        self.proc = launcher.spawn(argv, env=env, stderr_path=stderr)
        self.c = self.proc.client
        self.sessions: dict[str, str] = {}
        self.c.send("Target.setDiscoverTargets", {"discover": True})
        self.ws_session = None

    @property
    def pid(self) -> int:
        return self.proc.popen.pid

    def pages(self) -> list[dict]:
        return [t for t in self.c.send("Target.getTargets")["targetInfos"] if t["type"] == "page"]

    def attach(self, target_id: str) -> str:
        if target_id not in self.sessions:
            sid = self.c.send("Target.attachToTarget", {"targetId": target_id, "flatten": True})["sessionId"]
            self.sessions[target_id] = sid
        return self.sessions[target_id]

    def eval(self, target_id: str, expr: str, timeout: float = 30.0):
        sid = self.attach(target_id)
        r = self.c.send("Runtime.evaluate", {"expression": expr, "awaitPromise": True, "returnByValue": True,
                                             "userGesture": True}, session_id=sid, timeout=timeout)
        if "exceptionDetails" in r:
            raise RuntimeError(f"eval {expr[:80]!r}: {r['exceptionDetails'].get('exception', {}).get('description', r['exceptionDetails'])}")
        return r["result"].get("value")

    def wait_page(self, pred, timeout: float = 20.0) -> dict:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for t in self.pages():
                if pred(t):
                    return t
            time.sleep(0.1)
        raise RuntimeError("no such page")

    def ws_page(self) -> str:
        """The target id of chrome://workspace-test (opened in the current first tab if needed)."""
        if self.ws_session:
            return self.ws_session
        if self.fresh_ws_page:
            # After a restart the restored test tab may be unloaded (lazy restore): use a new one.
            tid = self.c.send("Target.createTarget", {"url": WS_TEST})["targetId"]
            self.wait_expr(tid, "typeof workspaceTest === 'object'")
            self.ws_session = tid
            return tid
        try:
            return self.wait_page(lambda t: t["url"].startswith(WS_TEST), timeout=0.5)["targetId"]
        except RuntimeError:
            pass
        first = self.pages()[0]["targetId"]
        sid = self.attach(first)
        self.c.send("Page.navigate", {"url": WS_TEST}, session_id=sid)
        tid = self.wait_page(lambda t: t["url"].startswith(WS_TEST))["targetId"]
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            try:
                if self.eval(tid, "typeof workspaceTest === 'object'"):
                    return tid
            except Exception:
                pass
            time.sleep(0.2)
        raise RuntimeError("chrome://workspace-test did not load (is --aerie-test-workspaces honoured?)")

    def ws(self, js: str):
        return self.eval(self.ws_page(), js)

    def open(self, url: str, ws: str, background: bool = False) -> str:
        """Open `url` in Workspace `ws` ("" = none); returns the new tab's target id."""
        before = {t["targetId"] for t in self.pages()}
        self.ws(f"workspaceTest.open({json.dumps(url)}, {json.dumps(ws)}, {json.dumps(background)})")
        t = self.wait_page(lambda t: t["targetId"] not in before)
        return t["targetId"]

    def wait_expr(self, tid: str, expr: str, timeout: float = 15.0):
        deadline = time.monotonic() + timeout
        last = None
        while time.monotonic() < deadline:
            try:
                last = self.eval(tid, expr)
                if last:
                    return last
            except Exception as e:  # page still loading
                last = str(e)
            time.sleep(0.2)
        raise RuntimeError(f"timed out waiting for {expr!r}: {last}")

    def whoami(self, tid: str) -> dict:
        self.wait_expr(tid, "!!window.whoReady")
        return self.eval(tid, "window.whoReady")

    def tabs(self) -> list[dict]:
        return self.ws("workspaceTest.list()")

    def index_of(self, url_part: str, ws: str | None = None) -> int:
        for t in self.tabs():
            if url_part in t["url"] and (ws is None or t["workspace"] == ws):
                return t["index"]
        raise RuntimeError(f"no tab {url_part} in {ws}")

    def activate(self, index: int) -> float:
        return self.ws(f"workspaceTest.activate({index})")["ms"]

    def close(self) -> dict:
        return self.proc.shutdown()


def pss_kb(root: int) -> int:
    """Total PSS of the browser and every descendant, from smaps_rollup."""
    pids = [root]
    children: dict[int, list[int]] = {}
    for p in Path("/proc").iterdir():
        if not p.name.isdigit():
            continue
        try:
            ppid = int((p / "stat").read_text().rsplit(")", 1)[1].split()[1])
        except (OSError, IndexError, ValueError):
            continue
        children.setdefault(ppid, []).append(int(p.name))
    i = 0
    while i < len(pids):
        pids += children.get(pids[i], [])
        i += 1
    total = 0
    for pid in pids:
        try:
            for line in Path(f"/proc/{pid}/smaps_rollup").read_text().splitlines():
                if line.startswith("Pss:"):
                    total += int(line.split()[1])
        except OSError:
            pass
    return total


def settle_pss(root: int, wait: float = 4.0) -> int:
    time.sleep(wait)
    return statistics.median(pss_kb(root) for _ in range(3) if not time.sleep(0.3))


def shot(display: str, path: Path) -> str:
    subprocess.run(["import", "-display", display, "-window", "root", str(path)], check=True,
                   capture_output=True, timeout=30)
    return str(path)


# ---------------------------------------------------------------- the runs


PARTIAL: dict = {"story92": {}, "leaks": {}}


def run_story_and_leaks(args, site: Site, out: Path) -> dict:
    res = PARTIAL
    prof = args.profiles / "story"
    shutil.rmtree(prof, ignore_errors=True)
    prof.mkdir(parents=True)
    (prof / "Default").mkdir()
    # Continue where you left off: session cookies persist (ShouldPersistSessionCookies) and tabs restore.
    (prof / "Default" / "Preferences").write_text(json.dumps({"session": {"restore_on_startup": 1}}))
    b = Browser(args.binary, prof, out / "netlog-story.json", out / "stderr-story.log", args.display)
    try:
        b.ws_page()
        # Story 92: two logins to one site, two Workspaces, one window.
        t1 = b.open(site.url("/login?user=A"), "ws1")
        b.wait_expr(t1, "window.loginDone === true")
        t2 = b.open(site.url("/login?user=B"), "ws2")
        b.wait_expr(t2, "window.loginDone === true")
        w1 = b.open(site.url("/whoami"), "ws1")
        w2 = b.open(site.url("/whoami"), "ws2")
        who1, who2 = b.whoami(w1), b.whoami(w2)
        res["story92"]["before_restart"] = {"ws1": who1, "ws2": who2, "tabs": b.tabs()}
        b.activate(b.index_of("/whoami", "ws1"))
        time.sleep(1)
        res["story92"]["shot_ws1"] = shot(args.display, out / "story92-ws1-whoami.png")
        b.activate(b.index_of("/whoami", "ws2"))
        time.sleep(1)
        res["story92"]["shot_ws2"] = shot(args.display, out / "story92-ws2-whoami.png")
        res["story92"]["pass_before"] = (who1["cookie"] == "A" and who2["cookie"] == "B"
                                         and who1["localStorage"] == "A" and who2["localStorage"] == "B")

        # Leak 1: storage. A third Workspace and the no-Workspace tab see nothing.
        w3 = b.open(site.url("/whoami"), "ws3")
        w0 = b.open(site.url("/whoami"), "")
        who3, who0 = b.whoami(w3), b.whoami(w0)
        empty = {"cookie": "", "localStorage": "", "sessionStorage": "", "indexedDB": "", "cacheStorage": ""}
        res["leaks"]["1_storage"] = {
            "ws1": who1, "ws2": who2, "ws3": who3, "none": who0,
            "pass": who3 == empty and who0 == empty and all(
                who1[k] == "A" and who2[k] == "B" for k in ("cookie", "localStorage", "indexedDB", "cacheStorage"))}

        # Leak 2: a service worker registered in ws1 is not seen in ws2.
        b.eval(w1, "navigator.serviceWorker.register('/sw.js').then(r => navigator.serviceWorker.ready).then(() => 1)")
        regs1 = b.eval(w1, "navigator.serviceWorker.getRegistrations().then(r => r.length)")
        regs2 = b.eval(w2, "navigator.serviceWorker.getRegistrations().then(r => r.length)")
        res["leaks"]["2_service_worker"] = {"ws1_registrations": regs1, "ws2_registrations": regs2,
                                            "pass": regs1 >= 1 and regs2 == 0}

        # Leak 3: HTTP cache. A max-age resource fetched in ws1 must be fetched again in ws2.
        cid = str(time.time_ns())
        f = f"fetch('/cached?id={cid}').then(r => r.text())"
        b.eval(w1, f)
        b.eval(w1, f)  # served from ws1's cache: hit count stays 1
        hits_after_ws1 = site.cached_hits.get(cid, 0)
        only_cached = b.eval(w2, f"fetch('/cached?id={cid}', {{cache: 'only-if-cached', mode: 'same-origin'}})"
                                 ".then(r => 'hit ' + r.status, e => 'miss')")
        b.eval(w2, f)
        hits_after_ws2 = site.cached_hits.get(cid, 0)
        res["leaks"]["3_http_cache"] = {"server_hits_after_ws1_twice": hits_after_ws1, "ws2_only_if_cached": only_cached,
                                        "server_hits_after_ws2": hits_after_ws2,
                                        "pass": hits_after_ws1 == 1 and only_cached == "miss" and hits_after_ws2 == 2}

        # Leak 6: HTTP basic auth entered in ws1 is not sent from ws2.
        auth_url = f"http://u:p@localhost:{site.port}/auth/"
        ta = b.open(auth_url, "ws1")
        time.sleep(1.5)
        n = len(site.auth_seen)
        a1 = b.eval(w1, "fetch('/auth/x?probe=1').then(r => r.status)")
        a2 = b.eval(w2, "fetch('/auth/x?probe=1').then(r => r.status)")
        res["leaks"]["6_basic_auth"] = {"login_sent_credentials": any(site.auth_seen[:n]),
                                        "ws1_probe_status": a1, "ws2_probe_status": a2,
                                        "pass": a1 == 200 and a2 == 403}
        b.c.send("Target.closeTarget", {"targetId": ta})

        # Leak 8: a noopener window from ws1 stays in ws1 (patch 0002).
        before = {t["targetId"] for t in b.pages()}
        b.eval(w1, "window.open('/whoami?noopener', '_blank', 'noopener'); 1")
        tn = b.wait_page(lambda t: t["targetId"] not in before)["targetId"]
        whon = b.whoami(tn)
        wsn = [t["workspace"] for t in b.tabs() if "noopener" in t["url"]]
        res["leaks"]["8_noopener"] = {"who": whon, "workspace": wsn, "pass": whon["cookie"] == "A" and wsn == ["ws1"]}
        b.c.send("Target.closeTarget", {"targetId": tn})

        # Leak 9: a tab moved ws1 -> ws2 gets ws2's login, no opener, no sessionStorage.
        tm = b.open(site.url("/whoami?moved"), "ws1")
        b.whoami(tm)
        before = {t["targetId"] for t in b.pages()}
        b.ws(f"workspaceTest.move({b.index_of('moved', 'ws1')}, 'ws2')")
        tmv = b.wait_page(lambda t: t["targetId"] not in before and "moved" in t["url"])["targetId"]
        whom = b.whoami(tmv)
        opener = b.eval(tmv, "window.opener === null")
        res["leaks"]["9_move"] = {"who_after_move": whom, "opener_null": opener,
                                  "workspace": [t["workspace"] for t in b.tabs() if "moved" in t["url"]],
                                  "pass": whom["cookie"] == "B" and whom["sessionStorage"] == "" and opener}

        # Leak 7 (G2, expected to fail): a notification grant for the origin applies in every Workspace.
        settings = b.open("chrome://settings/content/notifications", "")
        b.wait_expr(settings, "typeof chrome.send === 'function' && document.readyState === 'complete'")
        b.eval(settings, f"chrome.send('setOriginPermissions', [{json.dumps(site.url(''))}, 'notifications', 'allow']); 1")
        time.sleep(1)
        p1 = b.eval(w1, "Notification.permission")
        p2 = b.eval(w2, "Notification.permission")
        res["leaks"]["7_permission_G2"] = {"granted_for_origin": True, "ws1": p1, "ws2": p2,
                                           "expected": "fail", "pass": not (p1 == "granted" and p2 == "granted")}

        # Leak 10 (G3, expected to fail): Clear browsing data (cookies and site data, all time).
        b.eval(w0, "document.cookie = 'user=Z; path=/'; 1")
        # The clear-data handler needs its own page (the dialog initializes it; calling it from another
        # settings page crashes the browser).
        cbd = b.open("chrome://settings/clearBrowserData", "")
        b.wait_expr(cbd, "document.readyState === 'complete'")
        time.sleep(1)
        b.eval(cbd, "chrome.send('clearBrowsingData', ['cbd', ['browser.clear_data.cookies', "
                    "'browser.clear_data.cache'], 4]); 1")
        time.sleep(4)
        b.c.send("Page.reload", session_id=b.attach(w0))
        b.c.send("Page.reload", session_id=b.attach(w1))
        time.sleep(2)
        c0 = b.whoami(w0)["cookie"]
        c1 = b.whoami(w1)["cookie"]
        res["leaks"]["10_clear_data_G3"] = {"default_partition_cookie_after": c0, "ws1_cookie_after": c1,
                                            "expected": "fail", "pass": c0 == "" and c1 == ""}
        b.c.send("Target.closeTarget", {"targetId": settings})
        b.c.send("Target.closeTarget", {"targetId": cbd})
        # ws1 lost nothing above if G3 is open; log in again so the restart test is about restart only.
        b.c.send("Page.navigate", {"url": site.url("/login?user=A")}, session_id=b.attach(w1))
        b.wait_expr(w1, "window.loginDone === true")
        b.c.send("Page.navigate", {"url": site.url("/whoami")}, session_id=b.attach(w1))
        time.sleep(1)
        res["tabs_before_restart"] = b.tabs()
    finally:
        res["exit1"] = b.close()

    # Restart on the same profile.
    b = Browser(args.binary, prof, out / "netlog-story2.json", out / "stderr-story2.log", args.display,
                fresh_ws_page=True)
    try:
        time.sleep(4)
        restored = b.tabs()
        res["restored_tabs"] = restored
        restored_who = {}
        for t in b.pages():
            if "/whoami" in t["url"]:
                try:
                    restored_who[t["url"]] = b.whoami(t["targetId"])["cookie"]
                except Exception as e:  # noqa: BLE001
                    restored_who[t["url"]] = f"error {e}"
        ws_of_restored = [t["workspace"] for t in restored if "/whoami" in t["url"]]
        # Leak 11 (G4, expected to fail): Chromium's session restore keeps no Workspace.
        res["leaks"]["11_session_restore_G4"] = {"restored_whoami_workspaces": ws_of_restored,
                                                 "restored_whoami_cookies": restored_who, "expected": "fail",
                                                 "pass": bool(ws_of_restored) and all(ws_of_restored)}
        # Story 92 after restart: the test page restores the Workspaces itself (G4 is open).
        r1 = b.open(site.url("/whoami?after-restart"), "ws1")
        r2 = b.open(site.url("/whoami?after-restart"), "ws2")
        a1, a2 = b.whoami(r1), b.whoami(r2)
        b.activate(b.index_of("after-restart", "ws1"))
        time.sleep(1)
        res["story92"]["shot_restart_ws1"] = shot(args.display, out / "story92-restart-ws1.png")
        b.activate(b.index_of("after-restart", "ws2"))
        time.sleep(1)
        res["story92"]["shot_restart_ws2"] = shot(args.display, out / "story92-restart-ws2.png")
        res["story92"]["after_restart"] = {"ws1": a1, "ws2": a2}
        # G1 (patch 0004): the session cookie survives the restart.
        res["story92"]["pass_after"] = a1["cookie"] == "A" and a2["cookie"] == "B"
    finally:
        res["exit2"] = b.close()
    return res


def run_memory(args, site: Site, out: Path, run: int) -> dict:
    res: dict = {}
    for mode in ("empty_ws", "empty_one", "loaded_ws", "loaded_one"):
        prof = args.profiles / f"m{run}{mode[:3]}{mode[-2:]}"
        shutil.rmtree(prof, ignore_errors=True)
        b = Browser(args.binary, prof, out / f"netlog-mem-{run}-{mode}.json", out / f"stderr-mem-{run}-{mode}.log",
                    args.display)
        try:
            b.ws_page()
            series = [settle_pss(b.pid, 6.0)]
            for i in range(1, 6):
                url = "about:blank" if mode.startswith("empty") else site.url(f"/login?user=U{i}")
                ws = f"mem{i}" if mode.endswith("_ws") else "mem1"
                tid = b.open(url, ws, background=True)
                if not mode.startswith("empty"):
                    b.wait_expr(tid, "window.loginDone === true")
                series.append(settle_pss(b.pid))
            res[mode] = series
        finally:
            b.close()
    return res


def run_switch(args, site: Site, out: Path) -> dict:
    prof = args.profiles / "switch"
    shutil.rmtree(prof, ignore_errors=True)
    b = Browser(args.binary, prof, out / "netlog-switch.json", out / "stderr-switch.log", args.display)
    probe = """window.shown = []; document.addEventListener('visibilitychange', () => {
      if (document.visibilityState === 'visible') requestAnimationFrame(() =>
        window.shown.push(performance.timeOrigin + performance.now())); }); 1"""
    timings = []
    cold = {}
    try:
        b.ws_page()
        t0 = time.time() * 1000
        ta = b.open(site.url("/whoami?switch=a"), "swa")
        b.whoami(ta)
        cold["new_workspace_site_ms"] = time.time() * 1000 - t0
        t0 = time.time() * 1000
        tb = b.open(site.url("/whoami?switch=b"), "swb")
        b.whoami(tb)
        cold["second_new_workspace_site_ms"] = time.time() * 1000 - t0
        t0 = time.time() * 1000
        tc = b.open("about:blank", "swc")
        b.wait_page(lambda t: t["targetId"] == tc)
        cold["new_workspace_blank_ms"] = time.time() * 1000 - t0
        b.eval(ta, probe)
        b.eval(tb, probe)
        ia, ib = b.index_of("switch=a", "swa"), b.index_of("switch=b", "swb")
        for i in range(args.switches * 2):
            tid, idx = (ta, ia) if i % 2 == 0 else (tb, ib)
            n = b.eval(tid, "window.shown.length")
            sent = b.activate(idx)
            got = b.wait_expr(tid, f"window.shown.length > {n} && window.shown[{n}]", timeout=5)
            timings.append(got - sent)
            time.sleep(0.3)
    finally:
        b.close()
    timings.sort()
    return {"switch_ms": timings, "median_ms": statistics.median(timings),
            "p95_ms": timings[max(0, int(round(0.95 * len(timings))) - 1)], "cold_create": cold}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--display", default=os.environ.get("DISPLAY", ":99"))
    ap.add_argument("--profiles", type=Path, default=Path("/mnt/linux2/aerie-build/bench-tmp/ws"),
                    help="profile root; short, the singleton socket path has a length limit")
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--switches", type=int, default=10, help="round trips (2 switches each)")
    ap.add_argument("--only", choices=("story", "memory", "switch"))
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    site = Site()
    results: dict = {"binary": str(args.binary), "port": site.port}
    if args.only in (None, "story"):
        results["story_and_leaks"] = PARTIAL
        try:
            run_story_and_leaks(args, site, args.out)
        finally:
            (args.out / "results-story.json").write_text(json.dumps(PARTIAL, indent=1))
    if args.only in (None, "memory"):
        results["memory_runs"] = [run_memory(args, site, args.out, r) for r in range(args.runs)]
    if args.only in (None, "switch"):
        results["switch_runs"] = [run_switch(args, site, args.out) for _ in range(args.runs)]
    (args.out / "results.json").write_text(json.dumps(results, indent=1))
    print(json.dumps(results, indent=1)[:4000])


if __name__ == "__main__":
    main()
