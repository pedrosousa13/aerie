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


def _must_not_match(root: Path, rel: str, gate: str, pattern: str, message: str, require: bool) -> list[Finding]:
    text, missing = _read(root, rel, gate, require)
    if text is None:
        return missing
    return [Finding(gate, rel, message)] if re.search(pattern, text, re.S) else []


def check_crash_reporting(root: Path, require: bool = False) -> list[Finding]:
    out = _must_match(
        root, "components/crash/core/app/crash_reporter_client.cc", "crash_reporting",
        r"CrashReporterClient::GetUploadUrl\(\)\s*\{(?:\s*//[^\n]*\n)*\s*return std::string\(\);\s*\}",
        "GetUploadUrl() must return an empty string and nothing else", require)
    out += _must_match(
        root, "chrome/browser/crash_upload_list/helium_crash_reporting.cc", "crash_reporting",
        r"CrashReportingMode GetCrashReportingMode\(\)\s*\{(?:\s*//[^\n]*\n)*\s*return CrashReportingMode::kDisabled;\s*\}",
        "GetCrashReportingMode() must always return kDisabled", require)
    # Crash reports stay on the device (issue #262): nothing can switch the database's uploads on or
    # request one report's upload, and nothing deletes a pending report for being pending.
    crashpad = "components/crash/core/app/crashpad.cc"
    out += _must_match(
        root, crashpad, "crash_reporting",
        r"void ApplyUploadConsent\(bool[^)]*\)\s*\{[^}]*SetUploadsEnabled\(false\);\s*\}",
        "ApplyUploadConsent() must only ever switch the database's uploads off", require)
    out += _must_match(
        root, crashpad, "crash_reporting",
        r"void RequestSingleCrashUploadImpl\(const std::string& local_id\)\s*\{(?:\s*//[^\n]*\n)*\s*\}",
        "RequestSingleCrashUploadImpl() must do nothing", require)
    out += _must_not_match(
        root, crashpad, "crash_reporting", r"SetUploadsEnabled\(\s*(?!false)[^)]",
        "no code may switch the database's uploads on", require)
    out += _must_not_match(
        root, "chrome/browser/crash_upload_list/helium_crash_reporting.cc", "crash_reporting",
        r"SetUploadConsentFromUser\(\s*true\s*\)|DeletePendingReports",
        "the crash mode code must not switch uploads on, nor delete pending reports", require)
    return out


def check_fedcm_resolver(root: Path, require: bool = False) -> list[Finding]:
    return _must_match(
        root, "chrome/browser/chrome_content_browser_client.cc", "fedcm_resolver",
        r"ChromeContentBrowserClient::GetDnsTxtResolverUrlPrefix\(\)\s*\{(?:\s*//[^\n]*\n)*\s*return std::string\(\);\s*\}",
        "GetDnsTxtResolverUrlPrefix() must return an empty string (no built-in dns.google)", require)


def check_services_off(root: Path, require: bool = False) -> list[Finding]:
    return _must_match(
        root, "components/helium_services/helium_services_helpers.cc", "services_off",
        r"bool ShouldAccessServices\(const PrefService& prefs\)\s*\{\s*return false;\s*\}",
        "ShouldAccessServices() must return false", require)


def check_private_ai_off(root: Path, require: bool = False) -> list[Finding]:
    """Chromium 158's Private AI client (private_ai_cert_fetch, private_ai_tls_client) is off by switch, not by
    luck (issue #304): with kPrivateAi off PrivateAiServiceFactory builds no service, so nothing contacts
    Google's Private AI servers."""
    return _must_match(
        root, "components/private_ai/features.cc", "private_ai_off",
        r"BASE_FEATURE\(\s*kPrivateAi,\s*base::FEATURE_DISABLED_BY_DEFAULT\s*\)",
        "kPrivateAi must be FEATURE_DISABLED_BY_DEFAULT (the Private AI client stays off)", require)


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


AUDIO_CONTEXT_PATH = "third_party/blink/renderer/modules/webaudio/base_audio_context.cc"
AUDIO_BUFFER_PATH = "third_party/blink/renderer/modules/webaudio/audio_buffer.cc"
REALTIME_ANALYSER_PATH = "third_party/blink/renderer/modules/webaudio/realtime_analyser.cc"
AUDIO_DIR = "third_party/blink/renderer/modules/webaudio"


