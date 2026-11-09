#!/usr/bin/env python3
"""Bundle the sidebar mockup into one HTML file for a claude.ai Artifact.

Artifacts wrap the page in their own <html>/<head>/<body> and pass no query
string, so this inlines the CSS and JS, drops the document skeleton, and maps
`#<screen-id>` (from screens.json) to that screen's URL parameters.

Usage: python3 build_artifact.py OUT.html
"""
import json
import pathlib
import re
import sys

here = pathlib.Path(__file__).parent
html = (here / "index.html").read_text()
css = (here / "tokens.css").read_text() + "\n" + (here / "sidebar.css").read_text()
js = (here / "sidebar.js").read_text()

# Keep the mockup controls visible in the artifact: drop controls=0.
screens = {
    s["id"]: "&".join(p for p in s["query"].split("&") if p != "controls=0")
    for s in json.loads((here / "screens.json").read_text())
}
params_src = "const params = new URLSearchParams(location.search);"
assert params_src in js, "sidebar.js no longer reads params this way"
js = js.replace(
    params_src,
    "const SCREENS = " + json.dumps(screens) + ";\n"
    "const params = new URLSearchParams(location.search || SCREENS[location.hash.slice(1)] || '');\n"
    "addEventListener('hashchange', () => location.reload());",
)

body = re.search(r"<body>(.*)</body>", html, re.S).group(1)
body = body.replace('<script src="sidebar.js"></script>', "<script>\n" + js + "\n</script>")
title = re.search(r"<title>.*?</title>", html).group(0)

out = f"{title}\n<style>\n{css}\n</style>\n{body}"
pathlib.Path(sys.argv[1]).write_text(out)
print(f"wrote {sys.argv[1]} ({len(out)} bytes, {len(screens)} screens)")
