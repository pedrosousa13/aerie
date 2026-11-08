"""The Workspace test site (issue #73): the fixture server's /ws/ routes."""

import base64
import http.client
import json
import shutil
import ssl
import unittest

from aerie_bench.fixtures import FixtureServer


def get(server, path, headers=None, keep=False):
    conn = http.client.HTTPConnection("127.0.0.1", server.port, timeout=5)
    conn.request("GET", path, headers=headers or {})
    resp = conn.getresponse()
    body = resp.read()
    if not keep:
        conn.close()
    return resp.status, {k.lower(): v for k, v in resp.getheaders()}, body, conn


class SiteRoutesTest(unittest.TestCase):
    def setUp(self):
        self.server = FixtureServer().start()
        self.addCleanup(self.server.stop)

    def test_login_sets_a_session_cookie_for_the_user(self):
        status, headers, _, _ = get(self.server, "/ws/login?user=A")
        self.assertEqual(status, 200)
        self.assertEqual(headers["set-cookie"], "ws_user=A; Path=/; SameSite=Lax")

    def test_whoami_echoes_the_cookie_the_server_received(self):
        _, _, body, _ = get(self.server, "/ws/whoami", {"Cookie": "ws_user=B"})
        self.assertEqual(json.loads(body), {"user": "B"})
        _, _, body, _ = get(self.server, "/ws/whoami")
        self.assertEqual(json.loads(body), {"user": ""})

    def test_cached_resource_is_cacheable_and_can_be_slow(self):
        import time
        start = time.monotonic()
        _, headers, body, _ = get(self.server, "/ws/cached?id=x1&delay_ms=200")
        self.assertGreaterEqual(time.monotonic() - start, 0.18)
        self.assertEqual(body, b"x1")
        self.assertIn("max-age", headers["cache-control"])
        self.assertNotIn("no-store", headers["cache-control"])

    def test_delay_is_capped(self):
        import time
        start = time.monotonic()
        get(self.server, "/ws/cached?id=x2&delay_ms=999999")
        self.assertLess(time.monotonic() - start, 3.0)

    def test_basic_auth_challenges_then_accepts_and_the_probe_never_challenges(self):
        status, headers, _, _ = get(self.server, "/ws/auth/t1/login")
        self.assertEqual(status, 401)
        self.assertIn("Basic", headers["www-authenticate"])
        token = base64.b64encode(b"u:p").decode()
        status, _, _, _ = get(self.server, "/ws/auth/t1/login", {"Authorization": "Basic " + token})
        self.assertEqual(status, 200)
        status, _, body, _ = get(self.server, "/ws/auth/t1/probe")
        self.assertEqual((status, json.loads(body)), (200, {"sent": False}))
        _, _, body, _ = get(self.server, "/ws/auth/t1/probe", {"Authorization": "Basic " + token})
        self.assertEqual(json.loads(body), {"sent": True})

    def test_hsts_route_sends_the_header(self):
        _, headers, _, _ = get(self.server, "/ws/hsts")
        self.assertIn("max-age=", headers["strict-transport-security"])

    def test_ws_paths_keep_the_connection_open_other_paths_do_not(self):
        _, _, _, conn = get(self.server, "/ws/ping?w=A", keep=True)
        conn.request("GET", "/ws/ping?w=A")
        self.assertEqual(conn.getresponse().status, 200)  # the same socket served both
        conn.close()
        _, headers, _, conn2 = get(self.server, "/index.html", keep=True)
        self.assertEqual(headers.get("connection", "close").lower(), "close")
        conn2.close()

    def test_a_ws_head_request_has_no_body(self):
        conn = http.client.HTTPConnection("127.0.0.1", self.server.port, timeout=5)
        conn.request("HEAD", "/ws/ping?w=A")
        r = conn.getresponse()
        self.assertEqual(r.read(), b"")
        conn.request("GET", "/ws/ping?w=A")
        self.assertEqual(conn.getresponse().status, 200)
        conn.close()

    def test_requests_log_the_authorization_header(self):
        get(self.server, "/ws/auth/t1/probe", {"Authorization": "Basic abc"})
        self.assertEqual(self.server.snapshot()[-1]["authorization"], "Basic abc")

    def test_static_pages_exist(self):
        for path in ("/ws/probe.html", "/ws/probe.js", "/ws/sw.js", "/ws/shared.js", "/ws/link.html",
                     "/ws/page.html", "/ws/code.js"):
            self.assertEqual(get(self.server, path)[0], 200, path)


@unittest.skipUnless(shutil.which("openssl"), "needs the openssl command")
class TlsTest(unittest.TestCase):
    def test_second_origin_serves_the_hsts_header_over_tls_and_gives_the_spki_pin(self):
        server = FixtureServer().start()
        self.addCleanup(server.stop)
        pin = server.enable_tls()
        self.assertEqual(len(base64.b64decode(pin)), 32)
        self.assertNotEqual(server.tls_port, server.port)
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        conn = http.client.HTTPSConnection("127.0.0.1", server.tls_port, timeout=5, context=ctx)
        conn.request("GET", "/ws/hsts")
        resp = conn.getresponse()
        self.assertEqual(resp.status, 200)
        self.assertIn("max-age", resp.getheader("Strict-Transport-Security"))
        conn.close()
        self.assertTrue(any(r["path"] == "/ws/hsts" for r in server.snapshot()))

    def test_enable_tls_twice_gives_the_same_pin(self):
        server = FixtureServer().start()
        self.addCleanup(server.stop)
        self.assertEqual(server.enable_tls(), server.enable_tls())

    def test_tls_listener_binds_loopback_only(self):
        server = FixtureServer().start()
        self.addCleanup(server.stop)
        server.enable_tls()
        self.assertEqual(server.tls_bind_host, "127.0.0.1")


if __name__ == "__main__":
    unittest.main()
