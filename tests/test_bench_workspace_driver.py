"""The real Workspace driver (issue #73): PNG colour reading and the calls it makes to chrome://workspace-test."""

import json
import struct
import unittest
import zlib
from types import SimpleNamespace
from unittest import mock

from aerie_bench import workspace_driver as wd
from aerie_bench.cdp import CdpError


def png(width, height, pixel, ctype=2, filter_type=0, background=(255, 255, 255)):
    """A PNG whose pixels come from ``pixel(x, y)`` (an (r, g, b) tuple); every row uses one filter type."""
    bpp = 3 if ctype == 2 else 4
    rows, prev = [], bytearray(width * bpp)
    for y in range(height):
        line = bytearray()
        for x in range(width):
            line += bytes(pixel(x, y)) + (b"\xff" if ctype == 6 else b"")
        out = bytearray()
        for i, byte in enumerate(line):
            left = line[i - bpp] if i >= bpp else 0
            up = prev[i]
            upleft = prev[i - bpp] if i >= bpp else 0
            if filter_type == 0:
                pred = 0
            elif filter_type == 1:
                pred = left
            elif filter_type == 2:
                pred = up
            elif filter_type == 3:
                pred = (left + up) // 2
            else:
                p = left + up - upleft
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - upleft)
                pred = left if pa <= pb and pa <= pc else (up if pb <= pc else upleft)
            out.append((byte - pred) & 255)
        rows.append(bytes([filter_type]) + bytes(out))
        prev = line

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, ctype, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b""))


def red_on_white(x, y):
    return (255, 0, 0) if 4 <= x < 12 and 4 <= y < 12 else (255, 255, 255)


class PngColourTest(unittest.TestCase):
    def test_reads_the_colour_of_a_shape_on_a_white_page_with_every_filter(self):
        for ftype in range(5):
            for ctype in (2, 6):
                with self.subTest(filter=ftype, colour_type=ctype):
                    self.assertEqual(wd.png_dominant_colour(png(16, 16, red_on_white, ctype, ftype)), (255, 0, 0))

    def test_an_all_white_image_has_no_colour(self):
        self.assertIsNone(wd.png_dominant_colour(png(8, 8, lambda x, y: (255, 255, 255))))

    def test_anti_aliasing_does_not_beat_the_solid_colour(self):
        def pixel(x, y):
            if 4 <= x < 12 and 4 <= y < 12:
                return (0, 0, 255)
            return (180, 180, 255) if 3 <= x < 13 and 3 <= y < 13 else (255, 255, 255)
        self.assertEqual(wd.png_dominant_colour(png(16, 16, pixel)), (0, 0, 255))

    def test_rejects_what_it_cannot_read(self):
        with self.assertRaises(ValueError):
            wd.png_dominant_colour(b"not a png at all")
        with self.assertRaises(ValueError):
            wd.png_dominant_colour(b"\x89PNG\r\n\x1a\nFAKE")


class StubSession:
    def __init__(self, answers=None):
        self.answers, self.js = answers or {}, []
        self.timeout, self.client = 1.0, mock.Mock()
        self.client.send.return_value = {"targetInfos": []}

    def evaluate(self, expression, await_promise=False, user_gesture=False):
        self.js.append(expression)
        for needle, value in self.answers.items():
            if needle in expression:
                if isinstance(value, Exception):
                    raise value
                return value
        return None

    def navigate_url(self, url, wait=None):
        self.js.append("navigate " + url)
        return {"url": url, "error": None}

    def wait_for(self, expression, timeout=15.0, interval=0.25):
        return self.evaluate(expression)


class ChromeDriverTest(unittest.TestCase):
    def driver(self, **answers):
        session = StubSession(answers)
        return wd.ChromeDriver(session, SimpleNamespace()), session

    def test_unavailable_when_the_test_page_is_not_served(self):
        driver, _ = self.driver()
        why = driver.available()
        self.assertIn("chrome://workspace-test", why)
        self.assertIn("--aerie-test-workspaces", why)

    def test_available_when_the_page_defines_workspaceTest(self):
        driver, session = self.driver(**{"typeof workspaceTest": True})
        self.assertIsNone(driver.available())
        self.assertEqual(session.js[0], "navigate chrome://workspace-test/")

    def test_a_cdp_error_is_a_reason_not_an_exception(self):
        driver, session = self.driver()
        session.navigate_url = mock.Mock(side_effect=CdpError("boom"))
        self.assertIn("boom", driver.available())

    def test_calls_quote_every_argument(self):
        driver, session = self.driver()
        driver.activate_workspace('ws"1')
        driver.set_permission("http://localhost:1", "notifications", "allow", "ws1")
        driver.history("ws1")
        driver.omnibox("ws2", "a'b")
        self.assertEqual(session.js, [
            'workspaceTest.activateWorkspace("ws\\"1")',
            'workspaceTest.setPermission("http://localhost:1", "notifications", "allow", "ws1")',
            'workspaceTest.history("ws1")',
            'workspaceTest.omnibox("ws2", "a\'b")'])

    def test_history_and_omnibox_default_to_empty(self):
        driver, _ = self.driver()
        self.assertEqual((driver.history("ws1"), driver.omnibox("ws1", "x")), ([], []))

    def test_open_diffs_the_page_targets_to_find_the_new_tab(self):
        driver, session = self.driver()
        answers = iter([[{"targetId": "P0", "type": "page"}],
                        [{"targetId": "P0", "type": "page"}, {"targetId": "P1", "type": "page"}]])
        session.client.send.side_effect = lambda method, *a, **k: (
            {"targetInfos": next(answers)} if method == "Target.getTargets" else {"sessionId": "S", "result": {"value": True}})
        tab = driver.open("http://localhost:1/ws/probe.html", "ws1")
        self.assertEqual(tab.target_id, "P1")
        self.assertIn('workspaceTest.open("http://localhost:1/ws/probe.html", "ws1", false)', session.js)

    def test_a_tab_reports_its_workspace_and_pid_from_list(self):
        driver, _ = self.driver(**{"workspaceTest.list()": [
            {"index": 0, "targetId": "P1", "url": "x", "workspace": "ws2", "pid": 77}]})
        tab = wd.ChromeTab(driver, "P1")
        self.assertEqual((tab.workspace, tab.pid), ("ws2", 77))
        with self.assertRaises(wd.DriverError):
            wd.ChromeTab(driver, "P9").workspace


if __name__ == "__main__":
    unittest.main()
