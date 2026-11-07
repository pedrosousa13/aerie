"""Extensions cannot loosen the hardened prefs through chrome.privacy."""

from __future__ import annotations

from pathlib import Path

from ...session import Session
from .. import register
from .common import HardeningCheck, Verdict

# Fixed by the "key" in fixtures/hardening/extension/manifest.json (sha256 of the public key).
EXTENSION_ID = "knlpboobmlcbfihkhfbhepnojchckong"
CONTROLLED = "controlled_by_this_extension"


def flags_for(directory: Path) -> tuple[str, ...]:
    return (f"--load-extension={directory}",)


def extension_dir(session: Session) -> Path:
    return Path(session.fixtures.root) / "hardening" / "extension"


def probe_extension(session: Session) -> dict:
    nav = session.navigate_url(f"chrome-extension://{EXTENSION_ID}/probe.html")
    if nav["error"]:
        return {"loaded": False, "error": nav["error"], "results": []}
    results = session.evaluate("window.__probe()", await_promise=True)
    return {"loaded": True, "results": results or []}


@register
class ExtensionPrefs(HardeningCheck):
    name = "hardening_extension_prefs"
    control = "extension_lock"
    negative_control = ("The same extension changes an unlocked setting (referrersEnabled) in the same run, "
                        "so a refused write to a hardened setting is the lock, not a broken extension.")

    def observe(self, session: Session) -> dict:
        with session.variant(flags_for(extension_dir(session))) as v:
            return probe_extension(v)

    @staticmethod
    def judge(obs: dict) -> Verdict:
        if not obs["loaded"]:
            return "fail", f"the probe extension did not load ({obs.get('error')}); see the browser stderr log"
        by_name = {r["name"]: r for r in obs["results"]}
        controls = [r for r in obs["results"] if r["role"] == "control"]
        if not controls or any(r["set_error"] or r["level"] != CONTROLLED for r in controls):
            return "fail", f"negative control failed: the unlocked setting could not be changed: {controls}"
        hardened = [r for r in obs["results"] if r["role"] == "hardened"]
        if len(hardened) < 3:
            return "fail", f"expected three hardened settings to be tried, got {sorted(by_name)}"
        for r in hardened:
            if not r["set_error"] or r["level"] == CONTROLLED:
                return "fail", f"the extension could change {r['name']} (error {r['set_error']}, level {r['level']})"
        return "pass", ("the extension was refused for " + ", ".join(r["name"] for r in hardened)
                        + " and could still change referrersEnabled")
