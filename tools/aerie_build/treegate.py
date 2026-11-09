"""Source gates on a patched Chromium tree: cheap checks that need no build.

They catch the regressions the Bench would only find after a full build and a
six minute run: a Helium hostname back in source, a changed search list, a
network default flipped by a Helium bump. Each gate reads a few files and
looks for the text Aerie's patches put there. They run at the end of
``./aerie patches check`` (on the sparse tree) and on a real tree with
``./aerie patches gates``.

A gate whose file is not in the tree is skipped, unless ``require`` is set.
The sparse tree holds only the files the patch series touch, and every file a
gate reads is one of them.
"""

from __future__ import annotations

import fnmatch
import json
import os
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

HELIUM_HOSTS = re.compile(r"imput\.net|helium\.computer|imput\.dev|imputnet")

# Paths where a Helium name is expected and is not network traffic from the browser.
# Every entry needs a reason. Do not add a path that makes requests on its own.
HOST_ALLOWLIST: tuple[tuple[str, str], ...] = (
    ("AUTHORS", "credits"),
    ("tools/licenses/licenses.py", "license metadata for the build tools"),
    ("chrome/installer/linux/common/chromium-browser.info", "package metadata (maintainer, site); the browser never reads it"),
    ("net/http/transport_security_state_static.json", "HSTS preload list entry: data, not a request"),
    ("third_party/helium/*", "Helium's own README for the third_party entry"),
    ("third_party/ublock/*", "uBlock Origin fork: list headers and links in its pages, which the user clicks"),
    ("components/helium_onboarding/*", "Helium's old onboarding page, still unpacked in trees prepared before issue #46; nothing builds it"),
    ("components/crash/core/browser/resources/crashes.ts", "link the user clicks on chrome://crashes"),
    ("chrome/browser/ui/webui/settings/brave_full_disk_access_confirm_dialog_delegate_mac.mm", "help article link, macOS only, opened by the user"),
)

_SKIP_DIRS = {".git", "out", "node_modules"}
_MAX_BYTES = 2_000_000


@dataclass(frozen=True)
class Finding:
    gate: str
    path: str
    message: str
    line: int = 0


def _allowed(rel: str) -> bool:
    return any(fnmatch.fnmatchcase(rel, glob) for glob, _ in HOST_ALLOWLIST)


def scan_helium_hosts(root: Path) -> list[Finding]:
    """Every line of source text that names a Helium or imput host."""
    root = Path(root)
    found: list[Finding] = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if d not in _SKIP_DIRS)
        for name in sorted(filenames):
            path = Path(dirpath) / name
            rel = path.relative_to(root).as_posix()
            if _allowed(rel) or path.is_symlink():
                continue
            try:
                if path.stat().st_size > _MAX_BYTES:
                    continue
                data = path.read_bytes()
            except OSError:
                continue
            if b"\0" in data[:4096]:
                continue
            for no, line in enumerate(data.decode("utf-8", errors="replace").splitlines(), start=1):
                if HELIUM_HOSTS.search(line):
                    found.append(Finding("helium_hosts", rel, line.strip()[:160], no))
    return found


def _read(root: Path, rel: str, gate: str, require: bool) -> tuple[str | None, list[Finding]]:
    path = Path(root) / rel
    if not path.is_file():
        return None, ([Finding(gate, rel, "file is not in the tree")] if require else [])
    return path.read_text(encoding="utf-8", errors="replace"), []


SEARCH_LIST = "third_party/search_engines_data/resources/definitions/regional_settings.json"
WANTED_ENGINES = ["&duckduckgo", "&brave", "&kagi", "&startpage"]


