"""The extension proxy row on the privacy page (issue #23): its input pattern must compile in Chrome."""

import json
import re
import shutil
import subprocess
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
UI_PATCH = REPO / "patches" / "services" / "0006-remove-the-helium-services-ui.patch"

# Chrome compiles an input's pattern as ^(?:pattern)$ with the v flag. Inside [...] a "/" must be
# escaped in that mode, or the pattern is ignored and nothing is validated.
RUN = """
const [pattern, inputs] = JSON.parse(process.argv[1]);
let re;
try { re = new RegExp('^(?:' + pattern + ')$', 'v'); }
catch (e) { console.log(JSON.stringify({error: String(e)})); process.exit(0); }
console.log(JSON.stringify({results: inputs.map((i) => re.test(i))}));
"""


def row_pattern() -> str:
    """The pattern the browser sees: the patch's text after the template literal in the
    generated wrapper turns each backslash pair into one backslash and drops other backslashes."""
    added = [l[1:] for l in UI_PATCH.read_text(encoding="utf-8").splitlines() if l.startswith("+")]
    found = [m.group(1) for l in added for m in [re.search(r'^\s*pattern="([^"]*)"', l)] if m]
    assert len(found) == 1, found
    return re.sub(r"\\(.)", r"\1", found[0])


def run_node(pattern: str, inputs: list[str]) -> dict:
    out = subprocess.run(["node", "-e", RUN, json.dumps([pattern, inputs])],
                         capture_output=True, text=True, check=True).stdout
    return json.loads(out)


@unittest.skipUnless(shutil.which("node"), "node is not installed")
class PatternTest(unittest.TestCase):
    def test_the_pattern_compiles_with_the_v_flag(self):
        self.assertNotIn("error", run_node(row_pattern(), []), row_pattern())

    def test_it_accepts_https_origins_and_refuses_the_rest(self):
        good = ["https://proxy.example.org", "https://proxy.example.org/", "https://10.0.0.1:8443"]
        bad = ["http://proxy.example.org", "https://", "https://proxy.example.org/ext",
               "https://user@proxy.example.org", "https://proxy.example.org?x=1", "https://a b"]
        results = run_node(row_pattern(), good + bad)["results"]
        self.assertEqual(results, [True] * len(good) + [False] * len(bad))

    def test_the_check_can_fail(self):
        self.assertIn("error", run_node("https://[^/?#@ ]+/?", []))


if __name__ == "__main__":
    unittest.main()
