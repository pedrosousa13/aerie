"""Smoke: the browser launches, loads a fixture page, and shuts down cleanly."""

from ..session import CheckResult, Session
from . import register

EXPECTED_TITLE = "Aerie Bench Fixture"


@register
class Smoke:
    name = "smoke"
    seeds_helium_services = False

    def run(self, session: Session) -> CheckResult:
        session.navigate("/index.html")
        title = session.evaluate("document.title")
        session.screenshot("smoke.png")
        if title != EXPECTED_TITLE:
            return CheckResult(self.name, "fail", f"page title was {title!r}, expected {EXPECTED_TITLE!r}")
        return CheckResult(self.name, "pass", f"loaded fixture page, title {title!r}")

    def finalize(self, session: Session, result: CheckResult) -> CheckResult:
        if result.status != "pass":
            return result
        if not session.exit.get("clean"):
            return CheckResult(self.name, "fail",
                               f"page loaded but the browser did not exit cleanly ({session.exit})")
        return CheckResult(self.name, "pass", result.detail + "; exited cleanly")
