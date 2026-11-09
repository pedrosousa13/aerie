#!/usr/bin/env python3
"""Bundle the mockup pages into a directory for a multi-file claude.ai Artifact.

Usage: python3 build_artifact.py OUT_DIR

Writes:
  index.html                the sidebar mockup, with no doctype, html, head or body (the Artifact wraps it):
                            title + <style> + body content, CSS and JS inlined
  horizontal.html, workspace-editor.html, motion.html, tokens.html
                            standalone pages with their own doctype, charset and viewport metas, <title>,
                            inlined CSS and JS; tokens.css is inlined too. The spec pages carry tokens.json as
                            <script type="application/json" id="tokens-json"> (specs.js reads it, and falls back
                            to fetch for local use)

Artifacts pass no query string, so every page maps `#<screen-id>` to that screen's URL parameters. The ids come from
screens.json and every screens-*.json, grouped by each entry's "page" (default index.html). The mockup controls
stay visible: controls=0 is dropped from the mapped parameters.
"""
import json
import pathlib
import re
import sys

here = pathlib.Path(__file__).resolve().parent
PAGES = ["index.html", "horizontal.html", "workspace-editor.html", "motion.html", "tokens.html"]
PARAMS_SRC = {   # the line each page's script uses to read its parameters
    "sidebar.js": "const params = new URLSearchParams(location.search);",
    "horizontal.js": "const params = new URLSearchParams(location.search);",
    "workspace-editor.js": "const params = new URLSearchParams(location.search);",
    "specs.js": "const qs = new URLSearchParams(location.search);",
}
SPEC_PAGES = {"motion.html", "tokens.html"}   # their #m-... and #sec-... hashes are anchors, so a hash change must not reload


def screens_by_page():
    by = {p: {} for p in PAGES}
    for f in [here / "screens.json"] + sorted(here.glob("screens-*.json")):
        for e in json.loads(f.read_text()):
            page = e.get("page", "index.html")
            by[page][e["id"]] = "&".join(p for p in e.get("query", "").split("&") if p and p != "controls=0")
    return by


def with_hash_screens(js, script_name, screens, page):
    src = PARAMS_SRC[script_name]
    assert src in js, f"{script_name} no longer reads its parameters as: {src}"
    var = "qs" if script_name == "specs.js" else "params"
    reload_ = "" if page in SPEC_PAGES else "\naddEventListener('hashchange', () => { if (SCREENS[location.hash.slice(1)] !== undefined) location.reload(); });"
    return js.replace(
        src,
        f"const SCREENS = {json.dumps(screens)};\n"
        f"const {var} = new URLSearchParams(location.search || SCREENS[location.hash.slice(1)] || '');" + reload_,
    )


def script_safe(text, what):
    assert "</script" not in text.lower(), f"{what} contains a closing script tag"
    return text


def build_page(page, screens):
    html = (here / page).read_text()
    css_files = [m for m in re.findall(r'<link rel="stylesheet" href="([^"]+)">', html)]
    css = "\n".join((here / f).read_text() for f in css_files)
    js_files = re.findall(r'<script src="([^"]+)"></script>', html)
    assert len(js_files) == 1, f"{page}: expected one script"
    js_name = js_files[0]
    js = with_hash_screens((here / js_name).read_text(), js_name, screens, page)
    script = "<script>\n" + script_safe(js, js_name) + "\n</script>"
    if page in SPEC_PAGES:
        tokens = json.dumps(json.loads((here / "tokens.json").read_text()), separators=(",", ":")).replace("</", "<\\/")
        script = f'<script type="application/json" id="tokens-json">{tokens}</script>\n' + script

    # drop the stylesheet links, then put one <style> where the first one was
    first = True

    def style(_m):
        nonlocal first
        if first:
            first = False
            return "<style>\n" + css + "\n</style>"
        return ""

    out = re.sub(r'<link rel="stylesheet" href="[^"]+">\n?', style, html)
    out = out.replace(f'<script src="{js_name}"></script>', script)
    return html, out, css, script


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out_dir = pathlib.Path(sys.argv[1])
    out_dir.mkdir(parents=True, exist_ok=True)
    by = screens_by_page()
    for page in PAGES:
        html, out, css, script = build_page(page, by[page])
        if page == "index.html":
            # the Artifact supplies doctype, html, head and body: keep title + style + body content only
            title = re.search(r"<title>.*?</title>", html).group(0)
            body = re.search(r"<body[^>]*>(.*)</body>", out, re.S).group(1)
            out = f"{title}\n{body.lstrip()}"
            # the style block sat in <head>: put it back in front of the body content
            out = out.replace(title + "\n", title + "\n<style>\n" + css + "\n</style>\n", 1)
        else:
            assert out.lstrip().lower().startswith("<!doctype html>"), page
            assert 'charset="utf-8"' in out and 'name="viewport"' in out and "<title>" in out, page
        (out_dir / page).write_text(out)
    print(f"wrote {out_dir}")
    for page in PAGES:
        size = (out_dir / page).stat().st_size
        print(f"  {page:24}{size:>9} bytes  {len(by[page]):>3} screens")


if __name__ == "__main__":
    main()
