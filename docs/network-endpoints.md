# Network endpoints: what can contact Google or Helium, and how Aerie removes it

M1 promise: Aerie makes no request to a Google-owned or Helium-owned host
(denylist: [network-denylist.md](network-denylist.md)). This page lists every
endpoint found in the pinned source (Helium 0.21.1.1, Chromium 157.0.8103.38)
and what Aerie does about it. The proof is the Bench check `zero_traffic`
([bench.md](bench.md)); the source gates (`./aerie patches gates`) keep the
text of the fixes from regressing between Bench runs.

This page is about the browser. One build-time download is outside the promise:
`./aerie test-cpp`, a developer command, fetches Chromium's test fonts from
`storage.googleapis.com/chromium-fonts`, pinned by the sha256 in the tree's
`DEPS`. For the two Views UI test targets it also fetches the `chrome/test/data/webui`
files from `chromium.googlesource.com`, pinned in `helium.lock`. `./aerie build` never does,
and none of it is part of the browser.

"Dummy origin" below is `https://helium-services-are-disabled.qjz9zk`. The
network stack refuses it before any DNS query (ungoogled-chromium's
`block-trk-and-subdomains.patch`).

Two more requests belong to the Bench, developer tooling that never ships in the browser. Neither is made by
the browser, and a default `./aerie bench` makes neither:

| Endpoint | Used by | What |
| --- | --- | --- |
| `https://raw.githubusercontent.com/Turtlecute33/adblocktest/<commit>/src/d3host.txt` | `./aerie bench --check adblock_score` (the Bench process, not the browser) | The host list of the Turtlecute ad-block test, pinned by commit, sha256 and size in `tools/aerie_bench/adblock_hosts.lock.json`. https only, no redirect to http; cached in the Bench download cache. `--adblock-update-pin` also asks `api.github.com` for the newest commit. |
| `https://adblock.turtlecute.org` | `--adblock-online SEC`, opt in, informational | The live test page, loaded in a second Bench browser. Off by default. |

## Helium-owned endpoints

| Endpoint | Where it was | What it did | Aerie |
| --- | --- | --- | --- |
| `https://services.helium.imput.net` | `components/helium_services` (`kHeliumDefaultOrigin`) | Base of all services below | The constant is gone. `ShouldAccessServices()` is `false`, whatever the prefs say. Patch `services/0002`. |
| `{origin}/com` | `GetComponentUpdateURL` | Component updates (CRLSet, CT logs, Widevine CDM) | Dummy origin. See "Known blocked attempts". Only after the user opts in to Widevine, the update URL is Google's (next section). |
| `{origin}/ext`, `{origin}/ext/cws_snippet` | `GetExtensionUpdateURL`, `GetWebstoreSnippetURL` | Web Store download proxy, extension updates | Only the user's extension proxy, https, set in Settings. Not configured: the dummy origin and a dialog (below). |
| `{origin}/ubo/assets.json` | `GetUBlockAssetsURL` | uBlock Origin asset list | Dummy origin; uBlock Origin uses the lists it ships with. It stays until #8 replaces it. |
| `{origin}/bangs.json` | `template_url_bang_manager.cc` | Search bangs | Not fetched (`ShouldFetchBangs()` is `false`). |
| `{origin}/dict/` | `GetSpellcheckURL` | Spellcheck dictionaries | Dummy origin, and no request at all: `DownloadDictionary` fails at once. Patch `services/0004`. |
| `https://updates.helium.computer/` | `kUpdateBaseUrl` (macOS and Windows only) | Browser update check | Constant removed; `GetBrowserUpdateURL()` returns the dummy origin. Patch `services/0002`. |
| `https://crash.helium.computer/crash` | `CrashReporterClient::GetUploadUrl` | Crash upload (after the user agrees) | `GetUploadUrl()` returns an empty string, the crash handler starts without a URL, and the mode is always Disabled, which also discards pending reports. The `--custom-crash-server-url` switch is removed. Patch `crash/0001`. |
| `helium.computer` | `set_ping_enabled_domain` | Extension update pings | `ping-disabled.invalid`: never matches a real host. Patch `services/0003`. |
| `github.com/imputnet/...` | Crash page link, onboarding page, a macOS help link | Links the user clicks | Not browser-initiated. Listed with a reason in the gates' allowlist (`HOST_ALLOWLIST`). |
| `github.com/pedrosousa13/aerie/...` | Alpha notice links (`TERMS.md`, `PRIVACY.md`) on the setup page and on About (Settings) | Links the user clicks | Plain `target=_blank` anchors. Nothing loads until the user clicks. |

