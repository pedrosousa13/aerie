"""UI DevTools: drive the browser's own views (the toolbar, the address bar) from a check.

CDP reaches web pages only. A browser started with ``--enable-ui-devtools=0`` also serves the views
tree on a localhost WebSocket (``ws://127.0.0.1:<port>/0``) and writes the port to
``<profile>/UIDevToolsActivePort``. Its DOM domain lists each view as a node (class name only), its
CSS domain gives the view's properties (``Visible``, ``TooltipText``, ``Width``...), and
``DOM.dispatchMouseEvent`` sends a mouse event straight to one view. That is how a check presses an
address-bar icon in a headless browser, where there is no window to click.

Only for checks that start their own browser (``session.variant(extra_flags=FLAGS)``): the flag opens a listening
socket, so the main Bench browser never gets it.
"""

from __future__ import annotations

import base64
import json
import os
import socket
import struct
import time
from pathlib import Path
from typing import Iterator

# The server, on a free port, and synthetic events (DOM.dispatchMouseEvent is refused without the feature).
FLAGS = ("--enable-ui-devtools=0", "--enable-features=ui-debug-tools-enable-synthetic-events")
PORT_FILE = "UIDevToolsActivePort"


class UiDevToolsError(Exception):
    """The server is not there, closed the connection, or answered with an error."""


def mask_frame(payload: bytes, mask: bytes) -> bytes:
    """A client text frame (RFC 6455: FIN, opcode 1, masked)."""
    n = len(payload)
    if n < 126:
        head = bytes([0x81, 0x80 | n])
    elif n < 65536:
        head = bytes([0x81, 0x80 | 126]) + struct.pack(">H", n)
    else:
        head = bytes([0x81, 0x80 | 127]) + struct.pack(">Q", n)
    return head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload))


class UiDevTools:
    """One connection. ``send`` is request/reply by id; events in between are dropped."""

    def __init__(self, port: int, timeout: float = 15.0) -> None:
        try:
            self._sock = socket.create_connection(("127.0.0.1", port), timeout=timeout)
        except OSError as exc:
            raise UiDevToolsError(f"cannot connect to UI DevTools on port {port}: {exc}") from exc
        key = base64.b64encode(os.urandom(16)).decode()
        self._sock.sendall((f"GET /0 HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\n"
                            f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
                            "Sec-WebSocket-Version: 13\r\n\r\n").encode())
        self._buf = b""
        while b"\r\n\r\n" not in self._buf:
            self._fill()
        head, self._buf = self._buf.split(b"\r\n\r\n", 1)
        status = head.split(b"\r\n", 1)[0]
        if b" 101 " not in status + b" ":
            raise UiDevToolsError(f"UI DevTools refused the WebSocket: {status.decode(errors='replace')}")
        self._next_id = 0

    def _fill(self) -> None:
        try:
            chunk = self._sock.recv(65536)
        except OSError as exc:
            raise UiDevToolsError(f"UI DevTools connection failed: {exc}") from exc
        if not chunk:
            raise UiDevToolsError("UI DevTools closed the connection")
        self._buf += chunk

    def _take(self, n: int) -> bytes:
        while len(self._buf) < n:
            self._fill()
        data, self._buf = self._buf[:n], self._buf[n:]
        return data

    def _message(self) -> bytes:
        data = b""
        while True:
            b0, b1 = self._take(2)
            n = b1 & 0x7F
            if n == 126:
                n = struct.unpack(">H", self._take(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", self._take(8))[0]
            payload = self._take(n)
            if b0 & 0x0F == 8:
                raise UiDevToolsError("UI DevTools closed the connection")
            data += payload
            if b0 & 0x80:
                return data

    def send(self, method: str, params: dict | None = None) -> dict:
        self._next_id += 1
        mid = self._next_id
        body = json.dumps({"id": mid, "method": method, "params": params or {}}).encode()
        try:
            self._sock.sendall(mask_frame(body, os.urandom(4)))
        except OSError as exc:
            raise UiDevToolsError(f"UI DevTools connection failed: {exc}") from exc
        while True:
            reply = json.loads(self._message())
            if reply.get("id") == mid:
                if "error" in reply:
                    raise UiDevToolsError(f"{method}: {reply['error']}")
                return reply.get("result", {})

    def close(self) -> None:
        try:
            self._sock.close()
        except OSError:
            pass

    # Views.

    def nodes(self) -> Iterator[tuple[int, str]]:
        """(nodeId, class name) of every view, widget and window, depth first."""
        root = self.send("DOM.getDocument")["root"]
        stack = [root]
        while stack:
            node = stack.pop()
            attrs = node.get("attributes") or []
            cls = attrs[1] if len(attrs) > 1 and attrs[0] == "class" else node.get("nodeName", "")
            yield node["nodeId"], cls
            stack.extend(reversed(node.get("children") or []))

    def properties(self, node_id: int) -> dict[str, str]:
        """A view's properties (its metadata), the most specific class first."""
        styles = self.send("CSS.getMatchedStylesForNode", {"nodeId": node_id})
        props: dict[str, str] = {}
        for rule in styles.get("matchedCSSRules") or []:
            for prop in rule["rule"]["style"].get("cssProperties", []):
                props.setdefault(prop["name"], prop["value"])
        return props

    def find_view(self, cls: str, tooltip_prefix: str) -> tuple[int, dict] | None:
        """The first visible view of class ``cls`` whose tooltip starts with ``tooltip_prefix``."""
        for node_id, name in self.nodes():
            if name != cls:
                continue
            props = self.properties(node_id)
            if props.get("Visible") == "true" and props.get("TooltipText", "").startswith(tooltip_prefix):
                return node_id, props
        return None

    def press(self, node_id: int, props: dict) -> None:
        """A left click in the middle of the view: press, then release."""
        x, y = int(props.get("Width", "2")) // 2, int(props.get("Height", "2")) // 2
        for kind in ("mousePressed", "mouseReleased"):
            self.send("DOM.dispatchMouseEvent", {"nodeId": node_id, "event": {
                "type": kind, "x": x, "y": y, "button": "left", "wheelDirection": "none"}})


def connect(profile_dir: Path | str, timeout: float = 15.0) -> UiDevTools:
    """Wait for the browser to write its UI DevTools port into the profile, then connect."""
    path = Path(profile_dir) / PORT_FILE
    deadline = time.monotonic() + timeout
    while True:
        try:
            text = path.read_text().strip()
            if text:
                return UiDevTools(int(text), timeout=timeout)
        except (OSError, ValueError):
            pass
        if time.monotonic() >= deadline:
            raise UiDevToolsError(f"no {PORT_FILE} in the profile: was the browser started with {FLAGS[0]}?")
        time.sleep(0.25)
