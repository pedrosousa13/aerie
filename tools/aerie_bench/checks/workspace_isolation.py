"""Workspace isolation (issue #73): nothing crosses between two Workspaces.

Written before the feature. ADR 0006 chose one storage partition per Workspace in one profile; this
suite is the proof M5 has to satisfy. Until Workspaces exist every probe is *expected to fail*
(``EXPECTED_TO_FAIL``), so the check reports ``xfail`` and the run stays green. A probe that starts
to pass while it is still listed makes the check fail with ``xpass``: delete its id from the set.
M5 is done when the set is empty and the check passes. ``--workspaces-enforce`` ignores the set.

What a probe does (the same shape every time, so a leak cannot hide behind a broken probe):

1. Write a unique token in Workspace X, in one tab.
2. Negative control: read it back in a second tab of X. It must be there, or the probe proves nothing.
3. Read in Workspace Y. The token must not be there.
4. Do the same with X and Y swapped ("both directions").

The browser side is a driver, a small contract that a test build of Aerie has to offer (see
``workspace_driver.py`` and docs/bench.md). Probes only use the ``Driver`` and ``Tab`` methods below,
so they run unchanged against a fake browser in the unit tests.

``ADR_ROWS`` names the row of ADR 0006's shared-state table that each probe proves. A test reads the
ADR and fails if a row marked "split" has no probe and no entry in ``WAIVED_ROWS``.
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Protocol

from ..session import CheckResult, Session
from . import register

WS_A, WS_B = "ws1", "ws2"
TEST_FLAG = "--aerie-test-workspaces"
VISITED_RGB, UNVISITED_RGB = (255, 0, 0), (0, 0, 255)   # link.html: a:visited and a:link
CACHE_DELAY_MS = 300
PERMISSION = "notifications"


class Tab(Protocol):
    workspace: str
    pid: int | None

    def call(self, fn: str, *args: Any) -> Any: ...          # ``wsProbe[fn](*args)`` on probe.html, awaited
    def navigate(self, url: str) -> dict: ...                # {"error": str | None}
    def reload(self) -> None: ...
    def link_colour(self) -> tuple[int, int, int] | None: ...  # dominant colour of link.html's link
    def window_open_noopener(self, path: str) -> "Tab": ...
    def close(self) -> None: ...


class Driver(Protocol):
    def available(self) -> str | None: ...                   # None, or why the driver cannot be used
    def open(self, url: str, workspace: str) -> Tab: ...
    def move(self, tab: Tab, workspace: str) -> Tab: ...     # the moved tab is a NEW tab (ADR constraint)
    def open_external(self, url: str) -> Tab: ...            # a link handed over by another app
    def activate_workspace(self, workspace: str) -> None: ...
    def set_permission(self, origin: str, name: str, value: str, workspace: str) -> None: ...
    def history(self, workspace: str) -> list[str]: ...      # URLs in that Workspace's history
    def omnibox(self, workspace: str, text: str) -> list[str]: ...  # URLs the omnibox suggests there
    def resolve(self, workspace: str, host: str) -> None: ...       # resolve a name through that Workspace's resolver
    def dns_cache(self, workspace: str) -> list[str]: ...    # host names in that Workspace's host cache
    def set_client_cert_choice(self, workspace: str, host: str, cert: str) -> None: ...
    def client_cert_choice(self, workspace: str, host: str) -> str | None: ...  # the cached choice, or None


@dataclass
class Site:
    origin: str                      # http://localhost:<port>
    host: str                        # localhost
    port: int
    tls_port: int | None = None      # the https twin of the site, for HSTS
    tls_error: str | None = None

    def url(self, path: str) -> str:
        return self.origin + path


@dataclass
class ProbeResult:
    ok: bool
    detail: str
    observed: dict = field(default_factory=dict)


class World:
    """What a probe may use: the driver, the site, the fixture log and fresh tokens."""

    def __init__(self, driver: Driver, site: Site, hits: Callable[[str], int], profile_path: Path | None) -> None:
        self.driver, self.site, self._hits, self.profile_path = driver, site, hits, profile_path
        self._tabs: list[Tab] = []
        self._n = 0

    def token(self, label: str) -> str:
        self._n += 1
        return f"{label}-{self._n}"

    def hits(self, fragment: str) -> int:
        """Requests the fixture server has logged whose path contains ``fragment``."""
        return self._hits(fragment)

    def open(self, path_or_url: str, workspace: str) -> Tab:
        url = path_or_url if "://" in path_or_url else self.site.url(path_or_url)
        tab = self.driver.open(url, workspace)
        self._tabs.append(tab)
        return tab

    def probe_tab(self, workspace: str) -> Tab:
        return self.open("/ws/probe.html", workspace)

    def adopt(self, tab: Tab) -> Tab:
        self._tabs.append(tab)
        return tab

    def close_tabs(self) -> None:
        tabs, self._tabs = self._tabs, []
        for tab in tabs:
            try:
                tab.close()
            except Exception:  # noqa: BLE001 - a tab that is already gone must not hide the verdict
                pass

    def login_both(self) -> None:
        """User A in Workspace 1 and user B in Workspace 2 (cookie and localStorage). Every probe that
        needs the logins does this again, because the probes before it write other tokens in the same stores."""
        for ws, user in ((WS_A, "A"), (WS_B, "B")):
            tab = self.probe_tab(ws)
            tab.call("put", "cookie", user)
            tab.call("put", "localStorage", user)


DIRECTIONS = ((WS_A, WS_B), (WS_B, WS_A))


def _fail(detail: str, **observed) -> ProbeResult:
    return ProbeResult(False, detail, observed)


def _ok(detail: str, **observed) -> ProbeResult:
    return ProbeResult(True, detail, observed)


def _control_failed(what: str) -> ProbeResult:
    return _fail(f"negative control failed: {what}, so the probe cannot see the state it looks for")


# ---------------------------------------------------------------- probes


def _surface_probe(surface: str, *, same_tab: bool = False) -> Callable[[World], ProbeResult]:
    """Write a token in X; X must read it back (control); Y must not read it."""

    def probe(world: World) -> ProbeResult:
        seen_by = {}
        for x, y in DIRECTIONS:
            token = world.token(f"{surface}-{x}")
            writer = world.probe_tab(x)
            writer.call("put", surface, token)
            if same_tab:
                writer.reload()
                reader = writer
            else:
                reader = world.probe_tab(x)
            if reader.call("get", surface) != token:
                return _control_failed(f"{x} could not read back its own {surface}")
            other = world.probe_tab(y).call("get", surface)
            seen_by[f"{x}->{y}"] = other
            if other == token:
                return _fail(f"{surface} written in {x} was readable in {y}", **seen_by)
        return _ok(f"{surface} written in one Workspace is not readable in the other, both ways", **seen_by)

    return probe


def _broadcast_channel(world: World) -> ProbeResult:
    for x, y in DIRECTIONS:
        name, token = world.token("bc"), world.token(f"bc-{x}")
        poster, same, other = world.probe_tab(x), world.probe_tab(x), world.probe_tab(y)
        same.call("bcListen", name)
        other.call("bcListen", name)
        poster.call("bcPost", name, token)
        if token not in same.call("bcReceived", name):
            return _control_failed(f"a second tab of {x} did not get the BroadcastChannel message")
        if token in other.call("bcReceived", name):
            return _fail(f"a BroadcastChannel message from {x} reached {y}")
    return _ok("BroadcastChannel messages stay inside their Workspace, both ways")


def _web_locks(world: World) -> ProbeResult:
    for x, y in DIRECTIONS:
        name = world.token("lock")
        holder, same, other = world.probe_tab(x), world.probe_tab(x), world.probe_tab(y)
        holder.call("lockHold", name)
        if same.call("lockTry", name):
            return _control_failed(f"a second tab of {x} could take a Web Lock that {x} holds")
        if not other.call("lockTry", name):
            return _fail(f"a Web Lock held in {x} was held in {y} too")
    return _ok("a Web Lock held in one Workspace can be taken in the other, both ways")


def _shared_worker(world: World) -> ProbeResult:
    for x, y in DIRECTIONS:
        name = world.token(f"worker-{x}")  # a worker name per direction: the first one is still alive
        first, second, other = world.probe_tab(x), world.probe_tab(x), world.probe_tab(y)
        a, b = first.call("sharedCounter", name), second.call("sharedCounter", name)
        if not (isinstance(a, int) and b == a + 1):
            return _control_failed(f"two tabs of {x} did not share one SharedWorker (counts {a}, {b})")
        c = other.call("sharedCounter", name)
        if c != 1:
            return _fail(f"{y} joined the SharedWorker of {x} (its connection count was {c}, not 1)")
    return _ok("a SharedWorker serves one Workspace only, both ways")


def _blob_urls(world: World) -> ProbeResult:
    for x, y in DIRECTIONS:
        token = world.token(f"blob-{x}")
        maker, same, other = world.probe_tab(x), world.probe_tab(x), world.probe_tab(y)
        url = maker.call("blobMake", token)
        if same.call("blobRead", url) != token:
            return _control_failed(f"a second tab of {x} could not read {x}'s blob URL")
        if other.call("blobRead", url) == token:
            return _fail(f"a blob URL made in {x} was readable in {y}")
    return _ok("a blob URL made in one Workspace is not readable in the other, both ways")


DOM_SURFACES: tuple[tuple[str, Callable[[World], ProbeResult]], ...] = (
    ("localStorage", _surface_probe("localStorage")),
    ("sessionStorage", _surface_probe("sessionStorage", same_tab=True)),
    ("indexedDB", _surface_probe("indexedDB")),
    ("opfs", _surface_probe("opfs")),
    ("broadcastChannel", _broadcast_channel),
    ("webLocks", _web_locks),
    ("sharedWorker", _shared_worker),
    ("blobUrl", _blob_urls),
)


def _dom_storage(world: World) -> ProbeResult:
    """Row 2: localStorage, sessionStorage, IndexedDB, file system (OPFS), BroadcastChannel,
    Web Locks, SharedWorker and blob URLs. WebSQL was removed from Chromium, so there is nothing to probe."""
    failed, passed = [], []
    for name, probe in DOM_SURFACES:
        res = probe(world)
        (passed if res.ok else failed).append(name if res.ok else f"{name}: {res.detail}")
    if failed:
        return _fail("; ".join(failed), passed=passed, failed=failed)
    return _ok("every DOM storage surface is isolated, both ways: " + ", ".join(passed), passed=passed)


def _service_worker(world: World) -> ProbeResult:
    for x, y in DIRECTIONS:
        scope = world.site.url(f"/ws/{x}/")
        registered = world.probe_tab(x).call("swRegister", f"/ws/{x}/")
        if registered != scope:
            return _control_failed(f"{x} could not register a service worker (scope {registered!r})")
        if scope not in world.probe_tab(x).call("swScopes"):
            return _control_failed(f"a second tab of {x} did not list {x}'s service worker")
        if scope in world.probe_tab(y).call("swScopes"):
            return _fail(f"the service worker registered in {x} is listed in {y}")
    return _ok("a service worker registered in one Workspace is not listed in the other, both ways")


def _http_cache(world: World) -> ProbeResult:
    """The standard cache probe: a cached resource is fast and answers ``only-if-cached`` in X;
    in Y the same URL is slow, misses ``only-if-cached`` and reaches the server again."""
    seen = {}
    for x, y in DIRECTIONS:
        ident = world.token(f"cache-{x}")
        path = f"/ws/cached?id={ident}"
        tab = world.probe_tab(x)
        cold = tab.call("fetchCached", ident, CACHE_DELAY_MS)["ms"]
        warm = tab.call("fetchCached", ident, CACHE_DELAY_MS)["ms"]
        if world.hits(path) != 1 or tab.call("fetchOnlyIfCached", ident, CACHE_DELAY_MS) != "hit" \
                or warm > CACHE_DELAY_MS * 0.5:
            return _control_failed(f"{x} did not serve a cached resource from its own cache "
                                   f"(server hits {world.hits(path)}, warm {warm:.0f} ms)")
        other = world.probe_tab(y)
        verdict = other.call("fetchOnlyIfCached", ident, CACHE_DELAY_MS)
        timed = other.call("fetchCached", ident, CACHE_DELAY_MS)["ms"]
        hits = world.hits(path)
        seen[f"{x}->{y}"] = {"cold_ms": cold, "warm_ms": warm, "only_if_cached": verdict, "probe_ms": timed,
                             "server_hits": hits}
        if verdict == "hit":
            return _fail(f"{y} answered a fetch only-if-cached from the cache of {x}", **seen)
        if hits != 2 or timed < CACHE_DELAY_MS * 0.8:
            return _fail(f"{y} fetched a resource {x} had cached without reaching the server "
                         f"(server hits {hits}, {timed:.0f} ms)", **seen)
    return _ok("a resource cached in one Workspace is a cache miss in the other (by timing and by "
               "only-if-cached), both ways", **seen)


def code_cache_dirs(profile: Path) -> set[Path]:
    """Every ``Code Cache`` directory under the profile's partition directories."""
    root = Path(profile) / "Storage" / "ext"
    return {p for p in root.rglob("Code Cache") if p.is_dir()} if root.is_dir() else set()


