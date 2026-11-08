# Prototype report: Workspace isolation (issue #72)

Planned by an agent that could not build; **measured by build train 8** (2026-11-08) on one Aerie build: train 8
(main ea3c9ef + #23, #38, #68; train tip 425681f) with the option 2 overlay, patches 0001 to 0004 and the test-only
`test-overlay/` and `test-patches/0001` applied. Results and screenshots: `/mnt/linux2/aerie-build/train8/workspaces/`
(`results-story.json`, `memory/results.json`, `switch/results.json`). Driver: `measure/measure.py`.
Option 1 was not built or measured.

## Measured (train 8)

**It compiled.** The four hook patches compiled unchanged. Two fixes, both under `prototypes/`:

- `MoveTabToWorkspace` took a `Browser*` and called `browser->profile()`, which Chromium 157 no longer has. It now
  takes a `BrowserWindowInterface*` and calls `GetProfile()` and `GetTabStripModel()`.
- The overlay had no GN target. `test-patches/0001` adds `workspace_partition.cc/.h` to the sources of
  `//chrome/browser/ui/navigator:impl` (2 lines; `browser_navigator.cc` calls the helper, and the helper needs the tab
  strip, so a separate target would be a cycle). A real option 2 needs these 2 lines too.

Build: incremental on top of train 8, 4m55s. Machine: 12 cores, 30 GB. Headed browser on Xvfb (1600x1000),
software rendering, sandbox on, the Bench's launch flags plus `--aerie-test-workspaces`.

### Story 92: two logins to one site in one window (acceptance criterion 1)

Fixture: `http://localhost:<port>`, one origin. `/login?user=X` sets a session cookie (no expiry) and writes
localStorage, sessionStorage, IndexedDB and Cache Storage; `/whoami` reads them all back.

| Step | Result |
| --- | --- |
| `open(/login?user=A, ws1)`, `open(/login?user=B, ws2)`, then `/whoami` in each, one window | **Pass.** ws1 tab: cookie, localStorage, IndexedDB, Cache Storage all `A`; ws2 tab: all `B`. Screenshots `story92-ws1-whoami.png`, `story92-ws2-whoami.png` (both tabs, titled `whoami: A` and `whoami: B`, are in one tab strip). |
| Restart on the same profile ("continue where you left off"), open `/whoami` in ws1 and ws2 again | **Pass.** `A` and `B` again: the session cookie survived the restart in a Workspace partition (G1, patch 0004). Screenshots `story92-restart-ws1.png`, `story92-restart-ws2.png`. |
| The tabs Chromium restored itself | All in the default partition (`workspace: ""`), and `/whoami` there says `nobody`. This is G4: the test page had to reopen the Workspaces itself. |

sessionStorage reads empty in each `/whoami` tab because it is per tab and the login ran in another tab. That is expected.

### Leak checks

| # | Check | Verdict | Measured |
| --- | --- | --- | --- |
| 1 | Cookie, localStorage, IndexedDB, Cache Storage not visible across | **Pass** | ws1 sees A, ws2 sees B; a third Workspace and the no-Workspace tab see nothing. |
| 2 | Service worker registered in A is not in B | **Pass** | ws1 has 1 registration, ws2 has 0. |
| 3 | HTTP cache | **Pass** | A `max-age` resource fetched twice in ws1 reached the server once; ws2's `only-if-cached` fetch missed; ws2's normal fetch reached the server (2 hits). |
| 4 | HSTS | Not run | Needs HTTPS fixtures. |
| 5 | No connection reuse (NetLog socket ids) | Not run | NetLogs are saved (`netlog-story*.json`) but not analysed. |
| 6 | HTTP basic auth from A is not sent in B | **Pass** | After a login in ws1, a no-challenge probe got the cached credentials preemptively in ws1 (200) and none in ws2 (403). |
| 7 | Permission grant applies only in its Workspace (G2) | **Fail, as expected** | A notification grant for the origin (Settings, `setOriginPermissions`): `Notification.permission` is `granted` in ws1 and ws2. |
| 8 | `window.open(..., 'noopener')` from A stays in A (G6, patch 0002) | **Pass** | The new tab is in ws1 and sees user A. |
| 9 | Tab moved A to B | **Pass** | After `MoveTabToWorkspace` the tab is in ws2, sees B, has no `window.opener` and no sessionStorage. |
| 10 | Clear browsing data empties every Workspace (G3) | **Fail, as expected** | Cookies and cache, all time, from `chrome://settings/clearBrowserData`: the default partition's cookie is gone, ws1 still has `A`. |
| 11 | Session restore keeps each tab's Workspace (G4) | **Fail, as expected** | Every restored tab is in the default partition (see story 92). |
| 12 | Preload, prefetch, preconnect (G5) | Not run | Aerie's preloading is off by default; `hardening_prefetch` covers the default. |
| 13 | History and visited links | Not run | Report only, decision D1. |
| 14 | MV2 extension `webRequest` sees a Workspace tab | Not run | |

Note for #73: calling `clearBrowsingData` from a settings page other than `chrome://settings/clearBrowserData` (we
first tried `chrome://settings/content/notifications`) crashes the browser, with or without Workspaces. A check
must open the clear-data page itself.

