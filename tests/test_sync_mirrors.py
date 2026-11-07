"""tools/sync-mirrors.sh against local bare repos: the mirror must survive upstream rewrites."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent.parent / "tools" / "sync-mirrors.sh"
GIT_ENV = {
    "GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@example.invalid",
    "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@example.invalid",
}


def git(cwd, *args):
    env = {**os.environ, **GIT_ENV}
    return subprocess.run(["git", *args], cwd=cwd, env=env, check=True, capture_output=True, text=True).stdout.strip()


class SyncMirrorsTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)
        self.up_base = self.root / "up"
        self.mirror_base = self.root / "mirror"
        self.mirror_base.mkdir()
        self.work = {}
        for upstream, mirror in (("imputnet/helium", "helium-mirror"), ("imputnet/helium-linux", "helium-linux-mirror")):
            bare = self.up_base / f"{upstream}.git"
            bare.parent.mkdir(parents=True, exist_ok=True)
            git(self.root, "init", "-q", "--bare", "-b", "main", str(bare))
            git(self.root, "init", "-q", "--bare", str(self.mirror_base / f"{mirror}.git"))
            work = self.root / f"work-{mirror}"
            git(self.root, "clone", "-q", str(bare), str(work))
            git(work, "checkout", "-q", "-b", "main")
            self.commit(work, "a")
            git(work, "tag", "v1")
            git(work, "push", "-q", "origin", "main", "v1")
            self.work[mirror] = work
        self.w = self.work["helium-mirror"]

    def commit(self, work, name):
        (work / name).write_text(name)
        git(work, "add", name)
        git(work, "commit", "-q", "-m", name)
        return git(work, "rev-parse", "HEAD")

    def sync(self, date="20260101"):
        env = {**os.environ, "UPSTREAM_BASE": f"file://{self.up_base}/",
               "MIRROR_PUSH_BASE": f"{self.mirror_base}/", "MIRROR_DATE": date}
        return subprocess.run([str(SCRIPT)], env=env, capture_output=True, text=True)

    def mirror(self, *args):
        return git(self.mirror_base / "helium-mirror.git", *args)

    def refs(self):
        return self.mirror("for-each-ref", "--format=%(refname) %(objectname)").splitlines()

    def test_first_sync_copies_branches_and_tags(self):
        result = self.sync()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.mirror("rev-parse", "main"), git(self.w, "rev-parse", "HEAD"))
        self.assertEqual(self.mirror("rev-parse", "v1^{commit}"), git(self.w, "rev-parse", "HEAD"))

    def test_fast_forward_is_followed_without_backup(self):
        self.sync()
        tip = self.commit(self.w, "b")
        git(self.w, "push", "-q", "origin", "main")
        self.assertEqual(self.sync().returncode, 0)
        self.assertEqual(self.mirror("rev-parse", "main"), tip)
        self.assertFalse([r for r in self.refs() if "refs/backup" in r])

    def test_deleted_upstream_branch_and_tag_stay_in_the_mirror(self):
        git(self.w, "push", "-q", "origin", "main:feature")
        git(self.w, "tag", "v2")
        git(self.w, "push", "-q", "origin", "v2")
        self.sync()
        git(self.w, "push", "-q", "origin", ":feature", ":refs/tags/v2")
        result = self.sync()
        self.assertEqual(result.returncode, 0, result.stderr)
        names = [r.split()[0] for r in self.refs()]
        self.assertIn("refs/heads/feature", names)
        self.assertIn("refs/tags/v2", names)

    def test_force_pushed_branch_keeps_old_tip_in_dated_backup(self):
        self.sync()
        old = self.mirror("rev-parse", "main")
        git(self.w, "reset", "-q", "--hard", "HEAD~0")
        git(self.w, "checkout", "-q", "--orphan", "rewrite")
        new = self.commit(self.w, "rewritten")
        git(self.w, "push", "-q", "--force", "origin", "rewrite:main")
        result = self.sync("20260202")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.mirror("rev-parse", f"refs/backup/20260202/main-{old[:12]}"), old)
        self.assertEqual(self.mirror("rev-parse", "main"), new)

    def test_moved_upstream_tag_is_not_forced_in_the_mirror(self):
        self.sync()
        old = self.mirror("rev-parse", "v1^{commit}")
        new = self.commit(self.w, "c")
        git(self.w, "tag", "-f", "v1")
        git(self.w, "push", "-q", "--force", "origin", "v1", "main")
        result = self.sync("20260303")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.mirror("rev-parse", "v1^{commit}"), old)
        self.assertEqual(self.mirror("rev-parse", f"refs/backup/20260303/moved-tags/v1-{new[:12]}^{{commit}}"), new)

    def rewrite(self, name):
        git(self.w, "checkout", "-q", "--orphan", name)
        tip = self.commit(self.w, name)
        git(self.w, "push", "-q", "--force", "origin", f"{name}:main")
        return tip

    def test_two_rewrites_on_the_same_day_keep_both_old_tips(self):
        self.sync("20260404")
        first = self.mirror("rev-parse", "main")
        second = self.rewrite("one")
        self.assertEqual(self.sync("20260404").returncode, 0)
        third = self.rewrite("two")
        result = self.sync("20260404")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.mirror("rev-parse", f"refs/backup/20260404/main-{first[:12]}"), first)
        self.assertEqual(self.mirror("rev-parse", f"refs/backup/20260404/main-{second[:12]}"), second)
        self.assertEqual(self.mirror("rev-parse", "main"), third)

    def test_branch_does_not_move_when_its_backup_cannot_be_pushed(self):
        self.sync("20260505")
        old = self.mirror("rev-parse", "main")
        # Make the backup ref's name taken by an unrelated commit, so that push is rejected.
        git(self.w, "checkout", "-q", "--orphan", "other")
        other = self.commit(self.w, "other")
        git(self.w, "push", "-q", "origin", f"other:refs/heads/other")
        git(self.mirror_base / "helium-mirror.git", "fetch", "-q", str(self.up_base / "imputnet/helium.git"), "refs/heads/other:refs/heads/other")
        git(self.mirror_base / "helium-mirror.git", "update-ref", f"refs/backup/20260505/main-{old[:12]}", other)
        git(self.w, "checkout", "-q", "--orphan", "rw")
        self.commit(self.w, "rw")
        git(self.w, "push", "-q", "--force", "origin", "rw:main")
        result = self.sync("20260505")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.mirror("rev-parse", "main"), old)

    def test_moved_tag_backups_are_unique_too(self):
        self.sync("20260606")
        for name in ("c", "d"):
            new = self.commit(self.w, name)
            git(self.w, "tag", "-f", "v1")
            git(self.w, "push", "-q", "--force", "origin", "v1", "main")
            self.assertEqual(self.sync("20260606").returncode, 0)
            self.assertEqual(self.mirror("rev-parse", f"refs/backup/20260606/moved-tags/v1-{new[:12]}^{{commit}}"), new)

    def auth_wrapper(self):
        """A git that rejects any fetch, ls-remote or push to the mirror without the token env."""
        real = subprocess.run(["which", "git"], capture_output=True, text=True, check=True).stdout.strip()
        bindir = self.root / "bin"
        bindir.mkdir(exist_ok=True)
        wrapper = bindir / "git"
        wrapper.write_text(f"""#!/usr/bin/env bash
