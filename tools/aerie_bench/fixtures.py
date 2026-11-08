"""Local fixture server: loopback only, serves one directory, logs every request."""

from __future__ import annotations

import base64
import hashlib
import json
import re
import shutil
import socket
import ssl
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

HOST = "127.0.0.1"
FIXTURE_DIR = Path(__file__).resolve().parent / "fixtures"
_TYPES = {".html": "text/html; charset=utf-8", ".css": "text/css", ".js": "text/javascript",
          ".json": "application/json", ".txt": "text/plain; charset=utf-8", ".png": "image/png",
          ".mjs": "text/javascript", ".svg": "image/svg+xml", ".wasm": "application/wasm",
          ".jpg": "image/jpeg", ".woff2": "font/woff2"}
# Headers a static page needs. The device memory page opts in to the device memory client hint, so
# its next loads and their subresource requests carry it.
_PAGE_HEADERS = {"/hardening/device-memory.html": {"Accept-CH": "Sec-CH-Device-Memory, Device-Memory"}}


class FixtureServer:
    """Binds 127.0.0.1 on an ephemeral port. Never anything else."""

    def __init__(self, root: Path = FIXTURE_DIR, vendor_root: Path | None = None) -> None:
        self.root = Path(root).resolve()
        # The pinned benchmark copies (aerie_bench.benchmarks), served under /vendor/. None: nothing is.
        self.vendor_root = Path(vendor_root).resolve() if vendor_root else None
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
                extra = dynamic[3] if dynamic else dict(_PAGE_HEADERS.get(path, {}))
                location = None if dynamic else outer._redirects().get(path)
                if location:
                    body, status, ctype = b"", 302, "text/plain; charset=utf-8"
                is_ws = path.startswith("/ws/")
                with outer._lock:
                    outer.requests.append({
                        "time": time.time(), "method": "GET", "path": self.path,
                        "host": self.headers.get("Host", ""), "status": status,
                        "user_agent": self.headers.get("User-Agent", ""),
                        "cookie": self.headers.get("Cookie", ""),
                        "authorization": self.headers.get("Authorization", ""),
                        "sec_purpose": self.headers.get("Sec-Purpose", ""),
                        "purpose": self.headers.get("Purpose", ""),
                        "sec_ch_device_memory": self.headers.get("Sec-CH-Device-Memory", ""),
                        "device_memory": self.headers.get("Device-Memory", ""),
                    })
                if is_ws:
                    # The Workspace test site keeps connections open, so the Bench can see which
                    # socket carried a request. Everything else stays HTTP/1.0, one request per connection.
                    self.protocol_version = "HTTP/1.1"
                    self.close_connection = False
                try:
                    self.send_response(status)
                    self.send_header("Content-Type", ctype)
                    self.send_header("Content-Length", str(len(body)))
                    if "Cache-Control" not in extra:
                        self.send_header("Cache-Control", "no-store")
                    for key, value in extra.items():
                        self.send_header(key, value)
                    if status == 302 and location:
                        self.send_header("Location", location)
                    self.end_headers()
                    if self.command != "HEAD":
                        self.wfile.write(body)
                except (BrokenPipeError, ConnectionResetError):
                    pass  # the browser hung up (e.g. preconnect); the request is already logged

            do_HEAD = do_GET

        self._handler = Handler
        self._httpd = ThreadingHTTPServer((HOST, 0), Handler)
        self._httpd.daemon_threads = True
        self._thread = threading.Thread(target=self._httpd.serve_forever, daemon=True)
        self._tls: _Tls | None = None

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
        if path.startswith("/ws/"):
            return FixtureServer._workspace_site(path, raw_path, headers)
        if path == "/protection/hop":
            # A 302 to ?to=<url>, for redirect-chain checks. The port changes on every run, so a
            # static redirects.json cannot name it. Only loopback fixture hosts are accepted.
            target = parse_qs(urlsplit(raw_path).query).get("to", [""])[0]
            parts = urlsplit(target)
            host = parts.hostname or ""
            if parts.scheme == "http" and (host == HOST or host.endswith(".localhost")):
                return b"", 302, "text/plain; charset=utf-8", {"Location": target}
            return b"bad target", 400, "text/plain; charset=utf-8", {}
        return None

    @staticmethod
    def _workspace_site(path: str, raw_path: str, headers) -> tuple[bytes, int, str, dict] | None:
        """The Workspace isolation site (issue #73): a login, a cacheable resource, HTTP basic
        auth and an HSTS header. Everything it needs to remember, the browser stores."""
        query = parse_qs(urlsplit(raw_path).query)
        text = "text/plain; charset=utf-8"
        if path == "/ws/login":
            # Probe tokens look like "cookie-ws1-3": keep the hyphen, or the probe's read-back control fails.
            user = re.sub(r"[^A-Za-z0-9_-]", "", query.get("user", ["A"])[0]) or "A"
            # A session cookie: no expiry, so it lives in the Workspace until the browser exits.
            return b"{}", 200, "application/json", {"Set-Cookie": f"ws_user={user}; Path=/; SameSite=Lax"}
        if path == "/ws/whoami":
            match = re.search(r"(?:^|;\s*)ws_user=([A-Za-z0-9_-]*)", headers.get("Cookie", ""))
            body = json.dumps({"user": match.group(1) if match else ""})
            return body.encode(), 200, "application/json", {}
        if path == "/ws/cached":
            ident = re.sub(r"[^A-Za-z0-9_-]", "", query.get("id", ["x"])[0]) or "x"
            try:
                delay = min(max(int(query.get("delay_ms", ["0"])[0]), 0), 1000)
            except ValueError:
                delay = 0
            time.sleep(delay / 1000)
            return ident.encode(), 200, text, {"Cache-Control": "max-age=3600"}
        if re.fullmatch(r"/ws/auth/[A-Za-z0-9_]+/login", path):
            expected = "Basic " + base64.b64encode(b"u:p").decode()
            if headers.get("Authorization") == expected:
                return b"ok", 200, text, {}
            return b"login", 401, text, {"WWW-Authenticate": 'Basic realm="ws"'}
        if re.fullmatch(r"/ws/auth/[A-Za-z0-9_]+/probe", path):
            # Never challenges: it only says whether the browser sent credentials on its own.
            body = json.dumps({"sent": bool(headers.get("Authorization"))})
            return body.encode(), 200, "application/json", {}
        if path == "/ws/hsts":
            return b"hsts", 200, text, {"Strict-Transport-Security": "max-age=3600"}
        if path == "/ws/ping":
            return b"pong", 200, text, {}
        return None  # a static file under fixtures/ws/

    def _redirects(self) -> dict:
        """path -> Location from redirects.json in the fixture directory (optional)."""
        try:
            data = json.loads((self.root / "redirects.json").read_text())
        except (OSError, ValueError):
            return {}
        return {k: v for k, v in data.items() if k.startswith("/")}

    def _lookup(self, path: str) -> tuple[bytes, int, str]:
        rel = path.lstrip("/") or "index.html"
        root = self.root
        if rel.startswith("vendor/") and self.vendor_root is not None:
            root, rel = self.vendor_root, rel[len("vendor/"):]
        target = (root / rel).resolve()
        if target.is_dir():
            target = target / "index.html"
        if (target.is_file() and target.is_relative_to(root)):
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

    @property
    def tls_port(self) -> int:
        if self._tls is None:
            raise RuntimeError("enable_tls() was not called")
        return self._tls.httpd.server_address[1]

    @property
    def tls_bind_host(self) -> str:
        if self._tls is None:
            raise RuntimeError("enable_tls() was not called")
        return self._tls.httpd.server_address[0]

    def enable_tls(self) -> str:
        """Start a second, TLS listener on loopback (a self-signed certificate made with the
        ``openssl`` command) and return the base64 SHA-256 of its public key, for the browser flag
        ``--ignore-certificate-errors-spki-list``. Idempotent. Raises RuntimeError without openssl."""
        if self._tls is None:
            self._tls = _Tls(self._handler)
        return self._tls.pin

    def snapshot(self) -> list[dict]:
        with self._lock:
            return list(self.requests)

    def start(self) -> "FixtureServer":
        self._thread.start()
        return self

    def stop(self) -> None:
        self._httpd.shutdown()
        self._httpd.server_close()
        if self._tls is not None:
            self._tls.stop()

    def __enter__(self) -> "FixtureServer":
        return self.start()

    def __exit__(self, *exc) -> None:
        self.stop()


