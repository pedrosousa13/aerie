"""The build action that compiles the phishing list (compile_protection.py), and the committed self-checks.

The real tool needs cargo; with AERIE_TEST_CARGO=1 it is built and run on the committed list,
so the self-check hosts are proven against the real Phishing Army file.
"""

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
COMPONENT = REPO / "overlay" / "components" / "aerie_adblock"
SCRIPT = COMPONENT / "compile_protection.py"
LISTS = REPO / "overlay" / "third_party" / "filter_lists"
CARGO = os.environ.get("AERIE_TEST_CARGO") == "1" and shutil.which("cargo") is not None


def run_script(*args):
    return subprocess.run([sys.executable, "-I", str(SCRIPT), *args], capture_output=True, text=True)


class ActionTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.tmp = Path(self._tmp.name)
        self.data = self.tmp / "phishing_army.txt"
        self.data.write_bytes(b"# Title: Phishing Army\nevil.example\n")
        self.lock = self.tmp / "lock.json"
        self.set_lock(hashlib.sha256(self.data.read_bytes()).hexdigest())
        self.selfcheck = self.tmp / "selfcheck.json"
        self.selfcheck.write_text(json.dumps({"listed": ["evil.example"], "unlisted": ["good.example"]}))
        self.tool = self.tmp / "tool"
        self.out = self.tmp / "out" / "engine.dat"

    def set_lock(self, sha):
        self.lock.write_text(json.dumps({"lists": [{"file": "phishing_army.txt", "sha256": sha}]}))

    def tool_script(self, body):
        self.tool.write_text("#!/bin/sh\n" + body)
        self.tool.chmod(0o755)

    def go(self):
        return run_script("--tool", str(self.tool), "--out", str(self.out), "--selfcheck", str(self.selfcheck),
                          "--lock", str(self.lock), "--min-entries", "7", "--list", str(self.data))

    def test_a_list_that_differs_from_the_lock_stops_before_the_tool_runs(self):
        self.set_lock("1" * 64)
        self.tool_script("touch %s/ran\nexit 0\n" % self.tmp)
        proc = self.go()
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("lock says", proc.stderr)
        self.assertFalse((self.tmp / "ran").exists())
        self.assertFalse(self.out.exists())

    def test_the_tool_gets_the_checks_the_floor_and_the_list(self):
        record = self.tmp / "args"
        self.tool_script(f'echo "$@" > {record}\nout=""\nwhile [ $# -gt 0 ]; do [ "$1" = --out ] && out="$2"; shift; done\n'
                         'echo data > "$out"\n')
        proc = self.go()
        self.assertEqual(proc.returncode, 0, proc.stderr)
        args = record.read_text()
        for part in ("--expect-listed evil.example", "--expect-unlisted good.example", "--min-entries 7",
                     str(self.data)):
            self.assertIn(part, args)

    def test_a_failing_tool_leaves_no_output(self):
        self.out.parent.mkdir(parents=True)
        self.out.write_text("stale")
        self.tool_script("exit 3\n")
        self.assertEqual(self.go().returncode, 3)
        self.assertFalse(self.out.exists())

    def test_a_tool_that_writes_nothing_fails(self):
        self.tool_script("exit 0\n")
        proc = self.go()
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("wrote no engine", proc.stderr)

    def test_a_selfcheck_file_without_both_kinds_fails(self):
        self.selfcheck.write_text(json.dumps({"listed": ["evil.example"], "unlisted": []}))
        self.tool_script("exit 0\n")
        self.assertIn("needs both", self.go().stderr)

    def test_lists_must_match_the_lock_by_name(self):
        self.lock.write_text(json.dumps({"lists": [{"file": "other.txt", "sha256": "0" * 64}]}))
        self.tool_script("exit 0\n")
        self.assertIn("do not match the lock", self.go().stderr)


class RepoTest(unittest.TestCase):
    def test_the_gn_action_uses_the_committed_files_and_the_resource_is_packed(self):
        gn = (COMPONENT / "BUILD.gn").read_text()
        self.assertIn("$_lists_dir/phishing_army.txt", gn)
        self.assertIn("$_lists_dir/protection_lists.lock.json", gn)
        self.assertIn(":protection_engine", gn)
        grd = (COMPONENT / "resources" / "aerie_adblock_resources.grd").read_text()
        self.assertIn("IDR_AERIE_PROTECTION_ENGINE", grd)
        self.assertIn("aerie_protection_engine.dat", grd)

    def test_the_selfcheck_file_names_listed_and_unlisted_hosts(self):
        doc = json.loads((COMPONENT / "protection_selfcheck.json").read_text())
        self.assertTrue(doc["listed"] and doc["unlisted"])
        self.assertFalse(set(doc["listed"]) & set(doc["unlisted"]))

    def test_the_floor_in_gn_is_below_the_committed_list_size(self):
        import re
        floor = int(re.search(r'"--min-entries",\s*"(\d+)"', (COMPONENT / "BUILD.gn").read_text()).group(1))
        lines = [line for line in (LISTS / "phishing_army.txt").read_text().splitlines()
                 if line.strip() and not line.startswith("#")]
        self.assertGreater(len(lines), floor)
        self.assertLess(floor, len(lines))


@unittest.skipUnless(CARGO, "set AERIE_TEST_CARGO=1 (needs cargo and crates.io)")
class RealToolTest(unittest.TestCase):
    def test_the_committed_list_compiles_and_passes_the_committed_self_checks(self):
        with tempfile.TemporaryDirectory() as tmp:
            env = dict(os.environ, CARGO_TARGET_DIR=str(Path(tmp) / "target"))
            build = subprocess.run(
                ["cargo", "build", "--release", "--manifest-path", str(COMPONENT / "rust" / "Cargo.toml"),
                 "-p", "aerie_adblock_compile", "--bin", "aerie_protection_compile"],
                capture_output=True, text=True, env=env)
            self.assertEqual(build.returncode, 0, build.stderr[-3000:])
            tool = Path(tmp) / "target" / "release" / "aerie_protection_compile"
            out = Path(tmp) / "engine.dat"
            proc = run_script("--tool", str(tool), "--out", str(out), "--selfcheck",
                              str(COMPONENT / "protection_selfcheck.json"), "--lock",
                              str(LISTS / "protection_lists.lock.json"), "--min-entries", "50000", "--list",
                              str(LISTS / "phishing_army.txt"))
            self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
            self.assertIn("domain(s)", proc.stdout)
            self.assertGreater(out.stat().st_size, 1_000_000)
            # An unlisted host that is in the list would be caught: a poisoned copy fails the build.
            poisoned = Path(tmp) / "poisoned.txt"
            poisoned.write_bytes((LISTS / "phishing_army.txt").read_bytes() + b"example.com\n")
            lock = Path(tmp) / "lock.json"
            lock.write_text(json.dumps({"lists": [{"file": "poisoned.txt",
                                                   "sha256": hashlib.sha256(poisoned.read_bytes()).hexdigest()}]}))
            bad = run_script("--tool", str(tool), "--out", str(Path(tmp) / "bad.dat"), "--selfcheck",
                             str(COMPONENT / "protection_selfcheck.json"), "--lock", str(lock),
                             "--min-entries", "50000", "--list", str(poisoned))
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn("must not be listed", bad.stderr)
            self.assertFalse((Path(tmp) / "bad.dat").exists())


if __name__ == "__main__":
    unittest.main()