def check_search_list(root: Path, require: bool = False) -> list[Finding]:
    gate = "search_list"
    text, missing = _read(root, SEARCH_LIST, gate, require)
    if text is None:
        return missing
    body = "\n".join(line for line in text.splitlines() if not line.lstrip().startswith("//"))
    try:
        elements = json.loads(body)["elements"]
        engines = elements["ZZ"]["search_engines"]
    except (ValueError, KeyError, TypeError) as exc:
        return [Finding(gate, SEARCH_LIST, f"cannot read the default list: {exc!r}")]
    out = []
    if engines != WANTED_ENGINES:
        out.append(Finding(gate, SEARCH_LIST,
                           f"default list is {engines}, expected {WANTED_ENGINES} (duckduckgo first)"))
    elif set(elements) != {"ZZ"}:
        out.append(Finding(gate, SEARCH_LIST,
                           f"other region lists are live: {sorted(set(elements) - {'ZZ'})}"))
    return out


def _must_match(root: Path, rel: str, gate: str, pattern: str, message: str, require: bool) -> list[Finding]:
    text, missing = _read(root, rel, gate, require)
    if text is None:
        return missing
    return [] if re.search(pattern, text, re.S) else [Finding(gate, rel, message)]


def check_search_suggestions(root: Path, require: bool = False) -> list[Finding]:
    return _must_match(
        root, "chrome/browser/profiles/profile.cc", "search_suggestions",
        r"RegisterBooleanPref\(\s*prefs::kSearchSuggestEnabled,\s*false\s*,",
        "search suggestions must be registered with default false", require)


def check_secure_dns_default(root: Path, require: bool = False) -> list[Finding]:
    return _must_match(
        root, "chrome/browser/net/default_dns_over_https_config_source.cc", "secure_dns_default",
        r"SetDefaultPrefValue\(\s*prefs::kDnsOverHttpsMode,[^;]*SecureDnsMode::kOff",
        "secure DNS must default to off (SecureDnsMode::kOff)", require)


def check_crash_reporting(root: Path, require: bool = False) -> list[Finding]:
    out = _must_match(
        root, "components/crash/core/app/crash_reporter_client.cc", "crash_reporting",
        r"CrashReporterClient::GetUploadUrl\(\)\s*\{(?:\s*//[^\n]*)*\s*return std::string\(\);\s*\}",
        "GetUploadUrl() must return an empty string and nothing else", require)
    out += _must_match(
        root, "chrome/browser/crash_upload_list/helium_crash_reporting.cc", "crash_reporting",
        r"CrashReportingMode GetCrashReportingMode\(\)\s*\{(?:\s*//[^\n]*)*\s*return CrashReportingMode::kDisabled;\s*\}",
        "GetCrashReportingMode() must always return kDisabled", require)
    return out


def check_fedcm_resolver(root: Path, require: bool = False) -> list[Finding]:
    return _must_match(
        root, "chrome/browser/chrome_content_browser_client.cc", "fedcm_resolver",
        r"ChromeContentBrowserClient::GetDnsTxtResolverUrlPrefix\(\)\s*\{(?:\s*//[^\n]*)*\s*return std::string\(\);\s*\}",
        "GetDnsTxtResolverUrlPrefix() must return an empty string (no built-in dns.google)", require)


def check_services_off(root: Path, require: bool = False) -> list[Finding]:
    return _must_match(
        root, "components/helium_services/helium_services_helpers.cc", "services_off",
        r"bool ShouldAccessServices\(const PrefService& prefs\)\s*\{\s*return false;\s*\}",
        "ShouldAccessServices() must return false", require)


def check_https_first_dialog_off(root: Path, require: bool = False) -> list[Finding]:
    return _must_match(
        root, "components/security_interstitials/core/features.cc", "https_first_dialog_off",
        r"BASE_FEATURE\(\s*kHttpsFirstDialogUi,\s*base::FEATURE_DISABLED_BY_DEFAULT\s*\)",
        "kHttpsFirstDialogUi must be FEATURE_DISABLED_BY_DEFAULT (full HTTPS-only warning page)",
        require)


