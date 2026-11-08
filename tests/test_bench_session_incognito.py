"""Session.incognito: a throwaway off-the-record DevTools browser context, with its own page."""

import unittest
from pathlib import Path

from aerie_bench.session import Session


class StubClient:
    def __init__(self):
        self.calls = []

    def send(self, method, params=None, session_id=None, timeout=None):
        self.calls.append((method, params, session_id))
        if method == "Target.createTarget":
            if params.get("browserContextId"):
                return {"targetId": "OTR-T"}
            return {"targetId": "T1"}
        if method == "Target.attachToTarget":
            return {"sessionId": "S-" + params["targetId"]}
        if method == "Target.createBrowserContext":
            return {"browserContextId": "CTX1"}
        return {}

    def methods(self):
        return [c[0] for c in self.calls]


class IncognitoTest(unittest.TestCase):
    def make(self):
        client = StubClient()
        return client, Session(client, None, Path("."), timeout=1.0)

    def test_incognito_page_calls_go_to_the_incognito_target(self):
        client, session = self.make()
        session.cdp("Page.reload")  # Creates the regular page.
        with session.incognito() as inc:
            inc.cdp("Page.reload")
            session.cdp("Page.reload")  # The regular page stays reachable while incognito lives.
        session.cdp("Page.reload")
        reloads = [c[2] for c in client.calls if c[0] == "Page.reload"]
        self.assertEqual(reloads, ["S-T1", "S-OTR-T", "S-T1", "S-T1"])
        self.assertLess(client.methods().index("Page.reload"), client.methods().index("Target.disposeBrowserContext"))

    def test_target_is_created_in_the_new_context(self):
        client, session = self.make()
        with session.incognito() as inc:
            inc.cdp("Page.reload")
        create = next(c for c in client.calls if c[0] == "Target.createTarget" and c[1].get("browserContextId"))
        self.assertEqual(create[1]["browserContextId"], "CTX1")

    def test_context_is_disposed_on_exit_and_on_error(self):
        client, session = self.make()
        with session.incognito():
            pass
        self.assertIn("Target.disposeBrowserContext", client.methods())
        client, session = self.make()
        session.cdp("Page.reload")
        with self.assertRaises(RuntimeError):
            with session.incognito() as inc:
                inc.cdp("Page.reload")
                raise RuntimeError("boom")
        self.assertIn("Target.disposeBrowserContext", client.methods())
        self.assertEqual(session._sid, "S-T1")

    def test_each_call_is_a_new_context(self):
        client, session = self.make()
        for _ in range(2):
            with session.incognito():
                pass
        self.assertEqual(client.methods().count("Target.createBrowserContext"), 2)
        self.assertEqual(client.methods().count("Target.disposeBrowserContext"), 2)


if __name__ == "__main__":
    unittest.main()
