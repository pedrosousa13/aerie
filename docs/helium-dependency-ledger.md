# Helium dependency ledger

Aerie is built on Helium. If Helium stops, Aerie must be able to move to
another base (see [helium-fallback.md](helium-fallback.md)). This ledger says
exactly what Aerie takes from Helium, so that move is a short list and not a
search through the whole tree.

Update it in the same change as the code. The rule is in
[CONTRIBUTING.md](../CONTRIBUTING.md).

## Aerie patches that depend on a Helium patch

One row for each Aerie patch (or Aerie change to a Helium-owned file) that
only works if a specific Helium patch is applied first. A patch depends on a
Helium patch when it edits lines that patch adds or changes, calls code that
patch adds, or assumes behavior that patch sets.

- **Helium patch**: the patch file with its submodule prefix, for example
  `helium-chromium/patches/ungoogled-chromium/disable-gaia.patch`. It must be
  listed in that submodule's `patches/series`; the tests check this.
- **Why**: what breaks without it, in one line.
- **Portable?**: how to get the same result on ungoogled-chromium or plain
  Chromium (`copy`: lift the Helium patch as is; `rewrite`: Aerie needs its own
  version; `drop`: not needed on another base).

| Aerie patch | Helium patch | Why | Portable? |
| --- | --- | --- | --- |
| `patches/services/0001-add-extension-proxy-validator.patch` | none | Adds an Aerie-owned component and one `components_unittests` dependency line in `components/BUILD.gn`. | `copy` as is |
| `patches/services/0002-turn-helium-services-off.patch` | `helium-chromium/patches/helium/core/services-prefs.patch`, `helium-chromium/patches/helium/core/proxy-extension-downloads.patch`, `helium-chromium/patches/helium/core/reenable-spellcheck-downloads.patch`, `helium-chromium/patches/helium/core/add-updater-preference.patch`, `helium-chromium/patches/helium/core/ublock-helium-services.patch` | Edits `components/helium_services` (a Helium component) and the services settings strings and page those patches add. | `drop`: without Helium there are no services to turn off. Keep the validator and wire it to the extension update URL. |
| `patches/services/0003-extension-proxy-not-configured.patch` | `helium-chromium/patches/helium/core/proxy-extension-downloads.patch` | Calls `helium::ShouldAccessExtensionService` and edits the `set_ping_enabled_domain` line that patch changes. | `rewrite`: on ungoogled-chromium the Web Store update URL is `clients2.google.com`; point it at the proxy and keep the not-configured dialog. |
| `patches/services/0004-disable-spellcheck-downloads.patch` | `helium-chromium/patches/helium/core/reenable-spellcheck-downloads.patch` | Edits the same function; without Helium the dictionary URL is Google's. | `copy`: the early failure applies to any base. |
| `patches/services/0005-remove-fedcm-dns-resolver.patch` | none | Changes a Chromium function only. | `copy` as is |
| `patches/crash/0001-remove-crash-reporting.patch` | `helium-chromium/patches/helium/core/crash-reporting-prefs.patch`, `helium-chromium/patches/helium/core/crash-reporter-privacy-tweaks.patch` | Edits `helium_crash_reporting.cc` and the upload URL these patches add. | `rewrite`: on ungoogled-chromium make `GetUploadUrl()` return an empty string. |
| `patches/network/0001-secure-dns-off-by-default.patch` | none | Changes a Chromium default only. | `copy` as is |
| `patches/search/0001-trim-the-search-engine-list.patch` | `helium-chromium/patches/helium/core/search/engine-defaults.patch`, `helium-chromium/patches/helium/core/search/restore-google.patch` | Edits the one-entry `elements` list and engine names those patches create. | `rewrite`: set the default list in `regional_settings.json` on the new base. |
| `patches/hardening/0003-hardening-settings-page.patch` | `helium-chromium/patches/helium/settings/helium-noise-settings.patch`, `helium-chromium/patches/helium/settings/privacy-page-tweaks.patch`, `helium-chromium/patches/helium/core/services-prefs.patch` | The hook hunks in `router.ts`, `route.ts`, `BUILD.gn`, `lazy_load.ts`, `privacy_page.*` and `privacy_page_index.html` sit next to lines these patches add or change, and do not apply without them. | `rewrite`: anchor the hooks on the base's privacy page. |
| `patches/hardening/0003-hardening-settings-page.patch`, `patches/hardening/0004-hardening-settings-page-markup.patch` | `helium-chromium/patches/helium/core/noise/core.patch` | The fingerprinting row is a toggle on the `helium.noise.enabled` pref that this patch adds. | `rewrite`: drop the row, or lift the noise patches. |
| `patches/hardening/0004-hardening-settings-page-markup.patch` | `helium-chromium/patches/helium/core/webrtc-default-handling-policy.patch`, `helium-chromium/patches/inox-patchset/modify-default-prefs.patch` | The page, `docs/hardening.md` and the Bench check `hardening_pref_defaults` state the WebRTC, third-party cookie and preloading defaults that these patches set. Without them the Bench check fails. | `copy`: lift both patches as they are. |

