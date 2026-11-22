"""Text scrubbing shared by the privacy report and the browser comparison: no local path, URL query, cookie or
authorization header survives ``scrub_text``."""

from __future__ import annotations

import re

_PATH_RE = re.compile(r"(?<![\w.])/(?:home|Users|root|tmp|mnt|var|run|media|opt|private|proc)(?:/[^\s\"'<>,;)]*)?")
_URL_QUERY_RE = re.compile(r"([a-z][a-z0-9+.-]*://[^\s\"'<>?#]*)[?#][^\s\"'<>]*", re.I)
# Any absolute path in free text: "/" then a segment then "/" and more. Not after a word character, ".", ":",
# "/" or "<", so "text/html", "./aerie" and "https://host/a/b" are left alone.
_ANY_PATH_RE = re.compile(r"(?<![\w.:/<])/[\w.~%+@-]+(?:/[\w.~%+@-]*)+")
_LEAK_RE = re.compile(r"/(?:home|Users|root|tmp|mnt|var|run|media|private)/|[?&][\w.%-]+=|[Cc]ookie:|Set-Cookie")
_HEADER_RE = re.compile(r"\b(?:set-cookie|cookie|authorization)\s*[:=]\s*[^\n;]*", re.I)


def scrub_text(text: object) -> str:
    """Free text from a check, made safe: no URL query or fragment, no cookie or auth header, no local path."""
    s = str(text)
    s = _HEADER_RE.sub("<redacted header>", s)
    s = _URL_QUERY_RE.sub(r"\1", s)
    return _ANY_PATH_RE.sub("<path>", _PATH_RE.sub("<path>", s))