class _TlsHttpd(ThreadingHTTPServer):
    daemon_threads = True

    def get_request(self):
        sock, addr = super().get_request()
        # The handshake runs in the handler thread, on the first read, so a connection that
        # says nothing (a preconnect) cannot block the accept loop.
        return self.context.wrap_socket(sock, server_side=True, do_handshake_on_connect=False), addr


class _Tls:
    """The TLS side of the fixture server: one certificate, one listener, the same handler."""

    def __init__(self, handler) -> None:
        openssl = shutil.which("openssl")
        if not openssl:
            raise RuntimeError("the openssl command is needed for the TLS fixture and was not found")
        self._dir = tempfile.mkdtemp(prefix="aerie-bench-tls-")
        key, crt = f"{self._dir}/key.pem", f"{self._dir}/crt.pem"
        try:
            subprocess.run([openssl, "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1",
                            "-nodes", "-keyout", key, "-out", crt, "-days", "2", "-subj", "/CN=localhost",
                            "-addext", "subjectAltName=DNS:localhost,DNS:*.localhost"],
                           check=True, capture_output=True, timeout=30)
            der = subprocess.run([openssl, "x509", "-in", crt, "-pubkey", "-noout"], check=True,
                                 capture_output=True, timeout=30).stdout
            der = subprocess.run([openssl, "pkey", "-pubin", "-outform", "der"], input=der, check=True,
                                 capture_output=True, timeout=30).stdout
        except (subprocess.SubprocessError, OSError) as exc:
            shutil.rmtree(self._dir, ignore_errors=True)
            raise RuntimeError(f"cannot make the TLS fixture certificate: {exc}") from exc
        self.pin = base64.b64encode(hashlib.sha256(der).digest()).decode()
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(crt, key)
        context.set_alpn_protocols(["http/1.1"])
        self.httpd = _TlsHttpd((HOST, 0), handler)
        self.httpd.context = context
        self._thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self.httpd.shutdown()
        self.httpd.server_close()
        shutil.rmtree(self._dir, ignore_errors=True)


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
