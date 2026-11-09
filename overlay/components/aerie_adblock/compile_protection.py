#!/usr/bin/env python3
# Copyright 2026 The Aerie Authors
# You can use, redistribute, and/or modify this source code under
# the terms of the GPL-3.0 license that can be found in the LICENSE file.
"""GN action: compile the phishing and malware domain lists into the serialized protection engine.

Same shape as compile_engine.py: check each list against the committed lock a
second time, run the host tool aerie_protection_compile (built from the same
crates as the browser), and let its self-checks run on the reloaded engine.
Any failure stops the build and leaves no output (fail closed).
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compile_common  # noqa: E402


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--selfcheck", required=True)
    ap.add_argument("--lock", required=True)
    ap.add_argument("--min-entries", type=int, default=1)
    ap.add_argument("--list", action="append", required=True, dest="lists")
    args = ap.parse_args(argv)

    compile_common.verify_lists("compile_protection", args.lock, args.lists)

    with open(args.selfcheck, encoding="utf-8") as fh:
        checks = json.load(fh)
    if not checks.get("listed") or not checks.get("unlisted"):
        sys.exit("compile_protection: the self-check file needs both 'listed' and 'unlisted' hosts")
    # GN passes the tool relative to the build directory, a bare name on Linux.
    cmd = [os.path.abspath(args.tool), "--out", args.out, "--min-entries", str(args.min_entries)]
    for host in checks["listed"]:
        cmd += ["--expect-listed", host]
    for host in checks["unlisted"]:
        cmd += ["--expect-unlisted", host]
    cmd += args.lists
    compile_common.run_tool("compile_protection", cmd, args.out)


if __name__ == "__main__":
    main(sys.argv[1:])