def check_link_prefetch_off(root: Path, require: bool = False) -> list[Finding]:
    out = _must_match(
        root, "chrome/browser/chrome_content_browser_client.cc", "link_prefetch_off",
        r"web_prefs->dns_prefetching_enabled\s*=\s*prefetch::IsSomePreloadingEnabled\(\*prefs\)\s*==\s*"
        r"content::PreloadingEligibility::kEligible;",
        "OverrideWebPreferences must set dns_prefetching_enabled from IsSomePreloadingEnabled", require)
    out += _must_match(
        root, "third_party/blink/renderer/core/loader/preload_helper.cc", "link_prefetch_off",
        r"!document\.GetSettings\(\)->GetDNSPrefetchingEnabled\(\)\)\s*return;\s*UseCounter::Count\(document, WebFeature::kLinkRelPrefetch\)",
        "PrefetchIfNeeded must return early when GetDNSPrefetchingEnabled() is false", require)
    return out


WIDEVINE_REGISTRATION = "chrome/browser/component_updater/registration.cc"
WIDEVINE_CONFIGURATOR = "chrome/browser/component_updater/chrome_component_updater_configurator.cc"
WIDEVINE_UPDATE_CHECKER = "components/update_client/update_checker.cc"
WIDEVINE_PING_MANAGER = "components/update_client/ping_manager.cc"
WIDEVINE_INSTALLER = "chrome/browser/component_updater/widevine_cdm_component_installer.cc"
WIDEVINE_OPT_IN_HEADER = "components/aerie_widevine/widevine_opt_in.h"
# The first 16 bytes of the public key hash that the Widevine CRX must be signed with (extension id
# oimompecagnajdejgnnjijobebaeigek). The update client checks the CRX signature against this hash
# before it unpacks the component, so a changed hash would let another key's CRX load. The extension
# id is these 16 bytes written as letters (each nibble n is "a" + n), which is the app id that the
# update checker sends, so aerie_widevine::kWidevineAppId must equal it.
WIDEVINE_KEY_HASH = (r"0xe8,\s*0xce,\s*0xcf,\s*0x42,\s*0x06,\s*0xd0,\s*0x93,\s*0x49,\s*"
                     r"0x6d,\s*0xd9,\s*0x89,\s*0xe1,\s*0x41,\s*0x04,\s*0x86,\s*0x4a")
WIDEVINE_APP_ID = "oimompecagnajdejgnnjijobebaeigek"


def _app_id_of_hash(first16: bytes) -> str:
    return "".join(chr(ord("a") + (b >> 4)) + chr(ord("a") + (b & 15)) for b in first16)


