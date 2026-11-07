"""Branding checks (issue #5): no Helium in what users read, aerie:// works, About shows both versions.

Each check splits like the hardening checks: ``observe(session)`` drives the browser and returns
plain data, ``judge(obs)`` turns that data into a verdict and is unit-tested against recorded
observations (tests/test_bench_branding.py). They run on a built browser only, over DevTools.
"""

from __future__ import annotations

import re
import time
from pathlib import Path

from aerie_build import aerie_version, lock

from ..session import Session
from . import register
from .hardening.common import DEEP_JS, HardeningCheck, Verdict

REPO = Path(__file__).resolve().parents[3]

CONTROL_PATH = "/branding/helium.html"

# Internal pages a user can open, as DevTools reaches them. About, settings (with its section
# pages), flags, and the lists a user sees in the menu.
PAGES = (
    "chrome://version/",
    "chrome://chrome-urls/",
    "chrome://settings/",
    "chrome://settings/help",
    "chrome://settings/privacy",
    "chrome://settings/appearance",
    "chrome://settings/search",
    "chrome://settings/defaultBrowser",
    "chrome://settings/onStartup",
    "chrome://settings/languages",
    "chrome://settings/downloads",
    "chrome://settings/system",
    "chrome://settings/reset",
    "chrome://settings/privacy/services",
    "chrome://flags/",
    "chrome://history/",
    "chrome://downloads/",
    "chrome://extensions/",
    "chrome://bookmarks/",
    "chrome://management/",
    "chrome://components/",
    "chrome://setup/",
)

# Pages whose body text still names Helium's services. Issue #23 removes those settings and the
# onboarding consent. Their titles are still checked, and the hits in their text are reported.
PENDING_SERVICES_PAGES = {
    "chrome://settings/privacy": "the Helium services row",
    "chrome://settings/privacy/services": "the Helium services page",
    "chrome://settings/languages": "the spell-check dictionary download messages",
    "chrome://setup/": "the onboarding page",
}

# chrome://version lists the command line, the executable and the profile path. They hold the build root
# (.../src/helium-<version>/out/Default/aerie), which is a directory name, not branding.
_PATH_LINE_RE = re.compile(r"^(Command Line|Executable Path|Profile Path)\t.*$", re.MULTILINE)

# chrome://flags prints each experiment's internal id (#helium-noise) under its name. The id is a flag
# name that settings and command lines use, not a word the user reads as a name.
_FLAG_ID_RE = re.compile(r"#helium-[a-z0-9-]+")

# Aerie is derived from Helium and Chromium, so the copyright line on chrome://version credits
# "The Helium Authors" by name. That exact string is attribution, not branding.
_CREDIT_RE = re.compile(r"The Helium Authors")

_HELIUM_RE = re.compile(r"(?<![A-Za-z0-9_])helium(?![A-Za-z0-9_])", re.IGNORECASE)

# Text a user can read on the page: the document text, every shadow root, and the attributes that
# are spoken or shown on hover.
TEXT_JS = DEEP_JS + """
(() => {
  const parts = [];
  const walk = (root) => {
    for (const el of root.querySelectorAll('*')) {
      for (const a of ['aria-label', 'title', 'placeholder', 'alt', 'label']) {
        const v = el.getAttribute && el.getAttribute(a);
        if (v) parts.push(v);
      }
      if (el.shadowRoot) {
        parts.push(el.shadowRoot.textContent || '');
        walk(el.shadowRoot);
      }
    }
  };
  parts.push(document.body ? document.body.innerText : '');
  walk(document);
  return {title: document.title, text: parts.join('\\n')};
})()"""


def find_helium(text: str) -> list[str]:
    """Each place the word Helium (any case, a whole word) appears, with a little context."""
    return [text[max(0, m.start() - 20):m.end() + 20].replace("\n", " ") for m in _HELIUM_RE.finditer(text)]


def scrub(text: str, url: str) -> str:
    """Drop what is not branding, on the one page where it belongs: build paths and the credit to
    The Helium Authors on chrome://version, internal flag ids on chrome://flags."""
    if url == "chrome://version/":
        text = _CREDIT_RE.sub("", _PATH_LINE_RE.sub("", text))
    elif url == "chrome://flags/":
        text = _FLAG_ID_RE.sub("", text)
    return text


def read_page(session: Session, url: str) -> dict:
    nav = session.navigate_url(url, wait=20)
    if nav["error"]:
        return {"url": url, "title": "", "text": "", "error": nav["error"]}
    session.wait_for("document.readyState === 'complete'", timeout=10)
    time.sleep(1.0)  # WebUI pages fill in after load
    try:
        got = session.evaluate(TEXT_JS) or {}
    except Exception as exc:  # noqa: BLE001  (a page that cannot be read is data, not a crash)
        return {"url": url, "title": "", "text": "", "error": f"could not read: {exc}"}
    return {"url": url, "title": got.get("title", ""), "text": scrub(got.get("text", ""), url), "error": None}


