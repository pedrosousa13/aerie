import tempfile
import unittest
from pathlib import Path

from aerie_build import treegate

SEARCH_OK = """// header
{
  "aggregation": {"export_items": false},
  "elements": {
    "ZZ": {
      // Default
      "search_engines": [
          "&duckduckgo",
          "&brave",
          "&kagi",
          "&startpage"
      ]
    }
  },
  "elements_ignored": {"AE": {"search_engines": ["&google"]}}
}
"""
PROFILE_OK = """void Profile::RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(
      prefs::kSearchSuggestEnabled,
      false,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
}
"""
DOH_OK = """    local_state->SetDefaultPrefValue(prefs::kDnsOverHttpsMode,
                                     base::Value(SecureDnsConfig::ModeToString(
                                         net::SecureDnsMode::kOff)));
"""
CRASH_OK = """std::string CrashReporterClient::GetUploadUrl() {
  // comment
  return std::string();
}

bool CrashReporterClient::ShouldRateLimitUploads() {
  return true;
}
"""
CRASH_MODE_OK = """CrashReportingMode GetCrashReportingMode() {
  return CrashReportingMode::kDisabled;
}

bool UploadsDisabledByPolicy() {
  return false;
}
"""
FEDCM_OK = """std::string ChromeContentBrowserClient::GetDnsTxtResolverUrlPrefix() {
  // comment
  return std::string();
}
"""
SERVICES_OK = """bool ShouldAccessServices(const PrefService& prefs) {
    return false;
}
"""

HTTPS_DIALOG_OK = """// Enables a dialog-based UI for HTTPS-First Mode.
BASE_FEATURE(kHttpsFirstDialogUi, base::FEATURE_DISABLED_BY_DEFAULT);
"""

GOOD = {
    "third_party/search_engines_data/resources/definitions/regional_settings.json": SEARCH_OK,
    "chrome/browser/profiles/profile.cc": PROFILE_OK,
    "chrome/browser/net/default_dns_over_https_config_source.cc": DOH_OK,
    "components/crash/core/app/crash_reporter_client.cc": CRASH_OK,
    "chrome/browser/crash_upload_list/helium_crash_reporting.cc": CRASH_MODE_OK,
    "components/helium_services/helium_services_helpers.cc": SERVICES_OK,
    "chrome/browser/chrome_content_browser_client.cc": FEDCM_OK,
    "components/security_interstitials/core/features.cc": HTTPS_DIALOG_OK,
}


class TreeCase(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)

    def write(self, files):
        for rel, text in files.items():
            path = self.root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            if isinstance(text, bytes):
                path.write_bytes(text)
            else:
                path.write_text(text)

    def run_gates(self, require=False):
        return treegate.run(self.root, require=require)


class HostScanTest(TreeCase):
    def test_flags_helium_hosts_in_source_with_file_and_line(self):
        self.write({"components/x/a.cc": 'int a;\nconst char k[] = "https://services.helium.imput.net";\n',
                    "components/x/b.h": 'constexpr auto u = "https://updates.helium.computer/";\n',
                    "chrome/y.ts": "fetch('https://raw.githubusercontent.com/imputnet/helium/x');\n",
                    "chrome/z.html": '<a href="https://imput.dev/">x</a>\n'})
        found = treegate.scan_helium_hosts(self.root)
        self.assertEqual(sorted((f.path, f.line) for f in found),
                         [("chrome/y.ts", 1), ("chrome/z.html", 1), ("components/x/a.cc", 2), ("components/x/b.h", 1)])

    def test_clean_tree_has_no_findings(self):
        self.write({"a.cc": "int main() { return 0; }\n", "docs/readme.md": "nothing here\n"})
        self.assertEqual(treegate.scan_helium_hosts(self.root), [])

    def test_allowlisted_paths_are_skipped_and_each_has_a_reason(self):
        for glob, reason in treegate.HOST_ALLOWLIST:
            self.assertTrue(reason.strip(), glob)
        self.write({"AUTHORS": "imput.net\n",
                    "third_party/ublock/assets/helium/unbreak.txt": "! https://github.com/imputnet/helium-services\n",
                    "components/helium_onboarding/src/Welcome.svelte": "https://helium.computer/privacy\n",
                    "components/crash/core/browser/resources/crashes.ts": "'https://github.com/imputnet/helium/issues/new'\n",
                    "components/helium_services/real.cc": '"https://services.helium.imput.net"\n'})
        found = treegate.scan_helium_hosts(self.root)
        self.assertEqual([f.path for f in found], ["components/helium_services/real.cc"])

    def test_binary_files_and_node_modules_are_not_scanned(self):
        self.write({"img.png": b"\x89PNG\x00imput.net\x00", "components/helium_onboarding/node_modules/x/y.js": "imput.net\n",
                    "out/Default/gen.h": "imput.net\n"})
        self.assertEqual(treegate.scan_helium_hosts(self.root), [])

    def test_a_host_that_only_contains_the_words_is_still_flagged(self):
        self.write({"a.cc": '"notimput.net"\n'})
        # Substring on purpose: a look-alike host in source is as suspect as the real one.
        self.assertEqual(len(treegate.scan_helium_hosts(self.root)), 1)