def check_widevine_opt_in(root: Path, require: bool = False) -> list[Finding]:
    """Widevine is registered with the component updater only through the opt-in gate, and the
    component update URL goes through it, and the update check and ping leave out every app but
    Widevine while the user is opted in (Google's server sees nothing else). A Helium or Chromium bump that puts a bare
    ``RegisterWidevineCdmComponent(cus)`` back would download Widevine for everyone."""
    gate = "widevine_opt_in"
    out: list[Finding] = []
    text, missing = _read(root, WIDEVINE_REGISTRATION, gate, require)
    out += missing
    if text is not None:
        if not re.search(r"aerie_widevine::RegisterWhenEnabled\(", text):
            out.append(Finding(gate, WIDEVINE_REGISTRATION, "Widevine must be registered through aerie_widevine::RegisterWhenEnabled()"))
        if re.search(r"(?<![\w:])RegisterWidevineCdmComponent\(\s*cus\s*\)", text):
            out.append(Finding(gate, WIDEVINE_REGISTRATION, "RegisterWidevineCdmComponent(cus) is called unconditionally"))
    text, missing = _read(root, WIDEVINE_CONFIGURATOR, gate, require)
    out += missing
    if text is not None:
        for fn in ("UpdateUrl", "PingUrl"):
            m = re.search(r"ChromeConfigurator::%s\(\)\s*const\s*\{(.*?)\n\}" % fn, text, re.S)
            if not m or "aerie_widevine::ComponentUpdateUrl(" not in m.group(1):
                out.append(Finding(gate, WIDEVINE_CONFIGURATOR,
                                   f"{fn}() must go through aerie_widevine::ComponentUpdateUrl()"))
        m = re.search(r"ChromeConfigurator::ShouldSendToServer\([^)]*\)\s*const\s*\{(.*?)\n\}", text, re.S)
        if not m or "aerie_widevine::ShouldSendToUpdateServer(" not in m.group(1):
            out.append(Finding(gate, WIDEVINE_CONFIGURATOR,
                               "ShouldSendToServer() must answer through aerie_widevine::ShouldSendToUpdateServer()"))
    for rel in (WIDEVINE_UPDATE_CHECKER, WIDEVINE_PING_MANAGER):
        out += _must_match(
            root, rel, gate, r"config_->ShouldSendToServer\(\s*(?:app_id|component\.app_id)\s*,\s*url\s*\)",
            "the request must leave out apps that config_->ShouldSendToServer(app_id, url) refuses for the URL "
            "it uses, or Google would be sent every registered component while the user is opted in", require)
    out += _must_match(
        root, WIDEVINE_INSTALLER, gate, r"kWidevineSha2Hash\[\]\s*=\s*\{\s*" + WIDEVINE_KEY_HASH,
        "the pinned Widevine signing key hash changed; the CRX signature check would accept another key",
        require)
    # The app id the filter lets through must be the pinned key's id, or the filter would let
    # through the wrong component (or none) once opted in.
    if _app_id_of_hash(bytes.fromhex("e8cecf4206d093496dd989e14104864a")) != WIDEVINE_APP_ID:
        out.append(Finding(gate, WIDEVINE_INSTALLER, "WIDEVINE_APP_ID does not match the pinned hash"))
    text, missing = _read(root, WIDEVINE_OPT_IN_HEADER, gate, False)
    if text is not None:
        m = re.search(r'kWidevineAppId\[\]\s*=\s*"([^"]*)"', text)
        if not m or m.group(1) != WIDEVINE_APP_ID:
            out.append(Finding(gate, WIDEVINE_OPT_IN_HEADER,
                               "kWidevineAppId must be the id of the pinned signing key hash (" + WIDEVINE_APP_ID + ")"))
    return out


def check_js_optimizer(root: Path, require: bool = False) -> list[Finding]:
    """The per-site JavaScript JIT toggle (Chromium's javascript-optimizer content setting) is whole:
    default allow, reachable in settings, and a process never serves two optimizer modes."""
    gate = "js_optimizer"
    out = _must_match(
        root, "components/content_settings/core/browser/content_settings_registry.cc", gate,
        r'Register\(ContentSettingsType::JAVASCRIPT_OPTIMIZER,\s*"javascript-optimizer",\s*CONTENT_SETTING_ALLOW\b',
        "the javascript-optimizer content setting must be registered with CONTENT_SETTING_ALLOW as default", require)
    out += _must_match(
        root, "chrome/browser/resources/settings/route.ts", gate,
        r"r\.SITE_SETTINGS_JAVASCRIPT_OPTIMIZER\s*=\s*r\.SITE_SETTINGS\.createChild\('v8'\)",
        "the JavaScript optimization settings route (content/v8) must exist", require)
    host = "content/browser/renderer_host/render_process_host_impl.cc"
    out += _must_match(
        root, host, gate,
        r"host->AreV8OptimizationsDisabled\(\)\s*!=\s*site_info\.are_v8_optimizations_disabled\(\)\)\s*\{\s*return false;",
        "IsSuitableHost must refuse a process whose optimizer mode differs from the site's", require)
    out += _must_match(
        root, host, gate,
        r'else if \(AreV8OptimizationsDisabled\(\)\)\s*\{\s*AppendToCommaSeparatedSwitch\(\s*command_line,\s*'
        r'blink::switches::kJavaScriptFlags,\s*"--disable-optimizing-compilers"\)',
        "a renderer with the optimizer off must start with --disable-optimizing-compilers", require)
    out += _must_match(
        root, "content/browser/site_info.cc", gate,
        r"are_v8_optimizations_disabled_\s*==\s*other\.are_v8_optimizations_disabled_",
        "SiteInfo::IsExactMatch must compare are_v8_optimizations_disabled", require)
    out += _must_match(
        root, "components/site_isolation/features.cc", gate,
        r"BASE_FEATURE\(kOriginIsolationForJsOptExceptions,\s*base::FEATURE_ENABLED_BY_DEFAULT\)",
        "kOriginIsolationForJsOptExceptions must be enabled by default", require)
    return out


