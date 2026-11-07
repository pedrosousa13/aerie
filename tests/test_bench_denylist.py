import tempfile
import unittest
from pathlib import Path

from aerie_bench import denylist


def dl(text: str) -> denylist.Denylist:
    """Parse rules; a group line is added unless the text brings its own."""
    return denylist.parse(text if "@group" in text else "@group t\n" + text)


class HostMatchingTest(unittest.TestCase):
    def test_domain_matches_itself_and_subdomains_on_label_boundary(self):
        d = dl("@group g\ngstatic.com\n")
        self.assertIsNotNone(d.match_host("gstatic.com"))
        self.assertIsNotNone(d.match_host("fonts.gstatic.com"))
        self.assertIsNotNone(d.match_host("a.b.c.gstatic.com"))
        self.assertIsNone(d.match_host("notgstatic.com"))
        self.assertIsNone(d.match_host("gstatic.com.evil.example"))
        self.assertIsNone(d.match_host("gstatic.community"))

    def test_leading_wildcard_is_the_same_as_the_bare_domain(self):
        d = dl("*.doubleclick.net\n")
        self.assertIsNotNone(d.match_host("doubleclick.net"))
        self.assertIsNotNone(d.match_host("ad.doubleclick.net"))

    def test_case_trailing_dot_and_idna_are_normalised(self):
        d = dl("youtube.com\n")
        self.assertIsNotNone(d.match_host("WWW.YouTube.COM"))
        self.assertIsNotNone(d.match_host("www.youtube.com."))
        d2 = dl("xn--bcher-kva.example\n")
        self.assertIsNotNone(d2.match_host("bücher.example"))

    def test_ip_literals_and_empty_hosts_are_ignored(self):
        d = dl("1e100.net\n")
        for host in ("127.0.0.1", "8.8.8.8", "[::1]", "::1", "", "localhost"):
            self.assertIsNone(d.match_host(host), host)

    def test_all_country_google_pattern(self):
        d = dl("google.*\n")
        for host in ("google.com", "www.google.com", "google.de", "www.google.co.uk",
                     "google.com.br", "mail.google.co.jp", "accounts.google.fr"):
            self.assertIsNotNone(d.match_host(host), host)
        for host in ("notgoogle.com", "google.example.com", "google.com.evil.example",
                     "example.com", "mygoogle.de", "google", "evil.example/google.com"):
            self.assertIsNone(d.match_host(host), host)

    def test_bare_tld_entry_matches_only_the_tld(self):
        d = dl("google\n")
        self.assertIsNotNone(d.match_host("dns.google"))
        self.assertIsNotNone(d.match_host("google"))
        self.assertIsNone(d.match_host("google.com"))
        self.assertIsNone(d.match_host("example.com"))

    def test_match_reports_group_and_pattern(self):
        d = dl("@group helium\nhelium.computer\n@group google\ngoogleapis.com\n")
        m = d.match_host("updates.helium.computer")
        self.assertEqual((m.group, m.pattern), ("helium", "helium.computer"))
        self.assertEqual(d.match_host("x.googleapis.com").group, "google")


class UrlRuleTest(unittest.TestCase):
    def test_url_path_is_matched_case_insensitively(self):
        d = dl("@group helium\nurl github.com/imputnet/\n")
        self.assertIsNotNone(d.match_url("https://github.com/ImputNet/helium"))
        self.assertIsNotNone(d.match_url("https://GITHUB.com/IMPUTNET/x"))
        d2 = dl("@group helium\nurl github.com/ImputNet/\n")
        self.assertIsNotNone(d2.match_url("https://github.com/imputnet/x"))

    def test_url_prefix_rule_needs_host_and_path(self):
        d = dl("@group helium\nurl raw.githubusercontent.com/imputnet/\n")
        self.assertIsNotNone(d.match_url("https://raw.githubusercontent.com/imputnet/uBlock/x/assets.json"))
        self.assertIsNone(d.match_url("https://raw.githubusercontent.com/someoneelse/repo/x"))
        self.assertIsNone(d.match_url("https://raw.githubusercontent.com/imputnetwork/x"))
        self.assertIsNone(d.match_host("raw.githubusercontent.com"))

    def test_match_url_also_applies_host_rules(self):
        d = dl("imput.net\n")
        self.assertEqual(d.match_url("https://services.helium.imput.net/com").pattern, "imput.net")
        self.assertIsNone(d.match_url("http://127.0.0.1:5000/index.html"))
        self.assertIsNone(d.match_url("not a url"))


