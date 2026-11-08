import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path

from aerie_build import bump, lock

LS_REMOTE = textwrap.dedent(
    """\
    1111111111111111111111111111111111111111\trefs/heads/main
    2222222222222222222222222222222222222222\trefs/tags/0.21.1.1
    3333333333333333333333333333333333333333\trefs/tags/0.21.1.1^{}
    4444444444444444444444444444444444444444\trefs/tags/0.21.10.1
    5555555555555555555555555555555555555555\trefs/tags/0.21.2.1
    6666666666666666666666666666666666666666\trefs/tags/0.21.2.1-rc1
    7777777777777777777777777777777777777777\trefs/tags/nightly
    """
)

LOCK_TEXT = textwrap.dedent(
    """\
    # Aerie source lock.

    [helium]
    # Helium release this tree builds: helium-linux tag 0.21.1.1.
    version = "0.21.1.1"
    chromium_version = "157.0.8103.38"
    core_tag = "0.21.1"
    core_commit = "ecb36f09c4a4140e4cf4e67e77a3150bf3f3e7eb"
    platform_tag = "0.21.1.1"
    platform_commit = "4da5fdc8f90175592f62b6a3f62e90a65e171789"

    # from the SHA256SUMS of github.com/imputnet/chromium-tarballs release chromium-157.0.8103.38
    [downloads]
    "chromium-157.0.8103.38-base.tar.zst" = "{a}"
    "chromium-157.0.8103.38-linux-x64.tar.zst" = "{b}"
    "nonfree-1.tar.gz" = "{c}"

    [fallback.chromium]
    url = "https://commondatastorage.googleapis.com/chromium-browser-official/chromium-157.0.8103.38.tar.xz"
    hashes_url = "https://commondatastorage.googleapis.com/chromium-browser-official/chromium-157.0.8103.38.tar.xz.hashes"
    sha256 = "{d}"

    [tools.ccache]
    version = "4.14.2"
    """
).format(a="a" * 64, b="b" * 64, c="c" * 64, d="d" * 64)


class TagsTest(unittest.TestCase):
    def test_parse_ls_remote_prefers_the_peeled_commit_of_an_annotated_tag(self):
        tags = bump.parse_ls_remote(LS_REMOTE)
        self.assertEqual(tags["0.21.1.1"], "3" * 40)
        self.assertEqual(tags["0.21.2.1"], "5" * 40)
        self.assertNotIn("main", tags)

    def test_latest_release_compares_numbers_and_ignores_other_tags(self):
        tags = bump.parse_ls_remote(LS_REMOTE)
        self.assertEqual(bump.latest_release(tags), "0.21.10.1")

    def test_latest_release_without_release_tags_is_an_error(self):
        with self.assertRaises(bump.BumpError):
            bump.latest_release({"nightly": "1" * 40})

    def test_core_tag_is_the_first_three_numbers(self):
        self.assertEqual(bump.core_tag_for("0.21.2.1"), "0.21.2")

    def test_resolve_release_needs_the_matching_core_tag(self):
        platform = {"0.22.1.1": "a" * 40}
        with self.assertRaisesRegex(bump.BumpError, "0.22.1"):
            bump.resolve_release(platform, {"0.21.1": "b" * 40})
        rel = bump.resolve_release(platform, {"0.22.1": "b" * 40})
        self.assertEqual((rel.platform_tag, rel.platform_commit, rel.core_tag, rel.core_commit),
                         ("0.22.1.1", "a" * 40, "0.22.1", "b" * 40))

    def test_resolve_release_for_a_named_tag_must_be_an_upstream_tag(self):
        with self.assertRaisesRegex(bump.BumpError, "9.9.9.9"):
            bump.resolve_release({"0.22.1.1": "a" * 40}, {"0.22.1": "b" * 40}, to="9.9.9.9")

    def test_a_tag_that_is_not_a_plain_version_is_refused(self):
        with self.assertRaises(bump.BumpError):
            bump.resolve_release({"--upload-pack=x": "a" * 40}, {}, to="--upload-pack=x")


class NewerTest(unittest.TestCase):
    def test_is_newer(self):
        self.assertTrue(bump.is_newer("0.21.10.1", "0.21.9.3"))
        self.assertFalse(bump.is_newer("0.21.1.1", "0.21.1.1"))
        self.assertFalse(bump.is_newer("0.20.9.9", "0.21.1.1"))