def check_audio_noise_stable(root: Path, require: bool = False) -> list[Finding]:
    """Audio jitter is seeded from the site/session token, the data being read and the sample position only
    (issue #38; Helium 0.22 core/noise/audio.patch). AudioReadbackNoise hashes the kAudio token, then every
    source sample, and Jitter adds only the index. A per-render or per-read counter, or a random seed, would
    let a tracker average the noise away."""
    gate = "audio_noise_stable"
    out = _must_match(
        root, AUDIO_CONTEXT_PATH, gate,
        r"AudioReadbackNoise::AudioReadbackNoise\([^{]*\{.*?tokens\.find\(HeliumNoiseFeature::kAudio\).*?"
        r"hash_\.emplace\(it->second\)",
        "AudioReadbackNoise must seed its hash from the kAudio token only", require)
    out += _must_match(
        root, AUDIO_CONTEXT_PATH, gate,
        r"float AudioReadbackNoise::Jitter\(float value, uint64_t index\) const \{[^}]*?"
        r"HeliumNoiseHash hash = \*hash_;\s*hash\.Update\(index\);\s*const int steps = hash\.GetValueBelow\(",
        "Jitter must seed from the token and data hash plus the index alone: no counter and no random value", require)
    out += _must_match(
        root, AUDIO_BUFFER_PATH, gate,
        r"AudioBuffer::JitterAudioData\(ExecutionContext\* context\) \{\s*AudioReadbackNoise noise\(context\);"
        r".*?noise\.AddSource\(channel->AsSpan\(\)\);.*?noise\.Jitter\(sample, index\+\+\)",
        "an AudioBuffer must hash every channel into the noise, then jitter by sample index alone", require)
    out += _must_match(
        root, REALTIME_ANALYSER_PATH, gate,
        r"AudioReadbackNoise noise\(context\);",
        "analyser reads must use AudioReadbackNoise", require)
    base = root / AUDIO_DIR
    if base.is_dir():
        for path in sorted(base.rglob("*")):
            if path.suffix not in (".cc", ".h") or not path.is_file():
                continue
            if re.search(r"g_helium_audio\w*(?:sequence|counter)|helium_audio\w*_sequence",
                         path.read_text(encoding="utf-8", errors="replace")):
                out.append(Finding(gate, str(path.relative_to(root)),
                                   "no per-render or per-read counter may feed the audio noise"))
    return out


WORKLET_PROXY_PATH = "third_party/blink/renderer/core/workers/threaded_worklet_messaging_proxy.cc"
WORKLET_SCOPE_PATH = "third_party/blink/renderer/core/workers/worklet_global_scope.cc"
WORKLET_PROCESSOR_PATH = AUDIO_DIR + "/audio_worklet_processor.cc"
SCRIPT_PROCESSOR_PATH = AUDIO_DIR + "/script_processor_node.cc"


def check_audio_worklet_noise(root: Path, require: bool = False) -> list[Finding]:
    """An audio worklet and a ScriptProcessorNode read the samples before they leave the graph, so they get
    the same Helium audio noise as a rendered buffer (issue #157). The worklet carries its creating page's
    tokens (empty when protection is off), AudioWorkletProcessor::Process noises each block of input after it
    is copied and before process() runs, and a ScriptProcessorNode noises the input buffer of each event."""
    gate = "audio_worklet_noise"
    out: list[Finding] = []
    for rel, pattern, message in (
        (WORKLET_PROXY_PATH, r"helium_noise_tokens\s*=\s*window->GetHeliumNoiseTokens\(\)",
         "an audio worklet must be given the noise tokens of the window that created it"),
        (WORKLET_SCOPE_PATH, r"SetHeliumNoiseTokens\(\s*creation_params->helium_noise_tokens\s*\)",
         "a worklet global scope must take its tokens from the creation params"),
        (WORKLET_PROCESSOR_PATH,
         r"CopyPortToArrayBuffers\(isolate, inputs, input_array_buffers_\);\s*"
         r"JitterInputArrayBuffers\(isolate, global_scope_, input_array_buffers_\);",
         "Process must noise the copied input arrays before process() runs"),
        (WORKLET_PROCESSOR_PATH,
         r"void JitterInputArrayBuffers\([^)]*\)\s*\{\s*AudioReadbackNoise noise\(context\);.*?"
         r"noise\.AddSource\(.*?noise\.Jitter\(sample, index\+\+\)",
         "the worklet input must be noised with AudioReadbackNoise: hash every channel, then jitter by position"),
        (SCRIPT_PROCESSOR_PATH,
         r"AudioReadbackNoise noise\(GetExecutionContext\(\)\);.*?noise\.AddSource\(.*?noise\.Jitter\(sample, index\+\+\)",
         "a ScriptProcessorNode must noise its input buffer with AudioReadbackNoise"),
    ):
        out += _must_match(root, rel, gate, pattern, message, require)
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


