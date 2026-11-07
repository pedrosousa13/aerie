"""What a check sees: a live page over CDP, the fixture server, and (after exit) the NetLog."""

from __future__ import annotations

import base64
import json
from dataclasses import dataclass, field
from pathlib import Path

from .cdp import CdpClient, CdpError
from .fixtures import FixtureServer


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
        self.exit: dict = {}
        self.artifacts: list[str] = []
        self._sid: str | None = None

    def _page(self) -> str:
        if self._sid is None:
            target = self.client.send("Target.createTarget", {"url": "about:blank"}, timeout=self.timeout)
            att = self.client.send("Target.attachToTarget",
                                   {"targetId": target["targetId"], "flatten": True}, timeout=self.timeout)
            self._sid = att["sessionId"]
            self.client.send("Page.enable", session_id=self._sid, timeout=self.timeout)
        return self._sid

    def navigate(self, path: str) -> None:
        """Load a fixture path and wait for the load event."""
        sid = self._page()
        self.client.drain("Page.loadEventFired")
        res = self.client.send("Page.navigate", {"url": self.fixtures.base_url + path},
                               session_id=sid, timeout=self.timeout)
        if res.get("errorText"):
            raise CdpError(f"navigate {path}: {res['errorText']}")
        self.client.wait_event("Page.loadEventFired", session_id=sid, timeout=self.timeout)

    def evaluate(self, expression: str):
        res = self.client.send("Runtime.evaluate",
                               {"expression": expression, "returnByValue": True},
                               session_id=self._page(), timeout=self.timeout)
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