SETTINGS_RES = "chrome/browser/resources/settings/"
PRIVACY_PAGE = SETTINGS_RES + "privacy_page/"
OLD_SERVICES_PAGE_FILES = (PRIVACY_PAGE + "services_page.ts", PRIVACY_PAGE + "services_page.html")
STRINGS_PROVIDER = "chrome/browser/ui/webui/settings/settings_localized_strings_provider.cc"
# The two callers that opened the old services route (the app menu command and the download bubble).
SERVICES_URL_OPENERS = ("chrome/browser/ui/chrome_pages.cc", "chrome/browser/download/download_ui_model.cc")
PROXY_DIALOG = "chrome/browser/extensions/api/webstore_private/chrome_webstore_private_api_delegate.cc"


def check_services_ui(root: Path, require: bool = False) -> list[Finding]:
    """No Helium services in the user interface (#23): no services page, route or row in Settings,
    the Languages page does not point at Helium servers, and the extension-proxy dialog text is
    localized. The one working control, the extension proxy address, is a row of the privacy page."""
    gate = "services_ui"
    out: list[Finding] = []
    for rel in OLD_SERVICES_PAGE_FILES:
        if (Path(root) / rel).exists():
            out.append(Finding(gate, rel, "the Helium services page must be removed"))
    # route.ts is not required here: the js_optimizer gate already reports it when it is missing.
    route, _ = _read(root, SETTINGS_RES + "route.ts", gate, False)
    if route is not None and "HELIUM_SERVICES" in route:
        out.append(Finding(gate, SETTINGS_RES + "route.ts", "the Helium services route must be removed"))
    for rel in SERVICES_URL_OPENERS:
        text, missing = _read(root, rel, gate, require)
        out += missing
        if text is not None and "settings/privacy/services" in text:
            out.append(Finding(gate, rel, "must open chrome://settings/privacy, not the removed services route"))
    html, missing = _read(root, PRIVACY_PAGE + "privacy_page.html", gate, require)
    out += missing
    if html is not None and "heliumServices" in html:
        out.append(Finding(gate, PRIVACY_PAGE + "privacy_page.html", "the Helium services row must be removed"))
    out += _must_match(
        root, PRIVACY_PAGE + "privacy_page.ts", gate,
        r"mirrorPref\(\s*'helium\.services\.origin_override'",
        "the privacy page must show the extension proxy address (helium.services.origin_override)", require)
    strings, missing = _read(root, STRINGS_PROVIDER, gate, require)
    out += missing
    if strings is not None and re.search(
            r'"languagesDictionaryDownloadErrorHelp",\s*IDS_\w*HELIUM', strings):
        out.append(Finding(gate, STRINGS_PROVIDER,
                           "the Languages dictionary message must not use the Helium services text"))
    dialog, missing = _read(root, PROXY_DIALOG, gate, require)
    out += missing
    if dialog is not None:
        if 'u"' in dialog:
            out.append(Finding(gate, PROXY_DIALOG, "the extension proxy dialog text must come from grd messages, not u\"...\" literals"))
        elif "IDS_AERIE_EXTENSION_PROXY_UNSET_" not in dialog:
            out.append(Finding(gate, PROXY_DIALOG, "the extension proxy dialog must use IDS_AERIE_EXTENSION_PROXY_UNSET_* messages"))
    return out


AUDIO_BUFFER_PATH = "third_party/blink/renderer/modules/webaudio/audio_buffer.cc"
REALTIME_ANALYSER_PATH = "third_party/blink/renderer/modules/webaudio/realtime_analyser.cc"


