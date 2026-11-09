"""The UI DevTools client of the Bench: WebSocket framing and finding a view (issue #100)."""

import json
import socket
import struct
import threading
import unittest

from aerie_bench import ui_devtools


def unmask(frame: bytes) -> bytes:
    n = frame[1] & 0x7F
    at = 2
    if n == 126:
        n, at = struct.unpack(">H", frame[2:4])[0], 4
    elif n == 127:
        n, at = struct.unpack(">Q", frame[2:10])[0], 10
    mask, body = frame[at:at + 4], frame[at + 4:at + 4 + n]
    return bytes(b ^ mask[i % 4] for i, b in enumerate(body))


def frame_size(buf: bytes) -> int | None:
    """The size of the masked client frame at the start of ``buf``, once its header is in."""
    if len(buf) < 2:
        return None
    n = buf[1] & 0x7F
    if n == 126:
        return None if len(buf) < 4 else 8 + struct.unpack(">H", buf[2:4])[0]
    if n == 127:
        return None if len(buf) < 10 else 14 + struct.unpack(">Q", buf[2:10])[0]
    return 6 + n


def server_frame(payload: bytes) -> bytes:
    n = len(payload)
    head = bytes([0x81, n]) if n < 126 else bytes([0x81, 126]) + struct.pack(">H", n)
    return head + payload


