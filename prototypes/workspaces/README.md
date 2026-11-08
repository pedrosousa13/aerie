# Workspace isolation prototypes (issue #72)

Throwaway. **Not in `patches/series`, never built into Aerie.** The patch lint only looks under `patches/`, so these files
do not affect the build. No Workspace feature code is on `main`. The decision is in
[ADR 0006](../../docs/adr/0006-workspace-isolation.md) (accepted, 2026-11-08); the measured numbers are in [REPORT.md](REPORT.md).

**Option 2 compiled and was measured in build train 8** (see [REPORT.md](REPORT.md)). Option 1 has not been compiled.
Every patch in `partition/patches` and `profile/patches` passes `patch -p1 --dry-run --fuzz=0` against
`/mnt/linux2/aerie-build/src/helium-0.21.1.1` (Chromium 157, after Helium's steps).

## Layout

| Path | What |
| --- | --- |
| `partition/patches/0001-0004` | Option 2, a storage partition per Workspace: hook patches with the Aerie header. |
| `partition/overlay/chrome/browser/aerie_workspaces/` | Option 2 helper: ids, config, inheritance, `MoveTabToWorkspace`. Overlay layout, as `overlay/` in the repo root. |
| `partition/test-overlay/`, `partition/test-patches/0001` | Test builds only: `chrome://workspace-test` (with `--aerie-test-workspaces`), and the 2-line GN hook that builds the option 2 helper. |
| `measure/measure.py` | The train's driver: story 92, the leak checks, memory, switch time. |
| `profile/patches/0001` | Option 1, a profile per Workspace: the first hook, and no more (see below). |
| `profile/overlay/.../workspace_profile_manager.*` | Option 1 helper: creates a hidden profile for a Workspace. |

To try option 2 on a build tree (train only), as train 8 did: in a scratch worktree of the train, copy
`partition/overlay/` and `partition/test-overlay/` into `overlay/`, copy `partition/patches/0001-0004` and
`partition/test-patches/0001` (as `0005`) into `patches/workspaces/`, list them at the end of `patches/series`, run
`./aerie build`. Then, with an Xvfb server on `:99`:
`PYTHONPATH=tools python3 <this dir>/measure/measure.py --binary <out/Default/aerie> --out DIR --display :99`.
Never apply both candidates to one tree: both edit `browser_navigator.cc`.

## Option 2: what the four patches do

| Patch | Size (added/removed lines) | Fixes |
| --- | --- | --- |
| 0001 open new tabs in the Workspace partition | 18 / 0 | `NavigateParams::storage_partition_config`; `CreateTargetContents` pins a tab with no opener to the Workspace partition with `SiteInstance::CreateForFixedStoragePartition`. G7. |
| 0002 keep noopener windows in the opener partition | 6 / 0 | `WebContentsImpl::CreateNewWindow` sent a noopener window to the default partition. G6. |
| 0003 allow permission requests in a Workspace partition | 11 / 0 | Upstream blocks undelegated permissions in a non-default partition. Does **not** split grants (G2). |
| 0004 persist session cookies in a Workspace | 9 / 0 | Upstream drops session cookies for non-main partitions. G1. |

Total 44 added and no removed lines in 5 Chromium files (one in `content/`, one in `components/`, three in
`chrome/`), plus a 204-line overlay in three files (two in `chrome/browser/aerie_workspaces/`, and
`content/public/browser/aerie_workspace_partition.h`, which holds the partition domain every user of it shares). Compare Brave: it also carries a content-layer hook, a
permission change, a clear-data delegate, session persistence and sync encoding.

**Not prototyped, in order of size:**

| Gap | What it needs | Size class |
| --- | --- | --- |
| G2 permission grants split by Workspace | A Workspace-keyed layer in front of the content-settings read; every reader of `HostContentSettingsMap` that is not partition-aware must be audited. | L |
| G4 session, reopen-closed-tab, sync | Persist `workspace_id` in the M2 tab model and in Chromium's session entries (Brave uses a URL prefix and edits `PageState`). | L |
| G3 clear browsing data | Run the remover once per Workspace partition and fix the delegate's default-partition calls (lines 295, 468, 521, 705, 986, 1395). | S to M |
| Workspace-aware extension access (M-D1, decision D3) | A per-extension "allow in all Workspaces" setting, off for new installs, and a check where Chromium decides page access for a tab. | M |
| G8 history split per Workspace (decision D1) | A `HistoryService` per Workspace under its partition directory, about 25 to 40 call sites made Workspace-aware, 4 to 6 keyed services duplicated, and the visited-link table sent only to that Workspace's renderers. Estimated from the source in the ADR. | L |
| UI: sidebar labels, hover card, Workspace picker | M2 and M5. | M2/M5 |

Size classes are guesses from reading; they are not measured. S under a day, M a few days, L more than a week.

## Option 1: why the prototype stops at one patch

One patch (9 added, 1 removed) lets a link opened from a tab create its tab in that tab's profile. It is the first
of a long list, not a working prototype. Without it nothing else in the strip works, and with it the strip is still
wrong: `TabStripModel::profile()` and `Browser::profile()` say profile A for tabs of profile B. Counts in the
Chromium 157 tree: `browser->profile()` or `browser->GetProfile()` appears about 4,000 times in `chrome/browser/ui/**/*.cc`
and about 9,000 times in `chrome/browser`. The omnibox, the navigator, the command controller, bookmarks, extensions
menus, the downloads shelf and settings all take the profile from the Browser. The only alternative to patching them is
one hidden Browser per profile with its tab strip shown inside the visible window, which is a window-system change
rather than a patch. Rough bound for option 1, by files touched: at least the one hook shown, and at most an audit of the 1,685 files in
`chrome/browser` that call `browser->profile()` or `GetProfile()` on a Browser. A working window would need only the
callers that act for the visible tab, which we guess is tens to low hundreds of files. That guess is not counted
and the ADR does not depend on it.

`workspace_profile_manager` shows how a Workspace profile would be created (hidden, under the user data dir). Its
TODO says what a real one needs (a stored id-to-directory map; `CreateMultiProfileAsync` picks its own directory).
