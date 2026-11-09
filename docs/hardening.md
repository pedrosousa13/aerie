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
| JavaScript JIT per site | On for every site. | Chromium | Link to site settings | `hardening_js_optimizer` |
| Fingerprinting protection | On. | Helium | Toggle | `hardening_fingerprinting`, `hardening_fingerprinting_site`, `hardening_fingerprinting_worker`, `hardening_fingerprinting_popup`, `hardening_fingerprinting_gpu`, `hardening_fingerprinting_readpixels`, `hardening_fingerprinting_screen`, `hardening_fingerprinting_textmetrics`, `hardening_device_memory` |
| Widevine DRM | Off. | Aerie | Toggle | `hardening_pref_defaults`, `hardening_settings_page`, `widevine_opt_in` |
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

Data Saver and Battery Saver also turn link prefetch and renderer DNS prefetch off, even when the setting is not Off (the check runs IsSomePreloadingEnabled with should_ignore_saver_modes=false).

A change of the setting reaches open tabs only when their WebPreferences are next recomputed (reload or restart).

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

### JavaScript JIT per site

The JavaScript optimizing JIT (TurboFan and Maglev) is on for speed. Turn it off for a single site in Site settings, JavaScript optimization. A site with the JIT off never shares a process with a site that has it on.

The setting is the upstream javascript-optimizer content setting, so it also shows in the site details page and in Settings, Privacy and security, Site settings, Additional content settings. It applies on the next visit to the site in a new tab, or after a restart. An embedded frame, or a page opened from another tab, keeps its current process until you open the site in a new tab, because upstream only swaps the process on a main-frame navigation in a tab with no related windows. An exception for a non-default port (for example `http://host:8080`) does not apply, because the site URL has no port, so the rule is the same for every port of the host. A rule that names only a subdomain (for example `sub.example.com`) applies the same way; the Bench measured it on a real build, on the first visit and after leaving the page. It is kept in the profile across restarts.

Process rule: the optimizer mode is part of the SiteInfo and of the renderer process flags. IsSuitableHost returns false for a process whose mode differs, so a site with the JIT off is only ever given a process that was started without the optimizing compilers, and a site with the JIT on is never given that process.

- Default: On for every site.
- Default provided by: Chromium
- Where the default is set:
  - Chromium: components/content_settings/core/browser/content_settings_registry.cc, the javascript-optimizer content setting is registered with CONTENT_SETTING_ALLOW as its default
  - helium-chromium/patches/ungoogled-chromium/move-js-optimizer-unfamiliar-sites.patch: registers the unfamiliar sites pref (default off) in content settings now that the Safe Browsing prefs are gone, so the setting page loads and the default stays allow
- Upstream code:
  - chrome/browser/chrome_content_browser_client.cc (AreV8OptimizationsEnabledForSite, SiteIsolationPolicy hook that isolates a site with an exception)
  - content/browser/site_info.cc (are_v8_optimizations_disabled is part of the SiteInfo)
  - content/browser/renderer_host/render_process_host_impl.cc (IsSuitableHost refuses to reuse a process with a different optimizer mode, and the renderer starts with --disable-optimizing-compilers)
  - components/site_isolation/features.cc (kOriginIsolationForJsOptExceptions)
- Aerie patches:
  - `patches/hardening/0003-hardening-settings-page.patch`
  - `patches/hardening/0004-hardening-settings-page-markup.patch`
- Settings row: Link to site settings
- Cost: Pages that run heavy scripts, such as web apps, games and large spreadsheets, can run several times slower on a site with the JIT off. Turn it off only for sites you do not trust.
- Bench checks: `hardening_js_optimizer`

### Fingerprinting protection

