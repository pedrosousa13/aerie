"""The incognito session key is a type, not a bare pointer, and the rules live in the manager (issue #50).

These are source checks: the C++ itself is compiled and unit-tested in the combined build."""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "overlay"
DIRS = [ROOT / "components/aerie_adblock", ROOT / "chrome/browser/aerie_adblock",
        ROOT / "chrome/browser/ui/webui/aerie_filters", ROOT / "chrome/browser/ui/views/aerie_adblock"]


def sources(tests=False):
    for d in DIRS:
        for path in sorted(d.rglob("*")):
            if path.suffix in (".cc", ".h") and (tests or "unittest" not in path.name):
                yield path, re.sub(r"//[^\n]*", "", path.read_text())


class SessionKeyTest(unittest.TestCase):
    def test_no_bare_pointer_session_remains(self):
        bad = [f"{p.name}: {m}" for p, text in sources(tests=True)
               for m in re.findall(r"const void\*\s*(?:const\s*)?\w*session\w*", text, re.I)]
        self.assertEqual(bad, [])

    def test_no_session_parameter_has_a_default(self):
        bad = [f"{p.name}: {m}" for p, text in sources()
               for m in re.findall(r"SessionKey\s+session\w*\s*=\s*SessionKey::\w+\([^)]*\)\s*[,)]", text)]
        self.assertEqual(bad, [])

    def test_session_key_header_exists_with_explicit_regular_value(self):
        text = (ROOT / "components/aerie_adblock/browser/session_key.h").read_text()
        self.assertIn("static SessionKey Regular()", text)
        self.assertIn("static SessionKey OffTheRecord(", text)
        self.assertRegex(text, r"explicit\s+SessionKey\(")  # no implicit conversion from a pointer
        self.assertNotRegex(text, r"SessionKey\(\)\s*(=|\{)")  # no default constructor

    def test_ui_callers_leave_the_rules_to_the_manager(self):
        for path, text in sources():
            if path.parent.name in ("aerie_filters", "aerie_adblock") and "chrome" in path.parts:
                self.assertNotIn("WatchSession", text, path.name)
                self.assertNotIn("HasSavedException", text, path.name)

    def test_manager_enforces_the_rules(self):
        text = (ROOT / "components/aerie_adblock/browser/filter_list_manager.cc").read_text()
        self.assertIn("kSavedInRegularProfile", text)
        self.assertIn("WatchSession(", text)


if __name__ == "__main__":
    unittest.main()
