# Workspace isolation prototypes (issue #72)

Throwaway. **Not in `patches/series`, never built into Aerie.** The patch lint only looks under `patches/`, so these files
do not affect the build. No Workspace feature code is on `main`. The decision is in
[ADR 0005](../../docs/adr/0005-workspace-isolation.md) (Proposed); the numbers plan is in [REPORT.md](REPORT.md).

**Nothing here has been compiled.** The Chromium tree was read-only for this spike and another agent owned the build.
What was checked: every patch in `partition/patches` and `profile/patches` passes `patch -p1 --dry-run --fuzz=0`
against `/mnt/linux2/aerie-build/src/helium-0.21.1.1` (Chromium 157, after Helium's steps). The overlay files were
written against headers read in that tree, and may need small fixes to compile.

## Layout

| Path | What |
| --- | --- |
| `partition/patches/0001-0004` | Option 2, a storage partition per Workspace: hook patches with the Aerie header. |
| `partition/overlay/chrome/browser/aerie_workspaces/` | Option 2 helper: ids, config, inheritance, `MoveTabToWorkspace`. Overlay layout, as `overlay/` in the repo root. |
| `profile/patches/0001` | Option 1, a profile per Workspace: the first hook, and no more (see below). |
| `profile/overlay/.../workspace_profile_manager.*` | Option 1 helper: creates a hidden profile for a Workspace. |

To try one on a build tree (train only): copy the overlay into the source tree, apply the patches in order, build.
Never apply both candidates to one tree: both edit `browser_navigator.cc`.

## Option 2: what the four patches do

| Patch | Size (added/removed lines) | Fixes |
| --- | --- | --- |
| 0001 open new tabs in the Workspace partition | 18 / 0 | `NavigateParams::storage_partition_config`; `CreateTargetContents` pins a tab with no opener to the Workspace partition with `SiteInstance::CreateForFixedStoragePartition`. G7. |
| 0002 keep noopener windows in the opener partition | 6 / 0 | `WebContentsImpl::CreateNewWindow` sent a noopener window to the default partition. G6. |
| 0003 allow permission requests in a Workspace partition | 11 / 0 | Upstream blocks undelegated permissions in a non-default partition. Does **not** split grants (G2). |
| 0004 persist session cookies in a Workspace | 9 / 0 | Upstream drops session cookies for non-main partitions. G1. |

Total 44 added and no removed lines in 5 Chromium files (one in `content/`, one in `components/`, three in
`chrome/`), plus a 203-line overlay in three files (two in `chrome/browser/aerie_workspaces/`, and
`content/public/browser/aerie_workspace_partition.h`, which holds the partition domain every user of it shares). Compare Brave: it also carries a content-layer hook, a
permission change, a clear-data delegate, session persistence and sync encoding.

**Not prototyped, in order of size:**

| Gap | What it needs | Size class |
| --- | --- | --- |
| G2 permission grants split by Workspace | A Workspace-keyed layer in front of the content-settings read; every reader of `HostContentSettingsMap` that is not partition-aware must be audited. | L |
| G4 session, reopen-closed-tab, sync | Persist `workspace_id` in the M2 tab model and in Chromium's session entries (Brave uses a URL prefix and edits `PageState`). | L |
| G3 clear browsing data | Run the remover once per Workspace partition and fix the delegate's default-partition calls (lines 295, 468, 521, 705, 986, 1395). | S to M |
| Workspace-aware extension access (M-D1) | A check where Chromium decides page access for a tab. | M |
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
