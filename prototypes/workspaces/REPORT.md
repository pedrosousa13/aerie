# Prototype report: Workspace isolation (issue #72)

Written by an agent that could not build. **There are no measured numbers in this file.** Everything measured comes
from the next build train, with the plan below. Where a target appears it is a proposed pass bar, not a result.

## What exists now

| Item | State |
| --- | --- |
| Option 2 (partition) patches, 4 | Written. Apply cleanly with `--dry-run --fuzz=0` to the Helium 0.21.1.1 tree. Not compiled. |
| Option 2 overlay | Written, 203 lines in three files. Not compiled. |
| Option 1 (profile) patch and manager | One patch (9/1) and a 57-line overlay. Not compiled. Deliberately incomplete: see README. |
| Patch size, option 2 | 44 added, 0 removed, 5 files, 4 patches (counted from the patch files). |
| Patch size, option 1 | 9 added, 1 removed for the first hook. Full size unknown. Rough bound by files touched: the one hook shown, up to an audit of 1,685 files that call `browser->profile()`; a working window is guessed at tens to low hundreds of files (README). |
| Brave leak run | **Not done.** No Brave build, no permission to build. On the train plan. |
| Screenshot proof of story 92 | **Not done.** Needs a build. On the train plan. |
| Memory per Workspace, switch time | **Not measured.** On the train plan. |

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
