"""The request policy's allowlist is generated from docs/network-endpoints.md (issue #257).

The doc is the one reviewed source. The C++ include file is its generated copy; these tests fail when
the two differ, so a host cannot be added to the code without a reviewed line in the doc.
"""

import shutil
import tempfile
import unittest
from pathlib import Path

from aerie_netpolicy import __main__ as cli
from aerie_netpolicy import model

REPO = Path(__file__).resolve().parent.parent
DOC = REPO / "docs" / "network-endpoints.md"
GENERATED = REPO / model.GENERATED_PATH

DOC_SAMPLE = """# x
<!-- request-policy:hosts:begin -->
| Annotation | Host | Why |
| --- | --- | --- |
| `update_client` | `update.example` | the updater |
| `list_download` | `lists.example` | lists |
<!-- request-policy:hosts:end -->
<!-- request-policy:directed:begin -->
| Annotation | Why |
| --- | --- |
| `navigation_url_loader` | the user navigates |
<!-- request-policy:directed:end -->
"""


class ParseTest(unittest.TestCase):
    def test_reads_both_tables(self):
        table = model.parse_doc(DOC_SAMPLE)
        self.assertEqual(table.hosts, [("update_client", "update.example"), ("list_download", "lists.example")])
        self.assertEqual(table.directed, ["navigation_url_loader"])

    def test_a_doc_without_the_markers_is_an_error(self):
        with self.assertRaises(model.PolicyDocError):
            model.parse_doc("# nothing here\n")

    def test_bad_host_or_annotation_is_an_error(self):
        for bad_host in ("*.example.com", "https://x.example", "Upper.Example", "x.example/path", "a b.example", "localhost"):
            doc = DOC_SAMPLE.replace("update.example", bad_host)
            with self.assertRaises(model.PolicyDocError, msg=bad_host):
                model.parse_doc(doc)
        with self.assertRaises(model.PolicyDocError):
            model.parse_doc(DOC_SAMPLE.replace("update_client", "Update Client"))

    def test_duplicates_are_an_error(self):
        with self.assertRaises(model.PolicyDocError):
            model.parse_doc(DOC_SAMPLE.replace("lists.example", "update.example").replace("list_download", "update_client"))
        with self.assertRaises(model.PolicyDocError):
            model.parse_doc(DOC_SAMPLE.replace("| `navigation_url_loader` | the user navigates |",
                                               "| `a` | b |\n| `a` | b |"))


class RenderTest(unittest.TestCase):
    def test_render_names_every_entry(self):
        text = model.render(model.parse_doc(DOC_SAMPLE))
        self.assertIn('{"update_client", "update.example"}', text)
        self.assertIn('{"list_download", "lists.example"}', text)
        self.assertIn('"navigation_url_loader"', text)
        self.assertIn("Do not edit", text)


class RepoTest(unittest.TestCase):
    def test_generated_file_matches_the_doc(self):
        want = model.render(model.parse_doc(DOC.read_text(encoding="utf-8")))
        self.assertEqual(GENERATED.read_text(encoding="utf-8"), want,
                         f"{model.GENERATED_PATH} is stale; run: ./aerie netpolicy generate")

    def test_the_doc_keeps_the_hosts_the_browser_needs(self):
        table = model.parse_doc(DOC.read_text(encoding="utf-8"))
        self.assertIn(("aerie_filter_list_download", "easylist.to"), table.hosts)
        self.assertIn(("update_client", "update.googleapis.com"), table.hosts)
        self.assertIn("navigation_url_loader", table.directed)

    def test_a_host_added_to_the_doc_only_fails_the_check(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            (repo / "docs").mkdir()
            shutil.copy(DOC, repo / "docs" / "network-endpoints.md")
            (repo / GENERATED.relative_to(REPO)).parent.mkdir(parents=True)
            shutil.copy(GENERATED, repo / GENERATED.relative_to(REPO))
            self.assertEqual(cli.main(["check", "--repo", str(repo)]), 0)
            doc = repo / "docs" / "network-endpoints.md"
            doc.write_text(doc.read_text(encoding="utf-8").replace(
                "<!-- request-policy:hosts:end -->",
                "| `update_client` | `new-host.example` | test |\n<!-- request-policy:hosts:end -->"), encoding="utf-8")
            self.assertEqual(cli.main(["check", "--repo", str(repo)]), 1)
            self.assertEqual(cli.main(["generate", "--repo", str(repo)]), 0)
            self.assertEqual(cli.main(["check", "--repo", str(repo)]), 0)
            self.assertIn("new-host.example", (repo / GENERATED.relative_to(REPO)).read_text(encoding="utf-8"))

    def test_a_generated_file_edited_by_hand_fails_the_check(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            (repo / "docs").mkdir()
            shutil.copy(DOC, repo / "docs" / "network-endpoints.md")
            target = repo / GENERATED.relative_to(REPO)
            target.parent.mkdir(parents=True)
            target.write_text(GENERATED.read_text(encoding="utf-8") + '    {"evil", "evil.example"},\n', encoding="utf-8")
            self.assertEqual(cli.main(["check", "--repo", str(repo)]), 1)


if __name__ == "__main__":
    unittest.main()
