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
| `patches/branding/0001-set-product-name.patch` | `helium-chromium/patches/helium/core/change-chromium-branding.patch` | Edits the `BRANDING` lines that patch writes. | `rewrite`: set the same values in Chromium's own `BRANDING`. |
| `patches/branding/0002-aerie-internal-scheme.patch` | `helium-chromium/patches/helium/core/override-chrome-protocol.patch` | Changes `kHeliumUIScheme` and the string literals that patch adds for the alias scheme. | `copy`: lift Helium's patch, then this one; or `drop` the alias and keep `chrome://`. |
| `patches/branding/0003-linux-desktop-identity.patch`, `patches/branding/0004-name-the-executable-aerie.patch` | `helium-linux/patches/helium/linux/change-chromium-branding.patch`, `helium-linux/patches/helium/linux/rename-chrome-binary.patch` | Edit the desktop name, data directory, installer info, icon name, executable and crash handler names those patches set. | `rewrite`: set the Aerie values on the original Chromium lines. |
| `patches/branding/0005-flag-and-webstore-text-says-aerie.patch` | `helium-chromium/patches/helium/core/add-low-power-framerate-flag.patch`, `helium-chromium/patches/helium/core/flags-setup.patch`, `helium-chromium/patches/helium/core/bookmark-open-all-confirmation-flag.patch`, `helium-chromium/patches/helium/core/add-middle-click-autoscroll-flag.patch`, `helium-chromium/patches/helium/core/network/split-clienthello.patch`, `helium-chromium/patches/helium/core/network/ignore-system-proxy-server-flag.patch`, `helium-chromium/patches/helium/core/noise/canvas.patch`, `helium-chromium/patches/helium/core/add-update-channel-flag.patch`, `helium-chromium/patches/helium/core/noise/audio.patch`, `helium-chromium/patches/helium/core/add-updater-preference.patch`, `helium-chromium/patches/helium/core/noise/core.patch`, `helium-chromium/patches/helium/core/network/quic-v2-flag.patch`, `helium-chromium/patches/helium/core/network/disable-ech-flag.patch`, `helium-chromium/patches/helium/core/noise/hardware-concurrency.patch`, `helium-chromium/patches/helium/ui/native-frame-materials.patch`, `helium-chromium/patches/helium/ui/hide-page-actions-flag.patch`, `helium-linux/patches/helium/linux/add-middle-click-paste-flag.patch`, `helium-chromium/patches/helium/core/fixups-chrome-webstore-script.patch` | Edits the flag names and descriptions that these patches add to `helium_flag_entries.h`, and the Web Store fixup script. | `drop`: without Helium's flags and fixups there is nothing to rename. |
| `patches/branding/0006-show-the-aerie-release.patch`, build layer `aerie version` (`tools/aerie_build/aerie_version.py`) | `helium-chromium/patches/helium/core/add-helium-versioning.patch` | Edits the `HELIUM_PRODUCT_VERSION` template line that patch adds and appends to the `chrome/VERSION` that Helium's version step writes; About shows that value next to the Chromium version. | `rewrite`: show the Aerie release in the About version string on the new base. |
| build layer `branding` (`branding/resources.txt`) | none | Replaces the files that Helium's resource step (`helium-chromium/resources/helium_resources.txt`, `generate_resources.txt`) copied, at the same destinations. | `copy`: the same list works on a base without Helium's step if the destinations exist. |
| `patches/widevine/0001-register-widevine-only-when-opted-in.patch` | `helium-chromium/patches/helium/core/component-updates.patch`, `helium-chromium/patches/helium/core/crash-reporting-prefs.patch` | Opt-in only works because Helium's allowlist in `component_installer.cc` keeps the Widevine id (`oimompecagnajdejgnnjijobebaeigek`); without it the updater ignores the Widevine registration. The `browser_prefs.cc` hunk anchors on `static_cast<int>(helium::CrashReportingMode::kAsk));`, the line `crash-reporting-prefs.patch` adds before `ChromeContentBrowserClient::RegisterLocalStatePrefs`. | `copy`: lift the patch, allow that id in the base's component allowlist, and re-anchor the `browser_prefs.cc` hunk on the base's `RegisterLocalState` body. |
| `patches/widevine/0002-update-widevine-from-google-when-opted-in.patch` | `helium-chromium/patches/helium/core/component-updates.patch` | Edits the `UpdateUrl()` and `PingUrl()` lines that patch changes in `chrome_component_updater_configurator.cc`. The per-app filter it adds to `update_client` has no Helium dependency. | `rewrite`: on ungoogled-chromium or plain Chromium the configurator already points at Google; keep only the opt-in gate in `registration.cc`. |
| `patches/widevine/0003-allow-widevine-pref-in-settings.patch`, `patches/widevine/0004-ask-before-the-first-drm-request.patch` | none | Chromium code only (the `settings_private` allowlist, the key system support hook and the infobar enum). | `copy` as is |

