#!/usr/bin/env python3
"""A tiny stand-in for a Chromium binary, used by the Bench unit tests.

It speaks the --remote-debugging-pipe protocol on fd 3 (commands in) and
fd 4 (replies out), NUL-delimited JSON. Page navigation really fetches the
URL over HTTP so the fixture server sees it, and a NetLog file is written on
exit. Behaviour switches come from FAKE_BROWSER_* environment variables.
"""
import json
import os
import sys
import urllib.request

args = sys.argv[1:]
if "--version" in args:
    print("Aerie 1.2.3.4" if os.path.basename(sys.argv[0]).startswith("fake-aerie") else "Fake Chromium 1.2.3.4")
    sys.exit(0)


def flag(name):
    for a in args:
        if a.startswith(f"--{name}="):
            return a.split("=", 1)[1]
    return None


profile = flag("user-data-dir")
netlog_path = flag("log-net-log")
if os.environ.get("FAKE_BROWSER_DUMP_ARGS"):
    with open(os.environ["FAKE_BROWSER_DUMP_ARGS"], "w") as f:
        json.dump(args, f)
if profile:
    os.makedirs(os.path.join(profile, "Default"), exist_ok=True)
    open(os.path.join(profile, "Default", "touched"), "w").close()

if profile and os.environ.get("FAKE_BROWSER_DUMP_PREFS"):
    try:
        with open(os.environ["FAKE_BROWSER_DUMP_PREFS"], "w") as f:
            f.write(open(os.path.join(profile, "Default", "Preferences")).read())
    except OSError:
        pass
if profile and os.environ.get("FAKE_BROWSER_COPY_PREFS"):
    # Record the seeded Preferences of every launch (main and variants), one per line.
    try:
        with open(os.path.join(profile, "Default", "Preferences")) as f, \
                open(os.environ["FAKE_BROWSER_COPY_PREFS"], "a") as out:
            out.write(f.read() + "\n")
    except OSError:
        with open(os.environ["FAKE_BROWSER_COPY_PREFS"], "a") as out:
            out.write("none\n")

if profile and os.environ.get("FAKE_BROWSER_SERVICES"):
    # Mimic Helium: call the seeded services origin on startup.
    try:
        prefs = json.load(open(os.path.join(profile, "Default", "Preferences")))
        origin = prefs["helium"]["services"]["origin_override"]
        urllib.request.urlopen(origin + "/bangs.json", timeout=5)
    except Exception:
        pass

rd, wr = os.fdopen(3, "rb", buffering=0), os.fdopen(4, "wb", buffering=0)
buf = b""
titles = {}
requests = []
if os.environ.get("FAKE_BROWSER_EXTERNAL"):
    requests.append("https://update.example.invalid/check")
if os.environ.get("FAKE_BROWSER_EXTERNAL_URL"):
    requests.append(os.environ["FAKE_BROWSER_EXTERNAL_URL"])
if os.path.basename(sys.argv[0]).startswith("fake-google"):
    requests.append("https://www.google.com/")
current = "about:blank"
fake_base = ""


def reply(obj):
    wr.write(json.dumps(obj).encode() + b"\0")


def write_netlog():
    if not netlog_path:
        return
    consts = {"logEventTypes": {"URL_REQUEST_START_JOB": 1}, "logSourceType": {"URL_REQUEST": 1}}
    events = [
        {"phase": 1, "source": {"id": i, "type": 1}, "time": str(i), "type": 1,
         "params": {"url": u, "method": "GET"}}
        for i, u in enumerate(requests)
    ]
    with open(netlog_path, "w") as f:
        f.write('{"constants":' + json.dumps(consts) + ',"events":[\n')
        f.write(",\n".join(json.dumps(e) for e in events))
        if not os.environ.get("FAKE_BROWSER_TRUNCATE_NETLOG"):
            f.write("]}")