CANVAS_HELPER_PATH = "third_party/blink/renderer/core/helium_noise/canvas_noising_helper.cc"


def check_webgl_readpixels(root: Path, require: bool = False) -> list[Finding]:
    """WebGL readPixels is noised on the default framebuffer and nowhere else (issue #119). ReadPixelsHelper decides
    with ShouldNoiseReadPixels(/*default_framebuffer=*/!framebuffer, ...), so a read from a framebuffer object stays
    exact, and then calls CanvasNoisingHelper::MaybeNoiseReadPixels, which takes the canvas token from
    GetCanvasTokenHash and returns without noise when there is none (protection off)."""
    gate = "webgl_readpixels"
    out = _must_match(
        root, WEBGL_CONTEXT_PATH, gate,
        r"ReadPixelsHelper\(.*?aerie_webgl_readback::ShouldNoiseReadPixels\(\s*"
        r"/\*default_framebuffer=\*/!framebuffer,\s*format,\s*type\).*?"
        r"CanvasNoisingHelper::MaybeNoiseReadPixels\(",
        "ReadPixelsHelper must noise only when ShouldNoiseReadPixels(/*default_framebuffer=*/!framebuffer, format, type) "
        "holds, through CanvasNoisingHelper::MaybeNoiseReadPixels", require)
    out += _must_match(
        root, CANVAS_HELPER_PATH, gate,
        r"CanvasNoisingHelper::MaybeNoiseReadPixels\([^{]*\{\s*std::optional<HeliumNoiseHash> token_hash =\s*"
        r"GetCanvasTokenHash\(execution_context\);\s*if \(!token_hash\) \{\s*return false;\s*\}",
        "MaybeNoiseReadPixels must take the canvas token from GetCanvasTokenHash and do nothing without it", require)
    return out


WORKER_PARAMS_PATH = "third_party/blink/renderer/core/workers/global_scope_creation_params.h"
WORKER_SCOPE_PATH = "third_party/blink/renderer/core/workers/worker_global_scope.cc"
WORKER_DEDICATED_PATH = "third_party/blink/renderer/core/workers/dedicated_worker.cc"
WORKER_SHARED_IMPL_PATH = "third_party/blink/renderer/core/exported/web_shared_worker_impl.cc"
WORKER_SERVICE_IMPL_PATH = "third_party/blink/renderer/modules/exported/web_embedded_worker_impl.cc"
WORKER_TOKENS_PATH = "content/browser/helium_noise/noise_token_data.cc"
SHARED_WORKER_SERVICE_IMPL_PATH = "content/browser/worker_host/shared_worker_service_impl.cc"
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
        (WORKER_SHARED_IMPL_PATH, r"creation_params->helium_noise_tokens\s*=\s*noise_tokens",
         "a shared worker must pass the browser's noise tokens to its creation params"),
        (WORKER_SERVICE_IMPL_PATH,
         r"global_scope_creation_params->helium_noise_tokens\s*=\s*worker_start_data->noise_tokens",
         "a service worker must pass the browser's noise tokens to its creation params"),
        (SHARED_WORKER_SERVICE_IMPL_PATH,
         r"host->set_noise_tokens\(creator\.GetPage\(\)\.noise_tokens\(\)\)",
         "a shared worker must take the noise tokens of the page that created it"),
        (WORKER_SERVICE_HOST_PATH,
         r"params->noise_tokens\s*=\s*HeliumNoiseTokenData::GetTokensForWorker\(\s*"
         r"browser_context,\s*key_\s*,",
         "the browser must send a service worker GetTokensForWorker(browser context, storage key, partition)"),
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
            if not re.search(r"return GetTokens\(context,\s*\w+\.top_level_site\(\)", code):
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


PARTITION_NAV_PATH = "content/browser/renderer_host/navigation_request.cc"
STORAGE_PARTITION_IMPL_PATH = "content/browser/storage_partition_impl.cc"
SHARED_WORKER_SERVICE_HEADER_PATH = "content/browser/worker_host/shared_worker_service_impl.h"