def _code_cache(world: World) -> ProbeResult:
    """A layout check: every Workspace partition has its own ``Code Cache`` directory on disk. It does
    not read the cache entries."""
    if world.profile_path is None:
        return _fail("the Bench did not give the check the profile path")
    before = code_cache_dirs(world.profile_path)
    for ws in (WS_A, WS_B):
        for _ in range(3):
            world.probe_tab(ws).call("loadScript")
    new = sorted(code_cache_dirs(world.profile_path) - before)
    owners = {d.parent for d in new}
    if len(owners) < 2:
        return _fail(f"{len(owners)} Workspace partition(s) have their own Code Cache directory; two are needed",
                     new=[str(d) for d in new])
    return _ok(f"two Workspaces have two Code Cache directories ({len(owners)} partitions)",
               new=[str(d) for d in new])


def _hsts(world: World) -> ProbeResult:
    """X learns HSTS for a host over https; an http request in X is upgraded (control) and in Y it is not.
    An upgrade shows as a request the fixture server never saw: the http port does not speak TLS."""
    site = world.site
    if site.tls_port is None:
        return _fail(f"the https fixture is not available: {site.tls_error or 'TLS was not enabled'}")
    for x, y in DIRECTIONS:
        host = f"hsts-{x}.localhost"
        setter = world.probe_tab(x)
        setter.call("hstsSet", f"https://{host}:{site.tls_port}/ws/hsts")
        for who, expect_upgrade in ((x, True), (y, False)):
            ident = world.token(f"hsts-{who}")
            world.probe_tab(who).navigate(f"http://{host}:{site.port}/ws/ping?id={ident}")
            reached = world.hits(f"/ws/ping?id={ident}") > 0
            if expect_upgrade and reached:
                return _control_failed(f"{x} did not upgrade http://{host} after its own HSTS header")
            if not expect_upgrade and not reached:
                return _fail(f"{y} upgraded http://{host} to https after {x} got an HSTS header for it")
    return _ok("an HSTS header seen in one Workspace does not upgrade requests in the other, both ways")


