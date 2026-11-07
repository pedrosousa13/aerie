"""Local fixture server: loopback only, serves one directory, logs every request."""

from __future__ import annotations

import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

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
                body, status, ctype = outer._lookup(path)
                with outer._lock:
                    outer.requests.append({
                        "time": time.time(), "method": "GET", "path": self.path,
                        "host": self.headers.get("Host", ""), "status": status,
                        "user_agent": self.headers.get("User-Agent", ""),
                    })
                try:
                    self.send_response(status)
                    self.send_header("Content-Type", ctype)
                    self.send_header("Content-Length", str(len(body)))
                    self.send_header("Cache-Control", "no-store")
                    self.end_headers()
                    self.wfile.write(body)
                except (BrokenPipeError, ConnectionResetError):
                    pass  # the browser hung up (e.g. preconnect); the request is already logged

            do_HEAD = do_GET

        self._httpd = ThreadingHTTPServer((HOST, 0), Handler)
        self._httpd.daemon_threads = True
        self._thread = threading.Thread(target=self._httpd.serve_forever, daemon=True)

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
