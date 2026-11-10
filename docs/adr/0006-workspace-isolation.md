# 6. Isolate Workspaces with a storage partition each, in one profile

Status: accepted (issue #72). The maintainer decided D1 to D4 on 2026-11-08 (see "Decisions"); build train 8 built and measured the prototype.

## Context

Workspaces ship in M5 ([#1](https://github.com/pedrosousa13/aerie/issues/1), stories 91 to 95). How they isolate
storage shapes the M2 sidebar and tab model, so the design is decided now and built later. The bar is story 92: two
accounts of one service, logged in at the same time in two Workspaces of **one window**, with nothing crossing.

Two candidates, from the issue:

1. **A profile per Workspace.** One Chromium `Profile` for each Workspace, one window showing tabs of several profiles.
2. **A storage partition per Workspace.** One profile, and a non-default `StoragePartition` for each Workspace, the way
   Chromium isolates `<webview>` and Isolated Web Apps.

Everything below was read in the Chromium 157 tree under Helium 0.21.1.1 (`/mnt/linux2/aerie-build/src/helium-0.21.1.1`)
and in brave-core on GitHub. **The ADR was written from reading the source.** Build train 8 then built the option 2 prototype and measured it
(results in [prototypes/workspaces/REPORT.md](../../prototypes/workspaces/REPORT.md)). Claims marked "to verify" still need the
Bench (#73).

### What each candidate is made of

A tab is bound to a profile through `Browser` and `TabStripModel`. `TabStripModel::profile()`
(`chrome/browser/ui/tabs/tab_strip_model.h`) is one `Profile*`. `Browser::profile_` is one `Profile*`. About 4,000 call
sites in `chrome/browser/ui` alone read `browser->profile()` (grep count: 3,992 in `.cc` files under `ui/`, 8,995
in `chrome/browser`) and many of them mean "the profile of the tab the user is looking at": the omnibox, the
navigator, bookmarks, extensions, settings, history, downloads. For example `CreateTargetContents`
(`chrome/browser/ui/navigator/browser_navigator.cc:577`) builds the new tab's `WebContents` from
`params.browser->GetProfile()`. A tab of profile B inside a Browser of profile A would open its links in A.

A storage partition is picked per `SiteInstance`. `StoragePartitionConfig` (`content/public/browser/storage_partition_config.h`)
is `{partition_domain [a-z]*, partition_name, in_memory}`. It is part of `SiteInfo` and of `SiteInfo::IsExactMatch`
(`content/browser/site_info.cc:595`), so the same site in two partitions gets two renderer processes. The supported
way to pin a whole tab to a partition is `SiteInstance::CreateForFixedStoragePartition`
(`content/public/browser/site_instance.h:255`): the `BrowsingInstance` keeps the partition for every later
navigation, including an unrelated `SiteInstance` (`render_frame_host_manager.cc:4096`). Each partition gets its own
`NetworkContext` (`StoragePartitionImpl::InitNetworkContext`, `storage_partition_impl.cc:3540`), configured by
`ProfileNetworkContextService::ConfigureNetworkContextParamsInternal` with its own on-disk paths
(`<profile>/Storage/ext/<domain>/<hex of the first 6 bytes of SHA-256(name)>/`, `storage_partition_impl_map.cc:65-73, 121-126, 294-313`). That one fact gives the network-side state split below.

## Decision

**Recommended: option 2, a storage partition per Workspace in one profile.** Partition domain `aerieworkspace`,
partition name the Workspace id (a random UUID), persistent on disk. Every tab carries its Workspace for its whole
life, because the partition of a `BrowsingInstance` cannot change. Moving a tab (story 95) creates a new
`WebContents` in the target partition and closes the old one.

**Rejected: option 1, a profile per Workspace in one window.** It splits more state (history, permissions, passwords,
extensions) but needs the single-profile assumption removed from `Browser`, `TabStripModel` and thousands of callers,
or a hidden Browser per profile with its tab strip shown inside another's window. Either is a large, permanent fork
of the part of Chromium that moves fastest, in the milestone (M2) that builds the sidebar. Arc proves it can be
done by a company that owned its fork. Aerie's Patch Layer is a rebase every Chromium release.

### Based on Brave Containers, credited

Brave Containers (brave-core, MPL-2.0, Brave 1.92) use option 2 the same way. Aerie does not copy brave-core code; it
writes its own, as with the Filter Engine in [ADR 0001](0001-native-adblock-engine.md), and credits the design. What
Brave does, read from brave-core (`master`, fetched 2026-11-08):

- `components/containers/content/browser/storage_partition_utils.{h,cc}`: `partition_domain = "containers"`,
  `partition_name` = container UUID, ids limited to ASCII alphanumerics and `-`.
- `chromium_src/content/browser/site_instance_impl.cc`: `CreateForURLWithOptionalFixedStoragePartition`, which calls
  `CreateForFixedStoragePartition`. The same entry point as this ADR.
- `browser/containers/containers_navigator_utils.cc`: new tabs inherit the partition from `NavigateParams`
  (`storage_partition_config` or `source_site_instance`).
- `BraveContentBrowserClient::ShouldInheritStoragePartition`: noopener windows stay in the partition. This is the hole
  that the prototype closes in `content/browser/web_contents/web_contents_impl.cc` (see below).
- `chromium_src/components/permissions/permission_util.cc`: the same change to `IsPermissionBlockedInPartition`.
- `browser/browsing_data/brave_browsing_data_remover_delegate.cc`: "Clear browsing data" visits every used container
  partition. Without it only the default partition is cleared.
- `docs/containers/session_persistence.md`: Chromium's session format does not store the partition, so a restored tab
  would open in the default partition. Brave puts `containers+<uuid>:` in front of the URL in the session and sync data.
- `brave_content_browser_client.cc`: the container id seeds the per-worker farbling token; zoom is shared on purpose.

Brave's blog post ([brave.com/blog/containers](https://brave.com/blog/containers/)) says only cookies and storage are
separate. It does not list what stays shared. **The issue asks for a leak run of a real Brave build (HTTP cache probe,
HSTS, connection reuse, permissions, auth cache). That was not run: no Brave build is available to this spike and
nothing may be built. It is on the train plan; until it runs, "Brave passes" is not claimed.** The verdicts below come
from reading Chromium, not from Brave's behaviour.

### Other prior art (behaviour only)

| Product | Mechanism | Shared across its workspaces | Lesson for Aerie |
| --- | --- | --- | --- | --- |
| Arc (closed source) | A profile per Space, spaces can share one | Per profile: logins, history, extensions apart. Reports disagree on extensions ([forum](https://talk.macpowerusers.com/t/arc-browser-spaces-and-profiles/35872)). Arc is no longer supported. | Option 1 works in one window, with a lot of engineering. |
| Zen ([docs](https://docs.zen-browser.app/user-manual/workspaces)) | A workspace can be bound to a Firefox container | History and extensions shared. An external link needs a container tab to move to a workspace; plain links are not placed. | These are the story 94 gap: Aerie must route external links itself. |
| Firefox Multi-Account Containers ([support](https://support.mozilla.org/en-US/kb/containers)) | `userContextId` in every origin attribute | Own cookies, cache, local storage. History was shared at the time of the sources found. | A per-container key on every store is the checklist; here it comes from the partition. |
| Vivaldi, Opera, Edge | Tab groups only | Everything | UI only. |

## Shared-state verdicts for option 2

The last column gives the verdict for option 1, the rejected candidate, so the choice can be checked.
"Split" means separate state per Workspace, with the reason. "Accepted" is a decision to leave it shared, with a
reason and a residual risk (named R-n in the [threat model](../security/workspaces-threat-model.md)). Items marked
"gap" are shared by default and need work in M5; they are the work list for the build train and for #73.

| # | State | Option 2 (partition) | How, or why not | Option 1 (profile) |
| --- | --- | --- | --- | --- |
| 1 | Cookies | **Split** | One `NetworkContext` and one cookie DB per partition. Gap G1: upstream does not persist session cookies for a non-main partition (`profile_network_context_service.cc:1452`), so a login by session cookie dies at restart. Prototype patch 0004. | Split. A cookie DB per profile. |
| 2 | localStorage, sessionStorage, IndexedDB, WebSQL, FileSystem/OPFS, Blob URLs, BroadcastChannel, Web Locks, SharedWorker | **Split** | Owned by the `StoragePartition` (quota, DOM storage, IDB, file system contexts). Blob URL fallback to the domain's empty-name partition is off for non-guests. | Split. Per profile. |
| 3 | Cache Storage | **Split** | Per partition. | Split. Per profile. |
| 4 | HTTP cache | **Split** | `http_cache_directory` is under the partition's path, one cache per `NetworkContext`. The network isolation key keeps working inside it. To verify: cache-probe check. | Split. Per profile. |
| 4b | Code cache | **Split** | `<partition path>/Code Cache` (`storage_partition_impl.cc:1587`). | Split. Per profile. |
| 4c | GPU shader cache, font cache | **Accepted** | Process-level caches of compiled shaders and fonts. They hold no page content or account data. Residual timing signal, R-6. |
| 5 | Service workers (and push subscriptions held by them) | **Split** | Registrations live in the partition. Aerie sends nothing to Google, so no push service is reachable (see `docs/network-denylist.md`); to verify on the train. | Split. Per profile. |
| 6 | HSTS | **Split** | `TransportSecurityState` per `NetworkContext`, persisted in the partition's network dir. Expect-CT: **n/a**, Chromium removed it in M107. | Split. Per profile. |
| 7 | DNS | **Split in process, accepted below it** | `ContextHostResolver` has its own host cache per `NetworkContext` (to verify); the `HostResolverManager`, the OS resolver and the DoH setting are shared. DNS answers are public data, not an account, but one Workspace can learn from timing that the OS resolver already knows a name, R-6. | Split in process, accepted below it. Same as option 2. |
| 8 | Sockets, HTTP/2 and QUIC sessions, connection pools | **Split** | `HttpNetworkSession` per `URLRequestContext`. No connection is reused across partitions, so a TLS client certificate or an HTTP auth state on a connection cannot carry over. To verify: NetLog socket-reuse check. | Split. Separate `HttpNetworkSession`. |
| 9 | HTTP auth cache | **Split** | In the `HttpNetworkSession`. | Split. |
| 10 | Client certificate choices | **Split** | The "which certificate did this server get" cache is `SSLClientAuthCache` in the session. The OS certificate store itself is shared (**accepted**: it is an OS object, not Aerie state). `AUTO_SELECT_CERTIFICATE` is set by enterprise policy only. | Split. The OS store is shared, accepted, as in option 2. |
| 11 | Permission grants | **Gap G2, must split** | `HostContentSettingsMap` is per profile and keyed by origin, not partition. A camera grant for `https://x.test` given in Workspace A applies to Workspace B. This is the largest hole in option 2. Upstream also blocks undelegated permissions (notifications) in a non-default partition (`permission_util.cc:678`); prototype patch 0003 allows them. Fix: a Workspace-keyed layer in front of the content settings read; size L, not prototyped. **Decision D2: split before Workspaces ship; required work.** Train 8 confirmed the leak: a notification grant made in one Workspace read `granted` in the other. | **Split.** `HostContentSettingsMap` is per profile. This is the main thing option 1 gives. |
| 12 | History, visited-link colouring | **Split (decision D1), gap G8** | `HistoryService` and the visited-link store are per profile, so today a site open in both Workspaces can learn which of its own links the other visited. **Decision D1: history is split per Workspace.** How: one `HistoryService` per Workspace, each with its own History, Favicons and Top Sites databases under the Workspace's partition directory, created lazily on first use. The profile's own service stays for tabs with no Workspace. A visited-link writer follows each service, and sends its table only to renderers of that Workspace's partition. The rejected variant, one database with a Workspace column, needs a schema change and a workspace filter in every query of the 8,879-line history core (`urls` is unique by URL); too invasive. Cost estimate below (G8). | **Split.** `HistoryService` and the visited-link store are per profile. The omnibox in the shared window would need the active tab's profile (see the Browser assumption in Context). |
| 13 | Autofill, saved passwords, payment methods, passkeys | **Accepted** | Per profile. The password manager offering both accounts' logins is the wanted behaviour. A page cannot read them without a user gesture. Residual: R-4. | **Split.** One password store per profile; the user keeps N copies of a login they want in two Workspaces. Platform passkeys stay shared. |
| 14 | Extensions | **Accepted as one set, narrowed by D3** | One extension set per profile. Content scripts and `webRequest` see tabs of every Workspace. `chrome.cookies` does not: the API reads the default partition (`extensions/browser/api/cookies/cookies_api.cc:120`, `ParseStoreCookieManager`), so extensions cannot read Workspace cookies by API, which also breaks cookie-managing extensions in Workspace tabs. An extension is trusted with all browsing; **decision D3:** each extension has an "allow in all Workspaces" setting, off for new installs (M-D1), and that is M5 work (size M). | **Split.** One extension set per profile, so each extension is installed and updated N times. The extension UI is the Browser's profile's. |
| 15 | Per-site blocking exceptions (#10) | **Accepted** | Stored by the Filter Engine in `state.json`, keyed by site (incognito is keyed by profile, patch 0008 carries the key). They are a policy, not a credential. Cheap upgrade path: carry a Workspace key the way the incognito key travels. | Accepted. The Filter Engine state is not per profile (same as option 2). |
| 16 | Network and cache partitioning keys (network isolation key, storage key, CHIPS cookie partition key) | **n/a** | They partition by top-level site inside one `NetworkContext` or partition. The Workspace is an outer key, so no change, and no interaction expected. | n/a. |
| 17 | Renderer process | **Split** | The partition is part of `SiteInfo`, so a Workspace never shares a process with another Workspace. One `RenderProcessHost` has one partition, so a compromised renderer cannot ask for another partition's network or storage. | Split. A process serves one browser context. |
| 18 | Preconnect, prefetch, DNS prefetch | **Accepted, gap G5** | `LoadingPredictor` and `PrefetchManager` use the default partition (`loading_predictor.cc:141,581`; `prefetch_manager.cc:404`). Aerie turns preloading off by default (patch `hardening/0008`), so this is closed by default. The Bench must keep it checked. | Split. Each profile has its own predictors. |

Items outside the issue's list, found while reading:

| # | State | Option 2 (partition) | How, or why not | Option 1 (profile) |
| --- | --- | --- | --- | --- |
| 19 | "Clear browsing data" | **Gap G3** | `BrowsingDataRemoverImpl` clears the default partition unless the filter names a partition (`browsing_data_remover_impl.cc:413`); `ChromeBrowsingDataRemoverDelegate` also clears HTTP cache, host cache and origin data on the default partition only (lines 295, 468, 521, 705, 986, 1395). The user would think data is gone. Brave patches the same. Size S to M. | Gap. The remover runs on one profile; the Clear command would have to visit every Workspace profile. |
| 20 | Session restore, "Reopen closed tab", tab sync | **Gap G4** | `SerializedNavigationEntry` has no partition; `session_restore.cc` has no partition handling. A restored tab opens in the default partition, mixing Workspaces. The M2 tab model must store the Workspace id itself. | Gap, worse: the session service writes to the Browser's profile, so a tab of profile B would be restored into A. |
| 21 | noopener windows, `window.open` from a Workspace tab | **Gap G6** | `WebContentsImpl::CreateNewWindow` sends a non-guest noopener window to `SiteInstance::Create()`, the default partition. Prototype patch 0002. | Split. `CreateNewWindow` uses the opener's browser context. |
| 22 | Tabs without an opener (new tab, external link, command line) | **Gap G7** | `CreateTargetContents` picks the default partition. Prototype patch 0001 adds `NavigateParams::storage_partition_config`; routing external links to the active Workspace is M5 work (story 94). | Gap. `CreateTargetContents` uses the Browser's profile. The profile prototype patch fixes this one site. |
| 23 | Downloads, clipboard, bookmarks, settings, search engines, zoom | **Accepted** | User-level data, not account state. Zoom is shared on purpose, as in Brave. | Split, and a cost: downloads, bookmarks, settings, search engines and zoom are per profile, so the user keeps N copies. |
| 24 | Browser fingerprint (user agent, GPU, fonts, screen) and the per-profile media device id salt | **Accepted** | One device, one browser: this cannot be split. A site seeing both Workspaces can link them by fingerprint. Aerie has no farbling; Brave seeds it by container. The Workspace is a login boundary, not an anonymity boundary. R-5. | Accepted. Same as option 2. |
| 25 | PWAs and app windows | **Accepted for M5** | Bound to the default partition. Workspace-aware app windows are out of scope. | n/a. PWAs belong to a profile; a Workspace profile could host its own. |
| 26 | Incognito | **n/a** | An off-the-record profile forces `in_memory`. Workspaces are off in incognito windows. | n/a. |

## Gap G8: history split per Workspace (decision D1)

Estimated from the Chromium 157 source. These are reading estimates, not measurements, and the first build will correct
them.

- **Where history lives.** `HistoryServiceFactory` (`chrome/browser/history/history_service_factory.cc`) builds one
  `HistoryService` per profile, initialised with `context->GetPath()`, and gives it a `ContentVisitDelegate(context)`
  that owns the visited-link writer and feeds every renderer of the browser context. The backend is a SQLite database
  (`history_backend.cc` 4,204 lines, `visit_database.cc` 1,761, `url_database.cc` 918) with Favicons and Top Sites
  databases next to it.
- **Chosen design: a `HistoryService` per Workspace.** A new factory, `WorkspaceHistoryServiceFactory::GetForWorkspace(profile,
  workspace_id)`, keeps a map from Workspace id to service. Each service is `Init`-ed with the Workspace's partition
  directory (`<profile>/Storage/ext/aerieworkspace/<hash>/`) as its history directory. No history code changes: the
  whole history stack works unmodified, a deleted Workspace deletes its directory, and "clear data for one Workspace"
  is the same call as today on one service. The profile's own `HistoryService` stays for tabs with no Workspace.
- **What must become Workspace-aware.** 131 non-test call sites of `HistoryServiceFactory::GetForProfile` in 99 files,
  and 48 keyed-service factories that depend on `HistoryServiceFactory`. Most are profile-wide (sync, which Aerie has
  off, metrics, expiry) and keep the profile's service. The ones that act for the visible tab or the omnibox must
  take the tab's Workspace: the page-visit recorder, the omnibox history providers and `InMemoryURLIndex`, `ShortcutsBackend`,
  `FaviconService` and Top Sites, the `chrome://history` page (`BrowsingHistoryService`), and the `chrome.history`
  extension API. Of the call sites, we estimate 25 to 40 change; of the factories, 4 to 6 (`in_memory_url_index`,
  `shortcuts_backend`, `favicon_service`, top sites, `history_clusters` if present) need a per-Workspace instance.
- **Visited links.** The delegate's writer sends its table to all renderers of the browser context. For a Workspace it
  must send only to renderers whose `GetStoragePartition()->GetConfig()` is that Workspace, and a new renderer gets its
  Workspace's table. About 100 lines in `components/visitedlink/browser/visitedlink_event_listener.cc` and
  `components/history/content/browser/content_visit_delegate.cc`.
- **Size: L.** About 1,200 to 2,000 lines over 40 to 60 files, one to two weeks of work, mostly call-site audit. The risk
  is not a missed call site that leaks: a missed site keeps using the profile's service and only mixes history *into
  the default (no Workspace) history*, never between two Workspaces. The Bench check below catches both.
- **Memory.** Each service adds a backend sequence, SQLite connections and, for the omnibox, an in-memory index. Train 8's
  7.9 MB per empty Workspace was measured **without** history services. Create the service lazily on a Workspace's
  first page visit and re-measure on the train; the 10 MB bar must be checked again with history on.
- **Rejected: a single database with a `workspace_id` column.** `urls` is unique by URL, visit counts and typed counts
  are aggregated per URL, and every query would need a filter. Every Chromium history change would conflict with it.

## Constraints on the M2 sidebar and tab model

Rules the tab model must follow from now on (they bind #73's Bench, which needs a way to open and move tabs without
the UI):

1. **A tab's Workspace is fixed when its `WebContents` is created.** The tab model stores `workspace_id` on the tab
   record. It does not read it back from the `WebContents`, because Brave found that a discarded tab's stub
   `WebContents` has no partition until it is restored (`ContainersWebContentsUserData`).
2. **"Move to Workspace" is create-new-and-close-old.** The model API returns a new tab id. Drag and drop in the sidebar
   calls it. In-page state is lost on purpose (R-2).
3. **One `TabStripModel` and one profile for all Workspaces.** The Workspace is a property of a tab, like a group.
   Switching Workspaces (story 93) hides and shows tabs. It does not swap Browsers or tab strips. Background
   Workspaces keep their tabs alive; the discard policy applies per tab.
4. **Session, "Reopen closed tab", Favorites, Pinned tabs and sync of Workspaces store `workspace_id` themselves.**
   Chromium's session format loses it (G4). A tab whose Workspace no longer exists must restore blank, not in the
   default partition (Brave's fail-safe).
5. **New tabs take their Workspace from the source tab, else from the active Workspace of the target window.**
   `NavigateParams::storage_partition_config` is the carrier (prototype patch 0001). No code may create a tab with
   `SiteInstance::CreateForURL` for web content in a window that has Workspaces.
6. **WebUI and `chrome://`/`aerie://` pages in a Workspace tab are unverified.** `GetSiteInstanceForNewTab` creates
   WebUI with `CreateForURL`, in the default partition. A new-tab page in a Workspace must be tested on the train; if a
   fixed non-default partition cannot host the new-tab page, the new tab starts as a Workspace blank page.
7. **The Workspace list lives in one profile pref.** Clear-data, the Bench, history and the sidebar all enumerate it
   (to be written in M5; the prototype has no list).
8. **Test hook for the Bench.** The model exposes `OpenInWorkspace(url, id)` and `MoveToWorkspace(tab, id)` to a WebUI
   message handler, as `chrome://filters` exposes `aerieFilters.*` to `filter_lists`. The Bench drives them over CDP.
9. **Workspaces are off in incognito windows** and in app/popup windows that are not tabs of the main window.
10. **Process count grows with Workspaces:** one process per (site, Workspace). The sidebar's tab-hibernation and
    memory budget must count Workspaces. See the report for what is measured.
11. **History is per Workspace (D1).** The omnibox, the history page and the tab's visit recording use the Workspace of
    the active or recording tab. The tab model must give any code that records or reads history the tab's Workspace
    (`GetHistoryForWebContents`), not the profile.
12. **Every tab shows its Workspace (D4).** The sidebar and tab strip carry the Workspace icon and colour; an external link's
    new tab must show it from the first frame.

## Consequences

- The price of the recommendation is the gaps G1 to G8. G1, G6 and G7 are prototyped, and compiled and measured in train 8 (44 added lines in
  four hook patches touching five Chromium files, and a 204-line overlay). G2, G3, G4 and G8 are real work, each larger than the prototype.
- Chromium upgrades touch five files in four patches: three in `chrome/`, one in `content/` and one in
  `components/permissions/`. `content/` is the risky one; Brave carries similar hooks, which helps when comparing rebases.
- The story 92 bar is met by the mechanism (two cookie jars, two caches, two connection pools, two processes). Train 8
  proved it with screenshots: [Workspace 1](../../prototypes/workspaces/screenshots/story92-ws1-whoami.png) shows user A and
  [Workspace 2](../../prototypes/workspaces/screenshots/story92-ws2-whoami.png) shows user B in one window, and after a restart
  [A](../../prototypes/workspaces/screenshots/story92-restart-ws1.png) and
  [B](../../prototypes/workspaces/screenshots/story92-restart-ws2.png) are still apart (see the report).
- "Brave Containers pass the leak checks" is not established. Do not cite this ADR for that.

## Decisions (maintainer, 2026-11-08)

The maintainer decided the four product questions and the two pass bars. They replace the recommendations the ADR first
made. D1 reverses the recommendation: it was "shared".

| Id | Question | Decision | Effect on the design |
| --- | --- | --- | --- |
| D1 | Is history per Workspace? | **Split per Workspace.** | Verdict 12 is "split". One `HistoryService` per Workspace (gap G8, size L, above). Residual risk R-3 is gone. |
| D2 | Do permission grants split? | **Split before Workspaces ship.** | Gap G2 is required work for M5, not an option. A Workspace never starts with a grant made in another. |
| D3 | Which extensions run in a non-default Workspace? | **A per-extension "allow in all Workspaces" setting, off for new installs.** | M-D1 in the threat model is the design: an extension runs only in the default (no Workspace) tabs until the user allows it. Size M. |
| D4 | Where does a link from another app land? | **A new tab in the active Workspace, and the tab shows its Workspace.** | Story 94 as the ADR planned. The tab model needs the Workspace on every tab and a visible label. |
| P1 | Memory bar for an empty extra Workspace | **10 MB.** | Train 8 measured **7.9 MB: pass.** Loaded, an extra Workspace cost +1.1 MB over a tab. Measured without G8's history services, so re-measure with them. |
| P2 | Switch bar | **100 ms median.** | Train 8 measured **1.7 ms median, 4.3 ms p95: pass** (a stand-in: tab activation; the real switch is M5). |

Details, screenshots and caveats: [prototypes/workspaces/REPORT.md](../../prototypes/workspaces/REPORT.md).

### What #73's Bench suite must check

Besides the checks listed in the report, the `workspace_isolation` suite must prove the two decisions that were first
left open:

- **History split (D1, G8):** a page visited in Workspace A is absent from Workspace B's history and omnibox
  suggestions, and `:visited` styling of that link is off in B, in both directions.
- **Permission split (D2, G2):** a grant made in A reads `default` or `prompt` in B, in both directions. This is the
  check train 8 saw fail, as expected.

Both are "split" verdicts in this ADR, so each needs a check; they fail until G8 and G2 land and are marked
expected-to-fail until then.

## Consequences for the rest of the design (after the decisions)

- M5 cannot ship with G2 or G8 open; G3 (clear data) and G4 (session restore) follow the same rule, since both
  would otherwise mix Workspaces.
- Fingerprinting seeds are keyed on the Workspace as well as the site (#121). Helium keeps its noise tokens on the
  `BrowserContext`, which every Workspace shares, so without this two Workspaces would show one canvas, audio and
  core count for a site and link the accounts this ADR keeps apart (Brave keys its farbling token on the container
  for the same reason). The token key carries the `StoragePartitionConfig` (domain and name); the default partition
  adds nothing, so a profile without Workspaces keeps its keys. The partition is only hashed in memory, never stored
  or sent, and the global and per-site switches still apply in every Workspace. Dedicated workers and popups take the
  tokens of their page, shared and service workers those of their own partition. The Workspaces isolation suite has a
  `fingerprint` probe for it.
