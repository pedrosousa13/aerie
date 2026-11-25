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

from .. import ui_devtools, xinput
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
    "chrome://flags/",
    "chrome://history/",
    "chrome://downloads/",
    "chrome://extensions/",
    "chrome://bookmarks/",
    "chrome://management/",
    "chrome://components/",
    "chrome://setup/",
)

# chrome://version lists the command line, the executable and the profile path. They hold the build root
# (.../src/helium-<version>/out/Default/aerie), which is a directory name, not branding.
_PATH_LINE_RE = re.compile(r"^(Command Line|Executable Path|Profile Path)\t.*$", re.MULTILINE)

# chrome://flags prints each experiment's internal id (#helium-noise) under its name. The id is a flag
# name that settings and command lines use, not a word the user reads as a name.
_FLAG_ID_RE = re.compile(r"#helium-[a-z0-9-]+")

# Aerie is derived from Helium and Chromium, so the copyright line on chrome://version credits
# "The Helium Authors" by name, on chrome://version and About (chrome://settings/help). That exact
# string is attribution, not branding.
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
    The Helium Authors on chrome://version and About, internal flag ids on chrome://flags."""
    if url == "chrome://version/":
        text = _CREDIT_RE.sub("", _PATH_LINE_RE.sub("", text))
    elif url == "chrome://settings/help":
        text = _CREDIT_RE.sub("", text)
    elif url == "chrome://flags/":
        text = _FLAG_ID_RE.sub("", text)
    return text


def read_page(session: Session, url: str, label: str | None = None, before_read=None) -> dict:
    """Load `url`, let it settle and read its text. `label` names the page in the result (default: the
    url); `before_read(session)` runs once the page has loaded, for a case that must act on it first."""
    shown = label or url
    nav = session.navigate_url(url, wait=20)
    if nav["error"]:
        return {"url": shown, "title": "", "text": "", "error": nav["error"]}
    session.wait_for("document.readyState === 'complete'", timeout=10)
    time.sleep(1.0)  # WebUI pages fill in after load
    try:
        if before_read:
            before_read(session)
        got = session.evaluate(TEXT_JS) or {}
    except Exception as exc:  # noqa: BLE001  (a page that cannot be read is data, not a crash)
        return {"url": shown, "title": "", "text": "", "error": f"could not read: {exc}"}
    return {"url": shown, "title": got.get("title", ""), "text": scrub(got.get("text", ""), url), "error": None}


# chrome://components shows one row per component, each with a "Check for update" button (class
# button-check-update, id = the component id) and a status span (id = "status-<id>"). The click is
# what a user does; with component updates off it ends in the "disabled" status, the one that used
# to carry Helium's wording (issue #131). A plain "Update error" is a different status and does not count.
FAILED_UPDATE_PAGE_LABEL = "chrome://components/ (after a failed update)"
DISABLED_STATUS = "Component updates are disabled"
AERIE_DISABLED_TEXT = "Component updates are disabled in Aerie."
FORCE_UPDATE_JS = """
(() => {
  const buttons = document.querySelectorAll('.button-check-update');
  buttons.forEach((b) => b.click());
  return buttons.length;
})()"""
STATUSES_JS = """
Array.from(document.querySelectorAll('[id^="status-"]')).map((e) => e.textContent.trim())"""
DISABLED_SHOWN_JS = (
    "Array.from(document.querySelectorAll('[id^=\"status-\"]'))"
    f".some((e) => e.textContent.includes({DISABLED_STATUS!r}))")


def force_failed_update(session: Session, wait: float = 60.0) -> None:
    """Press Check for update on every component and wait (at most `wait` seconds) for the disabled status."""
    session.wait_for("document.querySelectorAll('.button-check-update').length > 0", timeout=15)
    session.evaluate(FORCE_UPDATE_JS)
    session.wait_for(DISABLED_SHOWN_JS, timeout=wait, interval=0.5)


def read_failed_update(session: Session) -> dict:
    """chrome://components after every component's update was forced to fail: a read_page() dict plus
    the statuses the page showed."""
    got = read_page(session, "chrome://components/", label=FAILED_UPDATE_PAGE_LABEL, before_read=force_failed_update)
    got["statuses"] = [] if got["error"] else (session.evaluate(STATUSES_JS) or [])
    return got


