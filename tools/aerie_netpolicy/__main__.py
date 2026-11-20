"""./aerie netpolicy {generate,check}  (or python3 -m aerie_netpolicy, with tools on the path)"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from . import model

REPO = Path(__file__).resolve().parents[2]


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="python3 -m aerie_netpolicy")
    p.add_argument("command", choices=("generate", "check"))
    p.add_argument("--repo", default=str(REPO), help="repository root (default: this checkout)")
    p.add_argument("--tree", default=None,
                   help="prepared Chromium source tree: check also that every annotation name exists in it")
    args = p.parse_args(argv)
    repo = Path(args.repo)
    try:
        table = model.parse_doc((repo / model.DOC_PATH).read_text(encoding="utf-8"))
        text = model.render(table)
    except (model.PolicyDocError, OSError) as exc:
        print(f"request policy tables in {model.DOC_PATH} are invalid: {exc}", file=sys.stderr)
        return 1
    target = repo / model.GENERATED_PATH
    if args.command == "generate":
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")
        print(f"wrote {target}")
        return 0
    status = 0
    if not target.is_file() or target.read_text(encoding="utf-8") != text:
        print(f"stale: {target} (run: ./aerie netpolicy generate)", file=sys.stderr)
        status = 1
    if args.tree:
        xml = Path(args.tree) / model.ANNOTATIONS_XML
        if not xml.is_file():
            print(f"no {model.ANNOTATIONS_XML} in {args.tree}: annotation names not checked", file=sys.stderr)
        else:
            unknown = model.unknown_annotations(
                table, xml.read_text(encoding="utf-8"), model.overlay_annotation_text(repo))
            for name in unknown:
                print(f"unknown traffic annotation in {model.DOC_PATH}: {name} "
                      "(not in the pinned source or in Aerie's overlay)", file=sys.stderr)
            status = 1 if unknown else status
    return status


if __name__ == "__main__":
    sys.exit(main())