## Helium patches that Aerie drops or replaces

`patches/helium-drop.txt` lists Helium patches that `./aerie build` and
`./aerie patches check` skip, each with a reason. `patches/helium-adapt.txt`
lists Helium patches that Aerie replaces, at the same place in the series, with
a copy in `patches/helium-adapted/` (Helium's patch with the hunks re-cut that
sat next to dropped code). One row for each entry; a test fails without it. The
check fails if an entry is no longer in Helium's series, so a Helium bump shows
up here. Changing either file after a tree was patched needs
`./aerie build --clean`.

The group below is Helium's bundled uBlock Origin, which Aerie replaces with
the native Filter Engine (issue #8). The `ungoogled-chromium/extensions-manifestv2.patch`
that keeps Manifest V2 working is a different patch and stays: none of these
carries an MV2 enabler (checked hunk by hunk; the Bench check `mv2_extension`
proves it on a real build).

| Helium patch | Action | Why | Portable? |
| --- | --- | --- | --- |
| `helium-chromium/patches/helium/core/ublock-setup-sources.patch` | drop | Builds the bundled uBlock Origin into `third_party/ublock` and its resource pack. | `drop`: not needed on another base. |
| `helium-chromium/patches/helium/core/ublock-install-as-component.patch` | drop | Loads uBlock Origin as a component extension and allowlists its id. | `drop` |
| `helium-chromium/patches/helium/core/ublock-reconfigure-defaults.patch` | drop | Special cases for the uBlock Origin component id (permissions, incognito, pinned by default, policy). Not MV2 related. | `drop` |
| `helium-chromium/patches/helium/core/ublock-helium-services.patch` | drop | uBlock Origin asset updates through Helium services, with the settings toggle and strings for them. | `drop` |
| `helium-chromium/patches/helium/core/ublock-dns-uncloaking.patch` | drop | The `dns` extension API and webRequest additions that only the Helium uBlock Origin used. | `drop` |
| `helium-chromium/patches/helium/ui/ublock-show-in-settings.patch` | drop | Shows the built-in uBlock Origin on the extensions page. | `drop` |
| `helium-chromium/patches/helium/core/component-updates.patch` | adapt (`patches/helium-adapted/helium/core/component-updates.patch`) | Adds Helium's component update helpers next to the uBlock Origin asset helpers that are dropped; the hunks are re-cut without them. | `copy`: Helium's original on a base that keeps uBlock Origin. |
| `helium-chromium/patches/helium/core/fixups-component-setup.patch` | adapt (`patches/helium-adapted/helium/core/fixups-component-setup.patch`) | Adds the webstore fixups component next to the uBlock Origin component lines that are dropped; the hunks are re-cut without them. | `copy` |
| `helium-chromium/deps.ini [ublock_origin]` | not downloaded | The uBlock Origin build that `third_party/ublock` was unpacked from. `./aerie build` reads a copy of deps.ini without that section (`tools/aerie_build/helium_deps.py`). | `drop` |

Issue #6's `services/0002-turn-helium-services-off.patch` has a hunk in
`chrome/app/settings_strings.grdp` that used to have the uBlock Origin settings
strings (added by the dropped `ublock-helium-services.patch`) as context. It is
re-cut against the text the Aerie layers see: Helium's patches minus the drops,
then Helium's own steps. Other Aerie patches whose files the dropped patches
also change (`services/0002`, `services/0005`, `hardening/0001`,
`adblock/0001`, `adblock/0002`) have line numbers that match that text, so
`./aerie patches check` sees no offset.

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
| Bundled assets | `helium-chromium/deps.ini` | Onboarding page and search engine data, from imputnet GitHub releases (the uBlock Origin build is dropped, see above). **Not covered by the Chromium tarball fallback.** |
