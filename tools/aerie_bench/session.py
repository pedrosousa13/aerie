"""What a check sees: a live page over CDP, the fixture server, and (after exit) the NetLog."""

from __future__ import annotations

import base64
import json
import time
from contextlib import contextmanager
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, ContextManager, Iterator

from .cdp import CdpClient, CdpError, CdpTimeout
from .fixtures import FixtureServer, StunProbe, TcpProbe


@dataclass
class CheckResult:
    name: str
    status: str  # pass | fail | error | skip
    detail: str = ""
    duration_s: float = 0.0
    data: dict = field(default_factory=dict)

    @property
    def ok(self) -> bool:
        return self.status in ("pass", "skip")


class Session:
    """One browser run. Live fields work while the browser runs; ``netlog`` and ``exit`` fill in after."""

    def __init__(self, client: CdpClient, fixtures: FixtureServer, out_dir: Path,
                 timeout: float = 30.0, binary: str = "", binary_version: str | None = None,
                 netlog_path: Path | None = None) -> None:
        self.client = client
        self.fixtures = fixtures
        self.out_dir = out_dir
        self.timeout = timeout
        self.binary = binary
        self.binary_version = binary_version
        self.netlog_path = netlog_path  # being written while the browser runs; may be partial
        self.netlog: list[dict] = []
        self.netlog_lookups: list[dict] = []   # host names in resolver, socket and connect events
        self.netlog_complete = False           # the NetLog was whole, valid JSON
        self.exit: dict = {}
        self.artifacts: list[str] = []
        # Set by the runner: starts a second browser (own profile, own flags) for negative controls.
        self.variant_launcher: Callable[..., ContextManager["Session"]] | None = None
        # Set by the runner: a context manager for a throwaway profile that outlives one browser
        # (pass it to variant(profile=...) twice to restart on the same profile).
        self.profile_factory: Callable[[], ContextManager] | None = None
        self.browser_pid: int | None = None  # set by the runner; the root of the browser's process tree
        self._sid: str | None = None
        # Set on the session that incognito() hands out: its page lives in this browser context.
        self._context_id: str | None = None

    def _page(self) -> str:
        if self._sid is None:
            params = {"url": "about:blank"}
            if self._context_id:
                params["browserContextId"] = self._context_id
            target = self.client.send("Target.createTarget", params, timeout=self.timeout)
            att = self.client.send("Target.attachToTarget",
                                   {"targetId": target["targetId"], "flatten": True}, timeout=self.timeout)
            self._sid = att["sessionId"]
            self.client.send("Page.enable", session_id=self._sid, timeout=self.timeout)
        return self._sid

    def cdp(self, method: str, params: dict | None = None) -> dict:
        """Send a CDP command to the page."""
        return self.client.send(method, params, session_id=self._page(), timeout=self.timeout)

    def take_events(self, method: str) -> list[dict]:
        """Consume the page's queued CDP events of one kind (see CdpClient.take_events)."""
        return self.client.take_events(method, session_id=self._page())

    def navigate(self, path: str) -> None:
        """Load a fixture path and wait for the load event."""
        sid = self._page()
        self.client.drain("Page.loadEventFired")
        res = self.client.send("Page.navigate", {"url": self.fixtures.base_url + path},
                               session_id=sid, timeout=self.timeout)
        if res.get("errorText"):
            raise CdpError(f"navigate {path}: {res['errorText']}")
        self.client.wait_event("Page.loadEventFired", session_id=sid, timeout=self.timeout)

    def navigate_url(self, url: str, wait: float | None = None) -> dict:
        """Load any URL. A failed navigation is data, not an exception: returns
        {"url", "error"} where error is the net error text or None."""
        sid = self._page()
        self.client.drain("Page.loadEventFired")
        res = self.client.send("Page.navigate", {"url": url}, session_id=sid, timeout=self.timeout)
        if res.get("errorText"):
            return {"url": url, "error": res["errorText"]}
        try:
            self.client.wait_event("Page.loadEventFired", session_id=sid, timeout=wait or self.timeout)
        except CdpTimeout:
            return {"url": url, "error": "load timeout"}
        return {"url": url, "error": None}

    def wait_for(self, expression: str, timeout: float = 15.0, interval: float = 0.25):
        """Poll a JS expression until it is truthy; return its value, or None on timeout."""
        deadline = time.monotonic() + timeout
        while True:
            try:
                value = self.evaluate(expression)
            except CdpError:
                value = None
            if value:
                return value
            if time.monotonic() >= deadline:
                return None
            time.sleep(interval)

    def targets(self) -> list[dict]:
        """Every DevTools target the browser knows (pages, OOPIFs, workers)."""
        return self.client.send("Target.getTargets", timeout=self.timeout).get("targetInfos", [])

    def tcp_probe(self) -> TcpProbe:
        return TcpProbe()

    def stun_probe(self) -> StunProbe:
        return StunProbe()

    def variant(self, extra_flags: tuple[str, ...] = (), prefs: dict | None = None,
                profile=None) -> ContextManager["Session"]:
        """A second browser with a fresh profile (or ``profile``, from new_profile()), extra flags and
        seeded prefs. Shares the fixture server. It is stopped on exit, also on error, and a fresh
        profile is deleted. After the ``with`` block the browser has exited and the session's
        ``netlog`` holds what that launch requested."""
        if self.variant_launcher is None:
            raise CdpError("this session cannot start a second browser")
        if profile is None:
            return self.variant_launcher(tuple(extra_flags), prefs)
        return self.variant_launcher(tuple(extra_flags), prefs, profile)

    @contextmanager
    def incognito(self) -> Iterator["Session"]:
        """A page in a new off-the-record browser context of the same browser (CDP
        Target.createBrowserContext). The context has its own cookies, storage, in-memory content
        settings and, for Aerie, its own fingerprint seeds. It is disposed on exit, also on error,
        and everything it held is gone. Each call is a new context."""
        context = self.client.send("Target.createBrowserContext", timeout=self.timeout)["browserContextId"]
        child = Session(self.client, self.fixtures, self.out_dir, timeout=self.timeout, binary=self.binary,
                        binary_version=self.binary_version)
        child._context_id = context
        child.browser_pid = self.browser_pid
        try:
            yield child
        finally:
            try:
                self.client.send("Target.disposeBrowserContext", {"browserContextId": context},
                                 timeout=self.timeout)
            except CdpError:
                pass  # the browser is gone; nothing is left to dispose
            self.artifacts.extend(a for a in child.artifacts if a not in self.artifacts)

    def new_profile(self) -> ContextManager:
        """A throwaway profile for variant(profile=...); deleted when this block ends."""
        if self.profile_factory is None:
            raise CdpError("this session cannot make a profile")
        return self.profile_factory()

    def evaluate(self, expression: str, await_promise: bool = False, user_gesture: bool = False):
        params = {"expression": expression, "returnByValue": True}
        if await_promise:
            params["awaitPromise"] = True
        if user_gesture:
            params["userGesture"] = True
        res = self.client.send("Runtime.evaluate", params, session_id=self._page(), timeout=self.timeout)
        if "exceptionDetails" in res:
            det = res["exceptionDetails"]
            what = (det.get("exception") or {}).get("description") or det.get("text", "exception")
            raise CdpError(f"evaluate raised: {what}")
        return res.get("result", {}).get("value")

    def click(self, selector: str) -> None:
        found = self.evaluate(
            f"(()=>{{const e=document.querySelector({json.dumps(selector)});"
            "if(!e)return false;e.click();return true})()")
        if not found:
            raise CdpError(f"click: no element matches {selector!r}")

    def screenshot(self, name: str) -> Path:
        res = self.client.send("Page.captureScreenshot", {"format": "png"},
                               session_id=self._page(), timeout=self.timeout)
        path = self.out_dir / name
        path.write_bytes(base64.b64decode(res["data"]))
        self.artifacts.append(name)
        return path
