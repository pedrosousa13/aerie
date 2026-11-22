"""Session.close_pages_opened_since: a check that opens popups gives the session's page back (issue #131).

A page that window.open()s makes the popup the foreground tab. The session's own page is then hidden and
never paints, so the next Page.captureScreenshot on it gets no reply (a 30 s CdpTimeout in
cosmetic_filtering, which ran after hardening_tamper's popups).
"""

import unittest
from pathlib import Path

from aerie_bench.session import Session


class FakeClient:
    """A browser with three tabs: the session page created on first use, and what a check opens."""

    def __init__(self):
        self.tabs = [{"targetId": "other", "type": "page"}]
        self.calls = []

    def send(self, method, params=None, session_id=None, timeout=None):
        self.calls.append((method, params, session_id))
        if method == "Target.createTarget":
            self.tabs.append({"targetId": "mine", "type": "page"})
            return {"targetId": "mine"}
        if method == "Target.attachToTarget":
            return {"sessionId": "sid-mine"}
        if method == "Target.getTargets":
            return {"targetInfos": list(self.tabs)}
        if method == "Target.closeTarget":
            self.tabs = [t for t in self.tabs if t["targetId"] != params["targetId"]]
            return {"success": True}
        return {}


class ClosePagesOpenedSinceTest(unittest.TestCase):
    def session(self):
        client = FakeClient()
        return Session(client, fixtures=None, out_dir=Path("."), timeout=1.0), client

    def test_closes_only_the_pages_opened_since_and_brings_the_session_page_to_the_front(self):
        session, client = self.session()
        session._page()
        before = session.page_target_ids()
        client.tabs += [{"targetId": "popup1", "type": "page"}, {"targetId": "popup2", "type": "page"},
                        {"targetId": "worker", "type": "service_worker"}]
        session.close_pages_opened_since(before)
        self.assertEqual({t["targetId"] for t in client.tabs}, {"other", "mine", "worker"})
        self.assertEqual(client.calls[-1][:1] + client.calls[-1][2:], ("Page.bringToFront", "sid-mine"))

    def test_never_closes_the_session_page(self):
        session, client = self.session()
        session._page()
        session.close_pages_opened_since(set())
        self.assertIn("mine", {t["targetId"] for t in client.tabs})


if __name__ == "__main__":
    unittest.main()