### Memory (PSS of every browser process, `/proc/<pid>/smaps_rollup`)

Three runs, fresh profile each, median per point. Each step opens one background tab and waits 4 s. Start: about
423 MB (browser, GPU, network, utility, the `chrome://workspace-test` tab).

| Series | +1 | +2 | +3 | +4 | +5 tabs | Slope |
| --- | --- | --- | --- | --- | --- | --- |
| `about:blank`, each in a new Workspace | 432.2 | 440.1 | 447.5 | 454.9 | 463.2 MB | **7.9 MB per Workspace** |
| `about:blank`, all in one Workspace | 432.2 | 440.0 | 447.4 | 454.7 | 463.0 MB | 7.9 MB per tab |
| Fixture site, each in a new Workspace | 440.7 | 450.5 | 460.8 | 471.0 | 482.2 MB | **12.0 MB per Workspace** |
| Fixture site, all in one Workspace | 441.8 | 450.2 | 459.0 | 467.7 | 477.1 MB | 10.9 MB per tab |

- **Empty Workspace: no measurable cost over a tab.** 7.9 MB per Workspace is the cost of the tab's renderer; a blank
  tab in one Workspace costs the same. That is under the proposed 10 MB bar. Caveat: an `about:blank` tab may not
  start the partition's network context, so this can be a lower bound.
- **Loaded: about 1.1 MB per extra Workspace** (60.2 MB for five Workspaces against 54.9 MB for five tabs in one,
  per-run totals 59.7 to 60.6 and 54.5 to 55.5). The process counts were the same: each background tab got its own
  renderer in both series, so the difference is the partitions' network and storage state.

### Switch time

Two loaded Workspace tabs, activated in turn through `workspaceTest.activate` (the M5 sidebar does not exist yet).
Measured from the browser's wall clock just before `TabStripModel::ActivateTabAt` to the page's first
`requestAnimationFrame` after `visibilitychange` to visible. 20 switches per run, 3 runs.

| | Median | p95 | Range |
| --- | --- | --- | --- |
| Pooled, 60 switches | **1.7 ms** | **4.3 ms** | 1.4 to 4.8 ms |
| Runs | 1.7, 1.6, 1.7 ms | 3.8, 4.3, 4.3 ms | |

Far under the 100 ms / 200 ms bars. Caveat: this ends at the renderer's next animation frame, not at a frame on screen
(Xvfb, software compositing); a switch between Workspaces is a plain tab activation, so the Workspace adds nothing here.

Cold Workspace create (`open` on a new id to a loaded page, polled every 200 ms): about 220 ms for the fixture site,
the same for a second new Workspace, and about 10 ms to an `about:blank` target. The 200 ms poll dominates; treat it
as "under 250 ms".

### Patch size (measured from the files that built)

| Part | Lines | Files |
| --- | --- | --- |
| Hook patches 0001 to 0004 | 44 added, 0 removed | 5 Chromium files |
| GN hook (in `test-patches/0001`, needed for real too) | 2 added | `chrome/browser/ui/navigator/BUILD.gn` |
| Overlay | 204 lines | 3 files |
| Test only: `chrome://workspace-test` page and its registration | 303 lines overlay, 4 lines of patch | 3 overlay files, 2 Chromium files |