case " $* " in
  *" fetch "*|*" ls-remote "*|*" push "*)
    if [[ "$*" == *"{self.mirror_base}"* && "${{GIT_CONFIG_VALUE_0:-}}" != *"x-access-token"* && "${{GIT_CONFIG_VALUE_0:-}}" != *"AUTHORIZATION"* ]]; then
      echo "fatal: Authentication failed" >&2; exit 128
    fi;;
esac
exec {real} "$@"
""")
        wrapper.chmod(0o755)
        return str(bindir)

    def sync_private(self, token, date="20260707"):
        env = {**os.environ, "UPSTREAM_BASE": f"file://{self.up_base}/",
               "MIRROR_PUSH_BASE": f"{self.mirror_base}/", "MIRROR_DATE": date,
               "PATH": self.auth_wrapper() + os.pathsep + os.environ["PATH"]}
        if token:
            env["MIRROR_TOKEN"] = token
        return subprocess.run([str(SCRIPT)], env=env, capture_output=True, text=True)

    def test_private_mirror_is_read_with_the_token_so_backups_still_happen(self):
        self.assertEqual(self.sync_private("s3cret").returncode, 0)
        old = self.mirror("rev-parse", "main")
        self.rewrite("rw")
        result = self.sync_private("s3cret")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.mirror("rev-parse", f"refs/backup/20260707/main-{old[:12]}"), old)
        self.assertNotIn("s3cret", result.stdout + result.stderr)

    def test_unreadable_mirror_fails_the_sync_and_pushes_nothing(self):
        self.sync()
        before = self.refs()
        self.rewrite("rw")
        result = self.sync_private(None)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("cannot read mirror", result.stderr)
        self.assertEqual(self.refs(), before)

    def test_unreachable_upstream_changes_nothing(self):
        self.sync()
        before = self.refs()
        env = {**os.environ, "UPSTREAM_BASE": f"file://{self.root}/nowhere/",
               "MIRROR_PUSH_BASE": f"{self.mirror_base}/"}
        result = subprocess.run([str(SCRIPT)], env=env, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.refs(), before)

    def test_token_is_not_on_the_command_line(self):
        self.assertNotIn(" -c ", SCRIPT.read_text().replace("git -C", ""))
        self.assertIn("GIT_CONFIG_KEY_0", SCRIPT.read_text())


if __name__ == "__main__":
    unittest.main()