def check_audio_noise_stable(root: Path, require: bool = False) -> list[Finding]:
    """Helium's audio jitter is seeded from the site/session token and the sample coordinates only
    (issue #38): channel and frame for rendered buffers, domain constant and index for analyser
    reads. A per-render or per-read counter would let a tracker average the noise away."""
    gate = "audio_noise_stable"
    out = _must_match(
        root, AUDIO_BUFFER_PATH, gate,
        r"const uint64_t coordinate_seed =\s*\(static_cast<uint64_t>\(i\) << 48\) \^ frame;\s*"
        r"samples\[frame\] = BaseAudioContext::JitterAudioData\(\s*samples\[frame\],\s*context,\s*coordinate_seed\)",
        "the AudioBuffer jitter seed must be exactly (static_cast<uint64_t>(i) << 48) ^ frame, with no "
        "per-render counter", require)
    out += _must_match(
        root, REALTIME_ANALYSER_PATH, gate,
        r"uint64_t CoordinateSeed\(uint64_t domain, uint64_t index\) const \{[^}]*?return domain \^ index;\s*\}",
        "the analyser jitter seed must be exactly domain ^ index, with no per-read counter", require)
    return out


WEBGL_CONTEXT_PATH = "third_party/blink/renderer/modules/webgl/webgl_rendering_context_base.cc"
GPU_ADAPTER_PATH = "third_party/blink/renderer/modules/webgpu/gpu_adapter.cc"


def check_gpu_masking(root: Path, require: bool = False) -> list[Finding]:
    """The graphics card is not a fingerprint surface (issue #112): WEBGL_debug_renderer_info goes
    through the one masking helper, with no second SpoofWebGLInfo branch stacked on it, and WebGPU
    adapter info is built from values that are empty while masked."""
    gate = "gpu_masking"
    out: list[Finding] = []
    text, missing = _read(root, WEBGL_CONTEXT_PATH, gate, require)
    out += missing
    if text is not None:
        for case, flag in (("kUnmaskedRendererWebgl", "false"), ("kUnmaskedVendorWebgl", "true")):
            pattern = (r"case WebGLDebugRendererInfo::" + case + r":(?:(?!case WebGLDebugRendererInfo::).)*?"
                       r"UnmaskedGpuString\(script_state,\s*/\*vendor=\*/" + flag)
            if not re.search(pattern, text, re.S):
                out.append(Finding(gate, WEBGL_CONTEXT_PATH,
                                   f"{case} must return UnmaskedGpuString(..., /*vendor=*/{flag}, ...), not the raw GL string"))
        if "ShouldMaskGpuInfoForContext(" not in text or "aerie_gpu_info::ShouldMaskGpuInfo(" in text:
            out.append(Finding(gate, WEBGL_CONTEXT_PATH,
                               "UnmaskedGpuString must decide with ShouldMaskGpuInfoForContext, the one shared helper, "
                               "not a copy of its logic"))
        if text.count("kSpoofWebGLInfo") > 1:
            out.append(Finding(gate, WEBGL_CONTEXT_PATH,
                               "kSpoofWebGLInfo may be read only inside UnmaskedGpuString, not in a second branch"))
    adapter, missing = _read(root, GPU_ADAPTER_PATH, gate, require)
    out += missing
    if adapter is not None:
        problems = []
        if "ShouldMaskGpuInfoForContext(" not in adapter or "aerie_gpu_info::ShouldMaskGpuInfo(" in adapter:
            problems.append("CreateAdapterInfoForAdapter must decide with ShouldMaskGpuInfoForContext, the one "
                            "shared helper, not a copy of its logic")
        for field in ("vendor", "architecture", "device", "description"):
            if f"{field} = masked ? g_empty_string : {field}_;" not in adapter:
                problems.append(f"WebGPU {field} must be empty while masked")
        if re.search(r"GPUAdapterInfo>\(\s*vendor_", adapter) or not re.search(
                r"GPUAdapterInfo>\(\s*vendor, architecture,", adapter):
            problems.append("GPUAdapterInfo must be built from the masked values, not vendor_ and architecture_")
        out += [Finding(gate, GPU_ADAPTER_PATH, m) for m in problems]
    return out