def _http_auth(world: World) -> ProbeResult:
    for x, y in DIRECTIONS:
        tag = world.token(x).replace("-", "_")  # the credentials are cached per path: one directory for each direction
        world.open(f"http://u:p@{world.site.host}:{world.site.port}/ws/auth/{tag}/login", x)
        if not world.probe_tab(x).call("authProbe", tag):
            return _control_failed(f"{x} did not send the HTTP credentials it had just entered")
        if world.probe_tab(y).call("authProbe", tag):
            return _fail(f"{y} sent HTTP basic credentials that were entered in {x}")
    return _ok("HTTP basic credentials entered in one Workspace are not sent in the other, both ways")


def _permissions(world: World) -> ProbeResult:
    """ADR decision D2, gap G2: a grant made in X reads ``default`` or ``prompt`` in Y."""
    origin = world.site.origin
    for x, y in DIRECTIONS:
        world.driver.set_permission(origin, PERMISSION, "allow", x)
        try:
            own = world.probe_tab(x).call("permission")
            other = world.probe_tab(y).call("permission")
        finally:
            world.driver.set_permission(origin, PERMISSION, "default", x)
        if own != "granted":
            return _control_failed(f"{x} did not read its own {PERMISSION} grant (reads {own!r})")
        if other not in ("default", "prompt"):
            return _fail(f"a {PERMISSION} grant made in {x} reads {other!r} in {y}; it must read default or prompt")
    return _ok("a permission granted in one Workspace reads default or prompt in the other, both ways")


