"""./aerie hardening {generate,check}  (or python3 -m aerie_hardening, with tools on the path)"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from . import model, render

REPO = Path(__file__).resolve().parents[2]


def outputs(repo: Path, table: dict) -> dict[Path, str]:
    return {
        repo / "docs" / "hardening.md": render.render_docs(table),
        repo / "patches" / render.MARKUP_PATCH: render.render_markup_patch(table),
    }


def _known_checks() -> set[str]:
    from aerie_bench import checks
    return set(checks.REGISTRY)


def _series(repo: Path) -> list[str] | None:
    series = repo / "patches" / "series"
    if not series.is_file():
        return None
    from aerie_build import patches
    return patches.parse_series(series)


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="python3 -m aerie_hardening")
    p.add_argument("command", choices=("generate", "check"))
    p.add_argument("--repo", default=str(REPO), help="repository root (default: this checkout)")
    args = p.parse_args(argv)
    repo = Path(args.repo)
    table = model.load()
    problems = model.validate(table, _known_checks(), series=_series(repo))
    if problems:
        print("controls.json is invalid:", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1
    stale = 0
    for path, text in outputs(repo, table).items():
        if args.command == "generate":
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
            print(f"wrote {path}")
        elif not path.is_file() or path.read_text(encoding="utf-8") != text:
            print(f"stale: {path} (run: ./aerie hardening generate)", file=sys.stderr)
            stale = 1
    return stale


if __name__ == "__main__":
    sys.exit(main())
