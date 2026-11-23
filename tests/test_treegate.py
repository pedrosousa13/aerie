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
CRASHPAD_OK = """void ApplyUploadConsent(bool /*consent*/) {
  if (!g_database)
    return;
  // Aerie: never.
  g_database->GetSettings()->SetUploadsEnabled(false);
}

void RequestSingleCrashUploadImpl(const std::string& local_id) {
  // Aerie: never.
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

PRIVATE_AI_OK = """BASE_FEATURE(kPrivateAi, base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kPrivateAiServerAttestation, base::FEATURE_ENABLED_BY_DEFAULT);
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
    treegate.DELEGATE_PATH: "aerie_network::ClassifyFactory(f); aerie_network::Decide(x); net::ERR_BLOCKED_BY_ADMINISTRATOR;",
    treegate.NETWORK_CONTEXT_PATH: "aerie_network::DecideConnection(a); aerie_network::DecideConnection(b);",
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
  return {aerie_widevine::ComponentUpdateUrl(ls, helium::GetComponentUpdateURL(p))};
}

std::vector<GURL> ChromeConfigurator::PingUrl() const {
  return {aerie_widevine::ComponentUpdateUrl(ls, helium::GetComponentUpdateURL(p))};
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
  AudioReadbackNoise noise(context);
  if (!noise.enabled()) {
    return;
  }
  for (const auto& channel : channels_) {
    noise.AddSource(channel->AsSpan());
  }
  uint64_t index = 0;
  for (const auto& channel : channels_) {
    for (float& sample : channel->AsSpan()) {
      sample = noise.Jitter(sample, index++);
    }
  }
}
"""

ANALYSER_NOISE_OK = """void RealtimeAnalyser::GetFloatTimeDomainData(ExecutionContext* context) {
    AudioReadbackNoise noise(context);
    if (noise.enabled()) {
      noise.JitterInPlace(destination.first(len));
    }
}
"""

AUDIO_CONTEXT_OK = """AudioReadbackNoise::AudioReadbackNoise(ExecutionContext* context) {
  const HeliumNoiseTokenMap& tokens = context->GetHeliumNoiseTokens();
  if (auto it = tokens.find(HeliumNoiseFeature::kAudio); it != tokens.end()) {
    hash_.emplace(it->second);
  }
}

float AudioReadbackNoise::Jitter(float value, uint64_t index) const {
  DCHECK(enabled());
  HeliumNoiseHash hash = *hash_;
  hash.Update(index);
  const int steps = hash.GetValueBelow(kMaxAudioJitterSteps + 1);
  return value;
}
"""
AUDIO_GOOD = {treegate.AUDIO_BUFFER_PATH: AUDIO_NOISE_OK, treegate.REALTIME_ANALYSER_PATH: ANALYSER_NOISE_OK,
              treegate.AUDIO_CONTEXT_PATH: AUDIO_CONTEXT_OK}

WORKLET_PROXY_OK = """  global_scope_creation_params->helium_noise_tokens =
      window->GetHeliumNoiseTokens();
"""
WORKLET_PROXY_HEADER_OK = "  bool CarriesNoiseTokens() const override { return true; }\n"
WORKLET_SCOPE_OK = """  SetReferrerPolicy(creation_params->referrer_policy);
  SetHeliumNoiseTokens(creation_params->helium_noise_tokens);
"""
WORKLET_PROCESSOR_OK = """  CopyPortToArrayBuffers(isolate, inputs, input_array_buffers_);
  JitterInputArrayBuffers(isolate, global_scope_, input_array_buffers_);
"""
WORKLET_JITTER_OK = """void JitterInputArrayBuffers(v8::Isolate* isolate, ExecutionContext* context,
                             const BackingArrayBuffers& array_buffers) {
  AudioReadbackNoise noise(context);
  if (!noise.enabled()) {
    return;
  }
  ForEachChannel(isolate, array_buffers, [&](base::span<float> c) { noise.AddSource(c); });
  uint64_t index = 0;
  ForEachChannel(isolate, array_buffers, [&](base::span<float> c) {
    for (float& sample : c) { sample = noise.Jitter(sample, index++); }
  });
}
"""
SCRIPT_PROCESSOR_OK = """  AudioReadbackNoise noise(GetExecutionContext());
  if (noise.enabled()) {
    noise.AddSource(external_input_buffer_->getChannelData(channel)->AsSpan());
    sample = noise.Jitter(sample, index++);
  }
