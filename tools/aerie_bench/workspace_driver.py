"""The real Workspace driver: ``chrome://workspace-test`` over CDP (issue #73).

Aerie has no Workspaces yet. ADR 0006's prototype (build train 8) drove a test build with a small
WebUI page, ``chrome://workspace-test``, served only with ``--aerie-test-workspaces`` (same pattern as
``chrome://filters``). This driver speaks that page's JavaScript API. The suite in
``checks/workspace_isolation.py`` only needs this contract, so M5's test page has to offer it:

    workspaceTest.open(url, workspaceId, background)   open a tab in a Workspace ("" = none)
    workspaceTest.list()                               [{index, targetId, url, workspace, pid}]
    workspaceTest.move(index, workspaceId)             MoveTabToWorkspace: opens a NEW tab, closes the old
    workspaceTest.activateWorkspace(workspaceId)       make a Workspace the active one
    workspaceTest.openExternal(url)                    what a link from another app does
    workspaceTest.setPermission(origin, name, value, workspaceId)
    workspaceTest.history(workspaceId)                 [url, ...] from that Workspace's history
    workspaceTest.omnibox(workspaceId, text)           [url, ...] its omnibox would suggest

``open``, ``list`` and ``move`` exist in the prototype (prototypes/workspaces/measure/measure.py);
``targetId`` and ``pid`` in ``list`` and the last five calls are new. While the page is absent,
``available()`` says so and every probe fails, which the suite expects.
"""

from __future__ import annotations

import base64
import json
import struct
import time
import zlib
from collections import Counter

from .cdp import CdpError
from .session import Session

WS_TEST_URL = "chrome://workspace-test/"


class DriverError(Exception):
    pass


