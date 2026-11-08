"""Load and validate the hardening control table (controls.json)."""

from __future__ import annotations

import json
import re
from pathlib import Path

TABLE_PATH = Path(__file__).resolve().parent / "controls.json"
PROVIDED_BY = ("chromium", "ungoogled", "helium", "aerie")
UI_KINDS = ("dropdown", "toggle", "readonly", "link")
REQUIRED = ("id", "title", "summary", "default", "provided_by", "default_set_in", "upstream",
            "aerie_patches", "pref", "cost", "ui", "bench_checks")
TEXT_KEYS = ("title", "summary", "default", "cost")
ID_RE = re.compile(r"^[a-z][a-z0-9_]*$")
ROUTE_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")  # a name in the settings `routes` object
# Text goes into a Polymer template and a Markdown table: no bindings, no markup.
FORBIDDEN_TEXT = (("[[", "template binding"), ("{{", "template binding"), ("$i18n", "template binding"),
                  ("${", "template binding"), ("`", "template literal"), ("\\", "backslash"), ("<", "markup"), (">", "markup"), ("&", "markup"), ('"', "quote"), ("|", "table separator"))


def load(path: Path = TABLE_PATH) -> dict:
    return json.loads(Path(path).read_text(encoding="utf-8"))


def _text_problems(where: str, value) -> list[str]:
    if not isinstance(value, str) or not value.strip():
        return [f"{where}: must be a non-empty string"]
    return [f"{where}: contains {why} syntax {token!r}" for token, why in FORBIDDEN_TEXT if token in value]


def _ui_problems(cid: str, c: dict) -> list[str]:
    ui = c.get("ui")
    where = f"{cid}.ui"
    if not isinstance(ui, dict) or ui.get("kind") not in UI_KINDS:
        return [f"{where}: kind must be one of {', '.join(UI_KINDS)}"]
    out: list[str] = []
    kind = ui["kind"]
    if kind == "readonly":
        out += _text_problems(f"{where}.state", ui.get("state"))
        return out
    if kind == "link":
        if not ROUTE_RE.match(str(ui.get("route", ""))):
            out.append(f"{where}: route must be a settings route name such as SITE_SETTINGS_JAVASCRIPT_OPTIMIZER")
        return out
    if not ui.get("pref"):
        out.append(f"{where}: pref is required for kind {kind}")
    if kind == "dropdown":
        options = ui.get("options")
        if not isinstance(options, list) or len(options) < 2:
            return out + [f"{where}: options needs at least two entries"]
        for i, o in enumerate(options):
            if "value" not in o:
                out.append(f"{where}.options[{i}]: value missing")
            out += _text_problems(f"{where}.options[{i}].label", o.get("label"))
        values = [o.get("value") for o in options]
        if ui.get("default_value") not in values:
            out.append(f"{where}: default_value {ui.get('default_value')!r} is not one of the option values")
        elif isinstance(c.get("pref"), dict) and c["pref"].get("name") == ui.get("pref") \
                and c["pref"].get("default") != ui["default_value"]:
            out.append(f"{where}: default_value does not match pref.default")
    return out


def validate(table: dict, known_checks: set[str], series: list[str] | None = None) -> list[str]:
    """Return a list of problems; empty means the table is usable."""
    problems: list[str] = []
    controls = table.get("controls")
    if table.get("schema") != 1:
        problems.append("schema must be 1")
    if not isinstance(controls, list) or not controls:
        return problems + ["controls: must be a non-empty list"]
    seen: set[str] = set()
    for idx, c in enumerate(controls):
        cid = c.get("id", f"#{idx}")
        for key in REQUIRED:
            if key not in c:
                problems.append(f"{cid}: missing key {key}")
        if not ID_RE.match(str(c.get("id", ""))):
            problems.append(f"{cid}: id must match {ID_RE.pattern}")
        if cid in seen:
            problems.append(f"{cid}: duplicate id")
        seen.add(cid)
        for key in TEXT_KEYS:
            if key in c:
                problems += _text_problems(f"{cid}.{key}", c[key])
        if c.get("provided_by") not in PROVIDED_BY:
            problems.append(f"{cid}: provided_by must be one of {', '.join(PROVIDED_BY)}")
        for key in ("default_set_in", "upstream"):
            if key in c and (not isinstance(c[key], list) or not c[key]):
                problems.append(f"{cid}.{key}: must be a non-empty list")
        patches = c.get("aerie_patches", [])
        if c.get("provided_by") == "aerie" and not patches:
            problems.append(f"{cid}: provided_by aerie needs aerie_patches")
        if series is not None:
            for p in patches:
                if p not in series:
                    problems.append(f"{cid}.aerie_patches: {p} is not in patches/series")
        pref = c.get("pref")
        if pref is not None and (not isinstance(pref, dict) or not pref.get("name") or "default" not in pref):
            problems.append(f"{cid}.pref: needs name and default (or null)")
        if "ui" in c:
            problems += _ui_problems(cid, c)
        checks = c.get("bench_checks")
        if not isinstance(checks, list) or not checks:
            problems.append(f"{cid}.bench_checks: needs at least one check")
        else:
            problems += [f"{cid}.bench_checks: unknown check {n}" for n in checks if n not in known_checks]
    for n in table.get("cross_cutting_checks", []):
        if n not in known_checks:
            problems.append(f"cross_cutting_checks: unknown check {n}")
    for i, d in enumerate(table.get("deferred", [])):
        if not d.get("title") or not d.get("reason"):
            problems.append(f"deferred[{i}]: needs title and reason")
    for i, e in enumerate(table.get("locked_from_extensions", [])):
        if not e.get("extension_pref") or not e.get("browser_pref"):
            problems.append(f"locked_from_extensions[{i}]: needs extension_pref and browser_pref")
    return problems
