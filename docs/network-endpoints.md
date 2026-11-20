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
and `chrome/test/data/pdf` files from `chromium.googlesource.com`, pinned in `helium.lock`. `./aerie build` never does,
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

## Request policy

The network service refuses a request that the browser makes on its own unless it is listed here
(`services/network/aerie`, issue #257, patches `network/0002` and `network/0003`). The design is Crest's
(`crest_request_policy`, MPL-2.0); the code is Aerie's own. It is defence in depth for the Bench check
`zero_traffic`: a request that a later Chromium or Helium adds fails at run time, and the Bench names it.

### What is judged

A **URL request** (`NetworkServiceNetworkDelegate::OnBeforeURLRequest`, so every redirect hop too), a
**preconnect** (`NetworkContext::PreconnectSockets`) and a **name lookup** the browser asks for
(`NetworkContext::ResolveHost`: DNS prefetch). Only `http`, `https`, `ws` and `wss` URLs are judged; the
other schemes reach no host. A refusal is net error `ERR_BLOCKED_BY_ADMINISTRATOR` (-22), a count in the
`Aerie.Network.RequestPolicy.Refused` histogram, and a log line with the host only.

**Who is trusted: the exact mechanism.** A request is skipped only when its factory is *positively* known to
serve a renderer: the factory's `process_id` is a valid renderer process (not the browser, not null), the
factory is not `is_trusted`, and it carries a `request_initiator_origin_lock`. Chromium requires that lock on
every factory vended to a renderer and allows its absence only on browser factories. A request that fails any
one of these, one with no `URLLoader` at all (WebSockets, the certificate verifier, DNS over HTTPS), and one
from an unknown factory, is **the browser's own** and is judged. Nothing a page can set is read: no header,
no `request_initiator`, no URL. A utility process, or a browser-made factory that carries a renderer's process
id but is trusted or unlocked, is judged.

A judged request is then decided by its traffic annotation:

1. No usable annotation (missing, `undefined`, or never set): refused.
2. An annotation in the "user" or the "site" table below: allowed for any host.
3. An annotation with a row in the host table: allowed to that host and its subdomains, on label boundaries.
4. Anything else, including an annotation a later Chromium adds: refused.

A preconnect or lookup with no annotation (the lookup API has none) is judged by host as browser traffic:
allowed only when some row of the host table names the host. So speculative connections and DNS prefetch are
refused for every ordinary site. That costs a little page-load speed and is the intended trade: the browser
does not resolve or connect to a name that nobody asked for.

No pref and no extension changes these tables: they are compiled in and generated from this page by
`./aerie netpolicy generate`. A test fails when the generated file differs from this page, and
`./aerie netpolicy check --tree <source>` (also a source gate in `./aerie patches gates`) fails when an
annotation name does not exist in the pinned source or in Aerie's overlay. Add a host by adding a row here, in
a reviewed change. There is no row for the browser update check (#130): it is not a browser request yet, and
it needs a row when it lands.

### Limits: what the policy does not see

Do not read this page as "every request is judged".

- Requests from a renderer process (pages, workers, extensions) are not judged by design.
- Raw DNS: the host resolver's own sockets, and a renderer's name lookups (`mojom::HostResolver`), are not
  hooked. DNS over HTTPS is a URL request and is judged (annotation `dns_over_https`).
- QUIC and HTTP/3: a connection to an already-allowed host can move to an alternative service the server
  names. The alternative is not judged.
- CORS preflight requests are made inside the network service for the request they guard and take that
  request's decision.
- `ftp:` is not supported by this browser. Other sockets (WebRTC, WebTransport, `UDPSocket`) are not URL
  requests and are not judged.
- The Bench's NetLog scan (`zero_traffic`) stays the end-to-end proof for everything above.

### Host rows

Annotation plus host, so a host is reachable only by the code that is meant to use it:

<!-- request-policy:hosts:begin -->
| Annotation | Host | Why |
| --- | --- | --- |
| `aerie_filter_list_download` | `easylist.to` | Built-in filter lists: EasyList and EasyPrivacy |
| `update_client` | `update.googleapis.com` | Widevine component update check, only after the user opts in (#7) |
| `update_client` | `dl.google.com` | Widevine component download, only after the user opts in (#7) |
| `update_client` | `gvt1.com` | Widevine component download from Google's CDN, only after the user opts in (#7) |
<!-- request-policy:hosts:end -->

### (a) Directed by the user

The user typed or chose the destination. Allowed for any host.

<!-- request-policy:user:begin -->
| Annotation | Why |
| --- | --- |
| `navigation_url_loader` | The user types an address or follows a link |
| `download_bubble_retry_download` | The user retries a download |
| `download_manager_resume` | The user resumes a download |
| `download_web_contents_frame` | The user downloads from a page |
| `drag_download_file` | The user drags a file out of a page |
| `parallel_download_job` | A download the user started, in parts |
| `save_file_manager` | The user saves a page |
| `render_view_context_menu` | The user picks a context menu action such as save link as |
| `dns_over_https` | The secure DNS server the user chose in Settings (off by default) |
| `proxy_config_direct` | Fetching the proxy script the user or the operating system configured |
| `proxy_config_settings` | Fetching the proxy script the user set in Settings |
| `proxy_config_system` | Fetching the proxy script the operating system configured |
| `devtools_network_resource` | The developer tools, opened by the user |
| `devtools_cdp_network_resource` | The developer tools protocol, attached by the user |
| `aerie_custom_filter_list_download` | A filter list at an address the user added (#10) |
<!-- request-policy:user:end -->

### (b) Trusted by annotation

The destination is set by the site being visited (or by an extension the user installed), never by Google or
Chromium code. Allowed for any host. A site can already reach any host from its own page, so this adds no
host a site did not already pick.

<!-- request-policy:site:begin -->
| Annotation | Why |
| --- | --- |
| `certificate_verifier_url_loader` | Certificate chain, OCSP and CRL fetches: the address is in the site's certificate |
| `certificate_verifier_url_request` | Certificate chain, OCSP and CRL fetches: the address is in the site's certificate |
| `certificate_verifier_2qwac_loader` | Certificate chain fetch for a qualified website certificate: the address is in the site's certificate |
| `service_worker_script_load` | A site's service worker script, at the site's address |
| `service_worker_update_checker` | A site's service worker update check, at the site's address |
| `service_worker_navigation_preload` | A site's service worker navigation preload |
| `service_worker_race_network_request` | A site's service worker network race |
| `worker_script_load` | A site's worker script |
| `reporting` | Reports to the endpoint a site named in its Report-To header |
| `dbsc_registration` | A site's device-bound session endpoint |
| `keepalive_fetch_retry` | Retry of a site's keepalive fetch |
| `background_fetch_context` | A site's Background Fetch |
| `early_hints_preload` | A resource a site listed in an Early Hints response |
| `speculation_rules_prefetch` | A prefetch a site's speculation rules asked for |
| `speculation_rules_prefetch_probe` | The probe before such a prefetch |
| `payment_manifest_downloader` | A payment manifest named by a site's payment request |
| `sigined_exchange_cert_fetcher` | A signed exchange certificate named by the exchange |
| `webauthn_rp_id_check` | The related-origins file of the site asking for a passkey |
| `web_app_origin_association_download` | The association file of a web app the user installs |
| `web_install_manifest_fetcher` | The manifest of a web app the user installs |
| `renderer_initiated_download` | A download a page started after a user gesture |
| `websocket_basic_stream` | A site's WebSocket |
| `websocket_stream` | A site's WebSocket |
| `bidirectional_stream` | A site's bidirectional stream |
| `downloads_api_run_async` | A download an extension the user installed asked for |
<!-- request-policy:site:end -->

### (c) Refused

Everything else, among them these browser features that Chromium would run on its own and that Aerie
therefore refuses (a refused feature is acceptable here):

- `predictive_prefetch`, `predictive_preconnect`, `network_hints_preconnect`, `search_engine_preconnect`,
  `preload_activation_beacon`: speculative loading. Nobody asked for the destination.
- `dns_transaction`, `ssl_name_mismatch_lookup`: name lookups, not URL requests; handled as above.
- `well_known_path_that_should_not_exist`, `pdf_plugin_placeholder`: no Aerie feature needs them.
- `blink_resource_loader`, `blink_extension_resource_loader`, `blink_fetch_later_manager`, `favicon_loader`:
  renderer annotations. A renderer's request is skipped, so they need no row; the same annotation on a
  browser request is a bug and is refused.

### Extension updates and installs (decision for the maintainer)

`extension_manifest_fetcher`, `extension_crx_fetcher` and `content_hash_verification_job` are **refused**
(group c). They cannot be called "the user's extension proxy": Aerie rewrites only the Web Store update URL to
the proxy (`ExtensionManagement::GetEffectiveUpdateUrl`); an extension with its own `update_url` in its
manifest is fetched at that address, whoever runs it. The proxy is a pref, and no pref may widen the policy.
Until the maintainer decides (options in the report of #257), extensions from the Web Store do not update or
install through the browser, which matches the state today: no proxy domain is built in (#19).

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
