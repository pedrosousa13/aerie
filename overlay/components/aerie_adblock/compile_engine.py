#!/usr/bin/env python3
# Copyright 2026 The Aerie Authors
# You can use, redistribute, and/or modify this source code under
# the terms of the GPL-3.0 license that can be found in the LICENSE file.
"""GN action: compile the filter lists into the serialized engine.

Runs the host tool aerie_adblock_compile (Rust, built from the same crates as
the browser, so the serialized form always matches). Before it does, it checks
each list against the committed lock, a second time after the build step that
fetched them; after it, the tool's self-checks run on the reloaded engine.
Any failure stops the build and leaves no output (fail closed).
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compile_common  # noqa: E402


def probe(entry):
    return "|".join([entry["url"], entry["hostname"], entry["source_hostname"], entry["type"],
                     "true" if entry["third_party"] else "false"])


def cosmetic_probe(entry):
    return "|".join([entry["url"], entry["classes"], entry["ids"], entry["selector"]])


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--selfcheck", required=True)
    ap.add_argument("--lock", required=True)
    ap.add_argument("--list", action="append", required=True, dest="lists")
    args = ap.parse_args(argv)

    compile_common.verify_lists("compile_engine", args.lock, args.lists)

    with open(args.selfcheck, encoding="utf-8") as fh:
        checks = json.load(fh)
    # GN passes the tool relative to the build directory, and on Linux that is a
    # bare name ("aerie_adblock_compile"), which subprocess would look up in PATH.
    cmd = [os.path.abspath(args.tool), "--out", args.out]
    for entry in checks["block"]:
        cmd += ["--expect-block", probe(entry)]
    for entry in checks["allow"]:
        cmd += ["--expect-allow", probe(entry)]
    for entry in checks.get("hide", []):
        cmd += ["--expect-hide", cosmetic_probe(entry)]
    for entry in checks.get("keep", []):
        cmd += ["--expect-keep", cosmetic_probe(entry)]
    cmd += args.lists
    compile_common.run_tool("compile_engine", cmd, args.out)


if __name__ == "__main__":
    main(sys.argv[1:])