WORKER_PARAMS_PATH = "third_party/blink/renderer/core/workers/global_scope_creation_params.h"
WORKER_SCOPE_PATH = "third_party/blink/renderer/core/workers/worker_global_scope.cc"
WORKER_DEDICATED_PATH = "third_party/blink/renderer/core/workers/dedicated_worker.cc"
WORKER_SHARED_IMPL_PATH = "third_party/blink/renderer/core/exported/web_shared_worker_impl.cc"
WORKER_SERVICE_IMPL_PATH = "third_party/blink/renderer/modules/exported/web_embedded_worker_impl.cc"
WORKER_TOKENS_PATH = "content/browser/helium_noise/noise_token_data.cc"
WORKER_SHARED_HOST_PATH = "content/browser/worker_host/shared_worker_host.cc"
WORKER_SERVICE_HOST_PATH = "content/browser/service_worker/service_worker_version.cc"
WORKER_GPU_PATH = "third_party/blink/renderer/core/execution_context/gpu_info_masking.h"


def check_worker_noise_tokens(root: Path, require: bool = False) -> list[Finding]:
    """Workers get the fingerprinting noise tokens (issue #113). A dedicated worker takes its creator's tokens.
    A shared worker and a service worker get theirs from the browser, derived from the top-level site of the
    storage key and only when the switch is on for that site, and only the derived tokens, never the session
    tokens. WebGL in a worker follows its token like a window, not always masked."""
    gate = "worker_noise_tokens"
    out: list[Finding] = []
    for rel, pattern, message in (
        (WORKER_PARAMS_PATH, r"HeliumNoiseTokenMap\s+helium_noise_tokens;",
         "GlobalScopeCreationParams must carry helium_noise_tokens"),
        (WORKER_SCOPE_PATH, r"SetHeliumNoiseTokens\(\s*creation_params->helium_noise_tokens\s*\)",
         "WorkerGlobalScope must take its tokens from the creation params"),
        (WORKER_DEDICATED_PATH,
         r"params->helium_noise_tokens\s*=\s*execution_context->GetHeliumNoiseTokens\(\)",
         "a dedicated worker must get its creator's noise tokens"),
        (WORKER_SHARED_IMPL_PATH, r"creation_params->helium_noise_tokens\s*=\s*helium_noise_tokens",
         "a shared worker must pass the browser's noise tokens to its creation params"),
        (WORKER_SERVICE_IMPL_PATH,
         r"global_scope_creation_params->helium_noise_tokens\s*=\s*worker_start_data->helium_noise_tokens",
         "a service worker must pass the browser's noise tokens to its creation params"),
        (WORKER_SHARED_HOST_PATH,
         r"HeliumNoiseTokenData::GetTokensForWorker\(\s*GetProcessHost\(\)->GetBrowserContext\(\),"
         r"\s*instance_\.worker_storage_key\(\)\)",
         "the browser must send a shared worker GetTokensForWorker(browser context, worker storage key)"),
        (WORKER_SERVICE_HOST_PATH,
         r"params->helium_noise_tokens\s*=\s*HeliumNoiseTokenData::GetTokensForWorker\(\s*"
         r"context_->wrapper\(\)->browser_context\(\),\s*key_\)",
         "the browser must send a service worker GetTokensForWorker(browser context, storage key)"),
    ):
        out += _must_match(root, rel, gate, pattern, message, require)
    text, missing = _read(root, WORKER_TOKENS_PATH, gate, require)
    out += missing
    if text is not None:
        body = re.search(r"HeliumNoiseTokenData::GetTokensForWorker\(.*?\n}\n", text, re.S)
        if not body:
            out.append(Finding(gate, WORKER_TOKENS_PATH, "GetTokensForWorker is missing"))
        else:
            code = body.group(0)
            if "key.top_level_site()" not in code:
                out.append(Finding(gate, WORKER_TOKENS_PATH,
                                   "worker tokens must be derived from the top-level site of the storage key"))
            if "HeliumNoiseEnabled(" not in code:
                out.append(Finding(gate, WORKER_TOKENS_PATH,
                                   "worker tokens must honour the global and per-site switch (HeliumNoiseEnabled)"))
            if not re.search(r"return GetTokens\(context,", code) or "session_tokens_" in code:
                out.append(Finding(gate, WORKER_TOKENS_PATH,
                                   "worker tokens must be the derived per-site tokens from GetTokens, never the session tokens"))
    gpu, missing = _read(root, WORKER_GPU_PATH, gate, require)
    out += missing
    if gpu is not None and "IsWorkerOrWorkletGlobalScope" in gpu:
        out.append(Finding(gate, WORKER_GPU_PATH,
                           "a worker must follow its kWebGL token like a window, not be always masked"))
    return out


