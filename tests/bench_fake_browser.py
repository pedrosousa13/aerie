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


def handle(msg):
    global current
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
        reply(out)
        if "error" not in out:
            reply({"method": "Page.loadEventFired", "params": {"timestamp": 1}, "sessionId": "S1"})
        return
    elif m == "Runtime.evaluate":
        expr = p["expression"]
        val = titles.get(current, "") if expr == "document.title" else expr
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