def _history(world: World) -> ProbeResult:
    """ADR decision D1, gap G8: history, omnibox suggestions and :visited styling are per Workspace."""
    for x, y in DIRECTIONS:
        tag = world.token(f"hist{x}")
        visited = world.site.url(f"/ws/page.html?v={tag}")
        world.open(visited, x)
        if not any(tag in u for u in world.driver.history(x)) or not any(tag in u for u in world.driver.omnibox(x, tag)):
            return _control_failed(f"{x}'s history or omnibox does not show a page visited in {x}")
        if any(tag in u for u in world.driver.history(y)):
            return _fail(f"a page visited in {x} is in {y}'s history")
        if any(tag in u for u in world.driver.omnibox(y, tag)):
            return _fail(f"a page visited in {x} is suggested by {y}'s omnibox")
        link = f"/ws/link.html?target={visited}"
        # Visited-link state reaches the renderer a moment after the visit, so look for a while.
        colour_x = _wait_colour(world, link, x, VISITED_RGB)
        if not _near(colour_x, VISITED_RGB):
            return _control_failed(f"a link to a page visited in {x} was not styled :visited in {x} (colour {colour_x})")
        colour_y = world.open(link, y).link_colour()
        if not _near(colour_y, UNVISITED_RGB):
            return _fail(f"a link to a page visited in {x} is styled :visited in {y} (colour {colour_y})")
    return _ok("history, omnibox suggestions and :visited styling stay inside their Workspace, both ways")