def check_workspace_noise_partition(root: Path, require: bool = False) -> list[Finding]:
    """The noise tokens are keyed on the storage partition (the Workspace) as well as the site (issue #121), so
    two Workspaces show two browsers. Every caller hands over the partition of what it serves: a navigation
    its site_info_, a popup's first document its site instance's, a service worker its context's. A shared
    worker takes its creator page's tokens (Helium 0.22), so it follows that page's partition, and it can
    only be joined from the same partition: each StoragePartitionImpl owns one SharedWorkerServiceImpl and
    looks workers up only in that service, so a second Workspace never reaches the first one's worker. The partition
    is only mixed into an in-memory hash; nothing stores or sends it."""
    gate = "workspace_noise_partition"
    out: list[Finding] = []
    for rel, pattern, message in (
        (PARTITION_NAV_PATH,
         r"HeliumNoiseTokenData::GetTokensForMainFrame\([^;]*site_info_\.GetStoragePartitionConfig\(\)",
         "a navigation must key its noise tokens on the storage partition of its site_info_"),
        (POPUP_BROWSER_PATH,
         r"GetTokensForMainFrame\([^;]*GetStoragePartitionConfig\(\)",
         "a popup's initial document must key its noise tokens on the storage partition of its site instance"),
        (WORKER_SERVICE_HOST_PATH,
         r"GetTokensForWorker\([^;]*wrapper\(\)->storage_partition\(\)->GetConfig\(\)",
         "a service worker must key its noise tokens on its storage partition"),
        (STORAGE_PARTITION_IMPL_PATH,
         r"shared_worker_service_\s*=\s*std::make_unique<SharedWorkerServiceImpl>\(\s*this,",
         "each storage partition must own its own SharedWorkerServiceImpl, so two Workspaces never share a shared worker"),
        (SHARED_WORKER_SERVICE_HEADER_PATH,
         r"const raw_ptr<StoragePartitionImpl>\s+storage_partition_;",
         "a SharedWorkerServiceImpl must belong to one StoragePartitionImpl"),
        (WORKER_TOKENS_PATH,
         r"NoiseSiteKey\(\s*url::Origin::Create\(top_level_site\.GetURL\(\)\),\s*aerie_fingerprint::NoisePartitionKey\(",
         "GetTokens must put the storage partition in the site key (NoiseSiteKey(origin, NoisePartitionKey(...)))"),
        (WORKER_TOKENS_PATH,
         r"return GetTokens\(context,[^;]*,\s*partition\);",
         "GetTokensForWorker must pass its partition on to GetTokens"),
    ):
        out += _must_match(root, rel, gate, pattern, message, require)
    return out


POPUP_MOJOM_PATH = "content/common/frame.mojom"
POPUP_BROWSER_PATH = "content/browser/renderer_host/render_frame_host_impl.cc"
POPUP_RENDERER_PATH = "content/renderer/render_frame_impl.cc"


def check_popup_noise_tokens(root: Path, require: bool = False) -> list[Finding]:
    """A window opened with window.open() gets its opener's noise tokens when it is created (issue #156).
    Its initial empty document is same-origin with the opener and never goes through a navigation commit,
    so without the tokens the opener reads the real canvas, GPU, memory and screen values through it. Helium
    0.22 does this: CreateNewWindow derives the tokens for the new window's origin
    (GetTokensForMainFrame), sets them on the new content::Page, and sends them in CreateNewWindowReply;
    the renderer puts them in the CreateViewParams of the new view. The partition argument is Aerie's
    (hardening/0024, see workspace_noise_partition)."""
    gate = "popup_noise_tokens"
    out: list[Finding] = []
    for rel, pattern, message in (
        (POPUP_MOJOM_PATH,
         r"struct CreateNewWindowReply \{[^}]*?map<blink\.mojom\.HeliumNoiseFeature,\s*blink\.mojom\.HeliumNoiseToken>"
         r"\s+noise_tokens;",
         "CreateNewWindowReply must carry the noise tokens"),
        (POPUP_BROWSER_PATH,
         r"noise_tokens =\s*HeliumNoiseTokenData::GetTokensForMainFrame\([^;]*;\s*"
         r"new_main_rfh->GetPage\(\)\.set_noise_tokens\(noise_tokens\)",
         "the browser must give the popup's page the noise tokens it derives for the new window"),
        (POPUP_BROWSER_PATH,
         r"mojom::CreateNewWindowReply::New\([^;]*?,\s*noise_tokens\);",
         "the browser must send the noise tokens in CreateNewWindowReply"),
        (POPUP_RENDERER_PATH, r"view_params->noise_tokens\s*=\s*reply->noise_tokens",
         "the renderer must create the popup's view with the tokens from the reply"),
    ):
        out += _must_match(root, rel, gate, pattern, message, require)
    return out


TEXT_METRICS_CANVAS_PATH = "third_party/blink/renderer/modules/canvas/canvas2d/base_rendering_context_2d.cc"
TEXT_METRICS_DOCUMENT_PATH = "third_party/blink/renderer/core/dom/document.cc"
TEXT_METRICS_FACTOR_PATH = "third_party/blink/renderer/core/helium_noise/text_metrics_noise.cc"