def emit_network(body, base):
    """FAKE_BROWSER_BLOCKING=block|none: act like a browser with or without an ad blocker.

    Reads the resource URLs out of the page, serves loopback ones (the fixture
    server sees them), and fails the external ones with the error a blocker
    (block) or the Bench's resolver rules (none) would give. Emits DevTools
    Network events on session S1, and records unblocked requests for the NetLog.
    """
    import re
    mode = os.environ.get("FAKE_BROWSER_BLOCKING")
    if os.environ.get("FAKE_BROWSER_MV2") in ("works", "with-ubo"):
        # An extension that cancels /mv2/ext-block-me.js; everything else loads.
        for n, url in enumerate(re.findall(r'<script src="([^"]+)"', body)):
            full = base + url
            rid = f"9.{n}"
            reply({"method": "Network.requestWillBeSent", "sessionId": "S1",
                   "params": {"requestId": rid, "request": {"url": full, "method": "GET"}, "type": "Script"}})
            if "ext-block-me" in url:
                reply({"method": "Network.loadingFailed", "sessionId": "S1",
                       "params": {"requestId": rid, "type": "Script", "errorText": "net::ERR_BLOCKED_BY_CLIENT", "canceled": False}})
            else:
                urllib.request.urlopen(full, timeout=5).read()
                requests.append(full)
                reply({"method": "Network.responseReceived", "sessionId": "S1",
                       "params": {"requestId": rid, "type": "Script", "response": {"url": full, "status": 200}}})
        return
    if not mode:
        return
    found = []
    for tag, attr, url in re.findall(r'<(script|img|iframe|link)[^>]*?(src|href)="([^"]+)"', body):
        found.append(({"script": "Script", "img": "Image", "iframe": "Document", "link": "Stylesheet"}[tag], url))
    for url in re.findall(r'fetch\("([^"]+)"', body):
        found.append(("Fetch", url))
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, *a, **k):
            return None

    def fetch_no_follow(full):
        """(status, Location) without following; the fixture server logs the request."""
        try:
            urllib.request.build_opener(NoRedirect).open(full, timeout=5).read()
            return 200, None
        except urllib.error.HTTPError as err:
            return err.code, err.headers.get("Location")
        except Exception:
            return 0, None

    for n, (kind, url) in enumerate(found):
        rid = f"7.{n}"
        full = url if url.startswith("http") else base + url
        if full.startswith(base) and "/blocking/go-" in full:
            # A first-party URL that redirects (fixtures/redirects.json).
            reply({"method": "Network.requestWillBeSent", "sessionId": "S1",
                   "params": {"requestId": rid, "request": {"url": full, "method": "GET"}, "type": kind}})
            status, location = fetch_no_follow(full)
            requests.append(full)
            target = location if location and location.startswith("http") else base + (location or "")
            reply({"method": "Network.requestWillBeSent", "sessionId": "S1",
                   "params": {"requestId": rid, "request": {"url": target, "method": "GET"}, "type": kind,
                              "redirectResponse": {"url": full, "status": status}}})
            if target.startswith(base):
                status2, _ = fetch_no_follow(target)
                requests.append(target)
                reply({"method": "Network.responseReceived", "sessionId": "S1",
                       "params": {"requestId": rid, "type": kind, "response": {"url": target, "status": status2}}})
            elif mode == "block":
                reply({"method": "Network.loadingFailed", "sessionId": "S1",
                       "params": {"requestId": rid, "type": kind, "errorText": "net::ERR_BLOCKED_BY_CLIENT", "canceled": False}})
            else:
                requests.append(target)
                reply({"method": "Network.loadingFailed", "sessionId": "S1",
                       "params": {"requestId": rid, "type": kind, "errorText": "net::ERR_NAME_NOT_RESOLVED", "canceled": False}})
            continue
        reply({"method": "Network.requestWillBeSent", "sessionId": "S1",
               "params": {"requestId": rid, "request": {"url": full, "method": "GET"}, "type": kind}})
        if full.startswith(base):
            try:
                urllib.request.urlopen(full, timeout=5).read()
                requests.append(full)
                status = 200
            except Exception:
                status = 404
            reply({"method": "Network.responseReceived", "sessionId": "S1",
                   "params": {"requestId": rid, "type": kind, "response": {"url": full, "status": status}}})
        elif mode == "block":
            reply({"method": "Network.loadingFailed", "sessionId": "S1",
                   "params": {"requestId": rid, "type": kind, "errorText": "net::ERR_BLOCKED_BY_CLIENT", "canceled": False}})
        else:
            requests.append(full)
            reply({"method": "Network.loadingFailed", "sessionId": "S1",
                   "params": {"requestId": rid, "type": kind, "errorText": "net::ERR_NAME_NOT_RESOLVED", "canceled": False}})