def _near(colour, want, tolerance: int = 48) -> bool:
    return colour is not None and all(abs(a - b) <= tolerance for a, b in zip(colour, want))


def _wait_colour(world: World, path: str, ws: str, want, seconds: float = 4.0):
    deadline, colour = time.monotonic() + seconds, None
    while True:
        colour = world.open(path, ws).link_colour()
        if _near(colour, want) or time.monotonic() >= deadline:
            return colour
        time.sleep(0.25)


def _renderer_process(world: World) -> ProbeResult:
    a, b = world.probe_tab(WS_A), world.probe_tab(WS_B)
    if a.pid is None or b.pid is None:
        return _control_failed("the driver did not report the renderer process of a tab")
    if a.pid == b.pid:
        return _fail(f"{WS_A} and {WS_B} share the renderer process {a.pid}")
    return _ok(f"the two Workspaces are in two renderer processes ({a.pid}, {b.pid})")


def _two_logins(world: World) -> ProbeResult:
    """Story 92: user A in Workspace 1 and user B in Workspace 2, one window, nothing crossing."""
    world.login_both()
    for ws, user in ((WS_A, "A"), (WS_B, "B")):
        who = world.probe_tab(ws).call("whoami")
        if who.get("cookie") != user or who.get("localStorage") != user:
            return _fail(f"{ws} should be logged in as {user} and shows {who}")
    return _ok("two logins to one site, in two Workspaces of one window, each see only their own")


def _tab_move(world: World) -> ProbeResult:
    """Story 95: a tab moved to the other Workspace uses that Workspace's login after reload."""
    world.login_both()
    for src, dst, user in ((WS_A, WS_B, "B"), (WS_B, WS_A, "A")):
        tab = world.probe_tab(src)
        tab.call("put", "sessionStorage", "carried")
        moved = world.adopt(world.driver.move(tab, dst))
        moved.reload()
        who = moved.call("whoami")
        if moved.workspace != dst:
            return _fail(f"the tab moved to {dst} reports Workspace {moved.workspace!r}")
        if who.get("cookie") != user or who.get("localStorage") != user:
            return _fail(f"a tab moved from {src} to {dst} should be logged in as {user} and shows {who}")
        if who.get("sessionStorage"):
            return _fail(f"a tab moved from {src} to {dst} kept its sessionStorage")
        if moved.call("hasOpener"):
            return _fail(f"a tab moved from {src} to {dst} still has window.opener")
    return _ok("a tab moved to the other Workspace uses that Workspace's login, with no opener and no sessionStorage")