def judge_failed_update(forced: dict | None) -> list[str]:
    """Problems with the page read after a forced failed update: it must have loaded, shown the disabled
    status (else the case proves nothing), say it in Aerie's words and not say Helium."""
    if not forced:
        return ["no case read chrome://components after a failed component update"]
    if forced.get("error"):
        return [f"chrome://components after a failed update did not load ({forced['error']})"]
    if not any(DISABLED_STATUS in t for t in forced.get("statuses", [])):
        return ["the forced component update did not fail with the disabled-updates status (statuses: "
                f"{forced.get('statuses') or 'none'}), so the case proves nothing"]
    hits = find_helium(forced.get("text", "")) + find_helium(forced.get("title", ""))
    if hits:
        return [f"chrome://components says Helium after a failed update: ...{hits[0]}... ({len(hits)} hit(s))"]
    if AERIE_DISABLED_TEXT not in forced.get("text", ""):
        return [f"chrome://components after a failed update does not say {AERIE_DISABLED_TEXT!r}"]
    return []


# A download that fails (issue #94). The fixture server answers 404 for a file that is not there, and a
# download of a 404 ends as an interrupted download, shown as "Failed". The browser that does it has UI
# DevTools and a window (Xvfb), because the download bubble is a native view (CDP cannot see it): its text is every Text,
# tooltip and accessible name of a view below a DownloadBubble* view.
FAILED_DOWNLOAD_PAGE_LABEL = "chrome://downloads/ (after a failed download)"
FAILED_DOWNLOAD_PATH = "/branding/no-such-file.bin"
FAILED_DOWNLOAD_BUBBLE = "the download bubble"
FORCE_DOWNLOAD_JS = f"""
(() => {{
  const a = document.createElement('a');
  a.href = {FAILED_DOWNLOAD_PATH!r};
  a.download = 'missing.bin';
  document.body.appendChild(a);
  a.click();
  return true;
}})()"""
# The downloads page says "Failed" in the row of an interrupted download.
DOWNLOAD_FAILED_JS = DEEP_JS + """
(() => deepQueryAll(document, '*').some((e) => /^Failed\\b/.test((e.textContent || '').trim()) &&
                                                  e.children.length === 0))()"""
BUBBLE_TEXT_PROPERTIES = ("Text", "TooltipText", "AccessibleName")
BUBBLE_WAIT = 20.0


def bubble_texts(ui: "ui_devtools.UiDevTools") -> list[str]:
    """The words on the download bubble: every text of every view below a view whose class starts with
    DownloadBubble. Empty when the bubble is not on screen."""
    texts: list[str] = []
    root = ui.send("DOM.getDocument")["root"]
    stack = [(root, False)]
    while stack:
        node, inside = stack.pop()
        attrs = node.get("attributes") or []
        cls = attrs[1] if len(attrs) > 1 and attrs[0] == "class" else node.get("nodeName", "")
        inside = inside or cls.startswith("DownloadBubble")
        if inside and node["nodeId"] != root["nodeId"]:
            try:
                props = ui.properties(node["nodeId"])
            except ui_devtools.UiDevToolsError as exc:
                if not str(exc).startswith("CSS.getMatchedStylesForNode:"):
                    raise
                props = {}
            texts += [props[k] for k in BUBBLE_TEXT_PROPERTIES if props.get(k)]
        stack.extend((c, inside) for c in reversed(node.get("children") or []))
    return texts


def press_download_button(ui: "ui_devtools.UiDevTools") -> bool:
    """Press the toolbar's Downloads button (a visible button whose tooltip says download: New download available) to open the bubble.
    False when there is none."""
    for node_id, cls in ui.nodes():
        if "Button" not in cls and "Download" not in cls:
            continue
        try:
            props = ui.properties(node_id)
        except ui_devtools.UiDevToolsError:
            continue
        if props.get("Visible") == "true" and "download" in props.get("TooltipText", "").lower():
            ui.press(node_id, props)
            return True
    return False


