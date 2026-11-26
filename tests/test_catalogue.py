"""The filter list catalogue file (issue #105).

The catalogue is the fixed set of publisher addresses the build ships. These tests read the file and fail if a
licence is off the allow-list, an address is not https, or a list is on by default. The C++ and the WebUI are
built and tested in the combined build."""

import importlib.util
import json
import re
import unittest
from pathlib import Path
from urllib.parse import urlsplit

REPO = Path(__file__).resolve().parent.parent
COMPONENT = REPO / "overlay/components/aerie_adblock"
CATALOGUE = COMPONENT / "catalogue.json"

# The maintainer's allow-list (issue #105). Written out here, not imported, so a change to the build tool's copy
# does not change what this test accepts.
ALLOWED_LICENCES = {"GPL-3.0", "GPL-3.0-or-CC-BY-SA-3.0", "CC-BY-3.0", "CC-BY-SA-3.0", "MPL-2.0"}
CATEGORIES = ["annoyances", "cookies", "social", "privacy", "regional"]
FIELDS = {"id", "title", "description", "url", "homepage", "licence", "langs", "category", "default_enabled"}
RESERVED_IDS = {"easylist", "easyprivacy", "custom"}  # built-in lists and the user's own rules
EXCLUDED_IDS = {"urlhaus", "peterlowe", "yoyo", "danpollock", "adguardtracking"}  # see the issue and its PR


def load():
    return json.loads(CATALOGUE.read_text(encoding="utf-8"))


def problems(entries):
    """What is wrong with `entries`, as text. Empty when the catalogue is fine."""
    out = []
    seen = set()
    for i, e in enumerate(entries):
        name = e.get("id", f"#{i}") if isinstance(e, dict) else f"#{i}"
        if not isinstance(e, dict) or set(e) != FIELDS:
            out.append(f"{name}: fields are not exactly {sorted(FIELDS)}")
            continue
        if not re.fullmatch(r"[a-z0-9]{1,32}", e["id"]):
            out.append(f"{name}: id must be [a-z0-9]{{1,32}}")
        if re.fullmatch(r"c[0-9a-f]{16}", e["id"]) or e["id"] in RESERVED_IDS:
            out.append(f"{name}: id collides with a built-in, custom or own-rules id")
        if e["id"] in seen:
            out.append(f"{name}: duplicate id")
        seen.add(e["id"])
        if e["licence"] not in ALLOWED_LICENCES:
            out.append(f"{name}: licence {e['licence']!r} is not on the allow-list")
        for key in ("url", "homepage"):
            parts = urlsplit(e[key]) if isinstance(e[key], str) else None
            if (not parts or parts.scheme != "https" or not parts.hostname or "." not in parts.hostname
                    or parts.username or parts.password or parts.fragment or len(e[key]) > 2048):
                out.append(f"{name}: {key} is not a plain https address")
        if e["default_enabled"] is not False:
            out.append(f"{name}: default_enabled must be false")
        if e["category"] not in CATEGORIES:
            out.append(f"{name}: unknown category {e['category']!r}")
        if not isinstance(e["langs"], list) or not all(isinstance(x, str) and re.fullmatch(r"[a-z]{2,3}", x) for x in e["langs"]):
            out.append(f"{name}: langs must be a list of language codes")
        elif (e["category"] == "regional") != bool(e["langs"]):
            out.append(f"{name}: only regional lists have langs, and every regional list has some")
        for key in ("title", "description"):
            if not isinstance(e[key], str) or not e[key].strip():
                out.append(f"{name}: {key} is empty")
    return out


class CatalogueFileTest(unittest.TestCase):
    def test_the_file_is_a_non_empty_list(self):
        entries = load()
        self.assertIsInstance(entries, list)
        self.assertGreater(len(entries), 0)

    def test_every_entry_is_fine(self):
        self.assertEqual(problems(load()), [])

    def test_every_licence_is_on_the_allow_list(self):
        self.assertEqual([e["id"] for e in load() if e["licence"] not in ALLOWED_LICENCES], [])

    def test_every_address_is_https(self):
        self.assertEqual([e["id"] for e in load() if urlsplit(e["url"]).scheme != "https"], [])

    def test_nothing_is_on_by_default(self):
        self.assertEqual([e["id"] for e in load() if e["default_enabled"] is not False], [])

    def test_lists_that_may_not_be_shipped_are_absent(self):
        ids = {e["id"] for e in load()}
        self.assertEqual(ids & EXCLUDED_IDS, set())
        hosts = {urlsplit(e["url"]).hostname for e in load()}
        self.assertFalse({h for h in hosts if h and ("abuse.ch" in h or "pgl.yoyo.org" in h)})

    def test_every_category_of_the_page_has_a_list(self):
        self.assertEqual({e["category"] for e in load()}, set(CATEGORIES))

    def test_a_list_is_counted_under_the_download_cap(self):
        # The build cannot know a list's size, but it can refuse a variant that is known to be over 5 MiB.
        self.assertNotIn("3.txt", [urlsplit(e["url"]).path.rsplit("/", 1)[-1] for e in load()
                                   if "adtidy" in e["url"]])