def _external_link(world: World) -> ProbeResult:
    """Story 94 (decision D4): a link from another app opens a new tab in the active Workspace."""
    world.login_both()
    for ws, user in ((WS_B, "B"), (WS_A, "A")):
        world.driver.activate_workspace(ws)
        tab = world.adopt(world.driver.open_external(world.site.url("/ws/probe.html")))
        if tab.workspace != ws:
            return _fail(f"a link opened from outside Aerie landed in {tab.workspace!r}, the active Workspace is {ws}")
        who = tab.call("whoami")
        if who.get("cookie") != user:
            return _fail(f"a link opened from outside Aerie in {ws} should see user {user} and shows {who}")
    return _ok("a link opened from outside Aerie lands in the active Workspace, with that Workspace's login")


def _dns(world: World) -> ProbeResult:
    """Row 7: a name resolved in X is in X's host cache and not in Y's. The test page resolves through the
    Workspace's own resolver and reads its host cache, so ``--host-resolver-rules`` does not hide it."""
    for x, y in DIRECTIONS:
        host = f"{world.token(f'dns-{x}')}.workspace-probe.test"
        world.driver.resolve(x, host)
        if host not in world.driver.dns_cache(x):
            return _control_failed(f"the host cache of {x} does not list {host} after {x} resolved it")
        if host in world.driver.dns_cache(y):
            return _fail(f"{host} resolved in {x} is in the host cache of {y}")
    return _ok("a name resolved in one Workspace is not in the other's host cache, both ways")


def _client_cert(world: World) -> ProbeResult:
    """Row 10: the certificate chosen for a server in X is remembered in X and not in Y. The OS
    certificate store itself is shared by design and is not probed."""
    for x, y in DIRECTIONS:
        host, cert = f"{world.token(f'cert-{x}')}.workspace-probe.test", world.token(f"choice-{x}")
        world.driver.set_client_cert_choice(x, host, cert)
        if world.driver.client_cert_choice(x, host) != cert:
            return _control_failed(f"{x} did not remember its own certificate choice for {host}")
        other = world.driver.client_cert_choice(y, host)
        if other is not None:
            return _fail(f"the certificate choice made in {x} for {host} is remembered in {y} ({other!r})")
    return _ok("a client certificate choice made in one Workspace is not remembered in the other, both ways")


def _noopener(world: World) -> ProbeResult:
    """Gap G6: ``window.open(..., 'noopener')`` from a Workspace tab stays in that Workspace."""
    world.login_both()
    for ws, user in ((WS_A, "A"), (WS_B, "B")):
        child = world.adopt(world.probe_tab(ws).window_open_noopener("/ws/probe.html"))
        who = child.call("whoami")
        if child.workspace != ws or who.get("cookie") != user:
            return _fail(f"a noopener window from {ws} opened in {child.workspace!r} and sees {who}")
    return _ok("a noopener window opened from a Workspace tab stays in that Workspace")


# The sockets probe only creates traffic here; ``finalize`` judges it from the NetLog.
def _sockets_traffic(world: World) -> ProbeResult:
    for ws in (WS_A, WS_B, WS_A, WS_B):
        tab = world.probe_tab(ws)
        for _ in range(3):
            tab.call("ping", ws)
    return _ok("pending: judged from the NetLog after the browser exits")


@dataclass(frozen=True)
class Probe:
    id: str
    title: str
    adr_rows: tuple[str, ...]      # rows of ADR 0006's shared-state table
    run: Callable[[World], ProbeResult]