def read_failed_download(session: Session) -> dict:
    """Force a download that fails in a second browser (with UI DevTools), then read chrome://downloads
    and the download bubble: a read_page() dict plus ``failed`` (the page shows a failed download) and
    ``bubble`` ({"texts", "error"})."""
    got: dict = {"url": FAILED_DOWNLOAD_PAGE_LABEL, "title": "", "text": "", "error": None,
                 "failed": False, "bubble": {"texts": [], "error": None}}
    try:
        # A headless browser never makes the download button, so the browser has a window on an Xvfb.
        with xinput.xvfb() as display, session.new_profile() as profile, \
                session.variant(extra_flags=ui_devtools.FLAGS, profile=profile, headed=True,
                                env={**xinput.display_env(display), "WAYLAND_DISPLAY": ""}) as browser:
            browser.navigate("/index.html")
            browser.evaluate(FORCE_DOWNLOAD_JS, user_gesture=True)
            deadline = time.monotonic() + BUBBLE_WAIT
            ui = None
            try:
                ui = ui_devtools.connect(profile.path, timeout=browser.timeout)
                pressed = False
                while True:
                    texts = bubble_texts(ui)
                    if any("Failed" in t for t in texts) or time.monotonic() >= deadline:
                        break
                    if not texts and not pressed and time.monotonic() > deadline - BUBBLE_WAIT / 2:
                        pressed = press_download_button(ui)  # it did not open by itself
                    time.sleep(0.5)
                got["bubble"]["texts"] = texts
                got["bubble"]["classes"] = sorted({c for _, c in ui.nodes() if c.startswith("Download")})
            except (ui_devtools.UiDevToolsError, OSError) as exc:
                got["bubble"]["error"] = f"{type(exc).__name__}: {exc}"
            finally:
                if ui is not None:
                    ui.close()

            def wait_failed(b: Session) -> None:
                b.wait_for(DOWNLOAD_FAILED_JS, timeout=BUBBLE_WAIT, interval=0.5)

            page_read = read_page(browser, "chrome://downloads/", label=FAILED_DOWNLOAD_PAGE_LABEL,
                                  before_read=wait_failed)
            got.update(page_read)
            got["failed"] = False if page_read["error"] else bool(browser.evaluate(DOWNLOAD_FAILED_JS))
    except Exception as exc:  # noqa: BLE001  (a browser that cannot be driven is data, not a crash)
        got["error"] = f"could not force a failed download: {type(exc).__name__}: {exc}"
    return got


def judge_failed_download(forced: dict | None) -> list[str]:
    """Problems with chrome://downloads and the download bubble after a failed download: both must have
    been read, the download must really have failed (else the case proves nothing), and neither may say
    Helium."""
    if not forced:
        return ["no case read the download bubble and chrome://downloads after a failed download"]
    if forced.get("error"):
        return [f"chrome://downloads after a failed download did not load ({forced['error']})"]
    problems: list[str] = []
    if not forced.get("failed"):
        problems.append("the forced download did not fail (chrome://downloads shows no failed download), "
                        "so the case proves nothing")
    bubble = forced.get("bubble") or {}
    if bubble.get("error") or not bubble.get("texts"):
        problems.append(f"{FAILED_DOWNLOAD_BUBBLE} was not read ({bubble.get('error') or 'no text on it'}; download views seen: "
                        f"{bubble.get('classes') or 'none'}), "
                        "so the case proves nothing")
    for where, text in (("chrome://downloads", forced.get("text", "") + "\n" + forced.get("title", "")),
                        (FAILED_DOWNLOAD_BUBBLE, "\n".join(bubble.get("texts") or []))):
        hits = find_helium(text)
        if hits:
            problems.append(f"{where} says Helium after a failed download: ...{hits[0]}... ({len(hits)} hit(s))")
    return problems


@register
class NoHelium(HardeningCheck):
    name = "branding_no_helium"
    control = "all"
    negative_control = ("A fixture page with the word Helium inside a shadow root is read by the same "
                        "scanner and must be reported, so the scan can see a hit. The failed-update case "
                        "also fails when no component update failed, and the failed-download case when no "
                        "download failed or the bubble could not be read, so neither can pass without "
                        "reaching the error path.")

    def observe(self, session: Session) -> dict:
        control = read_page(session, session.fixtures.base_url + CONTROL_PATH)
        return {"control": control, "pages": [read_page(session, url) for url in PAGES],
                "failed_update": read_failed_update(session),
                "failed_download": read_failed_download(session)}

    @staticmethod
    def judge(obs: dict) -> Verdict:
        control = obs["control"]
        if control.get("error") or not find_helium(control.get("text", "")):
            return "fail", ("negative control failed: the scanner did not find Helium on the control page "
                            f"({control.get('error') or 'text had no hit'})")
        if not obs["pages"]:
            return "fail", "no pages were scanned"
        problems: list[str] = []
        for p in obs["pages"]:
            if p.get("error"):
                problems.append(f"{p['url']} did not load ({p['error']})")
                continue
            if find_helium(p["title"]):
                problems.append(f"{p['url']} title says Helium: {p['title']!r}")
            hits = find_helium(p["text"])
            if not hits:
                continue
            problems.append(f"{p['url']} says Helium: ...{hits[0]}... ({len(hits)} hit(s))")
        problems += judge_failed_update(obs.get("failed_update"))
        problems += judge_failed_download(obs.get("failed_download"))
        if problems:
            return "fail", "; ".join(problems)
        return "pass", (f"no Helium in the titles and text of {len(obs['pages'])} pages, nor on "
                        "chrome://components after a failed component update, nor on chrome://downloads and the download "
                        "bubble after a failed download")


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
