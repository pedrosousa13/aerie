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
    ("components/helium_onboarding/*", "onboarding web UI: links the user clicks, build files"),
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
WIDEVINE_INSTALLER = "chrome/browser/component_updater/widevine_cdm_component_installer.cc"
# The first bytes of the public key hash that the Widevine CRX must be signed with (extension id
# oimompecagnajdejgnnjijobebaeigek). The update client checks the CRX signature against this hash
# before it unpacks the component, so a changed hash would let another key's CRX load.
WIDEVINE_KEY_HASH = r"0xe8,\s*0xce,\s*0xcf,\s*0x42,\s*0x06,\s*0xd0,\s*0x93,\s*0x49"


def check_widevine_opt_in(root: Path, require: bool = False) -> list[Finding]:
    """Widevine is registered with the component updater only through the opt-in gate, and the
    component update URL goes through it. A Helium or Chromium bump that puts a bare
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
            if not m or "aerie_widevine::UpdateUrl(" not in m.group(1):
                out.append(Finding(gate, WIDEVINE_CONFIGURATOR, f"{fn}() must go through aerie_widevine::UpdateUrl()"))
    out += _must_match(
        root, WIDEVINE_INSTALLER, gate, r"kWidevineSha2Hash\[\]\s*=\s*\{\s*" + WIDEVINE_KEY_HASH,
        "the pinned Widevine signing key hash changed; the CRX signature check would accept another key",
        require)
    return out


GATES: tuple[Callable[..., list[Finding]], ...] = (
    check_search_list, check_search_suggestions, check_secure_dns_default,
    check_crash_reporting, check_services_off, check_fedcm_resolver, check_widevine_opt_in,
    check_https_first_dialog_off, check_link_prefetch_off,
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
