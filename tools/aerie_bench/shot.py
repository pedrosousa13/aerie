"""Screenshot one URL with a built browser: the supported way to run an ad hoc CDP script.

    python3 tools/aerie_bench/shot.py --binary BROWSER URL --out FILE.png

It is also the template for a custom script: `launcher.spawn(build_argv(...), env=browser_env(...))`
gives a CdpClient, and `BrowserProcess.shutdown()` stops the browser. See docs/bench.md.
"""

from __future__ import annotations

import argparse
import base64
import os
import sys
import tempfile
from pathlib import Path

if __package__ in (None, ""):  # run as a file: make `aerie_bench` importable
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    __package__ = "aerie_bench"

from . import launcher
from .runner import browser_env


def screenshot(binary: str | Path, url: str, out: Path, *, headless: bool = True, no_sandbox: bool = False,
               timeout: float = 30.0, extra: tuple[str, ...] = ()) -> Path:
    """Load `url` in a throwaway profile and write a PNG of it to `out`."""
    with tempfile.TemporaryDirectory(prefix="aerie-shot-") as tmp:
        profile = Path(tmp) / "profile"
        profile.mkdir()
        argv = launcher.build_argv(binary, profile, Path(tmp) / "netlog.json", headless=headless,
                                   no_sandbox=no_sandbox, extra=extra)
        proc = launcher.spawn(argv, env=browser_env(os.environ, profile), stderr_path=Path(tmp) / "stderr.log")
        try:
            client = proc.client
            target = client.send("Target.createTarget", {"url": "about:blank"}, timeout=timeout)
            sid = client.send("Target.attachToTarget", {"targetId": target["targetId"], "flatten": True},
                              timeout=timeout)["sessionId"]
            client.send("Page.enable", session_id=sid, timeout=timeout)
            client.drain("Page.loadEventFired")
            res = client.send("Page.navigate", {"url": url}, session_id=sid, timeout=timeout)
            if res.get("errorText"):
                raise RuntimeError(f"navigate {url}: {res['errorText']}")
            client.wait_event("Page.loadEventFired", session_id=sid, timeout=timeout)
            data = client.send("Page.captureScreenshot", {"format": "png"}, session_id=sid, timeout=timeout)["data"]
            out = Path(out)
            out.write_bytes(base64.b64decode(data))
        finally:
            proc.shutdown(timeout=10)
    return out


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="shot.py", description="Screenshot a URL with a built browser.")
    p.add_argument("url")
    p.add_argument("--binary", required=True, help="browser binary (./aerie info prints it)")
    p.add_argument("--out", required=True, type=Path, help="PNG file to write")
    p.add_argument("--headed", action="store_true", help="show the window (needs a display)")
    p.add_argument("--no-sandbox", action="store_true", help="containers only")
    p.add_argument("--timeout", type=float, default=30.0)
    args = p.parse_args(argv)
    try:
        path = screenshot(args.binary, args.url, args.out, headless=not args.headed,
                          no_sandbox=args.no_sandbox, timeout=args.timeout)
    except Exception as exc:  # CdpError, RuntimeError, OSError
        print(f"shot: {exc}", file=sys.stderr)
        return 1
    print(f"{path} ({path.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