@register
class NoHelium(HardeningCheck):
    name = "branding_no_helium"
    control = "all"
    negative_control = ("A fixture page with the word Helium inside a shadow root is read by the same "
                        "scanner and must be reported, so the scan can see a hit.")

    def observe(self, session: Session) -> dict:
        control = read_page(session, session.fixtures.base_url + CONTROL_PATH)
        return {"control": control, "pages": [read_page(session, url) for url in PAGES]}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        control = obs["control"]
        if control.get("error") or not find_helium(control.get("text", "")):
            return "fail", ("negative control failed: the scanner did not find Helium on the control page "
                            f"({control.get('error') or 'text had no hit'})")
        if not obs["pages"]:
            return "fail", "no pages were scanned"
        problems: list[str] = []
        pending: list[str] = []
        for p in obs["pages"]:
            if p.get("error"):
                problems.append(f"{p['url']} did not load ({p['error']})")
                continue
            if find_helium(p["title"]):
                problems.append(f"{p['url']} title says Helium: {p['title']!r}")
            hits = find_helium(p["text"])
            if not hits:
                continue
            if p["url"] in PENDING_SERVICES_PAGES:
                pending.append(f"{p['url']} ({len(hits)})")
            else:
                problems.append(f"{p['url']} says Helium: ...{hits[0]}... ({len(hits)} hit(s))")
        if problems:
            return "fail", "; ".join(problems)
        detail = f"no Helium in the titles and text of {len(obs['pages'])} pages"
        if pending:
            detail += ("; text on these pages still names Helium services until issue #23 removes them: "
                       + ", ".join(pending))
        return "pass", detail


def open_target(session: Session, url: str, settle: float) -> dict:
    """Open a tab the way the browser UI does (it goes through the browser's navigator, which is
    where aerie:// is mapped), wait, and report where it ended up."""
    created = session.client.send("Target.createTarget", {"url": url}, timeout=session.timeout)
    target_id = created["targetId"]
    deadline = time.monotonic() + settle
    info: dict = {}
    while True:
        info = session.client.send("Target.getTargetInfo", {"targetId": target_id},
                                   timeout=session.timeout).get("targetInfo", {})
        if info.get("url") not in ("", "about:blank") and info.get("title") and info.get("title") != info.get("url"):
            break
        if time.monotonic() >= deadline:
            break
        time.sleep(0.25)
    try:
        session.client.send("Target.closeTarget", {"targetId": target_id}, timeout=session.timeout)
    except Exception:  # noqa: BLE001
        pass
    return {"url": info.get("url", ""), "title": info.get("title", "")}


def loads_version_page(seen: dict) -> bool:
    return seen["url"].startswith(("chrome://version", "aerie://version")) and bool(seen["title"])


@register
class Scheme(HardeningCheck):
    name = "branding_scheme"
    control = "all"
    negative_control = ("A tab opened on a made-up scheme (zzz://version) must not load the version page "
                        "either, so a page that loads anything is not mistaken for a registered scheme.")

    def observe(self, session: Session) -> dict:
        return {
            "aerie": open_target(session, "aerie://version", settle=10),
            "helium": open_target(session, "helium://version", settle=4),
            "bogus": open_target(session, "zzz://version", settle=4),
        }

    @staticmethod
    def judge(obs: dict) -> Verdict:
        if loads_version_page(obs["bogus"]):
            return "fail", f"negative control failed: an invented scheme loaded {obs['bogus']['url']}"
        if not loads_version_page(obs["aerie"]):
            return "fail", f"aerie://version did not load the version page (got {obs['aerie']['url']!r})"
        # An unknown scheme keeps its URL and has no title, so only a page with a title counts as
        # registered (the made-up scheme above is the control for exactly that).
        if loads_version_page(obs["helium"]) or (obs["helium"]["url"].startswith("helium:") and obs["helium"]["title"]):
            return "fail", (f"helium:// is still registered (helium://version ended at {obs['helium']['url']!r}, "
                            f"title {obs['helium']['title']!r})")
        return "pass", "aerie://version loads the version page; helium://version does not, like an unknown scheme"


def expected_versions() -> dict:
    pins = lock.load_lock(REPO / "helium.lock")
    release = (REPO / aerie_version.RELEASE_FILE).read_text().strip()
    aerie_version.parse_release(release)
    return {"aerie_release": release, "chromium_version": pins.chromium_version, "helium_version": pins.helium_version}


def has_number(text: str, number: str) -> bool:
    return re.search(rf"(?<![\d.]){re.escape(number)}(?![\d.])", text) is not None


@register
class Version(HardeningCheck):
    name = "branding_version"
    control = "all"
    negative_control = ("The check also requires that Helium's own version number is absent, so a build that "
                        "still shows Helium's version fails even when the other two numbers are present.")

    def observe(self, session: Session) -> dict:
        about = read_page(session, "chrome://settings/help")
        version = read_page(session, "chrome://version/")
        return {"about_text": about["text"] or about.get("error") or "",
                "version_page_text": version["text"] or version.get("error") or "",
                **expected_versions()}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        about, page = obs["about_text"], obs["version_page_text"]
        problems: list[str] = []
        for label, text in (("About", about), ("chrome://version", page)):
            for what, number in (("the Aerie release", obs["aerie_release"]),
                                 ("the Chromium version", obs["chromium_version"])):
                if not has_number(text, number):
                    problems.append(f"{label} does not show {what} {number}")
            if has_number(text, obs["helium_version"]):
                problems.append(f"{label} still shows Helium's version {obs['helium_version']}")
        if problems:
            return "fail", "; ".join(problems)
        return "pass", (f"About and chrome://version show Aerie {obs['aerie_release']} and "
                        f"Chromium {obs['chromium_version']}, and not Helium {obs['helium_version']}")
