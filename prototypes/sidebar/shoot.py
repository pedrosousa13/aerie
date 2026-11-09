#!/usr/bin/env python3
"""Screenshot the sidebar design pages.

Reads screens.json (page defaults to index.html) and every screens-*.json (each entry may set "page"
and "fullPage"), serves this folder on a local port and shoots each entry at 1440x900 in light and dark
(appends &mode=light|dark). Light is skipped for entries whose query has theme=oled, wsmode=dark or mode=dark.
Waits for document.body.dataset.ready === '1' when the page declares data-ready, else 300ms after load.
Writes <out>/<id>-<mode>.png. Exits 1 on any console error or page error, listing them.

  python3 shoot.py [--out DIR] [--only id1,id2] [--gallery] [--train NAME]
"""
import argparse, functools, http.server, json, pathlib, re, socketserver, subprocess, sys, tempfile, threading

HERE = pathlib.Path(__file__).resolve().parent
GALLERY = "/mnt/linux2/aerie-build/gallery/gallery.py"
FEATURE = "Sidebar design"
ISSUE = "142"
DARK_ONLY = re.compile(r"(^|&)(theme=oled|wsmode=dark|mode=dark)(&|$)")


def load_entries():
    files = [HERE / "screens.json"] + sorted(HERE.glob("screens-*.json"))
    entries = []
    for f in files:
        if not f.exists():
            continue
        try:
            for e in json.loads(f.read_text()):
                e.setdefault("page", "index.html")
                e["_src"] = f.name
                entries.append(e)
        except Exception as ex:  # another agent may be mid-edit
            print(f"warning: cannot read {f.name}: {ex}", file=sys.stderr)
    return entries


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def serve():
    class Server(socketserver.ThreadingTCPServer):
        allow_reuse_address = True
        daemon_threads = True
    srv = Server(("127.0.0.1", 0), functools.partial(Quiet, directory=str(HERE)))
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv, srv.server_address[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(pathlib.Path(tempfile.gettempdir()) / "aerie-sidebar-shots"))
    ap.add_argument("--only", default="", help="comma-separated entry ids")
    ap.add_argument("--gallery", action="store_true", help="register each shot in the Tailscale gallery")
    ap.add_argument("--train", default="")
    a = ap.parse_args()

    entries = load_entries()
    only = [x for x in a.only.split(",") if x]
    if only:
        missing = [x for x in only if x not in {e["id"] for e in entries}]
        if missing:
            print("unknown ids: " + ", ".join(missing), file=sys.stderr)
            return 2
        entries = [e for e in entries if e["id"] in only]
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)

    from playwright.sync_api import sync_playwright
    srv, port = serve()
    errors, shots = [], []
    with sync_playwright() as p:
        browser = p.chromium.launch()
        for order, e in enumerate(entries, 1):
            q = e.get("query", "")
            modes = ["dark"] if DARK_ONLY.search(q) else ["light", "dark"]
            for mode in modes:
                tag = f"{e['id']}-{mode}"
                ctx = browser.new_context(viewport={"width": 1440, "height": 900}, color_scheme=mode, device_scale_factor=1)
                page = ctx.new_page()
                page.on("console", lambda m, t=tag: errors.append(f"{t}: console.{m.type}: {m.text}") if m.type == "error" else None)
                page.on("pageerror", lambda x, t=tag: errors.append(f"{t}: pageerror: {x}"))
                page.on("requestfailed", lambda r, t=tag: errors.append(f"{t}: request failed: {r.url}"))
                url = f"http://127.0.0.1:{port}/{e['page']}?" + (q + "&" if q else "") + f"mode={mode}"
                try:
                    page.goto(url, wait_until="load")
                    if page.evaluate("document.body && document.body.dataset.ready !== undefined"):
                        page.wait_for_function("document.body.dataset.ready === '1'", timeout=15000)
                        page.wait_for_timeout(100)
                    else:
                        page.wait_for_timeout(300)
                    f = out / f"{tag}.png"
                    page.screenshot(path=str(f), full_page=bool(e.get("fullPage")))
                    shots.append((e, mode, f, order))
                    print(f"shot {f}")
                except Exception as ex:
                    errors.append(f"{tag}: {type(ex).__name__}: {str(ex).splitlines()[0]}")
                ctx.close()
        browser.close()
    srv.shutdown()

    if a.gallery and shots:
        for e, mode, f, order in shots:
            cmd = ["python3", GALLERY, "add", "--feature", FEATURE, "--title", e.get("title", e["id"]), "--theme", mode,
                   "--issue", ISSUE, "--order", str(e.get("order", order)), "--note", e.get("note", ""), "--no-build", str(f)]
            if a.train:
                cmd[cmd.index("--order"):cmd.index("--order")] = ["--train", a.train]
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode:
                errors.append(f"gallery add {f.name}: {r.stderr.strip() or r.stdout.strip()}")
        r = subprocess.run(["python3", GALLERY, "build"], capture_output=True, text=True)
        if r.returncode:
            errors.append(f"gallery build: {r.stderr.strip() or r.stdout.strip()}")
        else:
            print("gallery built")

    if errors:
        print(f"\n{len(errors)} error(s):", file=sys.stderr)
        for x in errors:
            print("  " + x, file=sys.stderr)
        return 1
    print(f"ok: {len(shots)} screenshots in {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
