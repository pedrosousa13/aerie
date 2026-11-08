# 5. Isolate Workspaces with a storage partition each, in one profile

Status: Proposed (issue #72). The maintainer accepts or changes it at review.

## Context

Workspaces ship in M5 ([#1](https://github.com/pedrosousa13/aerie/issues/1), stories 91 to 95). How they isolate
storage shapes the M2 sidebar and tab model, so the design is decided now and built later. The bar is story 92: two
accounts of one service, logged in at the same time in two Workspaces of **one window**, with nothing crossing.

Two candidates, from the issue:

1. **A profile per Workspace.** One Chromium `Profile` for each Workspace, one window showing tabs of several profiles.
2. **A storage partition per Workspace.** One profile, and a non-default `StoragePartition` for each Workspace, the way
   Chromium isolates `<webview>` and Isolated Web Apps.

Everything below was read in the Chromium 157 tree under Helium 0.21.1.1 (`/mnt/linux2/aerie-build/src/helium-0.21.1.1`)
and in brave-core on GitHub. **Nothing was built or run.** Claims marked "to verify" are for the build train; the plan
is in [prototypes/workspaces/REPORT.md](../../prototypes/workspaces/REPORT.md).

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
(`<profile>/Partitions/<domain>/<hash of name>/`). That one fact gives the network-side state split below.

## Decision

**Recommended: option 2, a storage partition per Workspace in one profile.** Partition domain `aerieworkspace`,
partition name the Workspace id (a random UUID), persistent on disk. Every tab carries its Workspace for its whole
life, because the partition of a `BrowsingInstance` cannot change. Moving a tab (story 95) creates a new
`WebContents` in the target partition and closes the old one.

**Rejected: option 1, a profile per Workspace in one window.** It splits more state (history, permissions, passwords,
extensions) but needs the single-profile assumption removed from `Browser`, `TabStripModel` and thousands of callers,
or a hidden Browser per profile with its tab strip shown inside another's window. Either is a large, permanent fork
of the part of Chromium that moves fastest, in the milestone (M2) that builds the sidebar. Arc proves it can be
done by a company that owned its fork. Aerie's patch layer is a rebase every Chromium release.

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
| --- | --- | --- | --- |
| Arc (closed source) | A profile per Space, spaces can share one | Per profile: logins, history, extensions apart. Reports disagree on extensions ([forum](https://talk.macpowerusers.com/t/arc-browser-spaces-and-profiles/35872)). Arc is no longer supported. | Option 1 works in one window, with a lot of engineering. |
| Zen ([docs](https://docs.zen-browser.app/user-manual/workspaces)) | A workspace can be bound to a Firefox container | History and extensions shared. An external link needs a container tab to move to a workspace; plain links are not placed. | These are the story 94 gap: Aerie must route external links itself. |
| Firefox Multi-Account Containers ([support](https://support.mozilla.org/en-US/kb/containers)) | `userContextId` in every origin attribute | Own cookies, cache, local storage. History was shared at the time of the sources found. | A per-container key on every store is the checklist; here it comes from the partition. |
| Vivaldi, Opera, Edge | Tab groups only | Everything | UI only. |

## Shared-state verdicts for option 2

"Split" means separate state per Workspace, with the reason. "Accepted" is a decision to leave it shared, with a
reason and a residual risk (named R-n in the [threat model](../security/workspaces-threat-model.md)). Items marked
"gap" are shared by default and need work in M5; they are the work list for the build train and for #73.

| # | State | Verdict | How, or why not |
| --- | --- | --- | --- |
| 1 | Cookies | **Split** | One `NetworkContext` and one cookie DB per partition. Gap G1: upstream does not persist session cookies for a non-main partition (`profile_network_context_service.cc:1452`), so a login by session cookie dies at restart. Prototype patch 0004. |
| 2 | localStorage, sessionStorage, IndexedDB, WebSQL, FileSystem/OPFS, Blob URLs, BroadcastChannel, Web Locks, SharedWorker | **Split** | Owned by the `StoragePartition` (quota, DOM storage, IDB, file system contexts). Blob URL fallback to the domain's empty-name partition is off for non-guests. |
| 3 | Cache Storage | **Split** | Per partition. |
| 4 | HTTP cache | **Split** | `http_cache_directory` is under the partition's path, one cache per `NetworkContext`. The network isolation key keeps working inside it. To verify: cache-probe check. |
| 4b | Code cache | **Split** | `<partition path>/Code Cache` (`storage_partition_impl.cc:1587`). |
| 4c | GPU shader cache, font cache | **Accepted** | Process-level caches of compiled shaders and fonts. They hold no page content or account data. Residual timing signal, R-6. |
| 5 | Service workers (and push subscriptions held by them) | **Split** | Registrations live in the partition. Aerie sends nothing to Google, so no push service is reachable (see `docs/network-denylist.md`); to verify on the train. |
| 6 | HSTS | **Split** | `TransportSecurityState` per `NetworkContext`, persisted in the partition's network dir. Expect-CT: **n/a**, Chromium removed it in M107. |
| 7 | DNS | **Split in process, accepted below it** | `ContextHostResolver` has its own host cache per `NetworkContext` (to verify); the `HostResolverManager`, the OS resolver and the DoH setting are shared. DNS answers are public data, not an account, but one Workspace can learn from timing that the OS resolver already knows a name, R-6. |
| 8 | Sockets, HTTP/2 and QUIC sessions, connection pools | **Split** | `HttpNetworkSession` per `URLRequestContext`. No connection is reused across partitions, so a TLS client certificate or an HTTP auth state on a connection cannot carry over. To verify: NetLog socket-reuse check. |
| 9 | HTTP auth cache | **Split** | In the `HttpNetworkSession`. |
| 10 | Client certificate choices | **Split** | The "which certificate did this server get" cache is `SSLClientAuthCache` in the session. The OS certificate store itself is shared (**accepted**: it is an OS object, not Aerie state). `AUTO_SELECT_CERTIFICATE` is set by enterprise policy only. |
| 11 | Permission grants | **Gap G2, must split** | `HostContentSettingsMap` is per profile and keyed by origin, not partition. A camera grant for `https://x.test` given in Workspace A applies to Workspace B. This is the largest hole in option 2. Upstream also blocks undelegated permissions (notifications) in a non-default partition (`permission_util.cc:678`); prototype patch 0003 allows them. Fix: a Workspace-keyed layer in front of the content settings read; size L, not prototyped. **Product decision D2.** |
| 12 | History, visited-link colouring | **Accepted, decision D1** | `HistoryService` and the visited-link store are per profile. A site open in both Workspaces can learn which of its own links the other Workspace visited (visited links are keyed by link, top-level site and frame origin, not by Workspace). Same as Brave, Zen and Firefox containers. Becomes "split" only if D1 says history is per Workspace; that is a large change (R-3). |
| 13 | Autofill, saved passwords, payment methods, passkeys | **Accepted** | Per profile. The password manager offering both accounts' logins is the wanted behaviour. A page cannot read them without a user gesture. Residual: R-4. |
| 14 | Extensions | **Accepted, with a mitigation** | One extension set per profile. Content scripts and `webRequest` see tabs of every Workspace. `chrome.cookies` does not: the API reads the default partition (`extensions/browser/api/cookies/cookies_api.cc:264`), so extensions cannot read Workspace cookies by API, which also breaks cookie-managing extensions in Workspace tabs. An extension is trusted with all browsing; mitigation M-D1 (per-Workspace extension access) is the M5 work item. |
| 15 | Per-site blocking exceptions (#10) | **Accepted** | Stored by the Filter Engine in `state.json`, keyed by site (incognito is keyed by profile, patch 0008 carries the key). They are a policy, not a credential. Cheap upgrade path: carry a Workspace key the way the incognito key travels. |
| 16 | Network and cache partitioning keys (network isolation key, storage key, CHIPS cookie partition key) | **n/a** | They partition by top-level site inside one `NetworkContext` or partition. The Workspace is an outer key, so no change, and no interaction expected. |
| 17 | Renderer process | **Split** | The partition is part of `SiteInfo`, so a Workspace never shares a process with another Workspace. One `RenderProcessHost` has one partition, so a compromised renderer cannot ask for another partition's network or storage. |
| 18 | Preconnect, prefetch, DNS prefetch | **Accepted, gap G5** | `LoadingPredictor` and `PrefetchManager` use the default partition (`loading_predictor.cc:141,581`; `prefetch_manager.cc:404`). Aerie turns preloading off by default (patch `hardening/0008`), so this is closed by default. The Bench must keep it checked. |

Items outside the issue's list, found while reading:

| # | State | Verdict | How, or why not |
| --- | --- | --- | --- |
| 19 | "Clear browsing data" | **Gap G3** | `BrowsingDataRemoverImpl` clears the default partition unless the filter names a partition (`browsing_data_remover_impl.cc:413`); `ChromeBrowsingDataRemoverDelegate` also clears HTTP cache, host cache and origin data on the default partition only (lines 295, 468, 521). The user would think data is gone. Brave patches the same. Size S to M. |
| 20 | Session restore, "Reopen closed tab", tab sync | **Gap G4** | `SerializedNavigationEntry` has no partition; `session_restore.cc` has no partition handling. A restored tab opens in the default partition, mixing Workspaces. The M2 tab model must store the Workspace id itself. |
| 21 | noopener windows, `window.open` from a Workspace tab | **Gap G6** | `WebContentsImpl::CreateNewWindow` sends a non-guest noopener window to `SiteInstance::Create()`, the default partition. Prototype patch 0002. |
| 22 | Tabs without an opener (new tab, external link, command line) | **Gap G7** | `CreateTargetContents` picks the default partition. Prototype patch 0001 adds `NavigateParams::storage_partition_config`; routing external links to the active Workspace is M5 work (story 94). |
| 23 | Downloads, clipboard, bookmarks, settings, search engines, zoom | **Accepted** | User-level data, not account state. Zoom is shared on purpose, as in Brave. |
| 24 | Browser fingerprint (user agent, GPU, fonts, screen) and the per-profile media device id salt | **Accepted** | One device, one browser: this cannot be split. A site seeing both Workspaces can link them by fingerprint. Aerie has no farbling; Brave seeds it by container. The Workspace is a login boundary, not an anonymity boundary. R-5. |
| 25 | PWAs and app windows | **Accepted for M5** | Bound to the default partition. Workspace-aware app windows are out of scope. |
| 26 | Incognito | **n/a** | An off-the-record profile forces `in_memory`. Workspaces are off in incognito windows. |

## What is chosen where the maintainer must decide

These are product trade-offs, not engineering ones. The recommendation is in bold.

- **D1. Is history per Workspace?** (a) Shared, like Brave, Zen and Firefox. **Recommended for M5**: one omnibox, one history page, simplest. Costs R-3. (b) Split. Needs a Workspace key through `HistoryService`, `URLDatabase` and visited links: large; or option 1.
- **D2. Do permission grants split in M5?** (a) **Split before M5 ships (G2)**: it is a safety property, and #73 lists "permission grants not shared" as a check. (b) Ship shared, with prompt text that says "in all Workspaces". Cheaper, weaker.
- **D3. Which extensions run in a non-default Workspace?** (a) **Each extension has an "allowed in Workspaces" setting, off for new installs** (M-D1). (b) All, as now. (c) None, until chosen.
- **D4. A link from another app lands in:** (a) **the active Workspace of the last active window, in a new tab, with the Workspace shown on the tab** (b) a chosen default Workspace (c) asks. See R-7.

If D1 or D2 is answered "split everything per Workspace" and the cost is accepted, option 1 comes back on the table. Even
then, option 2 is the bounded first step: its Workspace id on every tab is also what option 1 would need.

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
7. **The Workspace list lives in one profile pref.** Clear-data, the Bench and the sidebar all enumerate it
   (`GetAllWorkspacePartitionConfigs` in the prototype).
8. **Test hook for the Bench.** The model exposes `OpenInWorkspace(url, id)` and `MoveToWorkspace(tab, id)` to a WebUI
   message handler, as `chrome://filters` exposes `aerieFilters.*` to `filter_lists`. The Bench drives them over CDP.
9. **Workspaces are off in incognito windows** and in app/popup windows that are not tabs of the main window.
10. **Process count grows with Workspaces:** one process per (site, Workspace). The sidebar's tab-hibernation and
    memory budget must count Workspaces. See the report for what is measured.

## Consequences

- The price of the recommendation is the gaps G1 to G7 and D1 to D4. G1, G6 and G7 are prototyped (about 43 added lines in
  four hook patches and a 163-line overlay). G2, G3 and G4 are real work, each larger than the prototype.
- Chromium upgrades touch four hook points in `chrome/` and `content/`, and one in `components/permissions/`.
  `content/` is the risky one; Brave carries similar hooks, which helps when comparing rebases.
- The story 92 bar is met by the mechanism (two cookie jars, two caches, two connection pools, two processes). The
  screenshot proof needs a build: see the report.
- "Brave Containers pass the leak checks" is not established. Do not cite this ADR for that.