POPUP_MOJOM_PATH = "content/common/frame.mojom"
POPUP_BROWSER_PATH = "content/browser/renderer_host/render_frame_host_impl.cc"
POPUP_RENDERER_PATH = "content/renderer/render_frame_impl.cc"


def check_popup_noise_tokens(root: Path, require: bool = False) -> list[Finding]:
    """A window opened with window.open() gets its opener's noise tokens when it is created (issue #156).
    Its initial empty document is same-origin with the opener and never goes through a navigation commit,
    so without the tokens the opener reads the real canvas, GPU, memory and screen values through it. The
    browser sends the opener page's tokens in CreateNewWindowReply and sets them on the new content::Page;
    the renderer puts them in the CreateViewParams of the new view."""
    gate = "popup_noise_tokens"
    out: list[Finding] = []
    for rel, pattern, message in (
        (POPUP_MOJOM_PATH,
         r"struct CreateNewWindowReply \{[^}]*?map<blink\.mojom\.HeliumNoiseFeature,\s*blink\.mojom\.HeliumNoiseToken>"
         r"\s+noise_tokens;",
         "CreateNewWindowReply must carry the noise tokens"),
        (POPUP_BROWSER_PATH,
         r"new_main_rfh->GetPage\(\)\.set_noise_tokens\(\s*GetPage\(\)\.noise_tokens\(\)\s*\)",
         "the browser must give the popup's page its opener's noise tokens"),
        (POPUP_BROWSER_PATH,
         r"mojom::CreateNewWindowReply::New\([^;]*?new_main_rfh->GetPage\(\)\.noise_tokens\(\)\s*\)",
         "the browser must send the noise tokens in CreateNewWindowReply"),
        (POPUP_RENDERER_PATH, r"view_params->noise_tokens\s*=\s*reply->noise_tokens",
         "the renderer must create the popup's view with the tokens from the reply"),
    ):
        out += _must_match(root, rel, gate, pattern, message, require)
    return out


GATES: tuple[Callable[..., list[Finding]], ...] = (
    check_search_list, check_search_suggestions, check_secure_dns_default,
    check_crash_reporting, check_services_off, check_fedcm_resolver, check_widevine_opt_in,
    check_https_first_dialog_off, check_link_prefetch_off, check_js_optimizer, check_services_ui,
    check_audio_noise_stable, check_gpu_masking, check_worker_noise_tokens, check_popup_noise_tokens,
)


def run(root: Path, require: bool = False) -> list[Finding]:
    findings = scan_helium_hosts(root)
    for gate in GATES:
        findings += gate(root, require=require)
    return findings


def format_report(findings: list[Finding]) -> tuple[str, bool]:
    if not findings:
        return "source gates: all passed", True
    lines = [f"source gates: {len(findings)} finding(s)"]
    for f in findings:
        where = f"{f.path}:{f.line}" if f.line else f.path
        lines.append(f"GATE    {f.gate}: {where}: {f.message}")
    return "\n".join(lines), False
