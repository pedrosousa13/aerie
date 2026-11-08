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

LINK_PREFETCH_CLIENT_OK = """  web_prefs->dns_prefetching_enabled =
      prefetch::IsSomePreloadingEnabled(*prefs) ==
      content::PreloadingEligibility::kEligible;
"""

LINK_PREFETCH_BLINK_OK = """  if (document.GetSettings() &&
      !document.GetSettings()->GetDNSPrefetchingEnabled())
    return;
  UseCounter::Count(document, WebFeature::kLinkRelPrefetch);
"""

JS_OPT_REGISTRY_OK = """  Register(ContentSettingsType::JAVASCRIPT_OPTIMIZER, "javascript-optimizer",
           CONTENT_SETTING_ALLOW, WebsiteSettingsInfo::UNSYNCABLE,
           /*allowlisted_primary_schemes=*/{},
"""
JS_OPT_ROUTE_OK = "  r.SITE_SETTINGS_JAVASCRIPT_OPTIMIZER = r.SITE_SETTINGS.createChild('v8');\n"
JS_OPT_HOST_OK = """  if (host->AreV8OptimizationsDisabled() !=
      site_info.are_v8_optimizations_disabled()) {
    return false;
  }
  if (IsJitDisabled()) {
    AppendToCommaSeparatedSwitch(
        command_line, blink::switches::kJavaScriptFlags, "--jitless");
  } else if (AreV8OptimizationsDisabled()) {
    AppendToCommaSeparatedSwitch(command_line,
                                 blink::switches::kJavaScriptFlags,
                                 "--disable-optimizing-compilers");
  }
"""
JS_OPT_SITE_INFO_OK = "      are_v8_optimizations_disabled_ == other.are_v8_optimizations_disabled_ &&\n"
JS_OPT_FEATURE_OK = """BASE_FEATURE(kOriginIsolationForJsOptExceptions,
             base::FEATURE_ENABLED_BY_DEFAULT);
"""

JS_OPT_GOOD = {
    "components/content_settings/core/browser/content_settings_registry.cc": JS_OPT_REGISTRY_OK,
    "chrome/browser/resources/settings/route.ts": JS_OPT_ROUTE_OK,
    "content/browser/renderer_host/render_process_host_impl.cc": JS_OPT_HOST_OK,
    "content/browser/site_info.cc": JS_OPT_SITE_INFO_OK,
    "components/site_isolation/features.cc": JS_OPT_FEATURE_OK,
}

WIDEVINE_REG_OK = """void RegisterComponentsForUpdate() {
  aerie_widevine::RegisterWhenEnabled(g_browser_process->local_state(),
                                      base::BindRepeating(&RegisterWidevineCdmComponent, cus));
}
"""
WIDEVINE_CFG_OK = """std::vector<GURL> ChromeConfigurator::UpdateUrl() const {
  return {aerie_widevine::WidevineUpdateUrl(ls, helium::GetComponentUpdateURL(p))};
}

std::vector<GURL> ChromeConfigurator::PingUrl() const {
  return {aerie_widevine::WidevineUpdateUrl(ls, helium::GetComponentUpdateURL(p))};
}

bool ChromeConfigurator::ShouldSendToServer(const std::string& app_id,
                                            const GURL& url) const {
  return aerie_widevine::ShouldSendToUpdateServer(app_id, url);
}
"""
WIDEVINE_CHECKER_OK = """    if (!config_->ShouldSendToServer(app_id, url)) {
      continue;
    }
"""
WIDEVINE_PING_OK = """  if (!config_->ShouldSendToServer(component.app_id, url)) {
    return;
  }
"""

WIDEVINE_INSTALLER_OK = """const uint8_t kWidevineSha2Hash[] = {
    0xe8, 0xce, 0xcf, 0x42, 0x06, 0xd0, 0x93, 0x49, 0x6d, 0xd9, 0x89,
    0xe1, 0x41, 0x04, 0x86, 0x4a};
"""