class DummyHostTest(unittest.TestCase):
    def test_qjz9zk_is_a_blocked_attempt_not_a_denylist_hit(self):
        self.assertTrue(denylist.is_dummy_host("clients2.9oo91e.qjz9zk"))
        self.assertTrue(denylist.is_dummy_host("HELIUM-SERVICES-ARE-DISABLED.QJZ9ZK."))
        self.assertFalse(denylist.is_dummy_host("qjz9zk.example.com"))
        self.assertFalse(denylist.is_dummy_host("example.com"))


class ParseTest(unittest.TestCase):
    def test_comments_blanks_and_inline_comments(self):
        d = dl("# c\n\n@group a\nexample.org  # why\n")
        self.assertEqual([e.pattern for e in d.entries], ["example.org"])

    def test_bad_lines_are_errors_with_line_numbers(self):
        for bad in ("exa mple.com", "url nopath.example", "@group", "http://example.com", "a..b"):
            with self.assertRaises(denylist.DenylistError, msg=bad) as ctx:
                denylist.parse("@group t\nok.example\n" + bad + "\n")
            self.assertIn("line 3", str(ctx.exception))

    def test_entry_before_any_group_is_an_error(self):
        with self.assertRaises(denylist.DenylistError):
            denylist.parse("example.org\n")


class ShippedListTest(unittest.TestCase):
    """The security criterion: the list covers Google's main domains and Helium/imput, and is documented."""

    @classmethod
    def setUpClass(cls):
        cls.d = denylist.load_default()

    def test_google_main_domains_from_the_issue(self):
        for host in ("www.google.com", "google.co.uk", "www.google.de", "www.gstatic.com",
                     "fonts.googleapis.com", "update.googleapis.com", "redirector.gvt1.com",
                     "r1---sn.gvt2.com", "stats.g.doubleclick.net", "www.youtube.com",
                     "i.ytimg.com", "lh3.googleusercontent.com", "dns.google",
                     "clients2.google.com", "chromewebstore.google.com", "www.googletagmanager.com",
                     "www.google-analytics.com", "pagead2.googlesyndication.com", "t0.gstatic.com",
                     "ssl.gstatic.com", "accounts.google.com", "update.chromium.org",
                     "www.recaptcha.net", "chromium.googlesource.com", "cdn.ampproject.org", "r4---sn.gvt3.com"):
            m = self.d.match_host(host)
            self.assertIsNotNone(m, host)
            self.assertEqual(m.group, "google", host)

    def test_helium_and_imput_domains(self):
        for url in ("https://services.helium.imput.net/com", "https://updates.helium.computer/mac/appcast-arm64.xml",
                    "https://crash.helium.computer/crash", "https://imput.net/",
                    "https://raw.githubusercontent.com/imputnet/helium-services/main/filters/x.txt",
                    "https://github.com/imputnet/helium/issues/new"):
            m = self.d.match_url(url)
            self.assertIsNotNone(m, url)
            self.assertEqual(m.group, "helium", url)

    def test_sites_the_fixtures_and_search_defaults_use_are_not_listed(self):
        for host in ("duckduckgo.com", "search.brave.com", "kagi.com", "www.startpage.com",
                     "example.com", "127.0.0.1", "github.com", "cdn.jsdelivr.net"):
            self.assertIsNone(self.d.match_host(host), host)
        self.assertIsNone(self.d.match_url("https://github.com/someone/else"))

    def test_every_entry_is_documented(self):
        doc = (Path(__file__).resolve().parents[1] / "docs" / "network-denylist.md").read_text()
        for entry in self.d.entries:
            self.assertIn(entry.pattern, doc, f"{entry.pattern} is missing from docs/network-denylist.md")


if __name__ == "__main__":
    unittest.main()
