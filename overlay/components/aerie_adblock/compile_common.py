# Copyright 2026 The Aerie Authors
# You can use, redistribute, and/or modify this source code under
# the terms of the GPL-3.0 license that can be found in the LICENSE file.
"""Shared steps of the GN actions compile_engine.py and compile_protection.py.

Both check each list against the committed lock a second time, run a host tool
built from the same crates as the browser, and fail closed: a failed run leaves
no output.
"""

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


def verify_lists(who, lock_path, lists):
    """Exit unless `lists` are exactly the files the lock pins, with the pinned hashes."""
    with open(lock_path, encoding="utf-8") as fh:
        lock = json.load(fh)
    pinned = {entry["file"]: entry["sha256"] for entry in lock["lists"]}
    names = {os.path.basename(p) for p in lists}
    if names != set(pinned):
        sys.exit(f"{who}: lists {sorted(names)} do not match the lock {sorted(pinned)}")
    for path in lists:
        want = pinned[os.path.basename(path)]
        got = sha256(path)
        if got != want:
            sys.exit(f"{who}: {path} has sha256 {got}, the lock says {want}")


def run_tool(who, cmd, out):
    """Run the tool that writes `out`; remove `out` and exit if it fails or writes nothing."""
    if os.path.exists(out):
        os.unlink(out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    result = subprocess.run(cmd)
    if result.returncode != 0:
        if os.path.exists(out):
            os.unlink(out)
        sys.exit(result.returncode)
    if not os.path.isfile(out) or os.path.getsize(out) == 0:
        sys.exit(f"{who}: the tool wrote no engine")