SERVICES_UI_PRIVACY_HTML_OK = """    <cr-input id="extensionProxyInput" placeholder="https://proxy.example.org"
        .value="${this.extensionProxy_}" pattern="https://.*"></cr-input>
"""
SERVICES_UI_PRIVACY_TS_OK = """    this.mirrorPref('helium.services.origin_override', 'extensionProxyPref_');
"""
SERVICES_UI_STRINGS_OK = """      {"languagesDictionaryDownloadErrorHelp",
       IDS_SETTINGS_LANGUAGES_DICTIONARY_DOWNLOAD_FAILED_HELP_AERIE},
"""
SERVICES_UI_DELEGATE_OK = """  chrome::ShowWarningMessageBoxAsync(
      window,
      l10n_util::GetStringUTF16(IDS_AERIE_EXTENSION_PROXY_UNSET_TITLE),
      l10n_util::GetStringUTF16(IDS_AERIE_EXTENSION_PROXY_UNSET_BODY));
"""
SERVICES_UI_OPENER_OK = '  ShowSingletonTabIgnorePathOverwriteNTP(browser, GURL("chrome://settings/privacy"));\n'
SERVICES_UI_GOOD = {
    treegate.SERVICES_URL_OPENERS[0]: SERVICES_UI_OPENER_OK,
    treegate.SERVICES_URL_OPENERS[1]: SERVICES_UI_OPENER_OK,
    "chrome/browser/resources/settings/privacy_page/privacy_page.html": SERVICES_UI_PRIVACY_HTML_OK,
    "chrome/browser/resources/settings/privacy_page/privacy_page.ts": SERVICES_UI_PRIVACY_TS_OK,
    "chrome/browser/ui/webui/settings/settings_localized_strings_provider.cc": SERVICES_UI_STRINGS_OK,
    "chrome/browser/extensions/api/webstore_private/chrome_webstore_private_api_delegate.cc":
        SERVICES_UI_DELEGATE_OK,
}

AUDIO_NOISE_OK = """void AudioBuffer::JitterAudioData(ExecutionContext* context) {
  for (unsigned i = 0; i < channels_.size(); ++i) {
    for (size_t frame = 0; frame < samples.size(); ++frame) {
      const uint64_t coordinate_seed =
          (static_cast<uint64_t>(i) << 48) ^ frame;
      samples[frame] = BaseAudioContext::JitterAudioData(
          samples[frame], context, coordinate_seed);
    }
  }
}
"""

ANALYSER_NOISE_OK = """struct AudioAnalyzerNoiseState {
  uint64_t CoordinateSeed(uint64_t domain, uint64_t index) const {
    DCHECK(enabled);
    return domain ^ index;
  }
};
"""
AUDIO_GOOD = {treegate.AUDIO_BUFFER_PATH: AUDIO_NOISE_OK, treegate.REALTIME_ANALYSER_PATH: ANALYSER_NOISE_OK}

GPU_WEBGL_OK = """String UnmaskedGpuString() {
  if (base::FeatureList::IsEnabled(blink::features::kSpoofWebGLInfo)) return {};
}
    case WebGLDebugRendererInfo::kUnmaskedRendererWebgl:
      if (ExtensionEnabled(kWebGLDebugRendererInfoName)) {
        return WebGLAny(
            script_state,
            UnmaskedGpuString(script_state, /*vendor=*/false,
                              blink::features::kSpoofWebGLRendererParam,
                              String(ContextGL()->GetString(GL_VENDOR)),
                              String(ContextGL()->GetString(GL_RENDERER))));
      }
      return ScriptValue::CreateNull(script_state->GetIsolate());
    case WebGLDebugRendererInfo::kUnmaskedVendorWebgl:
      if (ExtensionEnabled(kWebGLDebugRendererInfoName)) {
        return WebGLAny(
            script_state,
            UnmaskedGpuString(script_state, /*vendor=*/true,
                              blink::features::kSpoofWebGLVendorParam,
                              String(ContextGL()->GetString(GL_VENDOR)),
                              String(ContextGL()->GetString(GL_RENDERER))));
      }
      return ScriptValue::CreateNull(script_state->GetIsolate());
"""
GPU_ADAPTER_OK = """GPUAdapterInfo* GPUAdapter::CreateAdapterInfoForAdapter() {
  const bool masked = aerie_gpu_info::ShouldMaskGpuInfo(kind, a, b, c);
  const String vendor = masked ? g_empty_string : vendor_;
  const String architecture = masked ? g_empty_string : architecture_;
  const String device = masked ? g_empty_string : device_;
  const String description = masked ? g_empty_string : description_;
  info = MakeGarbageCollected<GPUAdapterInfo>(
      vendor, architecture, subgroup_min_size_, subgroup_max_size_,
      is_fallback_adapter, device, description, driver,
      FromDawnEnum(backend_type_));
  info = MakeGarbageCollected<GPUAdapterInfo>(
      vendor, architecture, subgroup_min_size_, subgroup_max_size_,
      is_fallback_adapter);
}
"""
GPU_GOOD = {treegate.WEBGL_CONTEXT_PATH: GPU_WEBGL_OK, treegate.GPU_ADAPTER_PATH: GPU_ADAPTER_OK}