class DocsTest(unittest.TestCase):
    def test_every_publisher_host_is_listed_as_user_initiated_in_the_endpoints_doc(self):
        doc = (REPO / "docs/network-endpoints.md").read_text(encoding="utf-8")
        rows = re.findall(r"^\| `aerie_catalogue_filter_list_download` \| `([^`]+)` \| (.*) \|$", doc, re.M)
        self.assertEqual({host for host, _ in rows}, {urlsplit(e["url"]).hostname for e in load()})
        for host, why in rows:
            self.assertIn("User-initiated", why, host)

    def test_the_generated_allowlist_has_the_same_hosts(self):
        inc = (REPO / "overlay/services/network/aerie/request_allowlist.inc").read_text(encoding="utf-8")
        hosts = set(re.findall(r'\{"aerie_catalogue_filter_list_download", "([^"]+)"\}', inc))
        self.assertEqual(hosts, {urlsplit(e["url"]).hostname for e in load()})

    def test_the_privacy_policy_names_every_publisher(self):
        text = (REPO / "PRIVACY.md").read_text(encoding="utf-8")
        for host in {urlsplit(e["url"]).hostname for e in load()}:
            self.assertIn(f"`{host}`", text, host)


class CheckerTest(unittest.TestCase):
    """The check above must fail on a bad entry. Each case changes one field of a good one."""

    GOOD = {"id": "examplelist", "title": "Example", "description": "An example list.",
            "url": "https://lists.example/example.txt", "homepage": "https://lists.example/",
            "licence": "GPL-3.0", "langs": [], "category": "privacy", "default_enabled": False}

    def with_change(self, **change):
        return problems([{**self.GOOD, **change}])

    def test_a_good_entry_has_no_problem(self):
        self.assertEqual(problems([self.GOOD]), [])

    def test_a_licence_off_the_list_is_refused(self):
        self.assertTrue(self.with_change(licence="CC0-1.0"))
        self.assertTrue(self.with_change(licence="proprietary"))

    def test_an_address_that_is_not_https_is_refused(self):
        for url in ("http://lists.example/a.txt", "ftp://lists.example/a.txt", "//lists.example/a.txt",
                    "https://user:pw@lists.example/a.txt", "https://lists.example/a.txt#x"):
            self.assertTrue(self.with_change(url=url), url)

    def test_an_entry_on_by_default_is_refused(self):
        self.assertTrue(self.with_change(default_enabled=True))
        self.assertTrue(self.with_change(default_enabled="false"))

    def test_a_bad_id_or_a_reserved_id_is_refused(self):
        for bad in ("Has-Caps", "", "x" * 33, "easylist", "custom", "c0123456789abcdef"):
            self.assertTrue(self.with_change(id=bad), bad)

    def test_an_extra_or_missing_field_is_refused(self):
        self.assertTrue(problems([{**self.GOOD, "extra": 1}]))
        self.assertTrue(problems([{k: v for k, v in self.GOOD.items() if k != "licence"}]))


class BuildToolTest(unittest.TestCase):
    """generate_catalogue.py turns the file into the C++ table and applies the same rules at build time."""

    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location("generate_catalogue", COMPONENT / "generate_catalogue.py")
        cls.tool = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.tool)

    def test_the_tool_uses_the_same_allow_list(self):
        self.assertEqual(set(self.tool.ALLOWED_LICENCES), ALLOWED_LICENCES)

    def test_the_tool_accepts_the_catalogue_and_refuses_a_bad_entry(self):
        self.assertEqual(self.tool.validate(load()), [])
        bad = [{**CheckerTest.GOOD, "licence": "CC0-1.0"}]
        self.assertTrue(self.tool.validate(bad))

    def test_the_table_has_every_id_and_url(self):
        text = self.tool.render(load())
        for e in load():
            self.assertIn(json.dumps(e["id"]), text)
            self.assertIn(json.dumps(e["url"]), text)


if __name__ == "__main__":
    unittest.main()