def check_text_metrics_noise(root: Path, require: bool = False) -> list[Finding]:
    """measureText and client rect noise come from the site's kTextMetrics token (issue #118): stable for a
    site in a session, 1 without a token. The Bromite patch drew the factor with base::RandDouble() once per
    Document, which changes on every load and is itself a fingerprint."""
    gate = "text_metrics_noise"
    out: list[Finding] = []
    for rel, pattern, message in (
        (TEXT_METRICS_CANVAS_PATH,
         r"TextMetricsNoiseFactor\(\s*GetExecutionContext\(\),\s*TextMetricsNoiseKind::kMeasureText\)",
         "measureText must take its factor from TextMetricsNoiseFactor"),
        (TEXT_METRICS_DOCUMENT_PATH,
         r"double Document::GetNoiseFactorX\(\) \{\s*return TextMetricsNoiseFactor\([^;]*?kClientRectsX\);\s*\}"
         r"\s*double Document::GetNoiseFactorY\(\) \{\s*return TextMetricsNoiseFactor\([^;]*?kClientRectsY\);\s*\}",
         "the client rect factors must come from TextMetricsNoiseFactor"),
        (TEXT_METRICS_FACTOR_PATH,
         r"HeliumNoiseHash hash\(tokens\.at\(HeliumNoiseFeature::kTextMetrics\)\)",
         "the factor must be hashed from the kTextMetrics token"),
    ):
        text, missing = _read(root, rel, gate, require)
        if text is None:
            out += missing
        elif not re.search(pattern, text, re.S):
            out.append(Finding(gate, rel, message))
        elif re.search(r"base::Rand\w*\(", text):
            out.append(Finding(gate, rel, "a random number must not feed the factor: it would change on every load"))
    return out


DELEGATE_PATH = "services/network/network_service_network_delegate.cc"
NETWORK_CONTEXT_PATH = "services/network/network_context.cc"


def check_request_policy(root: Path, require: bool = False) -> list[Finding]:
    """The request policy (issue #257) is hooked into all three paths, and every annotation name in the
    tables of docs/network-endpoints.md exists in the source (or in Aerie's overlay)."""
    gate = "request_policy"
    out: list[Finding] = []
    text, missing = _read(root, DELEGATE_PATH, gate, require)
    if text is None:
        out += missing
    else:
        for needle, why in (("aerie_network::ClassifyFactory(", "the request origin must come from the factory facts"),
                            ("aerie_network::Decide(", "URL requests must be judged"),
                            ("ERR_BLOCKED_BY_ADMINISTRATOR", "a refusal must return the policy's net error")):
            if needle not in text:
                out.append(Finding(gate, DELEGATE_PATH, why))
    text, missing = _read(root, NETWORK_CONTEXT_PATH, gate, require)
    if text is None:
        out += missing
    elif text.count("aerie_network::DecideConnection(") != 2:
        out.append(Finding(gate, NETWORK_CONTEXT_PATH, "both PreconnectSockets and ResolveHost must call DecideConnection"))
    xml = Path(root) / "tools/traffic_annotation/summary/annotations.xml"
    if xml.is_file():
        from aerie_netpolicy import model  # tools/ is on the path whenever aerie_build is

        repo = Path(__file__).resolve().parents[2]
        try:
            table = model.parse_doc((repo / model.DOC_PATH).read_text(encoding="utf-8"))
        except (model.PolicyDocError, OSError) as exc:
            return out + [Finding(gate, model.DOC_PATH, f"the request policy tables are invalid: {exc}")]
        for name in model.unknown_annotations(table, xml.read_text(encoding="utf-8"), model.overlay_annotation_text(repo)):
            out.append(Finding(gate, model.DOC_PATH, f"traffic annotation {name!r} does not exist in the pinned source"))
    return out


GATES: tuple[Callable[..., list[Finding]], ...] = (
    check_search_list, check_search_suggestions, check_secure_dns_default,
    check_crash_reporting, check_services_off, check_fedcm_resolver, check_widevine_opt_in,
    check_https_first_dialog_off, check_private_ai_off, check_link_prefetch_off, check_js_optimizer, check_services_ui,
    check_audio_noise_stable, check_audio_worklet_noise, check_gpu_masking, check_webgl_readpixels, check_worker_noise_tokens, check_workspace_noise_partition, check_popup_noise_tokens,
    check_text_metrics_noise, check_request_policy,
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
