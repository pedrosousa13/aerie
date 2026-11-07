"""Scenario files: which checks to run, plus optional scripted page steps.

    {
      "name": "click-through",
      "checks": ["smoke"],
      "steps": [
        {"navigate": "/index.html"},
        {"click": "#go"},
        {"evaluate": "document.getElementById('heading').textContent", "expect": "clicked"},
        {"screenshot": "after-click.png"},
        {"wait_ms": 100}
      ]
    }

Steps only ever touch the local fixture server: ``navigate`` takes a path, not a URL.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from pathlib import Path

DEFAULT_CHECKS = ("smoke",)
_ACTIONS = ("navigate", "evaluate", "click", "screenshot", "wait_ms")
_SHOT = re.compile(r"^[A-Za-z0-9._-]+\.png$")


class ScenarioError(ValueError):
    pass


@dataclass(frozen=True)
class Step:
    action: str
    arg: object
    expect: str | None = None


@dataclass(frozen=True)
class Scenario:
    name: str = "default"
    checks: tuple[str, ...] = DEFAULT_CHECKS
    steps: tuple[Step, ...] = field(default_factory=tuple)


def _step(raw: object, index: int) -> Step:
    where = f"step {index}"
    if not isinstance(raw, dict):
        raise ScenarioError(f"{where}: must be an object")
    actions = [k for k in raw if k in _ACTIONS]
    if len(actions) != 1:
        raise ScenarioError(f"{where}: needs exactly one of {', '.join(_ACTIONS)}")
    action = actions[0]
    allowed = {action} | ({"expect"} if action == "evaluate" else set())
    extra = set(raw) - allowed
    if extra:
        raise ScenarioError(f"{where}: unknown key(s) {sorted(extra)}")
    arg = raw[action]
    if action == "navigate":
        if not isinstance(arg, str) or not arg.startswith("/") or arg.startswith("//"):
            raise ScenarioError(f"{where}: navigate takes a fixture path starting with '/', got {arg!r}")
    elif action == "screenshot":
        if not isinstance(arg, str) or not _SHOT.match(arg):
            raise ScenarioError(f"{where}: screenshot takes a plain file name ending in .png, got {arg!r}")
    elif action == "wait_ms":
        if not isinstance(arg, int) or isinstance(arg, bool) or not 0 <= arg <= 10_000:
            raise ScenarioError(f"{where}: wait_ms takes an integer from 0 to 10000")
    elif not isinstance(arg, str) or not arg:
        raise ScenarioError(f"{where}: {action} takes a non-empty string")
    expect = raw.get("expect")
    if expect is not None and not isinstance(expect, str):
        raise ScenarioError(f"{where}: expect must be a string")
    return Step(action, arg, expect)


def parse(text: str) -> Scenario:
    try:
        raw = json.loads(text)
    except json.JSONDecodeError as exc:
        raise ScenarioError(f"not valid JSON: {exc}") from exc
    if not isinstance(raw, dict):
        raise ScenarioError("top level must be an object")
    unknown = set(raw) - {"name", "checks", "steps"}
    if unknown:
        raise ScenarioError(f"unknown key(s) {sorted(unknown)}")
    name = raw.get("name", "scenario")
    checks = raw.get("checks", [] if "steps" in raw else list(DEFAULT_CHECKS))
    if not isinstance(name, str) or not name:
        raise ScenarioError("name must be a non-empty string")
    if not isinstance(checks, list) or not all(isinstance(c, str) for c in checks):
        raise ScenarioError("checks must be a list of check names")
    steps = raw.get("steps", [])
    if not isinstance(steps, list):
        raise ScenarioError("steps must be a list")
    return Scenario(name, tuple(checks), tuple(_step(s, i + 1) for i, s in enumerate(steps)))


def load(path: Path) -> Scenario:
    try:
        return parse(Path(path).read_text())
    except OSError as exc:
        raise ScenarioError(f"cannot read {path}: {exc}") from exc
