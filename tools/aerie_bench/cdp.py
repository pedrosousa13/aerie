"""Chrome DevTools Protocol over --remote-debugging-pipe.

The browser reads commands on fd 3 and writes replies and events on fd 4.
Every message is one JSON document followed by a single NUL byte.
"""

from __future__ import annotations

import json
import os
import select
import threading
import time
from collections import deque
from typing import Any, Callable


class CdpError(Exception):
    """The browser answered a command with an error, or did not answer."""


class CdpTimeout(CdpError):
    pass


def encode(message: dict) -> bytes:
    return json.dumps(message, separators=(",", ":")).encode() + b"\0"


class Decoder:
    """Incremental NUL-delimited JSON decoder; tolerates split reads."""

    def __init__(self) -> None:
        self._buf = b""

    def feed(self, data: bytes) -> list[dict]:
        self._buf += data
        out = []
        while b"\0" in self._buf:
            raw, self._buf = self._buf.split(b"\0", 1)
            if raw.strip():
                out.append(json.loads(raw))
        return out


class CdpClient:
    """Id-matched request/response plus an event log, over two fds."""

    def __init__(self, read_fd: int, write_fd: int) -> None:
        self._read_fd = read_fd
        self._write_fd = write_fd
        self._next_id = 0
        self._lock = threading.Condition()
        self._responses: dict[int, dict] = {}
        self._events: deque[dict] = deque()
        self._closed = False
        self._stop = threading.Event()
        self._write_lock = threading.Lock()
        self._thread = threading.Thread(target=self._read_loop, daemon=True)
        self._thread.start()

    def _read_loop(self) -> None:
        decoder = Decoder()
        try:
            while not self._stop.is_set():
                if not select.select([self._read_fd], [], [], 0.1)[0]:
                    continue
                data = os.read(self._read_fd, 65536)
                if not data:
                    break
                for msg in decoder.feed(data):
                    with self._lock:
                        if "id" in msg:
                            self._responses[msg["id"]] = msg
                        else:
                            self._events.append(msg)
                        self._lock.notify_all()
        except OSError:
            pass
        finally:
            with self._lock:
                self._closed = True
                self._lock.notify_all()

    @property
    def closed(self) -> bool:
        return self._closed

    def send(self, method: str, params: dict | None = None, session_id: str | None = None,
             timeout: float = 30.0) -> dict:
        with self._lock:
            self._next_id += 1
            msg_id = self._next_id
        msg: dict[str, Any] = {"id": msg_id, "method": method, "params": params or {}}
        if session_id:
            msg["sessionId"] = session_id
        try:
            with self._write_lock:
                os.write(self._write_fd, encode(msg))
        except OSError as exc:
            raise CdpError(f"{method}: browser pipe is closed ({exc})") from exc
        deadline = time.monotonic() + timeout
        with self._lock:
            while msg_id not in self._responses:
                if self._closed:
                    raise CdpError(f"{method}: browser closed the pipe before replying")
                left = deadline - time.monotonic()
                if left <= 0:
                    raise CdpTimeout(f"{method}: no reply within {timeout}s")
                self._lock.wait(left)
            resp = self._responses.pop(msg_id)
        if "error" in resp:
            err = resp["error"]
            raise CdpError(f"{method}: {err.get('message')} (code {err.get('code')})")
        return resp.get("result", {})

    def wait_event(self, method: str, session_id: str | None = None, timeout: float = 30.0,
                   predicate: Callable[[dict], bool] | None = None) -> dict:
        """Return (and consume) the first matching event, waiting if needed.

        Events that arrived before this call are considered, so there is no
        race between sending a command and waiting for its event.
        """
        deadline = time.monotonic() + timeout
        with self._lock:
            while True:
                for ev in list(self._events):
                    if ev.get("method") != method:
                        continue
                    if session_id and ev.get("sessionId") != session_id:
                        continue
                    if predicate and not predicate(ev):
                        continue
                    self._events.remove(ev)
                    return ev
                if self._closed:
                    raise CdpError(f"{method}: browser closed the pipe while waiting")
                left = deadline - time.monotonic()
                if left <= 0:
                    raise CdpTimeout(f"event {method}: not seen within {timeout}s")
                self._lock.wait(left)

    def take_events(self, method: str, session_id: str | None = None) -> list[dict]:
        """Return (and consume) every queued event of one kind, oldest first."""
        with self._lock:
            taken = [e for e in self._events
                     if e.get("method") == method and (not session_id or e.get("sessionId") == session_id)]
            for ev in taken:
                self._events.remove(ev)
            return taken

    def drain(self, method: str) -> None:
        """Forget queued events of one kind (e.g. a stale Page.loadEventFired)."""
        with self._lock:
            self._events = deque(e for e in self._events if e.get("method") != method)

    def close(self) -> None:
        """Stop the reader first, then close both fds (no fd-reuse race)."""
        self._stop.set()
        self._thread.join(timeout=2.0)
        fds = [self._write_fd] + ([] if self._thread.is_alive() else [self._read_fd])
        for fd in fds:
            try:
                os.close(fd)
            except OSError:
                pass