Rebase cost was not measured: there was no Helium bump to try.

## What exists now (updated by train 8)

| Item | State |
| --- | --- |
| Option 2 (partition) patches, 4 | Apply with fuzz 0 on train 8, compiled unchanged. |
| Option 2 overlay | 204 lines in three files, compiled after one fix (above). |
| Option 1 (profile) patch and manager | One patch (9/1) and a 57-line overlay. Not compiled. Deliberately incomplete: see README. |
| Patch size, option 2 | 44 added, 0 removed, 5 files, 4 patches (counted from the patch files). |
| Patch size, option 1 | 9 added, 1 removed for the first hook. Full size unknown. Rough bound by files touched: the one hook shown, up to an audit of 1,685 files that call `browser->profile()`; a working window is guessed at tens to low hundreds of files (README). |
| Brave leak run | **Not done.** Not in train 8. |
| Screenshot proof of story 92 | Done in train 8 (above). |
| Memory per Workspace, switch time | Measured in train 8 (above). |

## What the train must build

One Aerie build with the option 2 overlay and patches 0001 to 0004 applied, and a Bench-only way to drive it, since
the sidebar does not exist yet (M2):

1. A switch `--aerie-test-workspaces`, with a small WebUI page (`chrome://workspace-test`, test builds only, same
   pattern as `chrome://filters`) that offers over `chrome.send`:
   - `workspaceTest.open(url, workspaceId)`: opens a tab in that Workspace (calls `SiteInstance::CreateForFixedStoragePartition` through the overlay).
   - `workspaceTest.move(tabId, workspaceId)`: `MoveTabToWorkspace`.
   - `workspaceTest.list()`: the Workspaces and each tab's id.
2. The test page keeps the list of Workspace ids it has opened (a real pref comes with M5; the prototype has no list).
3. Optional, for the Brave comparison: a Brave release build, from Brave's own download, run as a black box.

## Two-logins-in-one-window proof (acceptance criterion 1)

Fixture: the planned #73 site, served on one origin, with a login form (`/login?user=A` sets a cookie and writes
`localStorage.user`), and `/whoami`, which prints the cookie user and the localStorage user.

Steps, scripted with CDP in a headed run (a screenshot needs a window, not `--headless`):