## What the unmodified build takes from Helium

Until Aerie has its own patches, `./aerie build` uses all of the following
from the pinned release (Helium 0.21.1.1, helium-chromium `0.21.1`,
helium-linux `0.21.1.1`). When Aerie changes or drops one of these, update the
row.

| Piece | Path in the submodule | What Aerie uses it for |
| --- | --- | --- |
| Google-removal patches | `helium-chromium/patches/ungoogled-chromium/` (70 patches) | Remove Google services, tracking and untraceable URLs. These come from ungoogled-chromium and are the first thing to lift on a move. |
| Other privacy patches | `helium-chromium/patches/inox-patchset/`, `helium-chromium/patches/iridium-browser/`, `helium-chromium/patches/bromite/`, `helium-chromium/patches/brave/`, `helium-chromium/patches/debian/` | Safe Browsing, autofill download, update pings and RLZ removal, and smaller fixes. Third-party patches Helium already carries. |
| Helium feature and branding patches | `helium-chromium/patches/helium/` (256 patches) | Helium's UI, branding, settings and features. This is what makes the browser Helium and not ungoogled-chromium. |
| Build fixes | `helium-chromium/patches/upstream-fixes/` | Fixes for Chromium build problems at this version. |
| Patch order | `helium-chromium/patches/series` | The only valid apply order. Aerie applies core, then platform, in one `patches.py apply` call. |
| Linux patches | `helium-linux/patches/` (7 patches) | Linux branding, binary name, default theme, desktop file check, OAuth switches. |
| Pruning list | `helium-chromium/pruning.list` | Binaries removed from the source tree before build. |
| Domain substitution | `helium-chromium/domain_regex.list`, `helium-chromium/domain_substitution.list` | Rewrites Google and other tracking domains in source files so nothing connects to them. |
| Name substitution, i18n, version | `helium-chromium/utils/name_substitution.py`, `helium-chromium/utils/i18n_apply.py`, `helium-chromium/utils/helium_version.py`, `helium-chromium/i18n/` | Product name, translations and the Helium version string. |
| Resources | `helium-chromium/resources/` | Icons, branding assets and the resource replacement list. |
| Build flags | `helium-chromium/flags.gn`, `helium-linux/flags.linux.gn` | The GN arguments for the build. `./aerie build` writes them to `args.gn`. |
| Build utilities | `helium-chromium/utils/downloads.py`, `helium-chromium/utils/patches.py`, `helium-chromium/utils/prune_binaries.py`, `helium-chromium/utils/domain_substitution.py` | Downloading, patching and pruning. Aerie calls them as they are. |
| Chromium source tarballs | `helium-chromium/downloads.ini`, `helium-linux/downloads.ini` | Prepared source and toolchain tarballs at `tarballs.imput.net`. The fallback is the official Chromium tarball. |
| Bundled assets | `helium-chromium/deps.ini` | uBlock Origin build, onboarding page and search engine data, from imputnet GitHub releases. **Not covered by the Chromium tarball fallback.** |