Canvas and audio output, WebGL readPixels on the default framebuffer, the widths that canvas text measurement reports and the reported number of CPU cores carry small random noise. The noise differs for each site and for each browser session, and is the same on every address of one site (www.example.com and shop.example.com share it). A private window starts with its own noise. Dedicated workers, shared workers and service workers get the same noise as their site, so a script cannot get clean values by moving into a worker. You can turn it off for a single site, and that covers every address of the site and its workers. WebGL reports only the vendor family of the graphics card (for example Google Inc. (AMD)), and WebGPU reports no adapter details. The screen size is reported as the smallest of seven common sizes that fits the window, and the window position as 0,0. While this is on, the reported device memory is capped at 8 GB. Turning it off, globally or for one site, reports the real value (web workers stay capped for now).

Audio noise is seeded from the site and session only, so one site gets the same audio output on every render and every AnalyserNode read of the same signal in one session. A tracker cannot average the noise away by reading again.

The noise is keyed on the site: the scheme plus the registrable domain (the public suffix list decides which part that is). Every address of one site gets the same canvas, audio and core count, so a tracker cannot tell the addresses apart by their noise, and the per-site switch is one pattern, https://[*.]example.com, for the same addresses. The scheme is part of the site, so http://example.com and https://example.com have separate noise and separate switches. Older per-address off settings are widened to the site the first time the setting is read or changed. The key function leaves room for a second component, so keying on the Workspace later is a small change.

