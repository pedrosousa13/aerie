import json
import socket
import tempfile
import unittest
import urllib.error
import urllib.request
from pathlib import Path

from aerie_bench import fixtures, profile


class ProfileTest(unittest.TestCase):
    def test_created_private_and_deleted_after(self):
        with tempfile.TemporaryDirectory() as root:
            with profile.fresh_profile(Path(root)) as p:
                self.assertTrue(p.path.is_dir())
                self.assertEqual(p.path.stat().st_mode & 0o777, 0o700)
                (p.path / "junk").write_text("x")
            self.assertFalse(p.path.exists())
            self.assertTrue(p.deleted)

    def test_deleted_after_exception_and_interrupt(self):
        for exc in (RuntimeError, KeyboardInterrupt):
            with tempfile.TemporaryDirectory() as root:
                with self.assertRaises(exc):
                    with profile.fresh_profile(Path(root)) as p:
                        raise exc()
                self.assertFalse(p.path.exists())

    def test_each_profile_is_a_new_directory(self):
        with tempfile.TemporaryDirectory() as root:
            with profile.fresh_profile(Path(root)) as a, profile.fresh_profile(Path(root)) as b:
                self.assertNotEqual(a.path, b.path)

    def test_seed_prefs_writes_valid_json(self):
        with tempfile.TemporaryDirectory() as root:
            with profile.fresh_profile(Path(root)) as p:
                p.seed_prefs({"helium": {"services": {"enabled": True}}})
                data = json.loads((p.path / "Default" / "Preferences").read_text())
                self.assertTrue(data["helium"]["services"]["enabled"])


class FixtureServerTest(unittest.TestCase):
    def test_binds_loopback_only(self):
        with fixtures.FixtureServer() as s:
            self.assertEqual(s.bind_host, "127.0.0.1")
            self.assertTrue(s.base_url.startswith("http://127.0.0.1:"))

    def test_not_reachable_on_other_interfaces(self):
        with fixtures.FixtureServer() as s:
            others = set()
            try:  # UDP connect sends no packet; it only asks the kernel for the source address
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as u:
                    u.connect(("192.0.2.1", 9))
                    others.add(u.getsockname()[0])
            except OSError:
                pass
            others -= {"127.0.0.1", "0.0.0.0"}
            if not others:
                self.skipTest("no non-loopback IPv4 address on this host")
            addr = sorted(others)[0]
            with self.assertRaises(OSError):
                socket.create_connection((addr, s.port), timeout=2).close()

    def test_serves_fixture_and_records_request(self):
        with fixtures.FixtureServer() as s:
            body = urllib.request.urlopen(s.base_url + "/index.html?x=1").read().decode()
            self.assertIn("<title>Aerie Bench Fixture</title>", body)
            (req,) = s.snapshot()
            self.assertEqual(req["path"], "/index.html?x=1")
            self.assertEqual(req["status"], 200)
            self.assertEqual(req["host"], f"127.0.0.1:{s.port}")

    def test_unknown_path_is_404_but_recorded(self):
        with fixtures.FixtureServer() as s:
            with self.assertRaises(urllib.error.HTTPError) as cm:
                urllib.request.urlopen(s.base_url + "/bangs.json")
            self.assertEqual(cm.exception.code, 404)
            self.assertEqual(s.snapshot()[0]["path"], "/bangs.json")

    def test_cannot_escape_the_fixture_dir(self):
        with fixtures.FixtureServer() as s:
            with socket.create_connection(("127.0.0.1", s.port)) as c:
                c.sendall(b"GET /../test_bench_profile_fixtures.py HTTP/1.0\r\n\r\n")
                data = c.recv(4096)
            self.assertIn(b" 404 ", data.split(b"\r\n")[0])

    def test_fixtures_reference_nothing_external(self):
        import re
        for f in fixtures.FIXTURE_DIR.iterdir():
            if f.suffix in (".html", ".css", ".js"):
                urls = re.findall(r"(?:https?:)?//[^\s\"')]+", f.read_text())
                for u in urls:
                    self.assertTrue(u.startswith(("http://127.0.0.1", "http://localhost")), f"{f.name}: {u}")


if __name__ == "__main__":
    unittest.main()
