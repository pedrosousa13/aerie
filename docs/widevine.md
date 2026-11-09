# Widevine (DRM) is opt-in

Widevine is a closed-source decryption module made by Google. Streaming sites use it to play
protected video. Aerie builds it in (`enable_widevine=true` in Helium's flags) but does not
download, register or load it until the user opts in.

## How the user opts in

- **Settings.** Privacy and security, Security hardening, "Widevine DRM". It is one row of the
  hardening table (`tools/aerie_hardening/controls.json`, id `widevine`), so `docs/hardening.md`,
  the page markup and the Bench pref check follow it. Default: off.
- **Prompt.** The first time a page uses encrypted media (it asks the browser for key system
  support), an infobar says what Widevine is and that it comes from Google, with two buttons:
  "Turn on Widevine" and "No thanks". "No thanks" records the site in
  `aerie.widevine.declined_sites`, and that site is never asked again. Closing the infobar neither
  turns Widevine on nor records a decline, so the next DRM request from that site asks again.
  A decline in a private (incognito) window is kept in memory only and applies to private windows
  until Aerie exits; regular windows ignore it. A decline in a regular window applies everywhere.
  After "Turn on Widevine" the prompt text says Aerie needs a restart to finish.
  Nothing is asked when Widevine is already on.

Both write the same pref, `aerie.widevine.enabled` (Local State, default `false`).

## What changes in the browser

| Piece | Where |
| --- | --- |
| Prefs, "should we ask" rules, update URL, which apps may reach Google, registration gate | `overlay/components/aerie_widevine` (unit tests in `widevine_opt_in_unittest.cc`) |
| Registration only after opt-in | `patches/widevine/0001-register-widevine-only-when-opted-in.patch` |
| Google update URL after opt-in, and only Widevine in what Google gets | `patches/widevine/0002-update-widevine-from-google-when-opted-in.patch` |
| Settings can read and write the pref | `patches/widevine/0003-allow-widevine-pref-in-settings.patch` |
| The prompt | `patches/widevine/0004-ask-before-the-first-drm-request.patch` and `overlay/chrome/browser/aerie_widevine`. Its text is three grd messages in `aerie_widevine_strings.grdp`, which `patches/widevine/0005-add-widevine-prompt-strings.patch` adds to the browser's strings |

With the pref off, `RegisterWidevineCdmComponent` is never called, so the component updater has
no Widevine entry and nothing asks for it. Helium's component update origin is still the dummy
host. With the pref on, `RegisterWhenEnabled` registers the component (at startup, or at once
when the pref turns on), and `ComponentUpdateUrl` gives the updater Google's server. The CDM registers
itself with the browser when its download finishes. On Linux that registration only takes effect at the
next browser start, so Aerie needs a restart to finish turning Widevine on. The prompt and the
settings row both say so.

## Signature check

The update client verifies the CRX3 signature of every component before it unpacks it. The
Widevine installer policy pins the signing key as the SHA-256 hash `kWidevineSha2Hash`
(extension id `oimompecagnajdejgnnjijobebaeigek`), and `VerifyInstallation` checks the unpacked
manifest and files before the CDM is registered. Aerie changes none of this. A source gate
(`widevine_opt_in` in `tools/aerie_build/treegate.py`, run by `./aerie patches gates`) fails if the
pinned hash changes.

## Network

Off: no Widevine request. On: `update.googleapis.com` (the update check and pings name only the Widevine
component, never the CRLSet or any other component) and the download host in the update reply. Playing
protected video also contacts the license server of the site, as it does in any browser; that is the site's
traffic, not Aerie's.
See `docs/network-endpoints.md`. The zero-traffic Bench run never opts in.

## Why a restart stays required on Linux

A Widevine that finishes downloading is installed by the component updater, which writes the
hint file (`WidevineCdm/latest-component-updated-widevine-cdm`) into the profile. Chromium on
Linux reads that file only while the browser process starts, in `chrome/common/media/cdm_registration.cc`,
and registers the CDM it names with the content layer then. That code runs in the browser and
renderer start-up path and has no pref or component updater to ask. Registering a CDM in a running
browser would need a new hook there and a way to tell renderer processes that already started. #56
leaves that out, so the prompt, the settings row and the hardening text say Aerie needs a restart to
finish turning Widevine on. Turning it off stays as described under "Known limits": a downloaded
module is not unloaded.

## Known limits

- **Turning it off does not unload a downloaded CDM.** Chromium reads a hint file in the profile
  at startup and registers the CDM it names. The toggle stops new downloads and updates; delete
  the `WidevineCdm` folder in the profile to remove the module. Fixing this needs a hook in
  `chrome/common/media/cdm_registration.cc`, which has no pref access.
- **The prompt fires on the first use of any encrypted media**, not only Widevine (the browser
  does not say which key system was asked for at that point). A site that only uses Clear Key
  gets the prompt too. It does so once for a site that declined.
- **The CRLSet is never updated.** Before this change the CRLSet got updates only while Widevine was
  opted in, because Google's server was the only one the updater could reach. Now Aerie never sends
  the CRLSet to Google. The updater has one server for all components, so a request to Google's
  server names only Widevine, and any other component is left out
  (`update_client::Configurator::ShouldSendToServer(app_id, url)`). The decision follows the URL
  the request uses, not the pref, so turning Widevine off while a check runs cannot let the other
  components through. Without the opt-in the request goes to the dummy origin and is refused.

## How it is checked

| Claim | How | When |
| --- | --- | --- |
| Fresh profile: key system refused, component not registered, no request that could fetch it | Bench check `widevine_opt_in` (`./aerie bench --check widevine_opt_in`) | needs a build |
| Same check's negative control: with the pref set in Local State, the component is listed | same | needs a build |
| Opt-in makes a DRM page report Widevine available | `--widevine-online SEC` on the same check, on a network that reaches Google | needs a build and a network |
| Default is off and the row is on the settings page | `hardening_pref_defaults`, `hardening_settings_page` | needs a build |
| Registration and update URL go through the gate; the update check and ping leave out every app but Widevine; signing key hash unchanged | source gate `widevine_opt_in` | now, on the patched tree |
| The update request body names only the Widevine app id | `--widevine-online SEC` on the same check: the opted-in browser logs its NetLog with `--net-log-capture-mode=Everything` and QUIC off, and the check reads the request body from the bytes sent to `update.googleapis.com`. It fails if the body names another app id, and also if it cannot read a body. | needs a build and a network |
| Turning the toggle on in a running browser opts in like the seeded pref | Bench check `widevine_opt_in`, subcase `toggle`: the settings page sets `aerie.widevine.enabled` through `chrome.settingsPrivate`, as the row does, and chrome://components then lists Widevine at once, with no restart | needs a build |
| Accepting the prompt opts in | Bench check `widevine_opt_in`, subcase `accept`: a browser started with UI DevTools loads `drm.html` (it asks for `com.widevine.alpha`), the check finds the infobar's accept button by its label, presses it through `DOM.dispatchMouseEvent` (an infobar is browser UI, so CDP cannot click it), and chrome://components then lists Widevine with no restart. It fails if the pref was on before the page, if no button appears, or if none was pressed | needs a build |
| Opt-in rules (prompt once, decline sticks, URL, registration) | `aerie_widevine_unittests` (`./aerie test-cpp`) | needs a build |
| The zero-Google check still passes with Widevine off | `./aerie bench --check zero_traffic` | needs a build |