class RewriteLockTest(unittest.TestCase):
    def setUp(self):
        self.release = bump.Release("0.22.1.1", "a" * 40, "0.22.1", "b" * 40)
        self.text = bump.rewrite_lock(
            LOCK_TEXT, self.release, "158.0.1.2",
            downloads={"chromium-158.0.1.2-base.tar.zst": "1" * 64,
                       "chromium-158.0.1.2-linux-x64.tar.zst": "2" * 64,
                       "nonfree-2.tar.gz": "3" * 64},
            fallback_sha256="4" * 64,
        )
        import tomllib
        self.data = tomllib.loads(self.text)

    def test_versions_and_commits(self):
        h = self.data["helium"]
        self.assertEqual(h["version"], "0.22.1.1")
        self.assertEqual(h["chromium_version"], "158.0.1.2")
        self.assertEqual((h["core_tag"], h["core_commit"]), ("0.22.1", "b" * 40))
        self.assertEqual((h["platform_tag"], h["platform_commit"]), ("0.22.1.1", "a" * 40))

    def test_downloads_are_replaced(self):
        self.assertEqual(self.data["downloads"], {
            "chromium-158.0.1.2-base.tar.zst": "1" * 64,
            "chromium-158.0.1.2-linux-x64.tar.zst": "2" * 64,
            "nonfree-2.tar.gz": "3" * 64})

    def test_fallback_is_moved_to_the_new_version(self):
        f = self.data["fallback"]["chromium"]
        self.assertTrue(f["url"].endswith("chromium-158.0.1.2.tar.xz"))
        self.assertTrue(f["hashes_url"].endswith("chromium-158.0.1.2.tar.xz.hashes"))
        self.assertEqual(f["sha256"], "4" * 64)

    def test_other_sections_and_comments_follow(self):
        self.assertEqual(self.data["tools"]["ccache"]["version"], "4.14.2")
        self.assertIn("helium-linux tag 0.22.1.1.", self.text)
        self.assertIn("release chromium-158.0.1.2", self.text)
        self.assertNotIn("157.0.8103.38", self.text)

    def test_rewriting_the_real_lock_with_its_own_values_changes_nothing(self):
        path = Path(__file__).resolve().parents[1] / "helium.lock"
        pins = lock.load_lock(path)
        release = bump.Release(pins.platform_tag, pins.platform_commit, pins.core_tag, pins.core_commit)
        text = path.read_text()
        again = bump.rewrite_lock(text, release, pins.chromium_version, pins.downloads, pins.fallback["sha256"])
        self.assertEqual(again, text)


class PinDownloadsTest(unittest.TestCase):
    def test_digests_come_from_the_ini_or_the_release_sums_and_dropped_sections_are_skipped(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            (d / "downloads.ini").write_text(textwrap.dedent("""\
                [chromium]
                download_filename = chromium-%(_chromium_version)s-base.tar.zst
                """))
            (d / "deps.ini").write_text(textwrap.dedent("""\
                [search]
                download_filename = search-1.tar.gz
                sha256 = %s
                [ublock_origin]
                download_filename = ub.zip
                sha256 = %s
                """ % ("5" * 64, "6" * 64)))
            sums = f"{'7' * 64}  chromium-9.1-base.tar.zst\n{'8' * 64}  other\n"
            pinned = bump.pin_downloads([d / "downloads.ini", d / "deps.ini"], "9.1", sums,
                                        dropped={"ublock_origin"})
        self.assertEqual(pinned, {"chromium-9.1-base.tar.zst": "7" * 64, "search-1.tar.gz": "5" * 64})

    def test_a_download_without_a_digest_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            ini = Path(tmp) / "downloads.ini"
            ini.write_text("[a]\ndownload_filename = x.tar\n")
            with self.assertRaisesRegex(bump.BumpError, "x.tar"):
                bump.pin_downloads([ini], "1", "", dropped=set())


def git(cwd, *args):
    return subprocess.run(["git", "-C", str(cwd), "-c", "user.name=t", "-c", "user.email=t@t.invalid",
                           "-c", "commit.gpgsign=false", "-c", "tag.gpgsign=false", *args],
                          check=True, capture_output=True, text=True).stdout.strip()


class GitTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.upstream = self.root / "upstream"
        self.upstream.mkdir()
        git(self.upstream, "init", "-q", "-b", "main")
        (self.upstream / "f").write_text("1")
        git(self.upstream, "add", "f")
        git(self.upstream, "commit", "-q", "-m", "one")
        git(self.upstream, "tag", "-a", "-m", "release", "0.1.1.1")
        self.commit = git(self.upstream, "rev-parse", "HEAD")
        (self.upstream / "f").write_text("2")
        git(self.upstream, "commit", "-q", "-am", "two")
        self.sub = self.root / "sub"
        self.sub.mkdir()
        git(self.sub, "init", "-q")

    def tearDown(self):
        self._tmp.cleanup()

    def test_remote_tags_are_read_from_the_given_url(self):
        tags = bump.remote_tags(str(self.upstream))
        self.assertEqual(tags, {"0.1.1.1": self.commit})

    def test_checkout_moves_the_submodule_to_the_tag_commit(self):
        bump.checkout_tag(self.sub, str(self.upstream), "0.1.1.1", self.commit)
        self.assertEqual(git(self.sub, "rev-parse", "HEAD"), self.commit)

    def test_checkout_refuses_when_the_fetched_tag_is_another_commit(self):
        with self.assertRaisesRegex(bump.BumpError, "0.1.1.1"):
            bump.checkout_tag(self.sub, str(self.upstream), "0.1.1.1", "9" * 40)
        with self.assertRaises(subprocess.CalledProcessError):
            git(self.sub, "rev-parse", "--verify", "HEAD")


class SubmoduleUrlTest(unittest.TestCase):
    def test_this_repo_points_at_the_pinned_upstreams(self):
        repo = Path(__file__).resolve().parents[1]
        bump.check_gitmodules(repo)

    def test_another_url_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / ".gitmodules").write_text(
                '[submodule "helium-chromium"]\n\tpath = helium-chromium\n\turl = https://example.invalid/helium.git\n'
                '[submodule "helium-linux"]\n\tpath = helium-linux\n\turl = %s\n' % bump.PLATFORM_UPSTREAM)
            with self.assertRaisesRegex(bump.BumpError, "helium-chromium"):
                bump.check_gitmodules(Path(tmp))