1. `open(site/login?user=A, ws1)`, `open(site/login?user=B, ws2)`: two tabs, one window.
2. Reload `/whoami` in each. Pass: tab 1 says A, tab 2 says B. Screenshot of the window with both tabs visible in
   the strip (side by side with split view, or one shot per tab with the Workspace id shown by the test page's title).
3. Restart the browser on the same profile. Pass: both tabs reopen with their own user. This is the G1 check (session
   cookie persistence) and also shows G4: the test page must restore the Workspace itself.
4. Save the three screenshots under `<build root>/bench/<timestamp>/` and attach them to #72 and the PR.

## What the train measures

All on one machine, one build, three runs, median reported, with the browser otherwise idle.
Memory is the proportional set size (PSS) of every browser process, read from `/proc/<pid>/smaps_rollup`.

| Metric | How | Proposed pass bar |
| --- | --- | --- |
| Memory per Workspace, empty | Start the browser, record total PSS of all processes (browser, network, GPU, renderers). Create Workspaces 1..5 with `about:blank` tabs; record PSS after each. Report the slope. | Under 10 MB per extra empty Workspace. Set by the report author; the maintainer should confirm or change it. |
| Memory per Workspace, loaded | Same, with the fixture site in each Workspace. Compare with 5 tabs of the same site in one Workspace. The difference is the isolation cost: an extra renderer process and an extra network context per Workspace. | Report the number. No bar: it is the input to a decision. |
| Switch time | The test page hides and shows tabs of two Workspaces (the real switch is M5). Measure from the command to the first frame of the shown tab with `Page.screencastFrame` timestamps or a trace; 20 repetitions, report median and p95. | Median under 100 ms, p95 under 200 ms, for tabs already loaded. |
| Cold Workspace create | Time from `open` on a new id to `Page.loadEventFired` on `about:blank`, then on the fixture site. First partition creation makes a directory and a `NetworkContext`. | Report. |
| Patch size | `git diff --numstat` of the applied patches and the overlay, and files touched. Compare both candidates only after the option 1 hook list is complete, which this spike did not do. | Report. |
| Rebase cost | Apply the four patches to the next Helium bump. Count hunks that need changes. | Zero or one over three bumps. |

## Leak checks the train runs on the prototype (and on Brave)

These are #73's checks, listed with the ADR verdict each one proves. A check marked "split" in the ADR must pass in
both directions. For each, the Brave run records pass or fail and what leaks; if Brave passes, the issue says to
credit it as the base of option 2 (already done in the ADR text, conditional on this run).

| # | Check | ADR verdict | Notes |
| --- | --- | --- | --- |
| 1 | Cookie, localStorage, sessionStorage, IndexedDB, Cache Storage not visible across | 1, 2, 3 | `/whoami` plus a probe page. |
| 2 | Service worker registered in A is not controlling B | 5 | `navigator.serviceWorker.getRegistrations()` in both. |
| 3 | HTTP cache probe by timing and by `cache: 'only-if-cached'` | 4 | The standard probe: fetch a unique resource in A, then in B. |
| 4 | HSTS: `https://x` sets the header in A; `http://x` in B must not upgrade | 6 | Needs HTTPS fixtures and `*.bench.test`; see `docs/bench.md` for host rules. |
| 5 | No connection reuse: NetLog shows two `SOCKET` ids for one host:port | 8 | `netlog.json`, filter by `URL_REQUEST` source and the socket id. |
| 6 | HTTP basic auth entered in A is not sent in B | 9 | Fixture with `WWW-Authenticate`. |
| 7 | Permission grant in A does not apply in B | 11 | **Expected to fail until G2 is done.** Mark expected-fail. |
| 8 | `window.open(..., 'noopener')` from A stays in A | G6 | With patch 0002 pass; without, fail (the control). |
| 9 | Tab moved A to B: B's login, no `window.opener`, no sessionStorage | T2 | |
| 10 | Clear browsing data empties every Workspace | G3 | **Expected to fail until G3 is done.** |
| 11 | Session restore keeps each tab's Workspace | G4 | **Expected to fail** with Chromium's session code alone. |
| 12 | Preload, prefetch and preconnect from a Workspace page do not touch the default partition | G5 | With Aerie's defaults (preloading off). |
| 13 | History and visited links | 12 | Report only; shared by decision D1. Becomes a check if D1 says split. |
| 14 | Extension: `webRequest` in the MV2 fixture extension sees a Workspace tab | 14 | Shows R-1 is real, so D3 can be decided on facts. |

## Estimates (reasoned, not measured)

- **Memory:** the dominant cost is processes. The same site in two Workspaces is two renderer processes by design
  (partition is in `SiteInfo`), where one Workspace would share one for same-site tabs. The partition itself is a
  `NetworkContext`, an HTTP cache index and a few service objects. Expect the empty Workspace to be small and the
  loaded Workspace to cost one renderer plus network-context state. Whether the empty figure meets the 10 MB bar is
  for the train to say.
- **Switch time:** with all tabs alive, a switch only toggles which tabs are shown. No navigation. The expected cost is
  UI layout and paint, not the isolation mechanism. The bar is for the M5 sidebar; this spike measures a stand-in.
- **Option 1, memory:** each profile adds a full set of keyed services (history, bookmarks, extensions, prefs, a
  full renderer-side extension set) on top of the partition's costs. Likely larger than option 2. Not measured.

## M2 constraints

Ten rules for the sidebar and tab model are in the ADR, section "Constraints on the M2 sidebar and tab model". The
two that change M2 work first: the tab model stores `workspace_id` itself (never derived from the `WebContents`), and
"move to Workspace" returns a new tab id.

## Open questions for the maintainer

D1 (history), D2 (permission grants), D3 (extensions), D4 (external links): see the ADR.
Also: whether the 10 MB and 100 ms bars are the right ones.
