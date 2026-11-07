# Hardening defaults

<!-- Generated from tools/aerie_hardening/controls.json. Do not edit by hand. Run: ./aerie hardening generate -->

Aerie ships strict but usable privacy and security defaults. Every one of them can be changed by the
user in Settings, Privacy and security, Security hardening. No page can change them.
This file says which controls the base browser already provided and which ones Aerie added.
It is generated from `tools/aerie_hardening/controls.json`, the same table that drives the settings
page and the Bench checks, so the three cannot drift apart.

## Controls at a glance

| Control | Default | Default provided by | Settings row | Bench checks |
| --- | --- | --- | --- | --- |
| HTTPS-only mode | On (strict: Always use secure connections). | Aerie | Dropdown | `hardening_https_only`, `hardening_localhost_dev`, `hardening_http_exception` |
| Third-party cookies | Blocked. | ungoogled-chromium (through Helium) | Dropdown | `hardening_third_party_cookies` |
| Partitioned storage | Always on. | Chromium | Read-only | `hardening_storage_partitioning` |
| WebRTC IP leak protection | Default public interface only. | Helium | Dropdown | `hardening_webrtc` |
| No prefetch or preloading | Off. | ungoogled-chromium (through Helium) | Dropdown | `hardening_prefetch` |
| Strict site isolation | Always on. | Chromium | Read-only | `hardening_site_isolation` |
| Fingerprinting randomization | On. | Helium | Toggle | `hardening_fingerprinting` |
| Extensions cannot loosen these settings | Always on. | Aerie | Read-only | `hardening_extension_prefs` |

## What upstream already provided

Aerie only pins these with a Bench check and a settings row, so a Helium update cannot
drop one without the build train noticing.

### Third-party cookies

Cookies set or sent by a site embedded in another site are blocked.

- Default: Blocked.
- Default provided by: ungoogled-chromium (through Helium)
- Where the default is set:
  - helium-chromium/patches/inox-patchset/modify-default-prefs.patch: profile.cookie_controls_mode defaults to block third-party
- Upstream code:
  - `components/content_settings/core/browser/cookie_settings.cc`
- Preference: `profile.cookie_controls_mode` (default `1`)
- Settings row: Dropdown
- Cost: Cross-site single sign-on that relies on cookies, embedded payment or login widgets and some video embeds can fail. Site exceptions and the Storage Access API are the escape hatches.
- Bench checks: `hardening_third_party_cookies`

### Partitioned storage

Storage used by an embedded site (localStorage, IndexedDB, caches) is kept separate for each top-level site.

- Default: Always on.
- Default provided by: Chromium
- Where the default is set:
  - Chromium: net/base/features.cc, kThirdPartyStoragePartitioning is enabled by default
- Upstream code:
  - `net/base/features.cc`
- Settings row: Read-only
- Cost: An embedded widget no longer shares its state between sites, so it can ask the user to sign in again on each site.
- Bench checks: `hardening_storage_partitioning`

### WebRTC IP leak protection

WebRTC only uses the default public network interface, so a page cannot read the local network addresses of the device.

- Default: Default public interface only.
- Default provided by: Helium
- Where the default is set:
  - helium-chromium/patches/helium/core/webrtc-default-handling-policy.patch: webrtc.ip_handling_policy defaults to default_public_interface_only
  - patches/hardening/0005-allow-webrtc-policy-in-settings.patch: lets the settings page read and write the pref (Aerie adds the row)
- Upstream code:
  - `chrome/browser/ui/browser_ui_prefs.cc`
  - `chrome/browser/extensions/api/settings_private/prefs_util.cc`
- Preference: `webrtc.ip_handling_policy` (default `"default_public_interface_only"`)
- Aerie patches:
  - `patches/hardening/0003-hardening-settings-page.patch`
  - `patches/hardening/0004-hardening-settings-page-markup.patch`
  - `patches/hardening/0005-allow-webrtc-policy-in-settings.patch`
- Settings row: Dropdown
- Cost: Some video calls and peer-to-peer apps cannot find a direct path and fall back to a relay, which adds delay. Stricter values can break calls that have no relay.
- Bench checks: `hardening_webrtc`

### No prefetch or preloading

Speculative loads are off: link prefetch, prerender, preconnect, DNS prefetch and speculation rules. rel=preload and modulepreload are ordinary page loads and are not touched.

- Default: Off.
- Default provided by: ungoogled-chromium (through Helium)
- Where the default is set:
  - helium-chromium/patches/inox-patchset/modify-default-prefs.patch: NetworkPredictionOptions::kDefault is kDisabled
- Upstream code:
  - `chrome/browser/preloading/preloading_prefs.h`
  - `chrome/browser/preloading/preloading_prefs.cc`
- Preference: `net.network_prediction_options` (default `2`)
- Aerie patches:
  - `patches/hardening/0008-no-link-prefetch-when-preloading-is-off.patch`
- Settings row: Dropdown
- Cost: Page loads are slightly slower, because the browser no longer opens connections or fetches pages ahead of a click.
- Bench checks: `hardening_prefetch`

### Strict site isolation

Every site gets its own renderer process, so a compromised page cannot read another site in memory.

- Default: Always on.
- Default provided by: Chromium
- Where the default is set:
  - Chromium: chrome/common/chrome_features.cc, kSitePerProcess is enabled by default on desktop
- Upstream code:
  - `chrome/common/chrome_features.cc`
  - `content/browser/site_isolation_policy.cc`
- Settings row: Read-only
- Cost: Uses more memory, because each site runs in its own process.
- Bench checks: `hardening_site_isolation`

### Fingerprinting randomization

Canvas and audio output carry small random noise. The noise differs for each site and for each browser session, and can be turned off for a single site.