def handle(msg):
    global current, fake_base
    m, p, i = msg["method"], msg.get("params", {}), msg["id"]
    sid = msg.get("sessionId")
    out = {"id": i, "result": {}}
    if sid:
        out["sessionId"] = sid
    if m == "Target.createTarget":
        out["result"] = {"targetId": "T1"}
    elif m == "Target.attachToTarget":
        out["result"] = {"sessionId": "S1"}
    elif m == "Page.navigate":
        url = p["url"]
        try:
            body = urllib.request.urlopen(url, timeout=5).read().decode()
            requests.append(url)
            import re
            t = re.search(r"<title>(.*?)</title>", body)
            titles[url] = t.group(1) if t else ""
        except Exception as exc:
            if os.environ.get("FAKE_BROWSER_NAV_ERRORTEXT"):
                out["result"] = {"errorText": "net::ERR_FAKE", "frameId": "F1"}
            else:
                out = {"id": i, "error": {"code": -32000, "message": str(exc)}}
        current = url
        from urllib.parse import urlsplit as _us
        fake_base = "%s://%s" % (_us(url).scheme, _us(url).netloc)
        reply(out)
        if "error" not in out:
            from urllib.parse import urlsplit
            emit_network(body, "%s://%s" % (urlsplit(url).scheme, urlsplit(url).netloc))
            reply({"method": "Page.loadEventFired", "params": {"timestamp": 1}, "sessionId": "S1"})
        return
    elif m == "Extensions.loadUnpacked":
        mode = os.environ.get("FAKE_BROWSER_MV2")
        if mode == "refuse":
            out = {"id": i, "error": {"code": -32000, "message": "Manifest version 2 is not supported"}}
        elif mode:
            out["result"] = {"id": "a" * 32}
    elif m == "Target.getTargets":
        mode = os.environ.get("FAKE_BROWSER_MV2")
        infos = [{"type": "page", "url": current, "targetId": "P1"}]
        if mode in ("works", "with-ubo"):
            infos.append({"type": "background_page", "url": "chrome-extension://" + "a" * 32 + "/_generated_background_page.html", "targetId": "B1"})
        if mode == "with-ubo":
            infos.append({"type": "service_worker", "url": "chrome-extension://blockjmkbacgjkknlgpkjjiijinjdanf/js/background.js", "targetId": "U1"})
        out["result"] = {"targetInfos": infos}
    elif m == "Runtime.evaluate":
        expr = p["expression"]
        val = titles.get(current, "") if expr == "document.title" else expr
        if "aerieCancelled" in expr:
            # What the MV2 fixture extension's background page recorded.
            val = json.dumps([f"{fake_base}/mv2/ext-block-me.js"]) \
                if os.environ.get("FAKE_BROWSER_MV2") in ("works", "with-ubo") else "[]"
        elif "data-aerie-mv2" in expr:
            val = "ran" if os.environ.get("FAKE_BROWSER_MV2") in ("works", "with-ubo") else None
        elif "mv2AllowedRan" in expr:
            val = os.environ.get("FAKE_BROWSER_MV2") in ("works", "with-ubo")
        if p.get("awaitPromise"):
            val = "awaited:" + str(val)
        if p.get("userGesture"):
            val = "gesture:" + str(val)
        out["result"] = {"result": {"type": "string", "value": val}}
    elif m == "Target.getTargets":
        out["result"] = {"targetInfos": [{"targetId": "T1", "type": "page", "url": current}]}
    elif m == "Page.captureScreenshot":
        import base64
        out["result"] = {"data": base64.b64encode(b"\x89PNG\r\n\x1a\nFAKE").decode()}
    elif m == "Browser.getHistogram" and os.environ.get("FAKE_BROWSER_BLOCKING") == "block" \
            and os.environ.get("FAKE_BROWSER_ENGINE") != "none":
        # An Aerie build: the engine loaded, and judged (and blocked) the requests.
        buckets = {"Aerie.Adblock.EngineLoadSuccess": [{"low": 1, "high": 2, "count": 1}],
                   "Aerie.Adblock.RedirectHopBlocked": [{"low": 0, "high": 1, "count": 1}, {"low": 1, "high": 2, "count": 1}],
                   "Aerie.Adblock.RequestBlocked": [{"low": 0, "high": 1, "count": 3}, {"low": 1, "high": 2, "count": 8}]}
        name = p["name"]
        if name in buckets:
            n = sum(b["count"] for b in buckets[name])
            out["result"] = {"histogram": {"name": name, "sum": 0, "count": n, "buckets": buckets[name]}}
        else:
            out = {"id": i, "error": {"code": -32000, "message": "Histogram not found"}}
    elif m == "Browser.getHistogram":
        out = {"id": i, "error": {"code": -32000, "message": "Histogram not found"}}
    elif m == "Browser.close":
        reply(out)
        write_netlog()
        sys.exit(int(os.environ.get("FAKE_BROWSER_EXIT", "0")))
    elif m == "Bad.method":
        out = {"id": i, "error": {"code": -32601, "message": "not found"}}
    reply(out)


while True:
    chunk = rd.read(4096)
    if not chunk:
        write_netlog()
        sys.exit(0)
    buf += chunk
    while b"\0" in buf:
        raw, buf = buf.split(b"\0", 1)
        if raw:
            handle(json.loads(raw))