def png_dominant_colour(data: bytes) -> tuple[int, int, int] | None:
    """The most common colour of an 8-bit RGB or RGBA PNG, ignoring near-white (the page background).
    ``None`` when the image has no other colour. Enough to read the colour of a styled link."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, header = 8, [], None
    while pos + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat.append(body)
        pos += 12 + length
    if header is None:
        raise ValueError("PNG without a header")
    width, height, depth, ctype, _, _, interlace = header
    if depth != 8 or ctype not in (2, 6) or interlace:
        raise ValueError(f"unsupported PNG (depth {depth}, colour type {ctype}, interlace {interlace})")
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(b"".join(idat))
    stride = width * bpp
    rows: list[bytearray] = []
    prev = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        kind, line = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        for i in range(stride):
            left = line[i - bpp] if i >= bpp else 0
            up = prev[i]
            upleft = prev[i - bpp] if i >= bpp else 0
            if kind == 1:
                line[i] = (line[i] + left) & 255
            elif kind == 2:
                line[i] = (line[i] + up) & 255
            elif kind == 3:
                line[i] = (line[i] + (left + up) // 2) & 255
            elif kind == 4:
                p = left + up - upleft
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - upleft)
                pred = left if pa <= pb and pa <= pc else (up if pb <= pc else upleft)
                line[i] = (line[i] + pred) & 255
        rows.append(line)
        prev = line
    counts: Counter = Counter()
    for line in rows:
        for x in range(0, stride, bpp):
            px = (line[x], line[x + 1], line[x + 2])
            if min(px) < 235:
                counts[px] += 1
    return counts.most_common(1)[0][0] if counts else None


class ChromeTab:
    def __init__(self, driver: "ChromeDriver", target_id: str) -> None:
        self.driver, self.target_id = driver, target_id
        self._sid: str | None = None

    # -- CDP plumbing

    @property
    def _client(self):
        return self.driver.session.client

    def _session(self) -> str:
        if self._sid is None:
            self._sid = self._client.send("Target.attachToTarget", {"targetId": self.target_id, "flatten": True},
                                          timeout=self.driver.session.timeout)["sessionId"]
            self._client.send("Page.enable", session_id=self._sid, timeout=self.driver.session.timeout)
        return self._sid

    def eval(self, expression: str, timeout: float | None = None):
        res = self._client.send("Runtime.evaluate", {"expression": expression, "awaitPromise": True,
                                                     "returnByValue": True, "userGesture": True},
                                session_id=self._session(), timeout=timeout or self.driver.session.timeout)
        if "exceptionDetails" in res:
            det = res["exceptionDetails"]
            raise DriverError(f"evaluate raised: {(det.get('exception') or {}).get('description') or det.get('text')}")
        return res.get("result", {}).get("value")

    def wait(self, expression: str, seconds: float = 15.0):
        deadline = time.monotonic() + seconds
        while True:
            try:
                value = self.eval(expression)
            except (CdpError, DriverError):
                value = None
            if value:
                return value
            if time.monotonic() >= deadline:
                raise DriverError(f"timed out waiting for {expression!r} in the page")
            time.sleep(0.2)

    def wait_loaded(self) -> None:
        self.wait("document.readyState === 'complete'")

    # -- the Tab contract

    def _entry(self) -> dict:
        for entry in self.driver.list():
            if entry.get("targetId") == self.target_id:
                return entry
        raise DriverError(f"workspaceTest.list() does not show tab {self.target_id}")

    @property
    def workspace(self) -> str:
        return self._entry().get("workspace", "")

    @property
    def pid(self) -> int | None:
        return self._entry().get("pid")

    def call(self, fn: str, *args):
        self.wait("typeof wsProbe === 'object'")
        return self.eval(f"wsProbe[{json.dumps(fn)}](...{json.dumps(list(args))})")

    def navigate(self, url: str) -> dict:
        res = self._client.send("Page.navigate", {"url": url}, session_id=self._session(),
                                timeout=self.driver.session.timeout)
        if res.get("errorText"):
            return {"error": res["errorText"]}
        self.wait_loaded()
        return {"error": None}

    def reload(self) -> None:
        self._client.send("Page.reload", session_id=self._session(), timeout=self.driver.session.timeout)
        time.sleep(0.3)
        self.wait_loaded()

    def link_colour(self) -> tuple[int, int, int] | None:
        """Screenshot the link on link.html and read its colour: the page cannot read :visited itself."""
        self.wait("!!document.getElementById('t')")
        rect = self.eval("(() => { const r = document.getElementById('t').getBoundingClientRect();"
                         "return {x: r.x, y: r.y, width: r.width, height: r.height}; })()")
        shot = self._client.send("Page.captureScreenshot", {"format": "png", "clip": {**rect, "scale": 1}},
                                 session_id=self._session(), timeout=self.driver.session.timeout)
        return png_dominant_colour(base64.b64decode(shot["data"]))

    def window_open_noopener(self, path: str) -> "ChromeTab":
        before = self.driver.page_ids()
        self.eval(f"window.open({json.dumps(path)}, '_blank', 'noopener'); 1")
        return self.driver.new_tab(before)

    def close(self) -> None:
        try:
            self._client.send("Target.closeTarget", {"targetId": self.target_id}, timeout=self.driver.session.timeout)
        except CdpError:
            pass  # already closed, for instance by a move


class ChromeDriver:
    def __init__(self, session: Session, site) -> None:
        self.session, self.site = session, site

    def _ws(self, js: str):
        return self.session.evaluate(js, await_promise=True, user_gesture=True)

    def available(self) -> str | None:
        try:
            self.session.navigate_url(WS_TEST_URL)
            ready = self.session.wait_for("typeof workspaceTest === 'object'", timeout=5.0)
        except CdpError as exc:
            return f"{WS_TEST_URL} could not be opened: {exc}"
        if not ready:
            return (f"{WS_TEST_URL} is not served: this build has no Workspaces, or was not started with "
                    "--aerie-test-workspaces")
        return None

    def list(self) -> list[dict]:
        return self._ws("workspaceTest.list()") or []

    def page_ids(self) -> set[str]:
        targets = self.session.client.send("Target.getTargets", timeout=self.session.timeout)["targetInfos"]
        return {t["targetId"] for t in targets if t["type"] == "page"}

    def new_tab(self, before: set[str], seconds: float = 20.0) -> ChromeTab:
        deadline = time.monotonic() + seconds
        while True:
            fresh = self.page_ids() - before
            if fresh:
                tab = ChromeTab(self, sorted(fresh)[0])
                tab.wait_loaded()
                return tab
            if time.monotonic() >= deadline:
                raise DriverError("no new tab appeared")
            time.sleep(0.1)

    def open(self, url: str, workspace: str) -> ChromeTab:
        before = self.page_ids()
        self._ws(f"workspaceTest.open({json.dumps(url)}, {json.dumps(workspace)}, false)")
        return self.new_tab(before)

    def move(self, tab: ChromeTab, workspace: str) -> ChromeTab:
        index = next((e["index"] for e in self.list() if e.get("targetId") == tab.target_id), None)
        if index is None:
            raise DriverError("the tab to move is not in workspaceTest.list()")
        before = self.page_ids()
        self._ws(f"workspaceTest.move({index}, {json.dumps(workspace)})")
        return self.new_tab(before)

    def open_external(self, url: str) -> ChromeTab:
        before = self.page_ids()
        self._ws(f"workspaceTest.openExternal({json.dumps(url)})")
        return self.new_tab(before)

    def activate_workspace(self, workspace: str) -> None:
        self._ws(f"workspaceTest.activateWorkspace({json.dumps(workspace)})")

    def set_permission(self, origin: str, name: str, value: str, workspace: str) -> None:
        self._ws(f"workspaceTest.setPermission({json.dumps(origin)}, {json.dumps(name)}, "
                 f"{json.dumps(value)}, {json.dumps(workspace)})")

    def history(self, workspace: str) -> list[str]:
        return self._ws(f"workspaceTest.history({json.dumps(workspace)})") or []

    def omnibox(self, workspace: str, text: str) -> list[str]:
        return self._ws(f"workspaceTest.omnibox({json.dumps(workspace)}, {json.dumps(text)})") or []
