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
import hashlib
import json
import os
import subprocess
import sys


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def probe(entry):
    return "|".join([entry["url"], entry["hostname"], entry["source_hostname"], entry["type"],
                     "true" if entry["third_party"] else "false"])


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--selfcheck", required=True)
    ap.add_argument("--lock", required=True)
    ap.add_argument("--list", action="append", required=True, dest="lists")
    args = ap.parse_args(argv)

    with open(args.lock, encoding="utf-8") as fh:
        lock = json.load(fh)
    pinned = {entry["file"]: entry["sha256"] for entry in lock["lists"]}
    names = {os.path.basename(p) for p in args.lists}
    if names != set(pinned):
        sys.exit(f"compile_engine: lists {sorted(names)} do not match the lock {sorted(pinned)}")
    for path in args.lists:
        want = pinned[os.path.basename(path)]
        got = sha256(path)
        if got != want:
            sys.exit(f"compile_engine: {path} has sha256 {got}, the lock says {want}")

    with open(args.selfcheck, encoding="utf-8") as fh:
        checks = json.load(fh)
    cmd = [args.tool, "--out", args.out]
    for entry in checks["block"]:
        cmd += ["--expect-block", probe(entry)]
    for entry in checks["allow"]:
        cmd += ["--expect-allow", probe(entry)]
    cmd += args.lists
    if os.path.exists(args.out):
        os.unlink(args.out)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    result = subprocess.run(cmd)
    if result.returncode != 0:
        if os.path.exists(args.out):
            os.unlink(args.out)
        sys.exit(result.returncode)
    if not os.path.isfile(args.out) or os.path.getsize(args.out) == 0:
        sys.exit("compile_engine: the tool wrote no engine")


if __name__ == "__main__":
    main(sys.argv[1:])
