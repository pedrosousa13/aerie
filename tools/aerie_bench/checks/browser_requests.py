"""Browser-level traffic: requests the browser makes on its own, not the page.

Two independent sources, either one is enough:

* Services probe: the throwaway profile is seeded so Helium's services origin
  is the local fixture server (``helium.services.origin_override``). A Helium
  binary then sends its service calls (``/bangs.json``, ``/ubo/assets.json``,
  ``/com``, ...) to us, and the fixture log proves a browser-initiated request.
* NetLog: any request to a non-loopback host. The fixtures reference nothing
  external, so every such request came from the browser itself; the Bench's
  resolver rules refuse it, the NetLog still records it.
"""

from __future__ import annotations

import time
from pathlib import Path

from .. import netlog
from ..session import CheckResult, Session
from . import register

SERVICE_PATHS = ("/bangs.json", "/ubo/assets.json", "/com", "/ext", "/dict/")
WAIT_SECONDS = 20.0
SKIP = "skip"  # require_services value: the caller asked, explicitly, to skip the probe


def services_prefs(origin: str) -> dict:
    return {"helium": {"services": {"origin_override": origin, "schema_version": 1,
                                    "disable_schema_alerts": True, "enabled": True}}}


def service_hits(requests: list[dict]) -> list[dict]:
    return [r for r in requests if r["path"].split("?", 1)[0].startswith(SERVICE_PATHS)]


def is_helium_family(binary: str, version: str | None) -> bool:
    """Helium or Aerie builds, which are expected to call their services origin."""
    text = f"{Path(binary).name} {version or ''}".lower()
    return "helium" in text or "aerie" in text


@register
class BrowserRequests:
    """``require_services``: True/False, None (auto: True for Helium/Aerie binaries) or "skip".

    When required, the check fails unless the seeded services probe reached the fixture
    server; a refused external request in the NetLog is then not enough.
    ``checks[].data.passed_via`` is "services", "netlog" or None.
    """

    name = "browser_requests"
    seeds_helium_services = True

    def __init__(self, wait_seconds: float = WAIT_SECONDS, require_services: bool | str | None = None,
                 skip_note: str | None = None):
        self.wait_seconds = wait_seconds
        self.require_services = require_services
        self.skip_note = skip_note

    def _required(self, session: Session) -> bool:
        if self.require_services is not None and self.require_services != SKIP:
            return self.require_services
        return is_helium_family(session.binary, session.binary_version)

    def run(self, session: Session) -> CheckResult:
        if self.require_services == SKIP:
            return CheckResult(self.name, "pending")
        required = self._required(session)
        deadline = time.monotonic() + self.wait_seconds
        while time.monotonic() < deadline:
            if service_hits(session.fixtures.snapshot()):
                break
            # Not required: any refused external request is enough, stop waiting.
            if not required and session.netlog_path and netlog.external(netlog.parse_file(session.netlog_path)):
                break
            time.sleep(0.25)
        return CheckResult(self.name, "pending")

    def finalize(self, session: Session, result: CheckResult) -> CheckResult:
        if self.require_services == SKIP:
            return CheckResult(self.name, "skip",
                               "skipped by --services skip: Aerie makes no browser-level requests, so this probe "
                               "has nothing to see (zero_traffic with --baseline proves observability instead)",
                               data={"skipped_by": self.skip_note or "--services skip",
                                     "needs_baseline": "proving that the Bench sees browser-level traffic on Aerie "
                                                       "needs zero_traffic --baseline <helium binary>"})
        required = self._required(session)
        hits = service_hits(session.fixtures.snapshot())
        ext = netlog.external(session.netlog)
        data = {"services_hits": [h["path"] for h in hits],
                "external_hosts": sorted({r["host"] for r in ext}),
                "require_services": required, "passed_via": None}
        if hits:
            data["passed_via"] = "services"
            return CheckResult(self.name, "pass", f"browser called the seeded services origin: {hits[0]['path']}", data=data)
        if required:
            return CheckResult(self.name, "fail",
                               "services probe required but the browser never called the seeded services origin"
                               + (f" ({len(ext)} external request(s) seen in the NetLog)" if ext else ""),
                               data=data)
        if ext:
            data["passed_via"] = "netlog"
            return CheckResult(self.name, "pass",
                               f"NetLog recorded {len(ext)} refused browser-level request(s), e.g. {ext[0]['url']}",
                               data=data)
        return CheckResult(self.name, "fail",
                           "no browser-level request seen (no services call, no external URL in the NetLog)",
                           data=data)