PROBES: tuple[Probe, ...] = (
    Probe("two_logins", "story 92: two logins in one window", (), _two_logins),
    Probe("cookies", "cookies", ("1",), _surface_probe("cookie")),
    Probe("dom_storage", "localStorage, sessionStorage, IndexedDB, OPFS, BroadcastChannel, Web Locks, "
          "SharedWorker, blob URLs", ("2",), _dom_storage),
    Probe("cache_storage", "Cache Storage", ("3",), _surface_probe("cacheStorage")),
    Probe("http_cache", "HTTP cache (timing and only-if-cached)", ("4",), _http_cache),
    Probe("code_cache", "code cache directories", ("4b",), _code_cache),
    Probe("service_workers", "service workers", ("5",), _service_worker),
    Probe("hsts", "HSTS", ("6",), _hsts),
    Probe("dns", "DNS host cache", ("7",), _dns),
    Probe("sockets", "no connection reuse across Workspaces (NetLog)", ("8",), _sockets_traffic),
    Probe("http_auth", "HTTP auth cache", ("9",), _http_auth),
    Probe("client_cert", "client certificate choices", ("10",), _client_cert),
    Probe("permissions", "permission grants (D2, G2)", ("11",), _permissions),
    Probe("history", "history, omnibox suggestions, :visited (D1, G8)", ("12",), _history),
    Probe("renderer_process", "one renderer process per Workspace", ("17",), _renderer_process),
    Probe("tab_move", "story 95: a moved tab uses the new Workspace's login", (), _tab_move),
    Probe("external_link", "story 94: an external link lands in the active Workspace", ("22",), _external_link),
    Probe("noopener", "noopener window stays in its Workspace", ("21",), _noopener),
)
PROBE_IDS = tuple(p.id for p in PROBES)

# ADR rows marked "split" that this suite cannot probe, with the reason. Empty: every split row has a probe.
WAIVED_ROWS: dict[str, str] = {}

# Every probe is expected to fail until Workspaces exist. M5 removes ids from this set as each turns
# green; it is done when the set is empty.
EXPECTED_TO_FAIL: frozenset[str] = frozenset(PROBE_IDS)


# ---------------------------------------------------------------- sockets (after exit)


def judge_sockets(records: list[dict], complete: bool) -> ProbeResult:
    """No socket carried a request of both Workspaces. Control: inside one Workspace sockets are
    reused, otherwise "no shared socket" would hold for any browser that never reuses one."""
    by_ws: dict[str, list[list[int]]] = {WS_A: [], WS_B: []}
    for r in records:
        url = r.get("url", "")
        for ws in by_ws:
            if f"/ws/ping?w={ws}" in url:
                by_ws[ws].append(r.get("sockets", []))
    if not complete:
        return _fail("the NetLog is incomplete, so socket reuse cannot be judged")
    for ws, socks in by_ws.items():
        if not socks or not any(socks):
            return _fail(f"the NetLog has no socket for the requests of {ws} ({len(socks)} requests)")
    used = {ws: {s for socks in lst for s in socks} for ws, lst in by_ws.items()}
    for ws, lst in by_ws.items():
        if len(used[ws]) >= len(lst):
            return _control_failed(f"{ws} used {len(used[ws])} sockets for {len(lst)} requests, none reused")
    shared = sorted(used[WS_A] & used[WS_B])
    if shared:
        return _fail(f"socket(s) {shared} carried requests of both {WS_A} and {WS_B}", shared=shared)
    return _ok(f"{WS_A} used {len(used[WS_A])} socket(s) and {WS_B} {len(used[WS_B])}, none in common; "
               "sockets were reused inside each Workspace")


# ---------------------------------------------------------------- the check


def _verdict(probes: dict[str, dict], enforce: bool) -> tuple[str, str]:
    failing = [i for i, p in probes.items() if p["raw"] != "pass"]
    if enforce:
        if failing:
            return "fail", f"{len(failing)} of {len(probes)} isolation probes failed: {', '.join(failing)}"
        return "pass", f"all {len(probes)} isolation probes pass: nothing crosses between two Workspaces"
    xpass = [i for i, p in probes.items() if p["expected_to_fail"] and p["raw"] == "pass"]
    unexpected = [i for i, p in probes.items() if not p["expected_to_fail"] and p["raw"] != "pass"]
    xfail = [i for i, p in probes.items() if p["expected_to_fail"] and p["raw"] != "pass"]
    if xpass or unexpected:
        parts = []
        if xpass:
            parts.append(f"xpass: {', '.join(xpass)} now pass; remove them from EXPECTED_TO_FAIL")
        if unexpected:
            parts.append(f"failed and not expected to: {', '.join(unexpected)}")
        return "fail", "; ".join(parts)
    if xfail:
        return "xfail", (f"expected to fail until Workspaces exist (M5): {len(xfail)} of {len(probes)} probes "
                         "do not pass yet")
    return "pass", f"all {len(probes)} isolation probes pass and none is marked expected to fail"