GOOD = {
    **JS_OPT_GOOD,
    **SERVICES_UI_GOOD,
    **AUDIO_GOOD,
    **GPU_GOOD,
    "chrome/browser/component_updater/widevine_cdm_component_installer.cc": WIDEVINE_INSTALLER_OK,
    "chrome/browser/component_updater/registration.cc": WIDEVINE_REG_OK,
    "components/update_client/update_checker.cc": WIDEVINE_CHECKER_OK,
    "components/update_client/ping_manager.cc": WIDEVINE_PING_OK,
    "chrome/browser/component_updater/chrome_component_updater_configurator.cc": WIDEVINE_CFG_OK,
    "third_party/search_engines_data/resources/definitions/regional_settings.json": SEARCH_OK,
    "chrome/browser/profiles/profile.cc": PROFILE_OK,
    "chrome/browser/net/default_dns_over_https_config_source.cc": DOH_OK,
    "components/crash/core/app/crash_reporter_client.cc": CRASH_OK,
    "chrome/browser/crash_upload_list/helium_crash_reporting.cc": CRASH_MODE_OK,
    "components/helium_services/helium_services_helpers.cc": SERVICES_OK,
    "chrome/browser/chrome_content_browser_client.cc": FEDCM_OK + LINK_PREFETCH_CLIENT_OK,
    "third_party/blink/renderer/core/loader/preload_helper.cc": LINK_PREFETCH_BLINK_OK,
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

    def test_link_prefetch_must_follow_the_preloading_pref(self):
        client = "chrome/browser/chrome_content_browser_client.cc"
        blink = "third_party/blink/renderer/core/loader/preload_helper.cc"
        self.write({client: LINK_PREFETCH_CLIENT_OK, blink: LINK_PREFETCH_BLINK_OK})
        self.assertEqual(treegate.check_link_prefetch_off(self.root), [])
        self.write({blink: LINK_PREFETCH_BLINK_OK.replace("!document.GetSettings()->GetDNSPrefetchingEnabled()", "false")})
        self.assertEqual([f.gate for f in treegate.check_link_prefetch_off(self.root)], ["link_prefetch_off"])
        self.write({blink: LINK_PREFETCH_BLINK_OK, client: LINK_PREFETCH_CLIENT_OK.replace("kEligible", "kUnspecified")})
        self.assertEqual([f.gate for f in treegate.check_link_prefetch_off(self.root)], ["link_prefetch_off"])

    def test_audio_noise_seed_must_be_the_exact_coordinate_expression(self):
        self.write(AUDIO_GOOD)
        self.assertEqual(treegate.check_audio_noise_stable(self.root, require=True), [])
        buf = treegate.AUDIO_BUFFER_PATH
        counter = AUDIO_NOISE_OK.replace("(static_cast<uint64_t>(i) << 48)", "render_seed ^ (static_cast<uint64_t>(i) << 48)")
        for text in (counter, "void AudioBuffer::JitterAudioData(ExecutionContext*) {}\n"):
            self.write({buf: text})
            self.assertEqual([f.path for f in treegate.check_audio_noise_stable(self.root)], [buf])
        self.write(AUDIO_GOOD)
        ana = treegate.REALTIME_ANALYSER_PATH
        for text in (ANALYSER_NOISE_OK.replace("domain ^ index", "domain ^ (read_seed << 32) ^ index"), ""):
            self.write({ana: text})
            self.assertEqual([f.path for f in treegate.check_audio_noise_stable(self.root)], [ana])

    def test_webgl_gpu_strings_must_go_through_the_masking_helper(self):
        self.write(GPU_GOOD)
        self.assertEqual(treegate.check_gpu_masking(self.root, require=True), [])
        path = treegate.WEBGL_CONTEXT_PATH
        raw = GPU_WEBGL_OK.replace(
            """UnmaskedGpuString(script_state, /*vendor=*/false,
                              blink::features::kSpoofWebGLRendererParam,
                              String(ContextGL()->GetString(GL_VENDOR)),
                              String(ContextGL()->GetString(GL_RENDERER)))""",
            "String(ContextGL()->GetString(GL_RENDERER))")
        stacked = "if (base::FeatureList::IsEnabled(blink::features::kSpoofWebGLInfo)) {}\n" + GPU_WEBGL_OK
        for text in (raw, stacked, ""):
            self.write({path: text})
            self.assertEqual({f.path for f in treegate.check_gpu_masking(self.root)}, {path})

    def test_webgpu_adapter_info_must_be_emptied_when_masked(self):
        path = treegate.GPU_ADAPTER_PATH
        for bad in (GPU_ADAPTER_OK.replace("masked ? g_empty_string : architecture_", "architecture_"),
                    GPU_ADAPTER_OK.replace("masked ? g_empty_string : description_", "description_"),
                    GPU_ADAPTER_OK.replace("        vendor, architecture, subgroup_min_size_", "        vendor_, architecture, subgroup_min_size_", 1)
                    .replace("      vendor, architecture, subgroup_min_size_, subgroup_max_size_,\n      is_fallback_adapter, device",
                             "      vendor_, architecture, subgroup_min_size_, subgroup_max_size_,\n      is_fallback_adapter, device"),
                    ""):
            self.write(GPU_GOOD)
            self.write({path: bad})
            self.assertEqual({f.path for f in treegate.check_gpu_masking(self.root)}, {path})

    def test_helium_services_must_be_off(self):
        self.write({"components/helium_services/helium_services_helpers.cc": SERVICES_OK})
        self.assertEqual(treegate.check_services_off(self.root), [])
        self.write({"components/helium_services/helium_services_helpers.cc":
                    SERVICES_OK.replace("return false;", "return prefs.GetBoolean(prefs::kX);")})
        self.assertEqual(len(treegate.check_services_off(self.root)), 1)


class JsOptimizerGateTest(TreeCase):
    def test_passes_on_the_upstream_shape(self):
        self.write(JS_OPT_GOOD)
        self.assertEqual(treegate.check_js_optimizer(self.root, require=True), [])

    def test_each_part_is_guarded(self):
        host = "content/browser/renderer_host/render_process_host_impl.cc"
        cases = {
            "components/content_settings/core/browser/content_settings_registry.cc":
                JS_OPT_REGISTRY_OK.replace("CONTENT_SETTING_ALLOW", "CONTENT_SETTING_BLOCK"),
            "chrome/browser/resources/settings/route.ts": JS_OPT_ROUTE_OK.replace("'v8'", "'jit'"),
            "content/browser/site_info.cc": JS_OPT_SITE_INFO_OK.replace("==", "!="),
            "components/site_isolation/features.cc": JS_OPT_FEATURE_OK.replace("ENABLED", "DISABLED"),
        }
        for rel, bad in cases.items():
            with self.subTest(rel):
                self.write(JS_OPT_GOOD)
                self.write({rel: bad})
                self.assertEqual([f.path for f in treegate.check_js_optimizer(self.root)], [rel])
        for what, bad in (("reuse", JS_OPT_HOST_OK.replace("return false;", "return true;")),
                          ("flag", JS_OPT_HOST_OK.replace("--disable-optimizing-compilers", "--other"))):
            with self.subTest(what):
                self.write(JS_OPT_GOOD)
                self.write({host: bad})
                self.assertEqual([f.gate for f in treegate.check_js_optimizer(self.root)], ["js_optimizer"])


class ServicesUiGateTest(TreeCase):
    PAGE = treegate.PRIVACY_PAGE

    def good(self):
        self.write(JS_OPT_GOOD)
        self.write(SERVICES_UI_GOOD)

    def test_passes_on_the_aerie_shape(self):
        self.good()
        self.assertEqual(treegate.check_services_ui(self.root, require=True), [])

    def test_each_part_is_guarded(self):
        route = treegate.SETTINGS_RES + "route.ts"
        strings = treegate.STRINGS_PROVIDER
        delegate = treegate.PROXY_DIALOG
        cases = {
            "route comes back": (route, JS_OPT_ROUTE_OK + "  r.HELIUM_SERVICES = r.PRIVACY.createChild('services');\n"),
            "services row comes back": (self.PAGE + "privacy_page.html",
                                        SERVICES_UI_PRIVACY_HTML_OK + '<cr-link-row id="heliumServicesLinkRow">'),
            "proxy row is gone": (self.PAGE + "privacy_page.ts", "// nothing\n"),
            "languages names Helium": (strings, SERVICES_UI_STRINGS_OK.replace("_AERIE", "_HELIUM")),
            "menu opens the old page": (treegate.SERVICES_URL_OPENERS[0],
                                        SERVICES_UI_OPENER_OK.replace("privacy", "privacy/services")),
            "download link opens the old page": (treegate.SERVICES_URL_OPENERS[1],
                                                 SERVICES_UI_OPENER_OK.replace("privacy", "privacy/services")),
            "dialog text is a literal": (delegate, SERVICES_UI_DELEGATE_OK.replace(
                "l10n_util::GetStringUTF16(IDS_AERIE_EXTENSION_PROXY_UNSET_BODY)", 'u"none is set"')),
        }
        for what, (rel, bad) in cases.items():
            with self.subTest(what):
                self.good()
                self.write({rel: bad})
                self.assertEqual([f.path for f in treegate.check_services_ui(self.root)], [rel])

    def test_the_old_services_page_files_must_be_gone(self):
        self.good()
        for name in ("services_page.ts", "services_page.html"):
            with self.subTest(name):
                self.write({self.PAGE + name: "x"})
                self.assertEqual([f.path for f in treegate.check_services_ui(self.root)], [self.PAGE + name])
                (self.root / self.PAGE / name).unlink()


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
        self.assertEqual(len(self.run_gates(require=True)), 31)

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
