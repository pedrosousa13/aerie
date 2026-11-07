import os
import subprocess
import sys
import unittest
from pathlib import Path

from aerie_bench import cdp, launcher

FAKE = Path(__file__).parent / "bench_fake_browser.py"


class FramingTest(unittest.TestCase):
    def test_encode_is_nul_terminated_compact_json(self):
        self.assertEqual(cdp.encode({"id": 1, "method": "A"}), b'{"id":1,"method":"A"}\0')

    def test_decoder_handles_split_and_batched_reads(self):
        d = cdp.Decoder()
        self.assertEqual(d.feed(b'{"id":1}\0{"id"'), [{"id": 1}])
        self.assertEqual(d.feed(b':2}\0\0'), [{"id": 2}])

    def test_decoder_keeps_utf8_split_across_reads(self):
        d = cdp.Decoder()
        raw = '{"v":"é"}\0'.encode()
        self.assertEqual(d.feed(raw[:7]), [])
        self.assertEqual(d.feed(raw[7:]), [{"v": "é"}])


class ClientAgainstFakeBrowserTest(unittest.TestCase):
    def setUp(self):
        self.proc = launcher.spawn([sys.executable, str(FAKE)])
        self.client = self.proc.client

    def tearDown(self):
        self.client.close()
        self.proc.popen.kill()
        self.proc.popen.wait()

    def test_matches_replies_by_id(self):
        r = self.client.send("Target.createTarget", {"url": "about:blank"})
        self.assertEqual(r["targetId"], "T1")

    def test_error_reply_raises(self):
        with self.assertRaises(cdp.CdpError) as cm:
            self.client.send("Bad.method")
        self.assertIn("not found", str(cm.exception))

    def test_timeout(self):
        with self.assertRaises(cdp.CdpTimeout):
            self.client.wait_event("Never.happens", timeout=0.2)

    def test_closed_pipe_raises_instead_of_hanging(self):
        self.client.send("Browser.close")
        self.proc.popen.wait(timeout=5)
        with self.assertRaises(cdp.CdpError):
            self.client.send("Target.createTarget", timeout=2)


class CloseTest(unittest.TestCase):
    def test_close_stops_reader_before_closing_fd(self):
        import os
        r, w = os.pipe()          # nobody writes to r, so the reader blocks
        r2, w2 = os.pipe()
        c = cdp.CdpClient(r, w2)
        c.close()
        self.assertFalse(c._thread.is_alive())
        for fd in (w, r2):
            os.close(fd)


if __name__ == "__main__":
    unittest.main()
