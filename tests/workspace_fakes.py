"""A fake browser with Workspaces, for the workspace_isolation unit tests.

State lives in "partitions": one per Workspace, unless a surface is named in ``leaks``, in which case
that surface is shared by every Workspace (a bug the probe for it must catch). The fake answers the
``wsProbe`` calls of tools/aerie_bench/fixtures/ws/probe.js with the same meaning.
"""

from __future__ import annotations

from urllib.parse import parse_qs, urlsplit

ALL_LEAKS = ("cookie", "localStorage", "sessionStorage", "indexedDB", "opfs", "cacheStorage", "broadcastChannel",
             "webLocks", "sharedWorker", "blobUrl", "serviceWorker", "httpCache", "httpAuth", "hsts",
             "permissions", "history", "visited", "process", "move", "external", "noopener", "dns", "clientCert",
             "fingerprint")


class FakeFixtures:
    def __init__(self, browser, port=4000, tls_port=4001):
        self._browser, self.base_url, self.tls_port = browser, f"http://127.0.0.1:{port}", tls_port

    def snapshot(self):
        return self._browser.requests


class FakeSession:
    def __init__(self, browser, profile_path=None):
        self.fixtures = FakeFixtures(browser)
        self.profile_path = profile_path
        self.netlog, self.netlog_complete = [], True
        self.timeout = 1.0


class FakeTab:
    def __init__(self, browser, url, ws):
        self.b, self.url, self.workspace, self.closed = browser, url, ws, False
        self.session_storage = {}
        self.listening = {}
        self.pid = browser.pid_for(ws)
        browser.navigated(self, url)

    def part(self, surface):
        return "" if surface in self.b.leaks else self.workspace

    def call(self, fn, *args):
        return getattr(self, "w_" + fn)(*args)

    # -- storage
    def w_put(self, surface, token):
        if surface == "sessionStorage":
            (self.b.shared_session if "sessionStorage" in self.b.leaks else self.session_storage)[surface] = token
        else:
            self.b.store[(self.part(surface), surface)] = token

    def w_get(self, surface):
        if surface == "sessionStorage":
            return (self.b.shared_session if "sessionStorage" in self.b.leaks else self.session_storage).get(surface, "")
        return self.b.store.get((self.part(surface), surface), "")

    def w_whoami(self):
        return {"cookie": self.w_get("cookie"), "localStorage": self.w_get("localStorage"),
                "sessionStorage": self.session_storage.get("sessionStorage", "")}

    def w_hasOpener(self):
        return False

    def w_bcListen(self, name):
        self.listening[name] = []
        return True

    def w_bcPost(self, name, token):
        for tab in self.b.tabs:
            if tab is not self and name in tab.listening and tab.part("broadcastChannel") == self.part("broadcastChannel"):
                tab.listening[name].append(token)
        return True

    def w_bcReceived(self, name):
        return self.listening.get(name, [])

    def w_lockHold(self, name):
        self.b.locks.add((self.part("webLocks"), name))
        return True

    def w_lockTry(self, name):
        return (self.part("webLocks"), name) not in self.b.locks

    def w_sharedCounter(self, name):
        key = (self.part("sharedWorker"), name)
        self.b.counters[key] = self.b.counters.get(key, 0) + 1
        return self.b.counters[key]

    def w_blobMake(self, token):
        url = f"blob:http://localhost/{token}"
        self.b.blobs[(self.part("blobUrl"), url)] = token
        return url

    def w_blobRead(self, url):
        return self.b.blobs.get((self.part("blobUrl"), url), "")

    def w_swRegister(self, scope):
        full = f"http://localhost:4000{scope}"
        self.b.workers.add((self.part("serviceWorker"), full))
        return full

    def w_swScopes(self):
        key = self.part("serviceWorker")
        return [s for p, s in self.b.workers if p == key]

    # -- network
    def w_fetchCached(self, ident, delay):
        key = (self.part("httpCache"), ident)
        if key in self.b.http_cache:
            return {"ms": 4.0, "text": ident}
        self.b.http_cache.add(key)
        self.b.requests.append({"path": f"/ws/cached?id={ident}&delay_ms={delay}"})
        return {"ms": delay + 10.0, "text": ident}

    def w_fetchOnlyIfCached(self, ident, delay):
        return "hit" if (self.part("httpCache"), ident) in self.b.http_cache else "miss"

    def w_authProbe(self, tag):
        return (self.part("httpAuth"), tag) in self.b.auth

    def w_ping(self, who):
        self.b.requests.append({"path": f"/ws/ping?w={who}"})
        return 200

    def w_hstsSet(self, url):
        host = urlsplit(url).hostname
        self.b.hsts.add((self.part("hsts"), host))
        return True

    def w_permission(self):
        return self.b.permissions.get(self.part("permissions"), "default")

    def w_fingerprint(self):
        # Seeds are keyed on the site and the partition (#121): the same site in one Workspace always shows
        # the same values; a leak makes every Workspace show the first one's.
        part = self.part("fingerprint")
        seed = sum(map(ord, part)) * 7919
        return {"canvas": f"c{seed:x}", "audio": f"a{seed * 31:x}", "cores": 4 + seed % 2}

    def w_loadScript(self):
        self.b.code_cache_hook(self.workspace)
        return True

    def navigate(self, url):
        return self.b.navigated(self, url)

    def reload(self):
        pass

    def link_colour(self):
        target = parse_qs(urlsplit(self.url).query).get("target", [""])[0]
        part = self.part("visited")
        return (255, 0, 0) if any(target == u and p == part for p, u in self.b.visited) else (0, 0, 255)

    def window_open_noopener(self, path):
        ws = "" if "noopener" in self.b.leaks else self.workspace
        return self.b.open(self.b.site_url(path), ws)

    def close(self):
        self.closed = True
        for key in [k for k in self.b.locks if k[0] == self.part("webLocks") and self.b.lock_owner.get(k) is self]:
            self.b.locks.discard(key)
        self.b.tabs = [t for t in self.b.tabs if t is not self]