@register
class WorkspaceIsolation:
    name = "workspace_isolation"
    seeds_helium_services = False
    negative_control = ("Every probe first reads its token back inside the Workspace that wrote it (a second "
                        "tab, or the same tab after a reload), and a socket, cache or HSTS probe that cannot "
                        "see reuse inside one Workspace fails, so a leak cannot hide behind a blind probe.")

    def __init__(self, enforce: bool = False, driver_factory: Callable | None = None) -> None:
        self.enforce = enforce
        self._driver_factory = driver_factory
        self.tls_pin: str | None = None
        self.tls_error: str | None = None
        self._probes: dict[str, dict] = {}

    def browser_flags(self, server) -> tuple[str, ...]:
        """The test switch, and trust for the fixture's self-signed certificate (public key pin only)."""
        flags = [TEST_FLAG]
        try:
            self.tls_pin = server.enable_tls()
            flags.append(f"--ignore-certificate-errors-spki-list={self.tls_pin}")
        except RuntimeError as exc:
            self.tls_error = str(exc)
        return tuple(flags)

    def _site(self, session: Session) -> Site:
        from urllib.parse import urlsplit
        parts = urlsplit(session.fixtures.base_url)
        tls_port = None
        try:
            tls_port = session.fixtures.tls_port
        except (RuntimeError, AttributeError):
            pass
        return Site(origin=f"http://localhost:{parts.port}", host="localhost", port=parts.port,
                    tls_port=tls_port, tls_error=self.tls_error)

    def _driver(self, session: Session, site: Site) -> Driver:
        if self._driver_factory:
            return self._driver_factory(session, site)
        from ..workspace_driver import ChromeDriver
        return ChromeDriver(session, site)

    def run(self, session: Session) -> CheckResult:
        site = self._site(session)
        driver = self._driver(session, site)
        why = driver.available()
        world = World(driver, site, lambda frag: sum(1 for r in session.fixtures.snapshot() if frag in r["path"]),
                      getattr(session, "profile_path", None))
        for probe in PROBES:
            if why:
                res = _fail(f"the Workspace test driver is not available: {why}")
            else:
                try:
                    res = probe.run(world)
                except Exception as exc:  # noqa: BLE001 - a CDP failure is a failed probe, the rest still run
                    res = _fail(f"{type(exc).__name__}: {exc}")
                finally:
                    world.close_tabs()
            self._probes[probe.id] = self._entry(probe, res)
        return self._result(why)

    def _entry(self, probe: Probe, res: ProbeResult) -> dict:
        # "pending": the probe only made traffic; finalize judges it from the NetLog. Until then it counts as failed.
        return {"title": probe.title, "adr_rows": list(probe.adr_rows), "raw": "pass" if res.ok else "fail",
                "pending": probe.id == "sockets" and res.ok, "detail": res.detail, "observed": res.observed,
                "expected_to_fail": probe.id in EXPECTED_TO_FAIL}

    def _result(self, driver_problem: str | None = None) -> CheckResult:
        probes = {i: dict(p) for i, p in self._probes.items()}
        for p in probes.values():
            if p["pending"]:
                p["raw"] = "fail"
        status, detail = _verdict(probes, self.enforce)
        for p in probes.values():
            p["status"] = p["raw"] if self.enforce else (
                ("xpass" if p["raw"] == "pass" else "xfail") if p["expected_to_fail"]
                else p["raw"])
        data = {"enforce": self.enforce, "probes": probes, "expected_to_fail": sorted(EXPECTED_TO_FAIL),
                "waived_rows": WAIVED_ROWS, "driver_problem": driver_problem}
        return CheckResult(self.name, status, detail, data=data)

    def finalize(self, session: Session, result: CheckResult) -> CheckResult:
        if "sockets" not in self._probes or not self._probes["sockets"]["pending"]:
            return result
        res = judge_sockets(session.netlog, session.netlog_complete)
        self._probes["sockets"].update(raw="pass" if res.ok else "fail", pending=False, detail=res.detail, observed=res.observed)
        return self._result(result.data.get("driver_problem"))
