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
<!-- request-policy:user:begin -->
| Annotation | Why |
| --- | --- |
| `navigation_url_loader` | the user navigates |
<!-- request-policy:user:end -->
<!-- request-policy:site:begin -->
| Annotation | Why |
| --- | --- |
| `reporting` | the site names the endpoint |
<!-- request-policy:site:end -->
"""


class ParseTest(unittest.TestCase):
    def test_reads_both_tables(self):
        table = model.parse_doc(DOC_SAMPLE)
        self.assertEqual(table.hosts, [("update_client", "update.example"), ("list_download", "lists.example")])
        self.assertEqual(table.user, ["navigation_url_loader"])
        self.assertEqual(table.site, ["reporting"])

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

    def test_an_annotation_in_two_groups_is_an_error(self):
        # One annotation, one group: the groups say why the destination is trusted.
        with self.assertRaises(model.PolicyDocError):
            model.parse_doc(DOC_SAMPLE.replace("| `reporting` |", "| `navigation_url_loader` |"))
        with self.assertRaises(model.PolicyDocError):
            model.parse_doc(DOC_SAMPLE.replace("| `reporting` |", "| `update_client` |"))


class RenderTest(unittest.TestCase):
    def test_render_names_every_entry(self):
        text = model.render(model.parse_doc(DOC_SAMPLE))
        self.assertIn('{"update_client", "update.example"}', text)
        self.assertIn('{"list_download", "lists.example"}', text)
        self.assertIn('{"navigation_url_loader"}', text)
        self.assertIn("kUserDirectedAnnotations", text)
        self.assertIn('{"reporting"}', text)
        self.assertIn("kSiteDirectedAnnotations", text)
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
        self.assertIn("navigation_url_loader", table.user)
        self.assertIn("certificate_verifier_url_loader", table.site)

    def test_no_fail_open_annotation_is_directed(self):
        # Review of #257, B3: a speculative or Google-reachable feature must be refused, so it must not sit
        # in a directed group. Extension fetchers wait for the maintainer's decision on the proxy.
        table = model.parse_doc(DOC.read_text(encoding="utf-8"))
        directed = set(table.user) | set(table.site)
        for name in ("predictive_prefetch", "predictive_preconnect", "network_hints_preconnect",
                     "search_engine_preconnect", "preload_activation_beacon", "extension_manifest_fetcher",
                     "extension_crx_fetcher", "content_hash_verification_job", "update_client",
                     "aerie_filter_list_download"):
            self.assertNotIn(name, directed)

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


class AnnotationNamesTest(unittest.TestCase):
    """Every annotation name must exist in the pinned source or be defined by Aerie, so a typo or a rename
    cannot turn a row into dead text (issue #257, review B3)."""

    XML = '<annotations><item id="update_client" hash_code="1"/><item id="reporting" hash_code="2"/>' \
          '<item id="navigation_url_loader" hash_code="3"/></annotations>'

    def test_known_names_pass(self):
        table = model.parse_doc(DOC_SAMPLE.replace("list_download", "aerie_list"))
        overlay_text = 'DefineNetworkTrafficAnnotation("aerie_list", R"(x)")'
        self.assertEqual(model.unknown_annotations(table, self.XML, overlay_text), [])

    def test_a_typo_is_named(self):
        table = model.parse_doc(DOC_SAMPLE.replace("reporting", "reportting").replace("list_download", "aerie_list"))
        self.assertEqual(model.unknown_annotations(table, self.XML, 'DefineNetworkTrafficAnnotation("aerie_list"'),
                         ["reportting"])

    def test_an_aerie_annotation_must_be_defined_in_the_overlay(self):
        table = model.parse_doc(DOC_SAMPLE.replace("list_download", "aerie_list"))
        self.assertEqual(model.unknown_annotations(table, self.XML, "nothing"), ["aerie_list"])

    def test_the_policy_names_exist_in_the_pinned_source(self):
        src = model.default_tree()
        if src is None:
            self.skipTest("no prepared source tree")
        xml = src / model.ANNOTATIONS_XML
        if not xml.is_file():
            self.skipTest(f"{model.ANNOTATIONS_XML} is not in the source tree")
        table = model.parse_doc(DOC.read_text(encoding="utf-8"))
        overlay_text = model.overlay_annotation_text(REPO)
        self.assertEqual(model.unknown_annotations(table, xml.read_text(encoding="utf-8"), overlay_text), [])

    def test_check_with_a_source_tree_reports_unknown_names(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo, src = Path(tmp) / "repo", Path(tmp) / "src"
            (repo / "docs").mkdir(parents=True)
            shutil.copy(DOC, repo / "docs" / "network-endpoints.md")
            (repo / GENERATED.relative_to(REPO)).parent.mkdir(parents=True)
            shutil.copy(GENERATED, repo / GENERATED.relative_to(REPO))
            (src / model.ANNOTATIONS_XML).parent.mkdir(parents=True)
            (src / model.ANNOTATIONS_XML).write_text(self.XML, encoding="utf-8")
            self.assertEqual(cli.main(["check", "--repo", str(repo), "--tree", str(src)]), 1)