The seeds are created from random numbers when a browser context first asks for noise tokens and are held as data of that browser context (Helium's HeliumNoiseTokenData in content/browser/helium_noise/noise_token_data.cc, from base::RandUint64() in its constructor); that code writes no seed to disk. Only per-site values derived from a seed reach web pages, never the seed itself. A private window has its own seeds, different from the normal profile's and from every earlier private window. A per-site off setting made in a private window is kept in memory only, is gone when the window closes, and never applies to the normal profile (unit tests in components/aerie_fingerprint).

Device memory (navigator.deviceMemory, in workers, and the Device-Memory and Sec-CH-Device-Memory headers) is capped at 8 GB while protection is on for the page, where Chromium's desktop maximum is 32 GB. A machine with less than 8 GB keeps its real bucket. There is no noise on this value. Like every other part of this control, the cap lifts when the setting is off, globally or for one site, and the real bucket is reported. A window decides this from its noise tokens (present only while protection is on for the site, the same rule as the graphics card masking). The two headers decide it in the browser process from the setting and the per-site exception of the site you are on, and never from anything the page can set. A worker has no noise tokens yet, so it stays capped even when protection is off, until issue #113 gives workers tokens. With --disable-features=HeliumNoise the cap is off everywhere.

WebGL vendor and renderer (WEBGL_debug_renderer_info) report the vendor family in Chrome's own format, for example Google Inc. (AMD) and ANGLE (AMD, AMD Radeon Graphics, OpenGL ES 3.2). The families are AMD, Intel, NVIDIA and Mesa (llvmpipe); a card that matches none of them reports Intel, because Chrome never reports an other family and Intel is the most common one on Linux. The model, driver and LLVM version are dropped. Other WebGL parameters and extension lists are not changed. A site that picks graphics quality from the card model gets the generic family instead.

WebGPU adapter info has an empty vendor, architecture, device and description. Aerie's own code does the family mapping and the masked strings (components/aerie_gpu_info), with unit tests.

Workers get the noise of their site (issue #113). A dedicated worker takes the tokens of the page that created it, and a worker started by a worker takes its parent's. A shared worker and a service worker get theirs from the browser, derived from the top-level site of the worker's storage key, which is the same site that keys a window, so a worker shows the same canvas and core count as its page on one site and different ones on another site. A worker in a frame of another site gets the tokens of the page that embeds it. The browser sends a worker the derived per-site tokens only, never the seeds, and sends none when protection is off for the browser or for the site, so the worker sees the real values. A change of the setting applies to workers started after it; a running worker keeps the tokens it started with, and a service worker takes new ones when it next starts. Worklets (audio, paint, layout and animation) get no tokens and are not noised; none of them can read a canvas or the core count.

A window that a page opens with window.open() gets the noise of its opener from the moment it is created (issue #156). Its first, empty document belongs to the same origin as the opener, so the opener can script it before anything loads in it; without the opener's noise it would read the real canvas, graphics card, device memory and screen size. The popup shows the same values as its opener. When the popup goes to a page, that page gets the noise of its own site, so a popup that goes to another site does not keep the opener's. With protection off for the browser or for the opener's site, the popup shows the real values, like the opener. A window opened with noopener or noreferrer cannot be scripted by its opener, but it is created in the same call, so it also starts with the opener's noise until its first commit replaces it with the noise of the site it loads.

The WebGL and WebGPU values follow the same switches as the other noise: the browser sends the page a token only when protection is on for the site, and turning it off for the site or for the whole browser gives the real values back. A worker follows the token it was given, like a window, so protection turned off for the site or the browser also gives the real values in its workers. The chrome://flags entry that sets a fixed WebGL value (SpoofWebGLInfo) still wins over the masking when you set it.

WebGL readPixels (issue #119): while protection is on for a site, readPixels from the default framebuffer (the page's own WebGL canvas, not a framebuffer object) in RGBA and UNSIGNED_BYTE format gets the same canvas noise as getImageData, from the same per-site and per-session canvas token, so the bytes are stable for one site in one session and differ across sites and sessions. The noise is keyed on the pixel's position in the framebuffer, so two reads of overlapping areas agree. Reads from a framebuffer object, reads in float or other formats, and reads into a pixel pack buffer are not changed, because GPGPU, TF.js and video readback need exact values. WebGPU readback is not changed. Turning protection off, for the whole browser or for the site, gives the exact bytes back, and a worker (OffscreenCanvas) follows its own token like a window. The decision which reads to noise and the layout of the read buffer are Aerie's own code with unit tests (components/aerie_webgl_readback); Helium's pixel noise does the noising. A site that checks readPixels output for exact values on its default framebuffer can see a difference of one in some channels.

Screen: while protection is on for a site, window.screen reports the smallest of 1280x800, 1366x768, 1440x900, 1680x1050, 1920x1080, 2560x1440 and 3840x2160 that is at least as wide and as tall as the browser window (a window larger than all of them keeps its own size, so the screen is never smaller than the window). The available size equals the screen size, availLeft and availTop are 0, colorDepth and pixelDepth are 24, isExtended is false, and window.screenX and screenY are 0. The device-width and device-height media features give the same size, so matchMedia agrees with screen.width. The list follows Brave's. Nothing is letterboxed: the page is not shrunk and there are no grey bars. devicePixelRatio, outerWidth, outerHeight, the user agent, the platform and the time zone are not changed. The Window Management API stays behind its permission. Full screen is not changed. Workers have no screen. Turning protection off, for the whole browser or for the site, gives the real screen back. The sizes and the choice are Aerie's own code with unit tests (components/aerie_screen). A page that lays itself out from screen.width gets the size from the list instead of the real one.

measureText: while protection is on for a site, every metric that CanvasRenderingContext2D.measureText returns (the width, the actual and font bounding boxes, the baselines) is scaled by one factor, between 0.9999985 and 1.0000015 times the real value (at most 0.00015% off, so at most 0.0003% between two sites). The factor comes from the site's own text-metrics token, so it is the same on every load of a site in one session, in its workers (OffscreenCanvas), and differs between sites and between sessions; a private window has its own. Upstream's patch drew the factor with a random number once per page load, so it changed on every load, and it was off. Turning protection off, for the whole browser or the site, gives the real metrics back. Layout is not touched: only the numbers a script reads from measureText change, and the Bench lays out a sticky header and a virtual list under the noise to show it. The chrome://flags entry for measureText noise no longer changes anything. The getClientRects and getBoundingClientRect noise reads the same kind of factor now (also from the site's token) but is still off by default. The factor range and the decision are Aerie's own code with unit tests (components/aerie_text_metrics).

- Default: On.
- Default provided by: Helium
- Where the default is set:
  - helium-chromium/patches/helium/core/noise/core.patch: helium.noise.enabled defaults to true
  - helium-chromium/patches/helium/core/noise/canvas.patch and audio.patch: the canvas and audio noise
  - patches/hardening/0010-mask-the-gpu-in-webgl-and-webgpu.patch: the kHeliumNoiseWebGL feature is on by default
  - patches/hardening/0015-report-a-common-screen-size.patch: the kHeliumNoiseScreen feature is on by default
  - patches/hardening/0018-seed-measuretext-noise-per-site.patch: the kHeliumNoiseTextMetrics feature is on by default, and measureText takes its factor from the site's token
- Upstream code:
  - `content/browser/helium_noise/noise_token_data.cc`
  - `chrome/browser/helium_noise/helium_noise_settings.cc`
  - third_party/blink/common/features.cc (HeliumNoise)
  - third_party/blink/renderer/modules/webgl/webgl_rendering_context_base.cc (getParameter)
  - third_party/blink/renderer/modules/webgpu/gpu_adapter.cc (CreateAdapterInfoForAdapter)
  - third_party/blink/renderer/modules/webgl/webgl_rendering_context_base.cc (ReadPixelsHelper)
  - third_party/blink/renderer/core/helium_noise/canvas_noising_helper.cc (MaybeNoiseReadPixels)
  - third_party/blink/common/device_memory/approximated_device_memory.cc (the 8 GB cap)
  - `third_party/blink/renderer/core/frame/navigator_device_memory.cc`
  - content/browser/client_hints/client_hints.cc (AddDeviceMemoryHeader)
  - third_party/blink/renderer/core/workers/worker_global_scope.cc and dedicated_worker.cc (the tokens of a dedicated worker)
  - content/browser/worker_host/shared_worker_host.cc and content/browser/service_worker/service_worker_version.cc (the tokens of a shared and a service worker)
  - third_party/blink/renderer/core/frame/screen.cc (Screen::GetRect, colorDepth, isExtended)
  - third_party/blink/renderer/core/frame/local_dom_window.cc (screenX, screenY)
  - third_party/blink/renderer/core/css/media_values.cc (device-width, device-height)
  - third_party/blink/renderer/modules/canvas/canvas2d/base_rendering_context_2d.cc (measureText)
  - third_party/blink/renderer/core/dom/document.cc (GetNoiseFactorX, GetNoiseFactorY)
- Preference: `helium.noise.enabled` (default `true`)
- Aerie patches:
  - `patches/hardening/0009-stable-audio-noise-per-site-and-session.patch`
  - `patches/hardening/0010-mask-the-gpu-in-webgl-and-webgpu.patch`
  - `patches/hardening/0011-key-fingerprinting-noise-on-the-site.patch`
  - `patches/hardening/0012-cap-device-memory-at-8gb.patch`
  - `patches/hardening/0013-give-dedicated-workers-the-pages-noise-tokens.patch`
  - `patches/hardening/0014-give-shared-and-service-workers-noise-tokens.patch`
  - `patches/hardening/0015-report-a-common-screen-size.patch`
  - `patches/hardening/0017-give-popups-the-openers-noise-tokens.patch`
  - `patches/hardening/0018-seed-measuretext-noise-per-site.patch`
  - `patches/hardening/0019-noise-webgl-readpixels-on-the-default-framebuffer.patch`
- Settings row: Toggle
- Cost: Sites that hash a canvas for bot defense can show more CAPTCHAs, and canvas editors that compare pixels can misbehave. Turn it off for a single site in the fingerprinting page.
- Bench checks: `hardening_fingerprinting`, `hardening_fingerprinting_site`, `hardening_fingerprinting_worker`, `hardening_fingerprinting_popup`, `hardening_fingerprinting_gpu`, `hardening_fingerprinting_readpixels`, `hardening_fingerprinting_screen`, `hardening_fingerprinting_textmetrics`, `hardening_device_memory`

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

### Widevine DRM

Widevine decodes protected video and audio (Netflix, Disney+ and similar). It is a closed-source module made by Google. Until you turn this on, Aerie does not download it, load it or contact Google about it. After you turn it on, Aerie downloads it from Google and checks Google for updates to it, and sends Google nothing else. The first time a site asks for protected content, Aerie asks once whether to turn it on. On Linux Aerie needs a restart to finish turning it on. Turning it off stops downloads and updates but does not remove a module that was already downloaded.

- Default: Off.
- Default provided by: Aerie
- Where the default is set:
  - overlay/components/aerie_widevine/widevine_opt_in.cc: aerie.widevine.enabled is registered in Local State with default false
  - patches/widevine/0001-register-widevine-only-when-opted-in.patch: the Widevine component is registered with the component updater only once the pref is true
- Upstream code:
  - `chrome/browser/component_updater/widevine_cdm_component_installer.cc`
  - `chrome/browser/component_updater/registration.cc`
- Preference: `aerie.widevine.enabled` (default `false`)
- Aerie patches:
  - `patches/widevine/0001-register-widevine-only-when-opted-in.patch`
  - `patches/widevine/0002-update-widevine-from-google-when-opted-in.patch`
  - `patches/widevine/0003-allow-widevine-pref-in-settings.patch`
  - `patches/widevine/0004-ask-before-the-first-drm-request.patch`
- Settings row: Toggle
- Cost: Turning it on downloads a closed-source Google module, which is checked against a pinned signing key before it loads, and lets Aerie ask Google for updates to it. Until you do, DRM video does not play. Before this setting, the CRLSet (the list of revoked certificates) got updates only while Widevine was on; now Aerie never sends the CRLSet to Google, so it is not updated.
- Bench checks: `hardening_pref_defaults`, `hardening_settings_page`, `widevine_opt_in`

### Extensions cannot loosen these settings

An extension with the privacy permission cannot write the WebRTC policy, the preloading option or the third-party cookie setting. While third-party cookies are blocked, an extension with the contentSettings permission cannot allow cookies (or allow them for the session) for a pattern that could apply to a third-party context, because chrome.contentSettings.cookies.set would otherwise let third-party cookies through for the sites it names. An allow for one site's own cookies (the secondary pattern equal to the primary pattern, one host, no domain wildcard) and blocking cookies still work, and nothing is refused while the user allows third-party cookies. Only the user can change these, in settings. Reading them and being told about changes still works. Other content setting types are not changed.

- Default: Always on.
- Default provided by: Aerie
- Where the default is set:
  - patches/hardening/0006-lock-hardened-prefs-from-extensions.patch: the write permission of three chrome.privacy entries is removed in chrome/browser/extensions/pref_mapping.cc
  - patches/hardening/0016-refuse-extension-cookie-allow-settings.patch: ContentSettingsContentSettingSetFunction::Run in chrome/browser/extensions/api/content_settings/content_settings_api.cc refuses a cookie allow or session-only setting for a pattern that could apply to a third-party context while third-party cookies are blocked
- Upstream code:
  - `chrome/browser/extensions/pref_mapping.cc`
  - `chrome/browser/extensions/api/preference/preference_api.cc`
  - `chrome/browser/extensions/api/content_settings/content_settings_api.cc`
- Aerie patches:
  - `patches/hardening/0006-lock-hardened-prefs-from-extensions.patch`
  - `patches/hardening/0016-refuse-extension-cookie-allow-settings.patch`
- Settings row: Read-only
- Cost: Privacy extensions that set these options through chrome.privacy get a permission error. An extension that allows cookies through chrome.contentSettings for a pattern that is not one site's own cookies (for example a whole domain, or a site in any context) gets an error while third-party cookies are blocked. The user can still make the same changes in settings.
- Bench checks: `hardening_extension_prefs`

## Manual fingerprint test

The fingerprint run is manual and outside the Bench, because the Bench sends no traffic to outside sites and these test sites are outside it. Run it by hand after a slice that changes a fingerprint surface, and write the result in the Result column below with the build train and the total in bits.

Procedure:

1. Use a build that has the change, with a profile that has Fingerprinting protection on (the default) and no per-site exception for the test sites.
2. Open Cover Your Tracks (coveryourtracks.eff.org) and run the test. Write down the total in bits and the bits of each surface in the table.
3. Open browserleaks.com and read the WebGL, WebGPU, Canvas, Fonts and JavaScript pages (JavaScript shows the screen, the device memory and the number of cores). Write down what each one reports for the surfaces in the table.
4. Repeat step 2 once with Fingerprinting protection turned off for the site, to see the real values. Turn it back on after.
5. The baseline run had NextDNS on. That changes the tracker blocking half of Cover Your Tracks and not the fingerprint half, so say in the Result column if NextDNS or another DNS filter was on.

Baseline: build train 8 (maintainer, 2026-11-08), Cover Your Tracks result strong protection, fingerprint randomized. Total: 18.58 bits.

| Surface | Value | Bits | Result |
| --- | --- | --- | --- |
| WebGL vendor and renderer | ANGLE (AMD, AMD Radeon RX 6600 (radeonsi navi23 LLVM 20.1.2), OpenGL ES 3.2) | 18.58 (1 in 392,956) |  |
| User agent | Chrome/157.0.0.0, Linux | 9.08 |  |
| Screen size | 3440x1440x24 | 7.31 |  |
| System fonts | 8 core fonts | 6.77 |  |
| Time zone | Europe/Zurich, -60 | 6.53 + 3.12 |  |
| Device memory | 32 GB | 3.86 |  |
| Platform | Linux x86_64 | 2.47 |  |
| Hardware concurrency | randomized | 1.84 |  |
| Canvas, WebGL hash and AudioContext | randomized by first party | about 1 each |  |
| Plugins | the standard 5 PDF viewers | 0.66 |  |
| DNT, cookies, touch, language | not listed | under 1 |  |

## HTTPS-only and local development

HTTPS-only mode is strict by default. Chromium already skips the upgrade for `localhost`, the whole
`127.0.0.0/8` block, `::1` and `*.localhost` (`net::IsLocalhost`), so a dev server on
`http://localhost:<port>` loads with no warning page. The Bench check `hardening_localhost_dev`
keeps that true.

For any other http site, for example a dev server on the local network, the warning page has one
Continue to site button. The choice is kept per host name for 15 days after the last visit
(the `HTTP_ALLOWED` content setting). Settings, Privacy and security, Security hardening, lists
every host that is allowed over http and has a Remove button for each one.

## Turning off the JIT for one site

The JavaScript optimizing JIT is on for every site. To turn it off for one site, open Settings,
Privacy and security, Site settings, Additional content settings, JavaScript optimization, and add
the site under Not allowed to use JavaScript optimization. The same switch is on the site details page.
The Security hardening page has a row that links to it. The change applies the next time a tab
navigates to the site, and it is kept after a restart.

This is Chromium's own setting (the `javascript-optimizer` content setting), so Aerie adds no code for it.
A renderer for a site with the JIT off starts with `--js-flags=--disable-optimizing-compilers`. The
mode is part of the `SiteInfo`, and `RenderProcessHostImpl::IsSuitableHost` will not give a site a
process that runs the other mode. Sites with the JIT off therefore never share a process with sites that
have it on, and never reuse one. A source gate (`js_optimizer` in `tools/aerie_build/treegate.py`)
and the Bench check `hardening_js_optimizer` keep that true.

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

- Workers: A worker gets no noise token yet (issue #113), so a worker always sees the masked WebGL values, also when protection is off for its site.
- WebGPU adapter info: Only the vendor, architecture, device, description and driver strings are emptied. The adapter limits, the feature list and the subgroup sizes are real values.
- Device memory above 8 GB: The baseline showed 32 GB because upstream raised the desktop maximum from 8 to 32 GB (crbug.com/454354290). Aerie caps it at 8 GB, and that cap stays when protection is off, so a site never learns the real value.
- Client rects noise: getClientRects and getBoundingClientRect noise exists upstream and now takes its factor from the site's text-metrics token, but it stays off until fonts are handled and it is measured again.
- WebGL readback beyond the default framebuffer: readPixels from framebuffer objects, in float formats and into pixel pack buffers, and WebGPU readback, stay exact because GPGPU, TF.js and video readback need exact values (issue #119).
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
