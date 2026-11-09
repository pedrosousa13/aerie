# Native ad blocking (the Filter Engine)

Aerie blocks ads and trackers in the browser itself. The engine is Brave's
adblock-rust (MPL-2.0, used unmodified). The lists are EasyList and EasyPrivacy.
Aerie wrote its own bridge, service and request proxy; it did not copy
brave-core. Decisions and reasons: [ADR 0001](adr/0001-native-adblock-engine.md).

## Status

The engine, the proxy (including redirect hops) and the build steps are written, and Helium's bundled
uBlock Origin is removed: `patches/helium-drop.txt` drops its six patches, `patches/helium-adapt.txt` replaces
two Helium patches whose hunks sat next to it, and the build does not download its assets. Manifest V2 support is
a different patch (`ungoogled-chromium/extensions-manifestv2.patch`) and stays, so users can install uBlock Origin
themselves. Aerie keeps MV2 only as long as Helium carries it. It does not patch MV2 back in or host MV2
extensions ([ADR 0001](adr/0001-native-adblock-engine.md#amendment-issue-76-2026-11-08-manifest-v2)). On a real
build, `./aerie bench --check ad_blocking --check mv2_extension` is the proof. The Patch Layer
feature and every dropped patch are explained in [helium-dependency-ledger.md](helium-dependency-ledger.md).

The two adapted Helium patches are pinned by sha256 to Helium's originals, so a Helium bump that touches them
fails `./aerie patches check` until they are re-cut. The method is in CONTRIBUTING.md ("Re-cutting an adapted
patch"). Helium's onboarding page, which offered a uBlock Origin toggle, is no longer downloaded: Aerie
has its own setup page ([onboarding.md](onboarding.md)).

## How a request is judged

```
renderer ──Mojo──> AdblockProxyingURLLoaderFactory ──> network service
                          │  (browser process, UI thread)
                          ▼
                   FilterEngineService ──posts──> its own sequence ──> AdblockEngine ──cxx──> adblock-rust
```

- `ChromeContentBrowserClient::WillCreateURLLoaderFactory` calls
  `aerie_adblock::MaybeProxyURLLoaderFactory` (one hook patch,
  `patches/adblock/0001-hook-url-loader-factory.patch`). It puts the proxy in the
  factory chain for document sub-resources, subframe navigations, workers, service
  workers, prefetch and early hints. Downloads and DevTools requests are not covered.
- The proxy runs in the browser process. The renderer holds the other end of a
  Mojo pipe. A compromised renderer cannot skip, read or change the verdict.
  The GN `visibility` of every Aerie target lists only `//chrome/browser:core`,
  so no renderer target can depend on them.
- `ClassifyRequest` (pure code, unit tested) turns a request into: URL, host,
  first party host, request type, third party or not. The first party is the
  top-level site: the request's trusted `IsolationInfo`, else the factory's (the
  hook passes it), else the initiator. So an ad iframe's own pixels are third
  party to the page, and a reload of an ad subframe knows its page. Third party
  uses Chromium's public suffix list (`SameDomainOrHost`), so adblock-rust's own
  domain resolver is never needed. It skips: non-http(s) URLs, main-frame
  documents, and first parties that are browser pages or extensions. Only
  beacons and `<a ping>` are pings; embed and object are `object`.
- `FilterEngineService` owns the one engine of the process. It is owned by a
  `ChromeBrowserMainExtraParts` (`chrome/browser/aerie_adblock`, hook patch
  `0004`): created before any profile, destroyed at shutdown. There is no
  singleton. Factories hold a `WeakPtr` and abort requests once it is gone. The
  engine lives on a thread pool sequence. The load and every query are posted to that
  sequence, so a query that comes before the load ends runs after it: requests
  are held until the engine is ready, never let through unchecked.
- **Redirects.** A judged request that is allowed is wrapped in an
  `AdblockProxyingURLLoader` (one per request). It forwards every
  `URLLoaderClient` call and `SetPriority`, and when the client answers a
  redirect with `FollowRedirect()` it judges the target (the client's URL if it
  names one) with the same top-level-site context as the first URL. The request
  for the new URL only starts when `FollowRedirect()` reaches the network
  service, so a blocked hop never goes out: the client gets
  `ERR_BLOCKED_BY_CLIENT` and the network request is cancelled. Requests that are
  never judged (main frames, non-http, browser pages) skip the wrapper. This
  Chromium's `URLLoader` interface has only `FollowRedirect` and `SetPriority`
  (no pause or resume calls).
- A blocked request fails with `net::ERR_BLOCKED_BY_CLIENT`. Nothing else about a
  request changes.
- If the shipped engine cannot be loaded, the browser crashes with a `CHECK`
  (after recording `Aerie.Adblock.EngineLoadSuccess=false`). The engine ships
  inside the build, so a failure means the build is broken, and failing open is
  not allowed. The build makes this a "cannot happen" case (see below).
- Histograms (declared in `overlay/tools/metrics/histograms/metadata/aerie/`):
  `Aerie.Adblock.EngineLoadSuccess`, `EngineLoadTime`, `RequestBlocked`. The Bench
  reads them.

Phishing and malware domain lists are a second category of the Filter Engine, with their own interstitial:
see [phishing-protection.md](phishing-protection.md).

Not covered: WebSocket blocking and main-frame blocking (both out of scope in
the issue). User controls are in [the section below](#user-controls-issue-10).

## User controls (issue #10)

[ADR 0003](adr/0003-user-managed-filter-lists.md) has the decisions. The parts:

| Part | What |
| --- | --- |
| Aerie Guard section of Settings, `chrome://settings/aerieGuard` (issue #103) | A top-level Lit route (`settings-aerie-guard-page`, in `chrome/browser/resources/settings/aerie_guard_page`; patches `0022` to `0025`), with an entry in the left menu. It has: the switch "Show the number of blocked items on the Guard icon" (the profile pref `aerie.guard.show_blocked_count`, on by default; when it is off the icon has no number and its tooltip still states the count), the switch for the automatic update, and the list of sites where blocking is off, with a field to add one. The Settings handler `AerieGuardSettingsHandler` (`chrome/browser/ui/webui/aerie_guard_settings`) wraps `FilterListManager`. It is added only to `chrome://settings`. Its messages are promises (`sendWithPromise`): `aerieGuard.getState`, `addList`, `removeList`, `updateList`, `updateAll`, `setListEnabled`, `setException`, `setAutoUpdate`; each answers with the state or rejects for an argument of the wrong type or longer than 4096 characters. Since issue #104 the page lists every filter list with its rule count, the number of exception rules it ignores (rules that match every address), where its copy comes from, its update status ("Updating", "Updated", "Update failed" with a Retry button, which is the same call as "Update now") and the last error, and a switch to turn the list on or off. The rule count of a list that was never downloaded is counted from the copy the build shipped, when the browser starts (nothing is fetched). EasyPrivacy and custom lists can be turned off; EasyList cannot, because it carries most of the cosmetic hiding, and the manager refuses it whatever the page sends (`listError: "required"`). A list that is off keeps its file, is left out of the engine and is not fetched by "update all" or the daily update; only removing a custom list deletes its file. The catalogue, custom filters and subscriptions have no rows yet (issues #105 to #108). Settings always opens in the regular profile, so it never shows or sets an incognito exception. |
| `chrome://filters` | Rewritten to `chrome://settings/aerieGuard` by `ChromeContentBrowserClient::HandleWebUI` (patch `0024`), as `chrome://help` is. The old page and its `aerieFilters.*` messages are gone (patch `0009` added them, `0024` removes them). |
| `FilterListManager` | `components/aerie_adblock/browser/filter_list_manager.*`. Owns the settings, downloads, checks, rebuilds the engine, runs the schedule. Owned by the main parts, next to the service. |
| Aerie Guard icon | The icon in the address bar, a page action (issue #99; it replaced the toolbar button). The controller and the icon art are in `chrome/browser/ui/views/aerie_adblock`; patches `0014` to `0017` register the action, add its action item, create a controller for each tab and add its strings. It shows on `http` and `https` pages only (not on `chrome://`, `aerie://`, `file://` or the new tab page): a shield for blocking on, another for blocking off on this site, with the blocked count drawn into the image (empty for 0, 1 to 99, then `99+`; the same number is in the tooltip and the accessible name; the Settings switch "Show the number of blocked items on the Guard icon" removes the number from the image and leaves it in the tooltip and the accessible name). What it shows comes from the browser process only: the committed URL of the tab's main frame, the `FilterListManager` exceptions and the `PageBlockCounter` (`components/aerie_adblock/browser/guard_state.*`, which has the unit tests). Pressing it opens the Guard panel (issue #100), a bubble that hosts `chrome://aerie-guard.top-chrome`, the first Aerie Lit WebUI (page in `chrome/browser/resources/aerie_guard`, controller, handler and Mojo interface in `chrome/browser/ui/webui/aerie_guard`; patches `0018` to `0021`). Pressing the icon again closes it. The panel shows the site host and favicon, the blocked count (live), and the "Guard on this site" switch, which turns blocking off for the site and its subdomains, or on again, and reloads the tab. In an incognito window the switch says the change is for this private session only and is not saved; when a regular window saved an exception, the switch is disabled and says why. The Advanced section (collapsed until the user opens it; the panel remembers whether it was open) holds the "Hide leftover ad boxes" row (issue #101, see "Cosmetic hiding off for one site" below) and the "Fingerprinting protection" row (issue #102): a switch that turns the fingerprinting protection on or off for the site and its subdomains (it calls `helium_noise::SetHeliumNoiseEnabledForOrigin`, so the scope is the one of that helper), the state behind it (on, off for this site, "Off in Settings" with an "Open Settings" link and no switch when protection is off for the whole browser, or "Not available"), a "Reload this page to apply the change." prompt with a Reload button (the noise tokens are fixed when a navigation commits, so the open page keeps its old values until it reloads), and a collapsed read-only list of what the protection changes (`aerie_fingerprint::ProtectedSurfaces()`). Under the blocked count an expandable list, "Show what was blocked", shows the addresses `PageBlockCounter` kept (at most 200, each at most 256 characters, with a line "Showing the first 200 of N blocked requests" when the page blocked more). They reach the page as plain strings in `PanelState.blocked_urls` and are rendered as text; the list has no allow or block actions. In an incognito window the row says the change is for this private session only; it writes to the incognito content-settings map, never to the regular profile or to disk. The handler refuses a change when the state has no switch or the host is not one `CanSetHeliumNoiseForOrigin` accepts. There is no per-surface switch. The footer link "Aerie Guard settings..." opens the Aerie Guard section of Settings (`chrome://settings/aerieGuard`, in the regular profile also from an incognito window). The panel is Mojo only (no `chrome.send`, no message handler). The handler acts only on the tab and host the browser captured when the bubble opened (`GuardTarget`); the page sends a bool or an enum, never a host or URL; on each call a tab that is gone or shows another host is refused and the bubble closes. The page is never preloaded. The Bench check `aerie_guard_panel` loads the page directly in a tab and proves it has no `chrome.send` and that a switch call and a fingerprinting call change nothing in `state.json` or in the `HELIUM_NOISE` settings, then presses the icon through UI DevTools to open, close, navigate and open the panel again with the browser alive; the browser tests check that an ordinary frame cannot bind `aerie_guard.mojom` and that a web page cannot navigate to the panel's URL, and the fingerprinting row turns the real canvas value back on for `a.localhost` only after a reload, refuses a stale or missing target and an off-in-Settings state, and in an incognito window writes to the incognito map only (they run in the combined build, not here). Helium's `helium-hide-page-actions` flag hides all page actions, this icon included (see the [Helium dependency ledger](helium-dependency-ledger.md)). |
| `PageBlockCounter` | One per tab. Counts what the engine blocked for the tab's page, from the frame id the factory got (patch `0008`). Resets when the tab commits another document. It also keeps the addresses of the first 200 blocked requests of the page, each cut to 256 characters (`BlockedUrlList`, issue #110), for the list in the Guard panel: memory only, never logged or saved, cleared with the count, kept the same way in an incognito tab and dropped with the tab; the count itself is not bounded. Recorded in `Aerie.Adblock.PageBlockedCount` when the page is left. |

How it works:

- **Files.** `<user data dir>/AerieFilterLists/state.json` and `lists/<id>.txt`. They are shared by
  all profiles, like the engine. Every write is atomic. A damaged or hostile `state.json` gives
  safe defaults: ids must be `[a-z0-9]{1,32}`, custom ids must be the hash of their address, only
  known error texts are kept.
- **Built-in lists** (EasyList, EasyPrivacy) cannot be removed. EasyPrivacy can be turned off
  (`enabled` in `state.json`; a missing or damaged value means on); EasyList cannot. Their text is also packed into
  `resources.pak`, because a serialized engine cannot be merged with another list. While the user
  changes nothing, the shipped serialized engine is used. After a list changes, the engine is
  compiled from text on its own sequence (about 0.2 s), without a restart.
- **A download** (`FilterListFetcher`) is https only, with no cookies, no referrer, no cache,
  a 5 MiB cap and a 60 s timeout. A redirect that leaves https stops it. Loopback http is allowed
  only with `--aerie-test-allow-loopback-filter-lists` (the Bench), like the extension proxy.
- **A check** (`check_list` in `aerie_adblock_core`) runs before a download replaces anything:
  UTF-8 text without NUL, not an HTML page, at least one network rule (an ignored exception rule does not count), 10 s to parse. A list that
  fails keeps the old copy; its error shows on the page. A saved copy that fails to build later is
  dropped and the built-in list goes back to the shipped text.
- **Schedule.** With automatic updates on (the default), all lists are fetched once a day while the
  browser runs. A fresh install waits a day for the first check, so a new profile makes no request
  at all (the Bench check `zero_traffic` depends on this). The only servers contacted are the
  lists' own addresses (see [network-endpoints.md](network-endpoints.md)). The day counts from
  the check, not from a success: a list that fails to download or check keeps its old copy, shows
  its error, and is not tried again until the next check a day later. There is no retry. The user
  can click Update for one list or all at any time.
- **Exceptions** are hosts. A request is never blocked when its top-level site is the host or a
  subdomain of it. They are read before the first request: the service holds requests until the
  settings are loaded. A change works at once and survives restart. The Aerie Guard icon reloads the
  page after a change so the effect shows. Turning blocking off for a site also turns cosmetic
  hiding off there: `FilterEngineService::IsCosmeticFilteringEnabled` (which the two stylesheet
  calls use) is false for an excepted site, besides what the policy says. Cosmetic queries wait
  for the settings like requests do.
- **Cosmetic hiding off for one site** (issue #101) is a second host set, `cosmetic_off` in the state file, with
  the same rules as exceptions: host plus subdomains, normalised by `NormalizeExceptionHost`, at most 1000 hosts,
  checked in `FromValue` (wrong types, bad or huge hosts are dropped, extra entries are cut). It turns only the
  hide sheets and the late watch off; network blocking stays on. An incognito window keeps its choices in memory
  per session (`SessionKey`), never writes them to the state file and drops them with the window. A choice the
  regular profile saved holds in incognito and cannot be undone there. `FilterListManager::Init()` installs the
  policy that reads these sets with `SetCosmeticFilteringPolicy`. The switch is the "Hide leftover ad boxes" row
  in the Advanced section of the Guard panel; changing it reloads the tab. The Bench check `cosmetic_site_switch`
  proves it.
- **Exception rules that match every address are ignored.** One `@@` rule with no scope can switch
  off every block for a type of request, for every site, with no error. The core drops a rule when
  it applies to arbitrary addresses. It tests the effect, not the text: the rule is compiled alone
  and asked about five reserved `.invalid` addresses (http, https, ws and wss), for each request type, as first party and
  third party, from a first party that no `$domain=` option names. A rule that applies to any of
  them is dropped. Examples: `@@http`, `@@|http`, `@@.`, `@@://`, `@@/./`, `@@||*`, `@@*$script`,
  `@@$image`. Host-anchored rules (`@@||host...`) are scoped by their host and are not tested.
  Rules scoped by host, path or `$domain=` (`@@||ads.example/ok`, `@@/safe/$domain=news.test`) work
  as before. The same test runs when a list is compiled (`sanitize_list`) and in `check_list`. A
  list that has such rules still passes the check, because the engine ignores them, and
  `ListCheck::ignored_exceptions` counts them. A list made only of them has no network rules and
  fails with "no blocking rules". `aerie_adblock_compile` prints a warning on stderr for each
  ignored rule (the first 20 per list). The count also goes through the FFI (`ListReport` and
  `BuildReport`), and the browser logs a `LOG(WARNING)` when it checks a downloaded list
  (`FilterListManager`) and when it compiles the lists (`FilterEngineService`). The filter page
  does not show it yet. The old rule that drops `@@` rules with an empty pattern also drops
  scoped ones with an empty pattern (`@@$generichide,domain=...`); that is unchanged, and the
  compile tool reports those under their own reason ("with no URL pattern").

  Known limit: the probe set is fixed and public, so a rule written to dodge it gets through. A
  rule that matches every address except those five probes (for example `@@.com^`, `@@||com^`,
  or a regex that excludes `.invalid`) is not caught. The check stops mistakes and lazy abuse,
  not a list author who has read this page. Lists are reviewed before they are pinned, and
  custom lists are the user's own choice.
- **Incognito.** An exception set from an off-the-record profile (the Aerie Guard icon in an
  incognito window) is kept in memory only, for that profile only
  (`FilterEngineService::SetSessionExceptions`, keyed by the profile). It is never written to
  `state.json`, it does not apply to the regular profile or to another incognito session, and it
  is dropped when the profile is destroyed. A saved exception also holds in incognito, and
  incognito cannot undo it. The Settings section lists the saved exceptions only: it runs in the regular profile and never shows an incognito exception.
  The session is a `SessionKey` (`session_key.h`), never a bare pointer, and
  `FilterListManager::SetException` owns both rules (it watches the profile's end and refuses
  the undo), so the Aerie Guard icon and the Settings handler only pass the `BrowserContext` (the handler
  always passes the regular profile). The manager's unit tests (`filter_list_manager_session_unittest.cc`)
  cover the incognito rules; the `filter_lists` Bench check no longer has an incognito step, because
  `chrome://settings` does not open in a DevTools incognito context.
- **Count.** Workers and requests with no frame are not counted. Another blocker's blocks are not
  counted either.

### MV2 extensions and the hook (issue #40)

`mv2_extension` failed on every real build from train 2 to train 4: the extension loaded and its
content script ran, but its blocking `webRequest` listener never saw `/mv2/ext-block-me.js`.
The first guess, that Aerie's proxy hides requests from the extensions' proxy, does not fit the
code. In `ChromeContentBrowserClient::WillCreateURLLoaderFactory` the extensions' proxy is
appended first, Aerie's after it, so a request reaches the extension before the Filter Engine.

What the evidence shows (train 2, 3 and 4 results; the NetLog of train 4):

* the whole check took 24 to 82 ms, and the NetLog has the page request and its two scripts
  within 10 ms of each other;
* the extension's persistent background page existed in the target list at the end;
* the check navigated to the fixture page right after `Extensions.loadUnpacked` returned.

A persistent background page starts in its own process and registers its listener some time
after the extension loads, and the browser learns of the listener later still. So the most
likely cause is a race in the check: the page loaded before the listener was live. This is
not proven without a real build. Aerie's hook and the extensions' proxy are not changed.

The check now waits first: it fetches a probe URL from the extension's own background page until
the extension cancels it, and only then loads the page. The result data has `listener_probe`
(`ready`, `attempts`, `seconds`). If the probe is never cancelled, the failure says the listener
never took effect, which points at MV2 `webRequestBlocking` gating or how the extension was
loaded. If the probe is cancelled but the page request still loads, the cause is in the
proxying of that request, and the hook is the place to look. For that case, run with
`--enable-logging=stderr --vmodule=aerie_adblock_hook=1` to log which factories get Aerie's proxy.

## Cosmetic filtering (hiding leftover ad containers)

Blocking a request leaves an empty box where the ad would have been. The Filter Engine
also reads EasyList's plain hide rules (`##.ad-slot`, `site.com##.x`, `#@#` exceptions)
and hides those elements. [ADR 0002](adr/0002-cosmetic-filtering-in-the-browser-process.md) put it in
the browser process; [ADR 0005](adr/0005-renderer-side-cosmetic-piece.md) (issue #68) added one small
renderer piece, with Brave Shields as the reference, so that a page cannot remove the hide and late
elements are hidden too.

```
browser process                                                      renderer (one agent per frame)
tab ──> AerieCosmeticTabHelper ──> FilterEngineService ──> engine    AerieCosmeticAgent
  │     decides everything                                  │          │  InsertStyleSheet(kUser)
  ├── SetRules(kind, css, append) ───── Mojo ───────────────┼──────────>  one page sheet, one generic sheet
  │                                                          │          │
  ├── isolated world: collect script ─ class/id lookup keys ─┘          │
  └── isolated world: late-watch script (MutationObserver)              │
        └─ __aerieCosmeticNotify(classes, ids) ──> agent ──> LateTokens(classes, ids) ──> helper
```

- `chrome/browser/aerie_adblock/aerie_cosmetic_tab_helper.*` is a `WebContentsObserver`, one
  per tab (hook patch `0007`, two `CreateForWebContents` calls in `TabFeatures`). When a
  frame commits it asks the service for the stylesheet for that URL (rules for the site, and
  the generic rules that do not start with a class or id) and sends it to the frame's agent. At
  `DOMContentLoaded` it starts the late watch, then reads the frame's class names and ids, asks
  for the generic rules that match, and sends those; it does it again at load. Rules for a page are keyed by
  the document (`WeakDocumentPtr`), so rules asked for one document never land on a later one of the same origin,
  and only active frames (not in the back-forward cache, not being deleted) are touched.
- **The agent** (`components/aerie_adblock/renderer`, hook patch `0012`: one `Create()` call in
  `ChromeContentRendererClient::RenderFrameCreated`) inserts the text as a **user-origin** sheet
  (`WebDocument::InsertStyleSheet(..., kUser)`). The page cannot see it (it is not in
  `document.adoptedStyleSheets` or `document.styleSheets`), cannot remove it, and cannot override
  it: a user-origin `!important` rule beats an author `!important` rule, so the inline
  `display:block !important` that anti-adblock scripts use loses. A frame has one page sheet
  (replaced whole on each navigation; a page with no rules clears it) and one generic sheet. The generic calls all
  append, and a line that is already in the sheet is not added again (`CosmeticSheetState`), so the passes at
  DOM ready, at load and for late elements can come in any order. Because an append removes and inserts the
  sheet again (Blink restyles), the agent paces it (`GenericInsertPacer`): the first insertion at once, later
  ones 250 ms apart at the soonest, 64 per document at most; rules that arrive in between wait and go in with
  the next one. There is always exactly one generic sheet.
- **Late elements.** The late-watch script (fixed text in `cosmetic_scripts.cc`, run in the Aerie
  isolated world) is a `MutationObserver` on the document. For inserted subtrees and for changed
  `class` or `id` attributes it collects the class names and ids it has not reported before (ASCII only: the
  engine takes no other, and skips such a token alone) and, `kLateFlushDelayMs` (250 ms) after the last mutation,
  calls `__aerieCosmeticNotify(classes, ids)`. Mutation records wait in a bounded queue and are read 200 records /
  2000 elements at a time, and a token leaves its queue only when the renderer accepted the report (the native
  function returns true), so the cost is bounded, and fair only under those limits: junk classes delay an ad's class, but a hostile page can exhaust them. The
  agent installs that one native function in the Aerie world only (`DidCreateScriptContext`); the
  page's world has no such function. The agent checks its two string arguments and sends
  `CosmeticHost::LateTokens(classes, ids)`. The helper asks the engine for the rules for those
  tokens and sends them with `append = true`. The patch that lets the report reach the helper is `0013`
  (one binder call in `chrome_content_browser_client_receiver_bindings.cc`).
- **The limits** are all in `components/aerie_adblock/common/cosmetic_limits.h`: the token limits
  of #44 (5000 class names, 5000 ids, 256 bytes each, 50 000 elements scanned: the Rust core's
  `MAX_PAGE_TOKENS`, `MAX_TOKEN_BYTES`), the sheet cap (1 MiB, the core's `MAX_COSMETIC_CSS_BYTES`) and the late
  watch's: per turn 200 records and 2000 elements, 5000 queued records, per document 200 000 elements and
  200 reports, one report per 200 ms from the renderer, at most 16 KiB per list, 32 classes read
  per element. The browser gates the reports per frame (half the renderer's interval) and for the whole tab
  with a token bucket (a burst of 64, then one per 25 ms, no lifetime cap; checked before the frame gate and taken
  from after it). The frame gate is `common/late_report_gate.h`, the same code the renderer uses. `tests/test_cosmetic_limits.py` fails if the shared ones disagree with the Rust core.
- **What the renderer piece may and may not do** (the terms of ADR 0005, enforced by
  `tests/test_cosmetic_renderer.py`): apply only CSS the browser sends; no network and no URL; read no
  filter list; two Mojo calls (`SetRules` in, `LateTokens` out, both in `cosmetic.mojom`); every input
  bounded. A compromised renderer can send token lists, and the browser treats them as untrusted: it
  refuses a list over the cap (and reports a bad message), gates the rate per frame, re-reads the policy
  and the per-site exception on every report, and only uses the tokens as lookup keys. Its visible effect
  is that it can ask the browser for generic hide rules, which are public list data.
- Per-site switch and off-switch: no sheet and no watch are started where cosmetic filtering is off (the
  policy, or a per-site exception, issue #10). Every late report is checked again, so turning blocking off
  stops the reports at once; sheets already inserted stay until the next navigation.
- The hook for issue #10: `FilterEngineService::SetCosmeticFilteringPolicy(callback)`. The
  callback gets the top-level page URL and the page's `SessionKey` and says whether cosmetic filtering is on.
  The default is on; `FilterListManager` installs the per-site policy of issue #101. Besides the policy, a site with a per-site "blocking off" exception (issue #10, see
  "Exceptions" above) gets no hide sheet: `IsCosmeticFilteringEnabled` checks both. A frame answers to its top-level page, so an ad iframe is not judged by its own
  host. A policy change reaches a page on its next navigation (stylesheets already
  injected stay).
- Output size is bounded: `Aerie.Adblock.CosmeticRuleCount` records the rules of each
  stylesheet; the core crate stops at 20 000 rules and 1 MiB, and reads at most 5 000 classes
  and 5 000 ids from a page.

### Security: what a list or a page can make the engine do

- Only `##` and `#@#` rules survive `sanitize_list`. Scriptlets (`##+js(`), HTML filters
  (`##^`), procedural rules (`#?#`), style injection (`#$#`) and `:style(` / `:remove(`
  actions are dropped when the lists are compiled, so the engine has no script to hand out.
- A selector is kept only if `is_safe_selector` accepts it: no `{ } ;`, no comments, no
  backslash escapes, no `<`, no `@`, no `url(`, balanced brackets, parentheses and
  quotes, at most 2 KiB. This runs when the lists are compiled and again on every stylesheet
  that leaves the engine, because adblock-rust is built without its `css-validation` feature
  (no extra crates to vet).
- One rule per selector: a selector a browser cannot parse voids only itself.
- The stylesheet goes into a script as a JSON string literal (`base::GetQuotedJSONString`).
  The only page-controlled text the browser reads is the class and id list, as engine lookup
  keys; it never reaches a script. The script that reads them only reads the DOM.
- The engine is the build's, from pinned lists: a page cannot add a rule.

### Limits

- A late element is hidden only for rules that match its class or id (or a selector the generic
  rules cover); a rule that needs a procedural filter is dropped like before.
- A page that makes more than the bounded number of late insertions (200 000 elements, 200 reports)
  stops being watched until it navigates.
- A hide arrives after the first paint of the element: a short flash is possible, as before.
- `site.*##rule` entity rules use a heuristic registrable domain (`AerieDomainResolver` in the
  core crate), because the embedded public suffix list is not compiled in and adblock-rust
  panics in cosmetic matching without a resolver. An unusual public suffix can miss an
  entity rule; it cannot apply a hide to a site it should not.
- Procedural cosmetic rules (`:has-text`, `:xpath`) and scriptlets are not supported.
- The engine grew from 4.7 MB to 6.0 MB with the cosmetic rules.

## What is built

`overlay/components/aerie_adblock/`:

| Path | What |
| --- | --- |
| `rust/core` | `aerie_adblock_core`: engine over adblock-rust. No C++, no panics, size and line limits. |
| `rust/ffi` | `aerie_adblock_ffi`: the cxx bridge. Four functions. No `Result` (Chromium has no C++ exceptions). |
| `rust/tool` | `aerie_adblock_compile`: host tool, lists in, serialized engine out, with self-checks. |
| `browser/` | C++: classifier, engine wrapper, service, proxying factory, hook, cosmetic scripts, unit tests. |
| `common/` | The limits of cosmetic filtering, the report gate and the two-call Mojo interface to the renderer piece (ADR 0005). The only targets besides `renderer/` that a renderer may depend on. |
| `renderer/` | The renderer piece: `AerieCosmeticAgent` (user-origin sheets, the native function for late tokens) and `CosmeticSheetState` (pure code, unit tested). |
| `selfcheck.json`, `compile_engine.py` | Build-time checks of the compiled engine against the real lists. |
| `resources/*.grd` | Packs the engine into `resources.pak` (hook patch `0002`). |

`overlay/third_party/filter_lists/`: the list lock, `README.chromium`,
GPL-3.0 text. The lists themselves are downloaded at build time.

The engine is compiled at build time (about 0.2 s, 4.7 MB for both lists) and
loaded at startup, so the delay before the first request is short.

## The build, step by step

`./aerie build` runs these after Helium's steps. Each step stops the build on
any problem.

1. **Overlay.** `overlay/` is an Aerie layer (`OverlayLayer` in
   `tools/aerie_build/overlay.py`). It runs after the patch series and before the
   Helium to Aerie substitution, on every build, with the same pristine and
   restore rules as the other layers (CONTRIBUTING.md, "Changing the Aerie layers
   of an existing tree"). It only adds files; a file that exists upstream is an
   error.
2. **Filter lists.** EasyList and EasyPrivacy are committed in the repo
   (`overlay/third_party/filter_lists/`, with the lock and the credits files) and
   arrive with the overlay. After the layers, `prepare_adblock` in
   `tools/aerie_build/pipeline.py` checks the copies in the tree against
   `filter_lists.lock.json` (sha256, size, the list's own `! Version:` line) and
   fetches nothing. Later, GN runs `compile_engine.py`, which checks the hashes
   again, compiles the lists, loads the result back and runs the self-checks in
   `selfcheck.json` (known ad hosts blocked, first-party allowed). An empty,
   truncated or wrongly parsed list fails there.
3. **Crates.** `prepare_adblock` runs `gnrt vendor` and `gnrt gen` under the crate lock
   `rust/crates.lock.toml`. See below.

### Refreshing the lists

easylist.to stays the recorded source, but it always serves the newest list, so
the lists change only through a reviewed commit:

```
./aerie lists update      # downloads both over https only (a redirect to http is refused), checks title and version, writes the files and the lock (with the final URL)
git diff --stat overlay/third_party/filter_lists/
```

Review it, update the versions in `README.chromium`, commit. The lists expire after
4 days upstream. The build never fetches. The browser fetches only when the user
asks, or on the daily schedule (see "User controls").

### The crates

Chromium keeps third-party crates in `third_party/rust`, made by `gnrt` from
`chromium_crates_io/Cargo.toml` and `Cargo.lock`. Patch `0003` declares one
dependency and fixes the gnrt settings it needs, `adblock = "=0.13.4"` (no default features; `full-regex-handling`,
`single-thread`). That brings in 12 new crates (arrayvec, flatbuffers, idna,
idna_adapter, percent-encoding, rustc-hash, rustc_version, seahash, semver,
thiserror 1.x and its macro) and changes 5 lock entries.

`rust/crates.lock.toml` records those packages, the crates.io sha256 of each added crate, and
the hashes of the `Cargo.toml` and `Cargo.lock` it was resolved against. The
build:

1. stops if Chromium's two files differ from those hashes (stale lock);
2. writes the locked entries into `Cargo.lock`, so `gnrt vendor` cannot choose other versions;
3. checks the gnrt settings Aerie's patch sets (adblock and regex in group `safe`, flatbuffers without its `build.rs`, thiserror v1 and v2 apart, aho-corasick allowed `unsafe`) and runs `gnrt vendor` and `gnrt gen`;
4. checks that the final `Cargo.lock` is the original plus the locked entries and nothing else;
5. downloads each added `.crate` from crates.io, checks its sha256, and checks that
   every file in it is in `chromium_crates_io/vendor` byte for byte;
6. checks that `third_party/rust/adblock/v0_13/README.chromium` says
   `License: MPL-2.0` and `Shipped: yes`, with a licence file that exists. That is
   what puts adblock-rust on `chrome://credits`.

Two added crates publish no licence text: flatbuffers (Apache-2.0) and seahash
(MIT). gnrt will not write a `README.chromium` without one, and Chromium credits
every shipped crate. The overlay adds Chromium-style crate patches,
`third_party/rust/chromium_crates_io/patches/{flatbuffers-v25,seahash-v4}/`, that
add each project's upstream `LICENSE` file. `gnrt vendor` applies them when it
downloads the crate. The flatbuffers text is the repository's `LICENSE` at the
commit the crate was published from (the same bytes as Chromium's
`//third_party/flatbuffers/src/LICENSE`). The seahash text was added upstream after
4.1.0 (redox-os/seahash `3088c5c`). gnrt skips a crate that is already vendored, so
the vendor step removes an added crate that has crate patches before it runs gnrt,
and the crate patches are part of the step's stamp. Drop a patch when a release of
its crate ships the file.

Chromium removes the `checksum` lines from its lock and empties the checksum
files of vendored crates, so cargo cannot do step 5 for us.

After a Chromium bump the lock is stale on purpose. Run `./aerie crates resolve`
(reads the tree, needs crates.io, writes only `rust/crates.lock.toml`), review
the diff, commit it.

## Run the tests

| What | Command |
| --- | --- |
| Python (build steps, Bench check logic) | `python3 -m unittest discover -s tests -t . -v` |
| Rust, FFI from C++, build tool (needs cargo, a C++ compiler, crates.io) | `AERIE_TEST_CARGO=1 python3 -m unittest tests.test_adblock_rust -v` |
| Rust only | `cd overlay/components/aerie_adblock/rust && cargo test` |
| Patches apply | `./aerie patches check` |
| C++ syntax against the real headers of a built tree (read-only, needs cargo) | `tools/check-overlay-syntax.sh` |
| C++ unit tests (in the train) | `autoninja -C out/Default aerie_adblock_unittests` |
| Blocking, for real (in the train) | `./aerie bench --check ad_blocking` |
| Blocking score on a list of real ad and tracker hosts (in the train) | `./aerie bench --check adblock_score` (see [bench.md](bench.md)) |
| Hiding, for real (in the train) | `./aerie bench --check cosmetic_filtering` (also proves the user-origin sheet and the late elements, see [bench.md](bench.md)) |
| Renderer piece and gate unit tests (in the train) | `./aerie test-cpp --target aerie_adblock_renderer_unittests --target aerie_adblock_common_unittests` |

## Credits

- adblock-rust: MPL-2.0, through the gnrt `README.chromium` (step 6 above).
- EasyList, EasyPrivacy: dual GPL-3.0-or-later / CC BY-SA 3.0, used under the GPL
  option. `overlay/third_party/filter_lists/README.chromium` and `LICENSE`,
  reached by the `filter_lists` GN group so that `licenses.py` finds them.