class SearchGateTest(TreeCase):
    PATH = "third_party/search_engines_data/resources/definitions/regional_settings.json"

    def test_passes_on_the_four_engines_in_order(self):
        self.write({self.PATH: SEARCH_OK})
        self.assertEqual(treegate.check_search_list(self.root), [])

    def test_fails_on_an_extra_or_missing_engine_or_a_different_default(self):
        for engines in (['"&duckduckgo"', '"&brave"', '"&kagi"', '"&startpage"', '"&bing"'],
                        ['"&duckduckgo"', '"&brave"', '"&kagi"'],
                        ['"&brave"', '"&duckduckgo"', '"&kagi"', '"&startpage"'],
                        ['"&duckduckgo"', '"&brave"', '"&kagi"', '"&break_stuff_google"']):
            text = SEARCH_OK.replace('"&duckduckgo",\n          "&brave",\n          "&kagi",\n          "&startpage"',
                                     ",".join(engines))
            self.write({self.PATH: text})
            findings = treegate.check_search_list(self.root)
            self.assertEqual(len(findings), 1, engines)
            self.assertIn("duckduckgo", findings[0].message)

    def test_fails_when_another_region_list_is_live(self):
        self.write({self.PATH: SEARCH_OK.replace('"elements_ignored"', '"elements2"').replace(
            '"ZZ": {', '"ZZ": {}, "DE": {')})
        self.assertTrue(treegate.check_search_list(self.root))

    def test_missing_file_is_skipped_unless_required(self):
        self.assertEqual(treegate.check_search_list(self.root), [])
        self.assertEqual(len(treegate.check_search_list(self.root, require=True)), 1)


class DefaultsGatesTest(TreeCase):
    def test_suggestions_must_default_to_false(self):
        self.write({"chrome/browser/profiles/profile.cc": PROFILE_OK})
        self.assertEqual(treegate.check_search_suggestions(self.root), [])
        self.write({"chrome/browser/profiles/profile.cc": PROFILE_OK.replace("false", "true")})
        self.assertEqual(len(treegate.check_search_suggestions(self.root)), 1)

    def test_secure_dns_must_default_to_off(self):
        self.write({"chrome/browser/net/default_dns_over_https_config_source.cc": DOH_OK})
        self.assertEqual(treegate.check_secure_dns_default(self.root), [])
        self.write({"chrome/browser/net/default_dns_over_https_config_source.cc": DOH_OK.replace("kOff", "kAutomatic")})
        self.assertEqual(len(treegate.check_secure_dns_default(self.root)), 1)

    def test_crash_upload_url_must_be_empty_and_mode_disabled(self):
        self.write({"components/crash/core/app/crash_reporter_client.cc": CRASH_OK,
                    "chrome/browser/crash_upload_list/helium_crash_reporting.cc": CRASH_MODE_OK})
        self.assertEqual(treegate.check_crash_reporting(self.root), [])
        self.write({"components/crash/core/app/crash_reporter_client.cc":
                    CRASH_OK.replace("std::string()", '"https://crash.example.org/crash"')})
        self.assertEqual(len(treegate.check_crash_reporting(self.root)), 1)
        self.write({"components/crash/core/app/crash_reporter_client.cc": CRASH_OK,
                    "chrome/browser/crash_upload_list/helium_crash_reporting.cc":
                    CRASH_MODE_OK.replace("kDisabled", "kAsk")})
        self.assertEqual(len(treegate.check_crash_reporting(self.root)), 1)

    def test_fedcm_dns_resolver_prefix_must_be_empty(self):
        path = "chrome/browser/chrome_content_browser_client.cc"
        self.write({path: FEDCM_OK})
        self.assertEqual(treegate.check_fedcm_resolver(self.root), [])
        google = FEDCM_OK.replace("std::string()", '"https://dns.google/resolve?type=txt&do=1&name="')
        self.write({path: google})
        self.assertEqual(len(treegate.check_fedcm_resolver(self.root)), 1)

    def test_https_first_dialog_ui_must_stay_off(self):
        path = "components/security_interstitials/core/features.cc"
        self.write({path: HTTPS_DIALOG_OK})
        self.assertEqual(treegate.check_https_first_dialog_off(self.root), [])
        self.write({path: HTTPS_DIALOG_OK.replace("DISABLED", "ENABLED")})
        findings = treegate.check_https_first_dialog_off(self.root)
        self.assertEqual([f.gate for f in findings], ["https_first_dialog_off"])

    def test_helium_services_must_be_off(self):
        self.write({"components/helium_services/helium_services_helpers.cc": SERVICES_OK})
        self.assertEqual(treegate.check_services_off(self.root), [])
        self.write({"components/helium_services/helium_services_helpers.cc":
                    SERVICES_OK.replace("return false;", "return prefs.GetBoolean(prefs::kX);")})
        self.assertEqual(len(treegate.check_services_off(self.root)), 1)


class RunTest(TreeCase):
    def test_all_good_tree_passes(self):
        self.write(GOOD)
        self.assertEqual(self.run_gates(require=True), [])

    def test_one_failure_is_reported_with_its_gate_name(self):
        self.write({**GOOD, "chrome/browser/net/default_dns_over_https_config_source.cc": DOH_OK.replace("kOff", "kAutomatic")})
        findings = self.run_gates()
        self.assertEqual([f.gate for f in findings], ["secure_dns_default"])

    def test_empty_tree_passes_unless_required(self):
        self.assertEqual(self.run_gates(), [])
        self.assertEqual(len(self.run_gates(require=True)), 8)

    def test_format_report(self):
        self.write({"a.cc": '"https://services.helium.imput.net"\n'})
        text, ok = treegate.format_report(self.run_gates())
        self.assertFalse(ok)
        self.assertIn("helium_hosts", text)
        self.assertIn("a.cc:1", text)
        text, ok = treegate.format_report([])
        self.assertTrue(ok)


if __name__ == "__main__":
    unittest.main()
