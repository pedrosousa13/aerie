"""Local fixture server: loopback only, serves one directory, logs every request."""

from __future__ import annotations

import json
import re
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

HOST = "127.0.0.1"
FIXTURE_DIR = Path(__file__).resolve().parent / "fixtures"
_TYPES = {".html": "text/html; charset=utf-8", ".css": "text/css", ".js": "text/javascript",
          ".json": "application/json", ".txt": "text/plain; charset=utf-8", ".png": "image/png"}


class FixtureServer:
    """Binds 127.0.0.1 on an ephemeral port. Never anything else."""

    def __init__(self, root: Path = FIXTURE_DIR) -> None:
        self.root = Path(root).resolve()
        self.requests: list[dict] = []
        self._lock = threading.Lock()
        outer = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):  # silence stderr
                pass

            def do_GET(self):
                path = self.path.split("?", 1)[0].split("#", 1)[0]
                dynamic = outer._dynamic(path, self.path, self.headers)
                body, status, ctype = dynamic[:3] if dynamic else outer._lookup(path)
                extra = dynamic[3] if dynamic else {}
                location = None if dynamic else outer._redirects().get(path)
                if location:
                    body, status, ctype = b"", 302, "text/plain; charset=utf-8"
                with outer._lock:
                    outer.requests.append({
                        "time": time.time(), "method": "GET", "path": self.path,
                        "host": self.headers.get("Host", ""), "status": status,
                        "user_agent": self.headers.get("User-Agent", ""),
                        "cookie": self.headers.get("Cookie", ""),
                        "sec_purpose": self.headers.get("Sec-Purpose", ""),
                        "purpose": self.headers.get("Purpose", ""),
                    })
                try:
                    self.send_response(status)
                    self.send_header("Content-Type", ctype)
                    self.send_header("Content-Length", str(len(body)))
                    self.send_header("Cache-Control", "no-store")
                    for key, value in extra.items():
                        self.send_header(key, value)
                    if status == 302:
                        self.send_header("Location", location)
                    self.end_headers()
                    self.wfile.write(body)
                except (BrokenPipeError, ConnectionResetError):
                    pass  # the browser hung up (e.g. preconnect); the request is already logged

            do_HEAD = do_GET

        self._httpd = ThreadingHTTPServer((HOST, 0), Handler)
        self._httpd.daemon_threads = True
        self._thread = threading.Thread(target=self._httpd.serve_forever, daemon=True)

    @staticmethod
    def _dynamic(path: str, raw_path: str, headers) -> tuple[bytes, int, str, dict] | None:
        """The few responses a static file cannot give: request echo and a Set-Cookie."""
        if path == "/hardening/echo":
            body = json.dumps({"cookie": headers.get("Cookie", ""), "host": headers.get("Host", ""),
                               "origin": headers.get("Origin", ""),
                               "sec_fetch_site": headers.get("Sec-Fetch-Site", "")})
            return body.encode(), 200, "application/json", {}
        if path == "/hardening/set-cookie":
            query = parse_qs(urlsplit(raw_path).query)
            name = re.sub(r"[^A-Za-z0-9_]", "", query.get("name", ["tp"])[0]) or "tp"
            value = re.sub(r"[^A-Za-z0-9_]", "", query.get("value", ["1"])[0]) or "1"
            return b"{}", 200, "application/json", {
                "Set-Cookie": f"{name}={value}; Path=/; SameSite=None; Secure"}
        return None

    def _redirects(self) -> dict:
        """path -> Location from redirects.json in the fixture directory (optional)."""
        try:
            data = json.loads((self.root / "redirects.json").read_text())
        except (OSError, ValueError):
            return {}
        return {k: v for k, v in data.items() if k.startswith("/")}

    def _lookup(self, path: str) -> tuple[bytes, int, str]:
        rel = path.lstrip("/") or "index.html"
        target = (self.root / rel).resolve()
        if target.is_dir():
            target = target / "index.html"
        if (target.is_file() and target.is_relative_to(self.root)):
            return target.read_bytes(), 200, _TYPES.get(target.suffix, "application/octet-stream")
        return b"not found", 404, "text/plain; charset=utf-8"

    @property
    def port(self) -> int:
        return self._httpd.server_address[1]

    @property
    def bind_host(self) -> str:
        return self._httpd.server_address[0]

    @property
    def base_url(self) -> str:
        return f"http://{HOST}:{self.port}"

    def snapshot(self) -> list[dict]:
        with self._lock:
            return list(self.requests)

    def start(self) -> "FixtureServer":
        self._thread.start()
        return self

    def stop(self) -> None:
        self._httpd.shutdown()
        self._httpd.server_close()

    def __enter__(self) -> "FixtureServer":
        return self.start()

    def __exit__(self, *exc) -> None:
        self.stop()


class TcpProbe:
    """A loopback TCP listener that only counts connections (to see preconnects)."""

    def __init__(self) -> None:
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind((HOST, 0))
        self._sock.listen(16)
        self._sock.settimeout(0.2)
        self._count = 0
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._serve, daemon=True)

    def _serve(self) -> None:
        held = []
        while not self._stop.is_set():
            try:
                conn, _ = self._sock.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            with self._lock:
                self._count += 1
            held.append(conn)  # keep it open; the browser may reuse it
        for conn in held:
            conn.close()

    @property
    def port(self) -> int:
        return self._sock.getsockname()[1]

    @property
    def bind_host(self) -> str:
        return self._sock.getsockname()[0]

    @property
    def connections(self) -> int:
        with self._lock:
            return self._count

    def start(self) -> "TcpProbe":
        self._thread.start()
        return self

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2)
        self._sock.close()

    def __enter__(self) -> "TcpProbe":
        return self.start()

    def __exit__(self, *exc) -> None:
        self.stop()


class StunProbe:
    """A loopback STUN server (RFC 5389 binding requests only), so ICE gathering has a server to ask.
    Answers with the sender's address; counts the requests it understood."""

    MAGIC = 0x2112A442

    def __init__(self) -> None:
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.bind((HOST, 0))
        self._sock.settimeout(0.2)
        self._count = 0
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._serve, daemon=True)

    def _serve(self) -> None:
        import struct
        while not self._stop.is_set():
            try:
                data, (addr, port) = self._sock.recvfrom(2048)
            except socket.timeout:
                continue
            except OSError:
                break
            if len(data) < 20 or struct.unpack("!HHI", data[:8])[0] != 0x0001 \
                    or struct.unpack("!I", data[4:8])[0] != self.MAGIC:
                continue
            ip = bytes(int(x) for x in addr.split("."))
            xaddr = bytes(a ^ b for a, b in zip(ip, struct.pack("!I", self.MAGIC)))
            attr = struct.pack("!HHBBH", 0x0020, 8, 0, 1, port ^ (self.MAGIC >> 16)) + xaddr
            with self._lock:
                self._count += 1
            self._sock.sendto(struct.pack("!HHI", 0x0101, len(attr), self.MAGIC) + data[8:20] + attr, (addr, port))

    @property
    def port(self) -> int:
        return self._sock.getsockname()[1]

    @property
    def bind_host(self) -> str:
        return self._sock.getsockname()[0]

    @property
    def requests(self) -> int:
        with self._lock:
            return self._count

    def start(self) -> "StunProbe":
        self._thread.start()
        return self

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2)
        self._sock.close()

    def __enter__(self) -> "StunProbe":
        return self.start()

    def __exit__(self, *exc) -> None:
        self.stop()