Settings has no Helium services page: the toggles did nothing, so the page, its
route and its row are gone (patch `services/0006`, source gate `services_ui`).
The "origin override" field lives on as the extension proxy row of Settings >
Privacy and security.

### Extension proxy

- Setting: the existing origin override pref, `helium.services.origin_override`
  (Settings > Privacy and security > Extension proxy). Empty by default: **not configured**.
- The domain is not decided. It is picked in #19. No domain is built in.
- Validation (`components/aerie_services/extension_proxy.cc`): https only; ASCII;
  a host; no user info, query or fragment; no path beyond `/`; loopback hosts
  (`localhost`, `127.0.0.0/8`, `::1`, their IPv4-mapped forms such as `[::ffff:7f00:1]`, and the
  unspecified address `0.0.0.0`/`::`) refused. Loopback is accepted only when the
  browser starts with `--aerie-test-allow-loopback-extension-proxy`, which the
  Bench uses and users never need.
- Not configured: `chrome.webstorePrivate.beginInstallWithManifest3` fails
  at once with "Extension proxy not configured. ..." (`unknown_error`), a
  warning dialog "Extension proxy not configured" tells the user what to do, and
  no request is made, to any host. Inline installs through
  `WebstoreStandaloneInstaller` fail the same way.
- Extension updates for installed extensions use the same proxy and the same rule.

## Google-owned endpoints

Chromium's Google endpoints are removed by ungoogled-chromium and Helium
(domain substitution, `trk:` URLs, code patches, GN flags). Aerie keeps all of
that. What Aerie adds, because it is still reachable on the pinned build:

| Endpoint | Why it was reachable | Aerie |
| --- | --- | --- |
| `redirector.gvt1.com` (dictionaries) | Seen in a recorded launch of a stock browser; Helium re-pointed it at its proxy | No download host and no request (patch `services/0004`). |
| Google Safe Browsing (`safebrowsing.googleapis.com` and related) | Chromium's phishing and malware protection | Aerie's protection does not use it. Aerie checks pinned local lists instead and sends nothing (patch `adblock/0011`, [phishing-protection.md](phishing-protection.md)). The lists are fetched only by `./aerie lists update`, from their publishers (not Google), never by the browser. |
| `dns.google` and other DoH providers | Chromium's default secure DNS mode is Automatic, which upgrades to a provider that matches the system resolver | Default is Off (patch `network/0001`). Every provider stays selectable. |
| `google.com`, `google.*` search | `break_stuff_google` in the built-in engine list | Not in the list (patch `search/0001`). |
| `dns.google/resolve` (FedCM) | `ChromeContentBrowserClient::GetDnsTxtResolverUrlPrefix()` returned it; a page using FedCM delegation could make the browser query Google DNS | Returns an empty string, and `dns_request.cc` answers with no result without a request (patch `services/0005`). A source gate checks it. |
| Search suggestions | Sent each keystroke to the engine | Off by default in Helium (`inox-patchset/modify-default-prefs.patch`); a source gate checks it. |

### Started by the user or a page, not patched

These Google hosts stay in the source. Nothing in the browser requests them
on its own at startup or idle, so they are not in the zero-traffic phases. If a
user or a site starts one, that request is theirs.