class FakeBrowser:
    """Implements the Driver contract of workspace_isolation."""

    def __init__(self, leaks=(), available=None, pids=True):
        unknown = set(leaks) - set(ALL_LEAKS)
        assert not unknown, unknown
        self.leaks, self._available, self.with_pids = set(leaks), available, pids
        self.tabs, self.requests = [], []
        self.store, self.locks, self.counters, self.blobs = {}, set(), {}, {}
        self.workers, self.http_cache, self.auth, self.hsts = set(), set(), set(), set()
        self.permissions, self.hist, self.visited = {}, [], set()
        self.shared_session = {}
        self.lock_owner = {}
        self.dns, self.certs = set(), {}
        self.active = ""
        self.code_cache_hook = lambda ws: None

    def pid_for(self, ws):
        if not self.with_pids:
            return None
        return 100 if "process" in self.leaks else 100 + sum(map(ord, ws))

    def site_url(self, path):
        return f"http://localhost:4000{path}" if path.startswith("/") else path

    def available(self):
        return self._available

    def navigated(self, tab, url):
        parts = urlsplit(url)
        tab.url = url
        host = parts.hostname or ""
        part = lambda s: "" if s in self.leaks else tab.workspace  # noqa: E731
        if parts.username:
            self.auth.add((part("httpAuth"), parts.path.split("/")[3]))
        if parts.path == "/ws/page.html":
            self.hist.append((part("history"), url))
            self.visited.add((part("visited"), url))
        if host.startswith("hsts-") and parts.scheme == "http":
            if (part("hsts"), host) in self.hsts:
                return {"error": "net::ERR_SSL_PROTOCOL_ERROR"}
            self.requests.append({"path": parts.path + ("?" + parts.query if parts.query else "")})
        return {"error": None}

    def open(self, url, ws):
        tab = FakeTab(self, self.site_url(url), ws)
        self.tabs.append(tab)
        return tab

    def move(self, tab, ws):
        tab.close()
        new = self.open(tab.url, ws)
        if "move" in self.leaks:
            new.b.store[(ws, "cookie")] = tab.b.store.get((tab.workspace, "cookie"), "")
            new.b.store[(ws, "localStorage")] = tab.b.store.get((tab.workspace, "localStorage"), "")
            new.session_storage = dict(tab.session_storage)
        return new

    def open_external(self, url):
        ws = "" if "external" in self.leaks else self.active
        return self.open(url, ws)

    def activate_workspace(self, ws):
        self.active = ws

    def set_permission(self, origin, name, value, ws):
        self.permissions["" if "permissions" in self.leaks else ws] = "granted" if value == "allow" else value

    def history(self, ws):
        return [u for p, u in self.hist if p == ("" if "history" in self.leaks else ws) or "history" in self.leaks]

    def omnibox(self, ws, text):
        return [u for u in self.history(ws) if text in u]

    def resolve(self, ws, host):
        self.dns.add(("" if "dns" in self.leaks else ws, host))

    def dns_cache(self, ws):
        return [h for p, h in self.dns if p == ("" if "dns" in self.leaks else ws)]

    def set_client_cert_choice(self, ws, host, cert):
        self.certs[("" if "clientCert" in self.leaks else ws, host)] = cert

    def client_cert_choice(self, ws, host):
        return self.certs.get(("" if "clientCert" in self.leaks else ws, host))
