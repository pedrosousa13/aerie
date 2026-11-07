"""Runs the ``steps`` of a scenario file. Added by the runner when steps exist."""

from ..session import CheckResult, Session
from . import register


@register
class ScenarioSteps:
    name = "scenario"
    seeds_helium_services = False

    def __init__(self, steps=()):
        self.steps = tuple(steps)

    def run(self, session: Session) -> CheckResult:
        for i, step in enumerate(self.steps, 1):
            label = f"step {i} ({step.action} {step.arg!r})"
            if step.action == "navigate":
                session.navigate(step.arg)
            elif step.action == "click":
                session.click(step.arg)
            elif step.action == "screenshot":
                session.screenshot(step.arg)
            elif step.action == "wait_ms":
                import time
                time.sleep(step.arg / 1000)
            elif step.action == "evaluate":
                value = session.evaluate(step.arg)
                if step.expect is not None and str(value) != step.expect:
                    return CheckResult(self.name, "fail", f"{label}: got {value!r}, expected {step.expect!r}")
        return CheckResult(self.name, "pass", f"{len(self.steps)} step(s) completed")