| Host | When it is used |
| --- | --- |
| `chromewebstore.googleapis.com` (`kChromeWebstoreApiURL`) | Extension block-status lookup, reached only through enterprise policy |
| `chrome.google.com/webstore`, `chromewebstore.google.com` | "Open Chrome Web Store" links and new-tab promos: opened when the user clicks. What Aerie's extension story is, store page included, is decided in #19 |
| `support.google.com/...` | Help links in Settings and dialogs: opened when the user clicks (same class as the allowlisted GitHub links) |
| DoH provider table (`net/dns/public/doh_provider_entry.cc`) | Selectable in Settings, off by default (see above) |

If a Bench run ever shows one of these at idle, it needs its own patch.

### Only after opting in to Widevine

Nothing here runs on a fresh profile. When the user turns on `aerie.widevine.enabled`
(Settings, or the one-time prompt), `aerie_widevine::ComponentUpdateUrl()` gives the component
updater Google's update server, and the Widevine component is registered. The update check and
the event ping name only the Widevine component (`ShouldSendToServer` in `update_client`); the
CRLSet and any other component are left out of what Google gets.

| Host | Request | Why |
| --- | --- | --- |
| `update.googleapis.com` | `POST /service/update2/json`: update check and ping, naming the Widevine component only | The only server that offers the Widevine component |
| The download host named in the update reply (Google's) | The Widevine CRX | The component itself. The updater checks its signature against the pinned key before it unpacks it |

Left alone on purpose:

- **Widevine** is opt-in (#7, [widevine.md](widevine.md)). Until the user turns
  it on, the component updater is not told about it and gets only the dummy
  origin. After opt-in, the update check and the download go to Google (see
  "Only after opting in to Widevine"). The Bench's zero-traffic run never opts in.
- **uBlock Origin** stays until #8, with asset updates off.

## Aerie-owned endpoints

| Endpoint | Where | When | Aerie |
| --- | --- | --- | --- |
| `https://easylist.to/easylist/easylist.txt`, `.../easyprivacy.txt` | `FilterListManager` (built-in lists) | Update from the filter list page, or the daily schedule (on by default; the first check is a day after install) | The publisher's own address. https only, no cookies, no referrer, 5 MiB, 60 s. Can be turned off on `chrome://filters`. |
| Any https address the user added | `FilterListManager` (custom lists) | When the user adds or updates it, and on the schedule | Same rules. Never http (loopback http only with `--aerie-test-allow-loopback-filter-lists`). A redirect that leaves https stops the download. |

Neither host is Google-owned or Helium-owned. Nothing is fetched on a fresh install before the first scheduled check, a day later. See [adblock.md](adblock.md).

## Known blocked attempts

The component updater, the uBlock Origin asset fetch and a few Google-origin
code paths still construct a request to a `.qjz9zk` host. It is refused before
any DNS query, so nothing leaves the machine. They show in the NetLog, so
`zero_traffic` lists them as `blocked` and passes. `--zero-traffic-strict`
fails on them. The first build-train run says how many there are. Each one
that shows up should get its own patch, or the resolver rule that the #6
rulings allow as a fallback.

## Not covered

- Requests from a process that does not use the browser's network stack. The
  NetLog does not see them. The crash handler is the one that matters, and
  it has no upload URL.
- Anything a user does: pages they open, searches they run, a proxy they
  configure, a search engine they add.

## What is verified where

| Claim | How | When |
| --- | --- | --- |
| The series applies, no fuzz, no offset | `./aerie patches check` | now |
| No Helium host in the patched source; search list; secure DNS, crash, services defaults | source gates, run at the end of `patches check` | now |
| Denylist matching, the check's verdicts, the baseline demo | Python unit tests against recorded and hand-made NetLogs | now |
| The check sees traffic | run on a stock Chrome for Testing: fails with Google hosts | now (one run, see the report) |
| The Aerie build makes no denylisted request in 6 minutes | `./aerie bench --check zero_traffic --baseline <unmodified helium>` | train build |
| The Aerie C++ compiles and the validator unit tests pass | build; the `AerieExtensionProxy*` tests cannot run yet: they link only into `components_unittests`, which Helium's tree cannot build (#82) | train build |
| The not-configured dialog shows | by hand on the build; no Bench driver yet (the install API is not reachable from a local page) | train build |