class FrameTest(unittest.TestCase):
    def test_short_and_long_frames_are_masked_and_round_trip(self):
        for size in (5, 125, 126, 70000):
            body = bytes(range(256)) * (size // 256) + bytes(size % 256)
            frame = ui_devtools.mask_frame(body, b"\x01\x02\x03\x04")
            self.assertEqual(frame[0], 0x81)
            self.assertTrue(frame[1] & 0x80, "client frames are masked")
            self.assertEqual(unmask(frame), body)


class FakeServer:
    """A one-connection UI DevTools stand-in: a tree with two page action views."""

    TREE = {"nodeId": 1, "nodeName": "root", "children": [
        {"nodeId": 2, "attributes": ["class", "PageActionView", "name", "PageActionView"]},
        {"nodeId": 3, "attributes": ["class", "PageActionView", "name", "PageActionView"]},
    ]}
    PROPS = {2: {"Visible": "true", "TooltipText": "Bookmark this tab", "Width": "24", "Height": "24"},
             3: {"Visible": "true", "TooltipText": "Aerie Guard: nothing blocked on this page",
                 "Width": "24", "Height": "20"}}
    CLOSES_ON: frozenset = frozenset()

    def __init__(self) -> None:
        self.sock = socket.socket()
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(1)
        self.port = self.sock.getsockname()[1]
        self.calls: list[dict] = []
        threading.Thread(target=self.serve, daemon=True).start()

    def serve(self) -> None:
        conn, _ = self.sock.accept()
        buf = b""
        while b"\r\n\r\n" not in buf:
            buf += conn.recv(4096)
        conn.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n")
        buf = buf.split(b"\r\n\r\n", 1)[1]
        while True:
            while frame_size(buf) is None or len(buf) < frame_size(buf):
                chunk = conn.recv(65536)
                if not chunk:
                    conn.close()
                    return
                buf += chunk
            n = frame_size(buf)
            msg = json.loads(unmask(buf[:n]))
            buf = buf[n:]
            self.calls.append(msg)
            if msg["method"] == "DOM.getDocument":
                result = {"root": self.TREE}
            elif msg["method"] == "CSS.getMatchedStylesForNode" and msg["params"]["nodeId"] in self.CLOSES_ON:
                conn.close()
                return
            elif msg["method"] == "CSS.getMatchedStylesForNode" and msg["params"]["nodeId"] not in self.PROPS:
                conn.sendall(server_frame(json.dumps(
                    {"id": msg["id"], "error": {"message": "no such node"}}).encode()))
                continue
            elif msg["method"] == "CSS.getMatchedStylesForNode":
                props = [{"name": k, "value": v} for k, v in self.PROPS[msg["params"]["nodeId"]].items()]
                result = {"matchedCSSRules": [{"rule": {"style": {"cssProperties": props}}}]}
            else:
                result = {}
            conn.sendall(server_frame(json.dumps({"method": "DOM.event", "params": {}}).encode()))
            conn.sendall(server_frame(json.dumps({"id": msg["id"], "result": result}).encode()))


class ToolbarServer(FakeServer):
    """An infobar: a text button with a label view inside it, and an unrelated label with the same text.

    Like the real server (train 13 build), it closes the connection when asked for the root's style.
    Node 6 has no style: the server answers that query with an error.
    """

    TREE = {"nodeId": 1, "nodeName": "root", "children": [
        {"nodeId": 6, "attributes": ["class", "Widget"], "children": []},
        {"nodeId": 2, "attributes": ["class", "Label"], "children": []},
        {"nodeId": 3, "attributes": ["class", "MdTextButton"], "children": [
            {"nodeId": 4, "attributes": ["class", "Label"]}]},
        {"nodeId": 5, "attributes": ["class", "MdTextButton"]},
    ]}
    PROPS = {2: {"Visible": "true", "Text": "Turn on Widevine soon", "Width": "80", "Height": "20"},
             3: {"Visible": "true", "Text": "Turn on Widevine", "Width": "120", "Height": "32"},
             4: {"Visible": "true", "Text": "Turn on Widevine", "Width": "100", "Height": "16"},
             5: {"Visible": "false", "Text": "No thanks", "Width": "80", "Height": "32"}}
    CLOSES_ON = frozenset({1})


class FindByTextTest(unittest.TestCase):
    def find(self, text):
        server = ToolbarServer()
        ui = ui_devtools.UiDevTools(server.port, timeout=5)
        try:
            return ui.find_by_text(text)
        finally:
            ui.close()

    def test_the_button_comes_before_the_label_inside_it(self):
        node_id, props = self.find("Turn on Widevine")
        self.assertEqual(node_id, 3)
        self.assertEqual(props["Width"], "120")

    def test_the_text_must_match_whole(self):
        self.assertIsNone(self.find("Turn on"))

    def test_a_node_the_server_has_no_style_for_is_skipped(self):
        # ToolbarServer has no style for node 6: the server answers with an error for it.
        self.assertIsNotNone(self.find("Turn on Widevine"))

    def test_the_root_is_never_asked_for_its_style(self):
        # The real server drops the connection on that query, which made the accept subcase fail.
        server = ToolbarServer()
        ui = ui_devtools.UiDevTools(server.port, timeout=5)
        try:
            self.assertIsNotNone(ui.find_by_text("Turn on Widevine"))
        finally:
            ui.close()
        styled = [c["params"]["nodeId"] for c in server.calls if c["method"] == "CSS.getMatchedStylesForNode"]
        self.assertNotIn(1, styled)

    def test_a_hidden_view_is_not_found(self):
        self.assertIsNone(self.find("No thanks"))
class TwoWindowsServer(FakeServer):
    """Two browser windows, each with a Guard icon (a regular one and an incognito one)."""

    TREE = {"nodeId": 1, "nodeName": "root", "children": [
        {"nodeId": 2, "attributes": ["class", "PageActionView", "name", "PageActionView"]},
        {"nodeId": 3, "attributes": ["class", "PageActionView", "name", "PageActionView"]},
        {"nodeId": 4, "attributes": ["class", "PageActionView", "name", "PageActionView"]},
    ]}
    PROPS = {2: {"Visible": "true", "TooltipText": "Aerie Guard: 3 blocked", "Width": "24", "Height": "24"},
             3: {"Visible": "false", "TooltipText": "Aerie Guard: hidden", "Width": "24", "Height": "24"},
             4: {"Visible": "true", "TooltipText": "Aerie Guard: nothing blocked", "Width": "24", "Height": "24"}}


class ClientTest(unittest.TestCase):
    def test_find_views_lists_every_visible_match_in_window_order(self):
        server = TwoWindowsServer()
        ui = ui_devtools.UiDevTools(server.port, timeout=5)
        try:
            self.assertEqual([n for n, _ in ui.find_views("PageActionView", "Aerie Guard")], [2, 4])
            self.assertEqual(ui.find_view("PageActionView", "Aerie Guard")[0], 2)
        finally:
            ui.close()


    def test_finds_the_view_by_tooltip_and_presses_its_middle(self):
        server = FakeServer()
        ui = ui_devtools.UiDevTools(server.port, timeout=5)
        try:
            found = ui.find_view("PageActionView", "Aerie Guard")
            self.assertIsNotNone(found)
            node_id, props = found
            self.assertEqual(node_id, 3)
            ui.press(node_id, props)
        finally:
            ui.close()
        presses = [c["params"] for c in server.calls if c["method"] == "DOM.dispatchMouseEvent"]
        self.assertEqual([p["event"]["type"] for p in presses], ["mousePressed", "mouseReleased"])
        self.assertEqual({(p["nodeId"], p["event"]["x"], p["event"]["y"]) for p in presses}, {(3, 12, 10)})

    def test_no_port_file_is_an_error(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(ui_devtools.UiDevToolsError):
                ui_devtools.connect(d, timeout=0.3)


if __name__ == "__main__":
    unittest.main()