- Default: On.
- Default provided by: Helium
- Where the default is set:
  - helium-chromium/patches/helium/core/noise/core.patch: helium.noise.enabled defaults to true
  - helium-chromium/patches/helium/core/noise/canvas.patch and audio.patch: the canvas and audio noise
- Upstream code:
  - `content/browser/helium_noise/noise_token_data.cc`
  - third_party/blink/common/features.cc (HeliumNoise)
- Preference: `helium.noise.enabled` (default `true`)
- Settings row: Toggle
- Cost: Sites that hash a canvas for bot defense can show more CAPTCHAs, and canvas editors that compare pixels can misbehave. Turn it off for a single site in the fingerprinting page.
- Bench checks: `hardening_fingerprinting`

## What Aerie added

### HTTPS-only mode

Every http:// page load is upgraded to https://. If the upgrade fails, a warning page offers one Continue to site button, and the exception is kept per site and listed in settings. localhost, 127.0.0.0/8, ::1 and *.localhost load over http with no warning page.

- Default: On (strict: Always use secure connections).
- Default provided by: Aerie
- Where the default is set:
  - patches/hardening/0001-default-https-only-mode.patch: the default of the https_only_mode_enabled pref in chrome/browser/ui/browser_ui_prefs.cc
  - patches/hardening/0007-full-https-only-warning-page.patch: kHttpsFirstDialogUi is off by default in components/security_interstitials/core/features.cc, so the failed upgrade shows the warning page and not a native dialog
- Upstream code:
  - chrome/browser/ssl/https_upgrades_interceptor.cc (net::IsLocalhost exemption, strict mode)
  - chrome/browser/ssl/generated_https_first_mode_pref.cc (settings radio group)
  - components/security_interstitials/core/https_only_mode_allowlist.cc (HTTP_ALLOWED content setting, 15 day expiry)
- Preference: `https_only_mode_enabled` (default `true`)
- Aerie patches:
  - `patches/hardening/0001-default-https-only-mode.patch`
  - `patches/hardening/0002-http-exceptions-settings-handler.patch`
  - `patches/hardening/0003-hardening-settings-page.patch`
  - `patches/hardening/0004-hardening-settings-page-markup.patch`
  - `patches/hardening/0007-full-https-only-warning-page.patch`
- Settings row: Dropdown
- Cost: Sites and devices that only speak http (router and NAS admin pages, printers, old intranet apps, captive portals) show a warning page first. The exception is one click and lasts 15 days from the last visit.
- Bench checks: `hardening_https_only`, `hardening_localhost_dev`, `hardening_http_exception`

### Extensions cannot loosen these settings

An extension with the privacy permission cannot write the WebRTC policy, the preloading option or the third-party cookie setting. Only the user can change them, in settings. Reading them and being told about changes still works.

- Default: Always on.
- Default provided by: Aerie
- Where the default is set:
  - patches/hardening/0006-lock-hardened-prefs-from-extensions.patch: the write permission of three chrome.privacy entries is removed in chrome/browser/extensions/pref_mapping.cc
- Upstream code:
  - `chrome/browser/extensions/pref_mapping.cc`
  - `chrome/browser/extensions/api/preference/preference_api.cc`
- Aerie patches:
  - `patches/hardening/0006-lock-hardened-prefs-from-extensions.patch`
- Settings row: Read-only
- Cost: Privacy extensions that set these options through chrome.privacy get a permission error. The user can still make the same change in settings.
- Bench checks: `hardening_extension_prefs`

## HTTPS-only and local development

HTTPS-only mode is strict by default. Chromium already skips the upgrade for `localhost`, the whole
`127.0.0.0/8` block, `::1` and `*.localhost` (`net::IsLocalhost`), so a dev server on
`http://localhost:<port>` loads with no warning page. The Bench check `hardening_localhost_dev`
keeps that true.

For any other http site, for example a dev server on the local network, the warning page has one
Continue to site button. The choice is kept per host name for 15 days after the last visit
(the `HTTP_ALLOWED` content setting). Settings, Privacy and security, Security hardening, lists
every host that is allowed over http and has a Remove button for each one.

## Extensions

An extension cannot loosen these settings. The write permission of these `chrome.privacy` entries
is removed, so the call fails with a permission error. Reading them and change events still work.

| Extension API | Preference |
| --- | --- |
| `chrome.privacy.network.webRTCIPHandlingPolicy` | `webrtc.ip_handling_policy` |
| `chrome.privacy.network.networkPredictionEnabled` | `net.network_prediction_options` |
| `chrome.privacy.websites.thirdPartyCookiesAllowed` | `profile.cookie_controls_mode` |

Other extension routes, such as the content settings API for cookie exceptions, are not covered
by this rule.

## Not covered

- Client rects noise: getClientRects and getBoundingClientRect noise exists upstream but is off and uses a per-document factor. Left to a follow-up issue.
- measureText noise: Canvas measureText noise exists upstream but is off and uses a per-document factor. Left to a follow-up issue.
- WebGL fingerprinting noise: WebGL readback and vendor strings are not covered by Helium noise. This is the largest item and a compatibility risk. Left to a follow-up issue.
- Fonts, screen metrics and client hints are not randomized.
- Helium keys its noise on the full origin, not on the registrable domain, so `a.example.com`
  and `b.example.com` see different canvas values.

## Change the table

Edit `tools/aerie_hardening/controls.json`, then run:

```
./aerie hardening generate   # rewrites this file and the markup patch
./aerie hardening check      # fails if either is stale or the table is invalid
```

The unit tests run the same check. Every row needs at least one Bench check, and every Bench check
has a negative control that proves the check can fail. See `docs/bench.md`.
