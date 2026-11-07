"""results.json (schema 1) and summary.txt."""

from __future__ import annotations

import json
from pathlib import Path

SCHEMA = 1


def to_json(run: dict) -> str:
    return json.dumps({"schema": SCHEMA, **run}, indent=2, sort_keys=False) + "\n"


def summary(run: dict) -> str:
    b = run["binary"]
    lines = [
        f"Aerie Bench: {run['scenario']}",
        f"binary    {b['path']}",
        f"version   {b.get('version') or 'unknown'}",
        f"started   {run['started']}  ({run['duration_s']:.1f}s)",
        f"mode      {'headless' if run['headless'] else 'headed'}"
        f"{', no-sandbox' if run['no_sandbox'] else ''}",
        "",
        "checks",
    ]
    for c in run["checks"]:
        lines.append(f"  {c['status'].upper():5}  {c['name']}: {c['detail']}")
    net = run["network"]
    lines += ["",
              f"network   {net['netlog_requests']} URL requests in the NetLog, "
              f"{len(net['fixture_requests'])} to the fixture server, "
              f"{len(net['external_requests'])} external (refused)"]
    for r in net["external_requests"][:10]:
        lines.append(f"  external  {r['method']} {r['url']}  net_error={r['net_error']}")
    lines += [
        f"exit      {'clean' if run['exit'].get('clean') else 'NOT clean'} (code {run['exit'].get('code')})",
        f"profile   {'deleted' if run['profile']['deleted'] else 'NOT deleted'}",
        f"result    {run['status'].upper()}",
        "",
        "files     " + ", ".join(run["files"]),
    ]
    return "\n".join(lines) + "\n"


def write(out_dir: Path, run: dict) -> None:
    (out_dir / "results.json").write_text(to_json(run))
    (out_dir / "summary.txt").write_text(summary(run))
