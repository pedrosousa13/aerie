"""PRIVACY.md must name every host that docs/network-endpoints.md says the browser contacts."""

import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
# Sections of the endpoint page that list hosts the shipped browser contacts.
SECTIONS = ("## Aerie-owned endpoints", "### Only after opting in to Widevine")


def _section(text: str, heading: str) -> str:
    start = text.index(heading)
    nxt = re.search(r"^#{2,3} ", text[start + len(heading):], re.M)
    return text[start: start + len(heading) + nxt.start()] if nxt else text[start:]


def _hosts(section: str) -> set[str]:
    hosts = set()
    for line in section.splitlines():
        if not line.startswith("| ") or line.startswith("| ---"):
            continue
        first = line.split("|")[1]
        for code in re.findall(r"`([^`]+)`", first):
            m = re.match(r"(?:https?://)?([a-z0-9.-]+\.[a-z]{2,})(?:/|$)", code)
            if m:
                hosts.add(m.group(1))
    return hosts


class PrivacyPolicyTest(unittest.TestCase):
    def test_policy_names_every_host_the_browser_contacts(self):
        endpoints = (REPO / "docs" / "network-endpoints.md").read_text(encoding="utf-8")
        policy = (REPO / "PRIVACY.md").read_text(encoding="utf-8")
        hosts = set().union(*(_hosts(_section(endpoints, h)) for h in SECTIONS))
        self.assertIn("easylist.to", hosts)  # the parser still finds the table
        self.assertIn("update.googleapis.com", hosts)
        missing = sorted(h for h in hosts if f"`{h}`" not in policy)
        self.assertEqual(missing, [], "PRIVACY.md does not list these hosts from docs/network-endpoints.md")


if __name__ == "__main__":
    unittest.main()