"""
WORKLET_GOOD = {
    treegate.WORKLET_PROXY_PATH: WORKLET_PROXY_OK, treegate.WORKLET_PROXY_HEADER_PATH: WORKLET_PROXY_HEADER_OK, treegate.WORKLET_SCOPE_PATH: WORKLET_SCOPE_OK,
    treegate.WORKLET_PROCESSOR_PATH: WORKLET_PROCESSOR_OK + WORKLET_JITTER_OK,
    treegate.SCRIPT_PROCESSOR_PATH: SCRIPT_PROCESSOR_OK,
}

GPU_WEBGL_OK = """String UnmaskedGpuString() {
  if (base::FeatureList::IsEnabled(blink::features::kSpoofWebGLInfo)) return {};
  if (!ShouldMaskGpuInfoForContext(ExecutionContext::From(script_state))) return real;
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
  const bool masked = ShouldMaskGpuInfoForContext(context);
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
READPIXELS_WEBGL_OK = """
void WebGLRenderingContextBase::ReadPixelsHelper() {
  WebGLFramebuffer* framebuffer = GetReadFramebufferBinding();
  ContextGL()->ReadPixels(x, y, width, height, format, type, data);
  if (data && aerie_webgl_readback::ShouldNoiseReadPixels(
                  /*default_framebuffer=*/!framebuffer, format, type)) {
    if (auto layout = aerie_webgl_readback::ComputeRgbaLayout(
            width, height, pack.alignment, pack.row_length, pack.skip_pixels,
            pack.skip_rows)) {
      CanvasNoisingHelper::MaybeNoiseReadPixels(
          Host()->GetTopExecutionContext(), pixels, width, height, x, y,
          layout->first_pixel_offset, layout->row_stride);
    }
  }
}
"""
READPIXELS_HELPER_OK = """bool CanvasNoisingHelper::MaybeNoiseReadPixels(
    ExecutionContext* execution_context,
    base::span<uint8_t> buffer,
    int width) {
  std::optional<HeliumNoiseHash> token_hash =
      GetCanvasTokenHash(execution_context);
  if (!token_hash) {
    return false;
  }
  return HeliumNoisePixelBuffer(*token_hash, kRGBA_8888_SkColorType, buffer,
                                width, height, x, y);
}
"""
GPU_WEBGL_OK += READPIXELS_WEBGL_OK
GPU_GOOD = {treegate.WEBGL_CONTEXT_PATH: GPU_WEBGL_OK, treegate.GPU_ADAPTER_PATH: GPU_ADAPTER_OK}

WORKER_PARAMS_OK = "  HeliumNoiseTokenMap helium_noise_tokens;\n"
WORKER_SCOPE_OK = "  SetHeliumNoiseTokens(creation_params->helium_noise_tokens);\n"
WORKER_DEDICATED_OK = "  params->helium_noise_tokens = execution_context->GetHeliumNoiseTokens();\n"
WORKER_SHARED_IMPL_OK = "  creation_params->helium_noise_tokens = noise_tokens;\n"
WORKER_SERVICE_IMPL_OK = (
    "  global_scope_creation_params->helium_noise_tokens =\n"
    "      worker_start_data->noise_tokens;\n")
WORKER_TOKENS_OK = """blink::HeliumNoiseTokenMap HeliumNoiseTokenData::GetTokensForWorker(
    BrowserContext* context,
    const blink::StorageKey& storage_key,
    const StoragePartitionConfig& partition) {
  const blink::StorageKey partitioned_key =
      storage_key.CopyWithForceEnabledThirdPartyStoragePartitioning();
  const GURL top_level_url = partitioned_key.top_level_site().GetURL();
  if (!GetContentClient()->browser()->HeliumNoiseEnabled(context,
                                                         top_level_url)) {
    return blink::HeliumNoiseTokenMap();
  }
  return GetTokens(context, partitioned_key.top_level_site(), partition);
}
"""
WORKER_SHARED_SERVICE_OK = """  host->set_noise_tokens(creator.GetPage().noise_tokens());
"""
WORKER_SERVICE_HOST_OK = """    params->noise_tokens = HeliumNoiseTokenData::GetTokensForWorker(
        browser_context, key_,
        context_->wrapper()->storage_partition()->GetConfig());
"""
WORKER_GPU_OK = """inline bool ShouldMaskGpuInfoForContext(const ExecutionContext* context) {
  const bool has_token =
      context &&
      context->GetHeliumNoiseTokens().contains(HeliumNoiseFeature::kWebGL);
  return true;
}
"""
WORKER_GOOD = {
    treegate.WORKER_PARAMS_PATH: WORKER_PARAMS_OK,
    treegate.WORKER_SCOPE_PATH: WORKER_SCOPE_OK,
    treegate.WORKER_DEDICATED_PATH: WORKER_DEDICATED_OK,
    treegate.WORKER_SHARED_IMPL_PATH: WORKER_SHARED_IMPL_OK,
    treegate.WORKER_SERVICE_IMPL_PATH: WORKER_SERVICE_IMPL_OK,
    treegate.WORKER_TOKENS_PATH: WORKER_TOKENS_OK,
    treegate.SHARED_WORKER_SERVICE_IMPL_PATH: WORKER_SHARED_SERVICE_OK,
    treegate.WORKER_SERVICE_HOST_PATH: WORKER_SERVICE_HOST_OK,
    treegate.WORKER_GPU_PATH: WORKER_GPU_OK,
}

POPUP_MOJOM_OK = """struct CreateNewWindowReply {
  gfx.mojom.Rect? window_screen_rect;
  map<blink.mojom.HeliumNoiseFeature, blink.mojom.HeliumNoiseToken>
      noise_tokens;
};
"""
POPUP_BROWSER_OK = """  const blink::HeliumNoiseTokenMap noise_tokens =
      HeliumNoiseTokenData::GetTokensForMainFrame(
          GetBrowserContext(), new_main_rfh->GetLastCommittedOrigin(),
          new_main_rfh->GetSiteInstance()
              ->GetSecurityPrincipal()
              .GetStoragePartitionConfig());
  new_main_rfh->GetPage().set_noise_tokens(noise_tokens);
  mojom::CreateNewWindowReplyPtr reply = mojom::CreateNewWindowReply::New(
      /*window_screen_rect=*/std::nullopt, noise_tokens);
"""
POPUP_RENDERER_OK = """  view_params->noise_tokens = reply->noise_tokens;
"""
POPUP_GOOD = {
    treegate.POPUP_MOJOM_PATH: POPUP_MOJOM_OK,
    treegate.POPUP_BROWSER_PATH: POPUP_BROWSER_OK,
    treegate.POPUP_RENDERER_PATH: POPUP_RENDERER_OK,
}

TEXT_METRICS_CANVAS_OK = """TextMetrics* BaseRenderingContext2D::measureText(const String& text) {
  const double factor = TextMetricsNoiseFactor(
      GetExecutionContext(), TextMetricsNoiseKind::kMeasureText);
  if (factor != 1.0) {
    text_metrics->Shuffle(factor);
  }
  return text_metrics;
}
"""
TEXT_METRICS_DOCUMENT_OK = """double Document::GetNoiseFactorX() {
  return TextMetricsNoiseFactor(GetExecutionContext(),
                                TextMetricsNoiseKind::kClientRectsX);
}

double Document::GetNoiseFactorY() {
  return TextMetricsNoiseFactor(GetExecutionContext(),
                                TextMetricsNoiseKind::kClientRectsY);
}
"""
TEXT_METRICS_FACTOR_OK = """double TextMetricsNoiseFactor(const ExecutionContext* context,
                              TextMetricsNoiseKind kind) {
  HeliumNoiseHash hash(tokens.at(HeliumNoiseFeature::kTextMetrics));
  hash.Update(static_cast<uint64_t>(kind) + 1);
  return aerie_text_metrics::NoiseFactor(
      hash.GetValueBelow(aerie_text_metrics::kSteps));
}
"""
TEXT_METRICS_GOOD = {
    treegate.TEXT_METRICS_CANVAS_PATH: TEXT_METRICS_CANVAS_OK,
    treegate.TEXT_METRICS_DOCUMENT_PATH: TEXT_METRICS_DOCUMENT_OK,
    treegate.TEXT_METRICS_FACTOR_PATH: TEXT_METRICS_FACTOR_OK,
}

PARTITION_TOKENS_OK = """blink::HeliumNoiseTokenMap HeliumNoiseTokenData::GetTokens(
    BrowserContext* context,
    const net::SchemefulSite& top_level_site,
    const StoragePartitionConfig& partition) {
  std::string site;
  if (!top_level_site.opaque()) {
    site = aerie_fingerprint::NoiseSiteKey(
        url::Origin::Create(top_level_site.GetURL()),
        aerie_fingerprint::NoisePartitionKey(partition.partition_domain(),
                                             partition.partition_name()));
  }
  return {};
}
"""
PARTITION_NAV_OK = """    noise_tokens_ = HeliumNoiseTokenData::GetTokensForMainFrame(
          browser_context, origin_to_commit.value(),
          site_info_.GetStoragePartitionConfig());
"""
PARTITION_STORAGE_OK = """  shared_worker_service_ =
      std::make_unique<SharedWorkerServiceImpl>(this, service_worker_context_);
"""
PARTITION_SERVICE_HEADER_OK = "  const raw_ptr<StoragePartitionImpl> storage_partition_;\n"
PARTITION_GOOD = {
    treegate.STORAGE_PARTITION_IMPL_PATH: PARTITION_STORAGE_OK,
    treegate.SHARED_WORKER_SERVICE_HEADER_PATH: PARTITION_SERVICE_HEADER_OK,
    treegate.WORKER_TOKENS_PATH: PARTITION_TOKENS_OK + WORKER_TOKENS_OK,
    treegate.PARTITION_NAV_PATH: PARTITION_NAV_OK,
}

GOOD = {
    **JS_OPT_GOOD,
    **SERVICES_UI_GOOD,
    **AUDIO_GOOD,
    **WORKLET_GOOD,
    **GPU_GOOD,
    **WORKER_GOOD,
    **PARTITION_GOOD,
    **POPUP_GOOD,
    **TEXT_METRICS_GOOD,
    "components/private_ai/features.cc": PRIVATE_AI_OK,
    treegate.CANVAS_HELPER_PATH: READPIXELS_HELPER_OK,
    "chrome/browser/component_updater/widevine_cdm_component_installer.cc": WIDEVINE_INSTALLER_OK,
    "chrome/browser/component_updater/registration.cc": WIDEVINE_REG_OK,
    "components/update_client/update_checker.cc": WIDEVINE_CHECKER_OK,
    "components/update_client/ping_manager.cc": WIDEVINE_PING_OK,
    "chrome/browser/component_updater/chrome_component_updater_configurator.cc": WIDEVINE_CFG_OK,
    "third_party/search_engines_data/resources/definitions/regional_settings.json": SEARCH_OK,
    "chrome/browser/profiles/profile.cc": PROFILE_OK,
    "chrome/browser/net/default_dns_over_https_config_source.cc": DOH_OK,
    "components/crash/core/app/crash_reporter_client.cc": CRASH_OK,
    "components/crash/core/app/crashpad.cc": CRASHPAD_OK,
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

    def test_crash_reports_must_stay_on_the_device(self):
        # Issue #262: no way to switch uploads on, none to request one, no deletion of pending reports.
        crashpad = "components/crash/core/app/crashpad.cc"
        mode = "chrome/browser/crash_upload_list/helium_crash_reporting.cc"
        self.write({crashpad: CRASHPAD_OK, mode: CRASH_MODE_OK, "components/crash/core/app/crash_reporter_client.cc": CRASH_OK})
        self.assertEqual(treegate.check_crash_reporting(self.root), [])
        for bad in (CRASHPAD_OK.replace("SetUploadsEnabled(false)", "SetUploadsEnabled(consent)"),
                    CRASHPAD_OK.replace("// Aerie: never.\n}\n", "g_database->RequestUpload(uuid);\n}\n")):
            self.write({crashpad: bad})
            self.assertTrue(treegate.check_crash_reporting(self.root), bad)
        self.write({crashpad: CRASHPAD_OK})
        for bad in (CRASH_MODE_OK + "\nvoid F() { crash_reporter::SetUploadConsentFromUser(true); }\n",
                    CRASH_MODE_OK + "\nvoid G() { DeletePendingReports(true); }\n"):
            self.write({mode: bad})
            self.assertTrue(treegate.check_crash_reporting(self.root), bad)

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

    def test_the_private_ai_client_must_stay_off(self):
        path = "components/private_ai/features.cc"
        self.write({path: PRIVATE_AI_OK})
        self.assertEqual(treegate.check_private_ai_off(self.root, require=True), [])
        self.write({path: PRIVATE_AI_OK.replace("kPrivateAi, base::FEATURE_DISABLED", "kPrivateAi, base::FEATURE_ENABLED")})
        self.assertEqual([f.gate for f in treegate.check_private_ai_off(self.root)], ["private_ai_off"])
        (self.root / path).unlink()
        self.assertEqual(treegate.check_private_ai_off(self.root), [])
        self.assertEqual(len(treegate.check_private_ai_off(self.root, require=True)), 1)
        self.assertIn(treegate.check_private_ai_off, treegate.GATES)

    def test_link_prefetch_must_follow_the_preloading_pref(self):
        client = "chrome/browser/chrome_content_browser_client.cc"
        blink = "third_party/blink/renderer/core/loader/preload_helper.cc"
        self.write({client: LINK_PREFETCH_CLIENT_OK, blink: LINK_PREFETCH_BLINK_OK})
        self.assertEqual(treegate.check_link_prefetch_off(self.root), [])
        self.write({blink: LINK_PREFETCH_BLINK_OK.replace("!document.GetSettings()->GetDNSPrefetchingEnabled()", "false")})
        self.assertEqual([f.gate for f in treegate.check_link_prefetch_off(self.root)], ["link_prefetch_off"])
        self.write({blink: LINK_PREFETCH_BLINK_OK, client: LINK_PREFETCH_CLIENT_OK.replace("kEligible", "kUnspecified")})
        self.assertEqual([f.gate for f in treegate.check_link_prefetch_off(self.root)], ["link_prefetch_off"])

    def test_audio_noise_must_come_from_the_token_and_the_data_with_no_counter(self):
        self.write(AUDIO_GOOD)
        self.assertEqual(treegate.check_audio_noise_stable(self.root, require=True), [])
        ctx = treegate.AUDIO_CONTEXT_PATH
        for text in (AUDIO_CONTEXT_OK.replace("hash.Update(index);", "hash.Update(index ^ g_render_count++);"),
                     AUDIO_CONTEXT_OK.replace("HeliumNoiseFeature::kAudio", "HeliumNoiseFeature::kCanvas"),
                     AUDIO_CONTEXT_OK.replace("HeliumNoiseHash hash = *hash_;", "HeliumNoiseHash hash(base::RandUint64());"),
                     ""):
            self.write(AUDIO_GOOD)
            self.write({ctx: text})
            self.assertEqual({f.path for f in treegate.check_audio_noise_stable(self.root)}, {ctx}, text)

    def test_audio_buffer_and_analyser_must_use_the_readback_noise(self):
        for path, text in ((treegate.AUDIO_BUFFER_PATH, AUDIO_NOISE_OK.replace("noise.AddSource(channel->AsSpan());", "")),
                           (treegate.AUDIO_BUFFER_PATH, AUDIO_NOISE_OK.replace("index++", "g_helium_audio_render_sequence++")),
                           (treegate.AUDIO_BUFFER_PATH, ""),
                           (treegate.REALTIME_ANALYSER_PATH, ANALYSER_NOISE_OK.replace("AudioReadbackNoise noise(context);", "")),
                           (treegate.REALTIME_ANALYSER_PATH, "")):
            self.write(AUDIO_GOOD)
            self.write({path: text})
            self.assertEqual({f.path for f in treegate.check_audio_noise_stable(self.root)}, {path}, text)

    def test_a_per_render_counter_anywhere_in_webaudio_is_flagged(self):
        self.write(AUDIO_GOOD)
        other = "third_party/blink/renderer/modules/webaudio/analyser_handler.cc"
        self.write({other: "uint64_t g_helium_audio_analyzer_read_sequence = 0;\n"})
        self.assertEqual([f.path for f in treegate.check_audio_noise_stable(self.root)], [other])

    def test_audio_worklet_input_must_be_noised_with_the_creators_tokens(self):
        # Issue #157: an AudioWorkletProcessor and a ScriptProcessorNode read the audio before it leaves the graph.
        self.write(WORKLET_GOOD)
        self.assertEqual(treegate.check_audio_worklet_noise(self.root, require=True), [])
        self.assertIn(treegate.check_audio_worklet_noise, treegate.GATES)
        for path, text in (
                (treegate.WORKLET_PROXY_PATH, ""),
                (treegate.WORKLET_PROXY_HEADER_PATH, WORKLET_PROXY_HEADER_OK.replace("true", "false")),
                (treegate.WORKLET_SCOPE_PATH, WORKLET_SCOPE_OK.replace("SetHeliumNoiseTokens", "Nothing")),
                (treegate.WORKLET_PROCESSOR_PATH, WORKLET_PROCESSOR_OK.replace("JitterInputArrayBuffers(isolate, global_scope_, input_array_buffers_);", "")
                 + WORKLET_JITTER_OK),
                (treegate.WORKLET_PROCESSOR_PATH, WORKLET_PROCESSOR_OK + WORKLET_JITTER_OK.replace("noise.Jitter(sample, index++)", "sample")),
                (treegate.WORKLET_PROCESSOR_PATH, WORKLET_PROCESSOR_OK + WORKLET_JITTER_OK.replace("AudioReadbackNoise noise(context);", "")),
                (treegate.SCRIPT_PROCESSOR_PATH, SCRIPT_PROCESSOR_OK.replace("AudioReadbackNoise noise(GetExecutionContext());", ""))):
            self.write(WORKLET_GOOD)
            self.write({path: text})
            self.assertEqual({f.path for f in treegate.check_audio_worklet_noise(self.root)}, {path}, text)

    def test_the_worklet_gate_follows_require(self):
        self.assertEqual(treegate.check_audio_worklet_noise(self.root), [])
        self.assertEqual(len(treegate.check_audio_worklet_noise(self.root, require=True)), 6)

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
        copied = GPU_WEBGL_OK.replace("ShouldMaskGpuInfoForContext(", "aerie_gpu_info::ShouldMaskGpuInfo(")
        for text in (raw, stacked, copied, ""):
            self.write({path: text})
            self.assertEqual({f.path for f in treegate.check_gpu_masking(self.root)}, {path})

    def test_webgpu_adapter_info_must_be_emptied_when_masked(self):
        path = treegate.GPU_ADAPTER_PATH
        for bad in (GPU_ADAPTER_OK.replace("masked ? g_empty_string : architecture_", "architecture_"),
                    GPU_ADAPTER_OK.replace("masked ? g_empty_string : description_", "description_"),
                    GPU_ADAPTER_OK.replace("ShouldMaskGpuInfoForContext(", "aerie_gpu_info::ShouldMaskGpuInfo("),
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


class WorkerNoiseTokensGateTest(TreeCase):
    """Issue #113: every kind of worker gets the per-site tokens, from the right site, and never the session token."""

    def findings(self):
        return treegate.check_worker_noise_tokens(self.root)

    def test_passes_when_every_worker_kind_is_wired(self):
        self.write(WORKER_GOOD)
        self.assertEqual(treegate.check_worker_noise_tokens(self.root, require=True), [])

    def test_each_missing_hand_off_is_reported_on_its_file(self):
        bad = {
            treegate.WORKER_PARAMS_PATH: "",
            treegate.WORKER_SCOPE_PATH: "  SetHeliumNoiseTokens({});\n",
            treegate.WORKER_DEDICATED_PATH: "",
            treegate.WORKER_SHARED_IMPL_PATH: "",
            treegate.WORKER_SERVICE_IMPL_PATH: "",
            treegate.SHARED_WORKER_SERVICE_IMPL_PATH: "",
            treegate.WORKER_SERVICE_HOST_PATH: "",
        }
        for path, text in bad.items():
            with self.subTest(path=path):
                self.write(WORKER_GOOD)
                self.write({path: text})
                self.assertEqual({f.path for f in self.findings()}, {path})

    def test_worker_tokens_must_come_from_the_top_level_site_and_the_switch(self):
        for text in (WORKER_TOKENS_OK.replace("partitioned_key.top_level_site().GetURL()", "storage_key.origin().GetURL()")
                     .replace("return GetTokens(context, partitioned_key.top_level_site()", "return GetTokens(context, storage_key.origin()"),
                     WORKER_TOKENS_OK.replace("HeliumNoiseEnabled(context,", "(void)(context,"),
                     WORKER_TOKENS_OK.replace("return GetTokens(", "return data->session_tokens_; (")):
            self.write(WORKER_GOOD)
            self.write({treegate.WORKER_TOKENS_PATH: text})
            self.assertEqual({f.path for f in self.findings()}, {treegate.WORKER_TOKENS_PATH}, text)

    def test_a_worker_must_not_be_always_masked_again(self):
        self.write(WORKER_GOOD)
        self.write({treegate.WORKER_GPU_PATH: WORKER_GPU_OK.replace(
            "const bool has_token", "const bool is_worker = !context || context->IsWorkerOrWorkletGlobalScope();\n  const bool has_token")})
        self.assertEqual({f.path for f in self.findings()}, {treegate.WORKER_GPU_PATH})

    def test_the_gate_runs_with_the_others(self):
        self.assertIn(treegate.check_worker_noise_tokens, treegate.GATES)


class WorkspaceNoisePartitionGateTest(TreeCase):
    """Issue #121: the token key carries the storage partition, at every place that asks for tokens."""

    def good(self):
        self.write(WORKER_GOOD)
        self.write(POPUP_GOOD)
        self.write(PARTITION_GOOD)

    def findings(self):
        return treegate.check_workspace_noise_partition(self.root)

    def test_passes_when_every_caller_passes_its_partition(self):
        self.good()
        self.assertEqual(treegate.check_workspace_noise_partition(self.root, require=True), [])

    def test_the_key_must_include_the_partition(self):
        self.good()
        self.write({treegate.WORKER_TOKENS_PATH: (PARTITION_TOKENS_OK + WORKER_TOKENS_OK).replace(
            "url::Origin::Create(top_level_site.GetURL()),\n        aerie_fingerprint::NoisePartitionKey(partition.partition_domain(),\n                                             partition.partition_name())",
            "url::Origin::Create(top_level_site.GetURL())")})
        self.assertEqual({f.path for f in self.findings()}, {treegate.WORKER_TOKENS_PATH})

    def test_a_worker_must_pass_its_partition_on(self):
        self.good()
        self.write({treegate.WORKER_TOKENS_PATH: PARTITION_TOKENS_OK + WORKER_TOKENS_OK.replace(
            ", partition);", ");")})
        self.assertEqual({f.path for f in self.findings()}, {treegate.WORKER_TOKENS_PATH})

    def test_each_caller_is_reported_on_its_file(self):
        bad = {
            treegate.PARTITION_NAV_PATH: PARTITION_NAV_OK.replace("site_info_.GetStoragePartitionConfig()", "{}"),
            treegate.POPUP_BROWSER_PATH: POPUP_BROWSER_OK.replace(
                "GetSecurityPrincipal()\n              .GetStoragePartitionConfig()", "GetSecurityPrincipal()\n              .GetSomethingElse()"),
            treegate.WORKER_SERVICE_HOST_PATH: WORKER_SERVICE_HOST_OK.replace(
                "context_->wrapper()->storage_partition()->GetConfig()", "{}"),
        }
        for path, text in bad.items():
            with self.subTest(path=path):
                self.good()
                self.write({path: text})
                self.assertIn(path, {f.path for f in self.findings()})

    def test_a_shared_worker_service_must_belong_to_one_storage_partition(self):
        for path, text in ((treegate.STORAGE_PARTITION_IMPL_PATH,
                            PARTITION_STORAGE_OK.replace("make_unique<SharedWorkerServiceImpl>(this,", "make_unique<SharedWorkerServiceImpl>(nullptr,")),
                           (treegate.SHARED_WORKER_SERVICE_HEADER_PATH, "")):
            self.good()
            self.write({path: text})
            self.assertEqual({f.path for f in self.findings()}, {path})

    def test_the_gate_runs_with_the_others(self):
        self.assertIn(treegate.check_workspace_noise_partition, treegate.GATES)


class PopupNoiseTokensGateTest(TreeCase):
    """Issue #156: a window that a page opens gets its opener's tokens when it is created."""

    def test_passes_when_the_tokens_reach_the_new_view(self):
        self.write(POPUP_GOOD)
        self.assertEqual(treegate.check_popup_noise_tokens(self.root, require=True), [])

    def test_each_hand_off_is_reported_on_its_file(self):
        bad = {
            treegate.POPUP_MOJOM_PATH: POPUP_MOJOM_OK.replace("noise_tokens", "tokens"),
            treegate.POPUP_BROWSER_PATH: POPUP_BROWSER_OK.replace(
                "new_main_rfh->GetPage().set_noise_tokens(noise_tokens);\n", ""),
            treegate.POPUP_RENDERER_PATH: "",
        }
        for path, text in bad.items():
            with self.subTest(path=path):
                self.write(POPUP_GOOD)
                self.write({path: text})
                self.assertEqual({f.path for f in treegate.check_popup_noise_tokens(self.root)}, {path})

    def test_the_browser_must_send_the_tokens_in_the_reply(self):
        self.write(POPUP_GOOD)
        self.write({treegate.POPUP_BROWSER_PATH: POPUP_BROWSER_OK.replace(
            "std::nullopt, noise_tokens);", "std::nullopt, {});")})
        self.assertEqual({f.path for f in treegate.check_popup_noise_tokens(self.root)}, {treegate.POPUP_BROWSER_PATH})

    def test_the_gate_runs_with_the_others(self):
        self.assertIn(treegate.check_popup_noise_tokens, treegate.GATES)


class TextMetricsNoiseGateTest(TreeCase):
    """Issue #118: measureText and client rect noise come from the site's token, never from a per-document
    random factor (the Bromite patch's base::RandDouble)."""

    def test_passes_when_the_factor_comes_from_the_token(self):
        self.write(TEXT_METRICS_GOOD)
        self.assertEqual(treegate.check_text_metrics_noise(self.root, require=True), [])

    def test_a_missing_file_is_reported_only_when_required(self):
        self.assertEqual(treegate.check_text_metrics_noise(self.root), [])
        self.assertEqual(len(treegate.check_text_metrics_noise(self.root, require=True)), 3)

    def test_each_part_is_guarded(self):
        bad = {
            treegate.TEXT_METRICS_CANVAS_PATH: TEXT_METRICS_CANVAS_OK.replace("TextMetricsNoiseFactor(", "GetNoiseFactorX("),
            treegate.TEXT_METRICS_DOCUMENT_PATH: TEXT_METRICS_DOCUMENT_OK.replace(
                "return TextMetricsNoiseFactor(GetExecutionContext(),\n                                TextMetricsNoiseKind::kClientRectsX);",
                "return noise_factor_x_;"),
            treegate.TEXT_METRICS_FACTOR_PATH: TEXT_METRICS_FACTOR_OK.replace("HeliumNoiseFeature::kTextMetrics",
                                                                              "HeliumNoiseFeature::kCanvas"),
        }
        for path, text in bad.items():
            with self.subTest(path=path):
                self.write(TEXT_METRICS_GOOD)
                self.write({path: text})
                self.assertEqual({f.path for f in treegate.check_text_metrics_noise(self.root)}, {path})

    def test_a_per_document_random_factor_is_reported(self):
        for path, text in (
                (treegate.TEXT_METRICS_CANVAS_PATH, TEXT_METRICS_CANVAS_OK + "double f = 1 + (base::RandDouble() - 0.5) * 0.000003;\n"),
                (treegate.TEXT_METRICS_DOCUMENT_PATH, TEXT_METRICS_DOCUMENT_OK + "noise_factor_x_ = 1 + (base::RandDouble() - 0.5);\n"),
                (treegate.TEXT_METRICS_FACTOR_PATH, TEXT_METRICS_FACTOR_OK + "double r = base::RandDouble();\n")):
            with self.subTest(path=path):
                self.write(TEXT_METRICS_GOOD)
                self.write({path: text})
                self.assertEqual({f.path for f in treegate.check_text_metrics_noise(self.root)}, {path})

    def test_the_gate_runs_with_the_others(self):
        self.assertIn(treegate.check_text_metrics_noise, treegate.GATES)


class WebglReadPixelsGateTest(TreeCase):
    """Issue #119: readPixels from the default framebuffer is noised with the canvas token, nothing else is."""

    def test_passes_when_only_the_default_framebuffer_is_noised(self):
        self.write({treegate.WEBGL_CONTEXT_PATH: READPIXELS_WEBGL_OK, treegate.CANVAS_HELPER_PATH: READPIXELS_HELPER_OK})
        self.assertEqual(treegate.check_webgl_readpixels(self.root, require=True), [])

    def test_noise_for_every_framebuffer_is_reported(self):
        self.write({treegate.CANVAS_HELPER_PATH: READPIXELS_HELPER_OK})
        for bad in (READPIXELS_WEBGL_OK.replace("/*default_framebuffer=*/!framebuffer", "/*default_framebuffer=*/true"),
                    READPIXELS_WEBGL_OK.replace("ShouldNoiseReadPixels(", "ShouldNoiseSomething("),
                    READPIXELS_WEBGL_OK.replace("CanvasNoisingHelper::MaybeNoiseReadPixels(", "Other("),
                    ""):
            with self.subTest(bad=bad[:60]):
                self.write({treegate.WEBGL_CONTEXT_PATH: bad})
                self.assertEqual({f.path for f in treegate.check_webgl_readpixels(self.root)},
                                 {treegate.WEBGL_CONTEXT_PATH})

    def test_the_helper_must_use_the_canvas_token(self):
        self.write({treegate.WEBGL_CONTEXT_PATH: READPIXELS_WEBGL_OK})
        for bad in (READPIXELS_HELPER_OK.replace("GetCanvasTokenHash(execution_context)", "std::optional<HeliumNoiseHash>()"),
                    READPIXELS_HELPER_OK.replace("if (!token_hash) {\n    return false;\n  }\n", ""),
                    ""):
            with self.subTest(bad=bad[:60]):
                self.write({treegate.CANVAS_HELPER_PATH: bad})
                self.assertEqual({f.path for f in treegate.check_webgl_readpixels(self.root)},
                                 {treegate.CANVAS_HELPER_PATH})

    def test_the_gate_runs_with_the_others(self):
        self.assertIn(treegate.check_webgl_readpixels, treegate.GATES)


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
        self.assertEqual(len(self.run_gates(require=True)), 71)

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


class RequestPolicyGateTest(unittest.TestCase):
    """The request policy (issue #257) stays hooked into URL requests, preconnect and name lookups."""

    DELEGATE = "aerie_network::ClassifyFactory(f); aerie_network::Decide(x); net::ERR_BLOCKED_BY_ADMINISTRATOR;"
    CONTEXT = "aerie_network::DecideConnection(a); aerie_network::DecideConnection(b);"

    def tree(self, delegate, context):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        for rel, text in ((treegate.DELEGATE_PATH, delegate), (treegate.NETWORK_CONTEXT_PATH, context)):
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text)
        return root

    def test_the_hooks_in_place_pass(self):
        self.assertEqual(treegate.check_request_policy(self.tree(self.DELEGATE, self.CONTEXT), require=True), [])

    def test_a_missing_judgement_of_urls_is_found(self):
        found = treegate.check_request_policy(self.tree("nothing", self.CONTEXT), require=True)
        self.assertEqual(len(found), 3)

    def test_a_missing_hook_in_preconnect_or_resolve_is_found(self):
        found = treegate.check_request_policy(
            self.tree(self.DELEGATE, "aerie_network::DecideConnection(a);"), require=True)
        self.assertEqual([f.path for f in found], [treegate.NETWORK_CONTEXT_PATH])

    def test_a_tree_with_none_of_the_files_is_skipped_unless_required(self):
        empty = tempfile.TemporaryDirectory()
        self.addCleanup(empty.cleanup)
        self.assertEqual(treegate.check_request_policy(Path(empty.name)), [])
        self.assertTrue(treegate.check_request_policy(Path(empty.name), require=True))