class ReportTest(unittest.TestCase):
    def test_summary_names_old_and_new_versions(self):
        text = bump.summary("0.21.1.1", "157.0.8103.38", "0.22.1.1", "158.0.1.2")
        self.assertIn("Helium 0.21.1.1 -> 0.22.1.1", text)
        self.assertIn("Chromium 157.0.8103.38 -> 158.0.1.2", text)


if __name__ == "__main__":
    unittest.main()


class FakeResponse:
    def __init__(self, body: bytes):
        self.body = body

    def read(self):
        return self.body

    def close(self):
        pass


def make_upstream(root: Path, name: str, files: dict[str, str], tag: str) -> Path:
    path = root / name
    path.mkdir()
    git(path, "init", "-q", "-b", "main")
    for rel, text in files.items():
        (path / rel).write_text(text)
    git(path, "add", ".")
    git(path, "commit", "-q", "-m", "release")
    git(path, "tag", "-a", "-m", tag, tag)
    return path


class ApplyTest(unittest.TestCase):
    """The whole bump on a throwaway repo with two submodules and fake upstream and network."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        core_v1 = {"version.txt": "0.3.4\n", "chromium_version.txt": "157.0.8103.38\n", "revision.txt": "1\n",
                   "downloads.ini": "[c]\ndownload_filename = chromium-%(_chromium_version)s-base.tar.zst\n"
                                    "url = https://t.invalid/chromium-%(_chromium_version)s-base.tar.zst\n",
                   "deps.ini": "[search]\ndownload_filename = s-1.tar.gz\nurl = https://t.invalid/s-1.tar.gz\n"
                               "sha256 = %s\n[ublock_origin]\ndownload_filename = u.zip\nurl = https://t.invalid/u.zip\n"
                               "sha256 = %s\n[onboarding]\ndownload_filename = o.tar.gz\nurl = https://t.invalid/o\n"
                               "sha256 = %s\n" % ("5" * 64, "6" * 64, "6" * 64)}
        self.core_up = make_upstream(self.root, "core-up", core_v1, "0.21.1")
        self.plat_up = make_upstream(self.root, "plat-up", {"revision.txt": "1\n"}, "0.21.1.1")
        self.repo = self.root / "repo"
        self.repo.mkdir()
        git(self.repo, "init", "-q", "-b", "main")
        for name, up in (("helium-chromium", self.core_up), ("helium-linux", self.plat_up)):
            git(self.repo, "-c", "protocol.file.allow=always", "submodule", "add", "-q", str(up), name)
        # the 0.22.1 release upstream: Chromium 158
        (self.core_up / "chromium_version.txt").write_text("158.0.1.2\n")
        (self.core_up / "version.txt").write_text("0.3.4\n")
        git(self.core_up, "commit", "-q", "-am", "0.22.1")
        git(self.core_up, "tag", "-a", "-m", "0.22.1", "0.22.1")
        git(self.plat_up, "commit", "-q", "--allow-empty", "-m", "0.22.1.1")
        git(self.plat_up, "tag", "-a", "-m", "0.22.1.1", "0.22.1.1")
        self.pins = {
            "helium-chromium": git(self.repo, "-C", "helium-chromium", "rev-parse", "HEAD"),
            "helium-linux": git(self.repo, "-C", "helium-linux", "rev-parse", "HEAD"),
        }
        (self.repo / "tools").mkdir()
        (self.repo / "helium.lock").write_text(LOCK_TEXT.replace("0.21.1.1", "0.21.1.1").replace(
            "ecb36f09c4a4140e4cf4e67e77a3150bf3f3e7eb", self.pins["helium-chromium"]).replace(
            "4da5fdc8f90175592f62b6a3f62e90a65e171789", self.pins["helium-linux"]).replace(
            '"nonfree-1.tar.gz"', '"s-1.tar.gz"').replace("\n[tools.ccache]\nversion = \"4.14.2\"\n", ""))
        git(self.repo, "add", ".")
        git(self.repo, "commit", "-q", "-m", "pins")
        self.upstreams = {"helium-chromium": str(self.core_up), "helium-linux": str(self.plat_up)}
        self.urls: list[str] = []

        def opener(request):
            self.urls.append(request.full_url)
            if request.full_url.endswith("SHA256SUMS"):
                return FakeResponse(f"{'7' * 64}  chromium-158.0.1.2-base.tar.zst\n".encode())
            return FakeResponse(f"sha256  {'8' * 64}  chromium-158.0.1.2.tar.xz\n".encode())

        self.opener = opener

    def tearDown(self):
        self._tmp.cleanup()

    def release(self):
        return bump.resolve_release(bump.remote_tags(str(self.plat_up)), bump.remote_tags(str(self.core_up)))

    def apply(self):
        return bump.apply(self.repo, self.release(), self.opener, log=lambda _m: None, upstreams=self.upstreams)

    def test_moves_submodules_and_lock_and_reports_versions(self):
        old, new = self.apply()
        self.assertEqual((old.chromium_version, new.chromium_version), ("157.0.8103.38", "158.0.1.2"))
        self.assertEqual((old.helium_version, new.helium_version), ("0.21.1.1", "0.22.1.1"))
        self.assertEqual(git(self.repo, "-C", "helium-linux", "rev-parse", "HEAD"), new.platform_commit)
        self.assertEqual(git(self.repo, "-C", "helium-chromium", "rev-parse", "HEAD"), new.core_commit)
        self.assertEqual(new.downloads, {"chromium-158.0.1.2-base.tar.zst": "7" * 64, "s-1.tar.gz": "5" * 64})
        self.assertEqual(new.fallback["sha256"], "8" * 64)
        self.assertTrue(all(u.startswith("https://") for u in self.urls))

    def test_a_failure_puts_the_submodules_back_and_leaves_the_lock(self):
        before = (self.repo / "helium.lock").read_text()

        def broken(request):
            raise OSError("offline")

        with self.assertRaises(Exception):
            bump.apply(self.repo, self.release(), broken, log=lambda _m: None, upstreams=self.upstreams)
        self.assertEqual((self.repo / "helium.lock").read_text(), before)
        self.assertEqual(git(self.repo, "-C", "helium-chromium", "rev-parse", "HEAD"), self.pins["helium-chromium"])
        self.assertEqual(git(self.repo, "status", "--porcelain"), "")

    def test_uncommitted_pin_changes_stop_the_bump(self):
        (self.repo / "helium.lock").write_text((self.repo / "helium.lock").read_text() + "# edit\n")
        with self.assertRaisesRegex(bump.BumpError, "uncommitted"):
            bump.apply(self.repo, self.release(), self.opener, log=lambda _m: None, upstreams={
                "helium-chromium": git(self.repo, "config", "-f", ".gitmodules", "--get", "submodule.helium-chromium.url"),
                "helium-linux": git(self.repo, "config", "-f", ".gitmodules", "--get", "submodule.helium-linux.url")})


class CommandTest(unittest.TestCase):
    """./aerie bump --check: exit status says whether a newer release exists."""

    def run_check(self, platform_tags, extra=()):
        import contextlib
        import io
        from unittest import mock

        from aerie_build import cli

        core = {"0.20.1": "e" * 40, "0.21.1": "b" * 40, "0.30.1": "c" * 40}
        remote = {bump.PLATFORM_UPSTREAM: platform_tags, bump.CORE_UPSTREAM: core}
        out = io.StringIO()
        with mock.patch.object(bump, "remote_tags", side_effect=lambda url: remote[url]), \
                mock.patch.object(bump, "apply") as apply, contextlib.redirect_stdout(out):
            rc = cli.main(["bump", "--check", *extra])
        apply.assert_not_called()
        return rc, out.getvalue()

    def test_up_to_date_exits_zero(self):
        rc, out = self.run_check({"0.21.1.1": "a" * 40})
        self.assertEqual(rc, bump.OK)
        self.assertIn("up to date", out)

    def test_newer_release_exits_20_and_names_the_tag(self):
        rc, out = self.run_check({"0.21.1.1": "a" * 40, "0.30.1.1": "d" * 40})
        self.assertEqual(rc, bump.UPDATE_AVAILABLE)
        self.assertRegex(out, r"latest\s+0\.30\.1\.1")

    def test_an_older_upstream_release_is_not_an_update(self):
        rc, _ = self.run_check({"0.20.1.1": "a" * 40})
        self.assertEqual(rc, bump.OK)
