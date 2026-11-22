# Import an Arc sidebar

Issue #171. Settings has an action, "Import from Arc", next to "Import bookmarks and settings". The user picks Arc's `StorableSidebar.json` and one Space from it. Aerie adds that Space's pinned tabs and folders to Pinned, its Favorites to Favorites, and Arc's custom tab titles as custom names (#145), all in the current Workspace. Every Space as its own Workspace is #252.

The file is untrusted input. Arc stops working on 2026-12-22 (byearc.com), but the file stays on disk, and anyone can hand Aerie a hostile one.

## Where the file is

- macOS: `~/Library/Application Support/Arc/StorableSidebar.json`.
- Windows: under the Arc package's `LocalCache\Local\Arc\`.
- Linux: Arc does not run there. The user copies the file over.

Aerie does not look for the file. It opens the system file chooser on `.json`. (The macOS default path comes with #132.)

## The format

Arc publishes no specification. This is what two independent published readers agree on. Aerie has no real Arc file; the fixtures are written by hand from these readers.

Sources (read on 2026-11-19):

- Zen's Arc importer: <https://github.com/zen-browser/desktop/blob/300f9292ddbaaebf24626b4a7451e5426a11d3d8/src/zen/migration/ZenMigrationUtils.sys.mjs> (`parseArcSidebar`), and its fixture builder <https://github.com/zen-browser/desktop/blob/300f9292ddbaaebf24626b4a7451e5426a11d3d8/src/zen/tests/migration/head.js> (`buildArcSidebar`).
- Crest's Arc reader: <https://github.com/pauljoda/Crest/blob/35a9bfb397cd2178a6dc60cfebbfe63aa5e4b4f8/CrestCore/src/CrestCore/Application/Portability/ArcSidebar.cs> and its real-looking fixture <https://github.com/pauljoda/Crest/blob/35a9bfb397cd2178a6dc60cfebbfe63aa5e4b4f8/CrestCore/tests/CrestCore.Tests/Portability/Fixtures/arc-rich/StorableSidebar.json>.

```
{ "version": 1,
  "sidebar": { "containers": [ ... ] } }
```

`containers` is a Swift array: a marker, then the container it describes. `{"global": {}}` marks the main sidebar. `{"littleBrowser": {"_0": "<id>"}}` marks the sidebar of a Little Arc window, and the container after it is that sidebar. (Crest also accepts containers with no marker; so does Aerie.) A container is an object:

- `items`: a flat list. Zen's fixtures alternate an id and the item (`[id, {...}, id, {...}]`). Crest also accepts an object keyed by id, and an item that has its own `id`.
- `spaces`: the same shape, for Spaces.
- `topAppsContainerIDs`: pairs of an Arc profile marker and the id of the Favorites container of that profile: `[{"default": {}}, "<id>", {"custom": {"_0": {"directoryBasename": "Profile 1"}}}, "<id>"]`. (Zen writes `{"default": {}}`, Crest `{"default": true}`; Aerie only looks at the key.)

An item:

- `id`, `parentID`, `childrenIds` (ordered ids of the children).
- `title`: the name the user gave the item. `null` when they gave none (Crest's reading; Zen uses it only as a fallback).
- `data` holds exactly one kind: `tab` (`savedURL`, `savedTitle`: the page's own title), `list` (a folder, named by `title`), `splitView` (its children are the tabs of the split), `itemContainer` (a root, such as a Space's pinned container).

A Space: `id`, `title` (can be absent), `profile` (the same marker as above), `customInfo.iconType` (`emoji_v2`: the emoji as text; older files: `emoji`: a code point; or `icon`: the name of one of Arc's own icons), and `containerIDs`: `["unpinned", "<id>", "pinned", "<id>"]`. A marker string, then the id of the container for that section. Crest falls back to `newContainerIDs` when `containerIDs` is empty, and so does Aerie.

Where `childrenIds` is empty or absent, Zen finds the children by `parentID`. So does Aerie.

Not verified against a real file: what Arc writes for an Easel, a Note, or any other item kind. Neither reader says. Aerie treats every `data` kind it does not know as an Arc-only item and counts it.

## What Aerie takes

| From the file | Into Aerie |
| --- | --- |
| A Space's `pinned` container, its tabs and `list` folders, in order | Pinned: tabs and folders, same nesting and order |
| The `topAppsContainerIDs` container of the Space's profile | Favorites (Arc's "top apps"), up to the cap of 12 minus what the Workspace has |
| A tab's `savedURL` | The tab's pinned URL. Only `http` and `https`. The address is stored in its canonical form, without a user name or password |
| A tab's `savedTitle` | The title the row shows until the page loads (`last_title`) |
| A tab's `title`, when it is not blank and not the same as `savedTitle` | The custom name (`manual_name`) |
| A folder's `title` | The folder name |
| A Space's `title` and emoji | Only the picker shows them |
| A `splitView` | Its tabs, in place. Aerie has no split view |

What Aerie does not take, and says so in the result: the Space's unpinned ("Today") tabs, Space colours and themes, Boosts, Easels, Little Arc windows, Peek, and logins, cookies and history (logins are not imported). Little Arc sidebars are skipped as in Zen. Other Arc profiles' Favorites are not imported: a Space gets the Favorites of its own profile (as in Crest).

Why only Pinned and Favorites: those are what the user arranged by hand. Today tabs are open pages, and the import makes no request.

## Fixtures

Hand-written, in `overlay/components/aerie_tab_model/testdata/arc/`. The unit tests, the browser tests and the interactive tests read them.

| File | What it holds |
| --- | --- |
| `one_space` | One Space with an emoji, a folder, three pinned tabs, two Favorites and a Today tab (not imported) |
| `three_spaces` | Three Spaces over two Arc profiles (each with its own Favorites), one without a name, and a Little Arc sidebar |
| `nested_folders` | Folders seven levels deep: levels 6 and 7 are merged into level 5 |
| `favorites` | Fourteen Favorites: twelve fit |
| `custom_titles` | A tab with a custom name, one without, one whose name equals its page title, one with a blank name |
| `bad_schemes` | `javascript:`, `file:`, `data:`, `chrome:`, `aerie:`, `arc:`, a padded upper-case `JavaScript:`, an upper-case `HTTPS:` and an address with a user name and password |
| `markup_titles` | A Space, a folder and a tab whose names are markup, and a title with control characters |
| `bad_json` | Not JSON |
| `wrong_types` | Lists where text belongs, text where lists belong, numbers as ids |
| `missing_keys` | Items and no Space |
| `not_an_object`, `no_containers` | JSON that is not an Arc sidebar |

The files that are too big to keep in a repository are made by the tests: more than 10 000 items, a tree 40 levels deep, 101 Spaces, a loop in `childrenIds`, an Easel, JSON nested 100 000 levels, and a sparse file one byte over 32 MiB.

## Limits (the file is untrusted)

| Limit | Value | When it is passed |
| --- | --- | --- |
| File size | 32 MiB | Refused before reading |
| Spaces | 100 | The rest are not read; the result says "too many" |
| Items (entries of every `items` list) | 10 000 | The rest are not read; counted as "too many" |
| Depth of the item tree | 32 | Items below it are skipped, counted as "too deep" |
| Folder levels | 5 (the Tab Model's own limit) | A folder below level 5 is merged into its level-5 parent; its tabs stay |
| Title length | 1024 UTF-16 units | Cut. A custom name or folder name is cut again at the Tab Model's 200 |
| Favorites | 12 per Workspace | The rest are skipped |

What one import may add (issue #275). These are limits of what Aerie writes, not of the file, and they apply to the plan after parsing:

| Limit | Value | When it is passed |
| --- | --- | --- |
| Tabs per import | 250 (Pinned and Favorites together) | The import brings in a whole prefix of the plan, in plan order: the Pinned tree first, then the Favorites. Every tab after the cut is left out and counted as "over the limit" |
| Pinned tabs per Workspace | 1000, counting the Pinned tabs the Workspace has already | Pinned only: the Pinned tabs after the cut are counted as "over the limit", and the Favorites still come in (within the other limits). Only the importer enforces this limit: the Tab Model itself has no cap on Pinned tabs |
| Text of one import | 1 MiB of addresses, titles and custom names together | Same: a prefix, the rest counted as "over the limit" |

The text limit keeps the saved Tab Model far below the 16 MiB that the reader accepts (`kMaxStateBytes`), with room for the rest of the model. The writer holds to the same limit: if the state would be larger than the reader accepts, it drops the oldest archived tabs from what it writes until the state fits (and logs how many). If it still does not fit with no archive left, it does not write, and the last good `state.json` stays. Folders after the cut are not made.

The Chromium tabs of the imported records are made in batches of 5, each batch in its own task on the UI thread (issue #276, `PinnedTabsController::RestoreMissingTabs`), so the browser stays responsive. The result appears when the records are in the Tab Model; the tabs follow within moments, unloaded, as before. A browser test holds the longest task of a 250-tab restore under 100 ms.

## Importing the same Space again (issue #276)

A second import of the same Space adds nothing. A tab whose address is already in the same place is skipped and counted as "already there": the same Workspace and Section (Pinned or Favorites) and the same parent folder. A folder with the same name in the same place as an earlier one is reused, so its tabs go into that folder and no second folder is made. The same address in another folder, or in Pinned and in Favorites, is not a duplicate. Only what was there before the import counts: a Space that lists one address twice in one folder still brings in both. A tab that is already there uses up no room under the Favorites cap or under the limits above, and a changed title or custom name in the file does not change the tab that exists.

A title loses control characters and bidirectional override characters. Markup in a title stays as it is: it is text, and every surface shows it as text.

Every item is visited once, so a loop in `childrenIds` ends.

A file that is not JSON, that has no `sidebar.containers`, or that has no Space fails. It changes nothing. A single item of the wrong type is skipped (counted as an item that Aerie could not read); the rest imports.

## Result

A summary: how many tabs, folders and Favorites came in and how many custom names, then what was skipped with its reason: an address that is not `http` or `https` (counted as "unsupported address"), no usable address, too many items, nesting too deep, Favorites over the cap, tabs already there, tabs over the limits of one import (250 tabs, 1000 Pinned tabs per Workspace, 1 MiB of text; see Limits), and Arc-only items. One fixed line names Boosts, Easels, Little Arc and Peek as not imported, and one says that logins are not imported.

## Security notes

- The file is read only after its size is known to be at most 32 MiB, and the read itself is capped again.
- Parsing is done on a thread-pool sequence, never on the UI thread, with a depth limit of 64. **Chromium 157 has no JSON parser in `data_decoder`** (`services/data_decoder` has an XML, a structured-headers, an image and a gzip service, and `ParseJsonIsolated` is gone; JSON parsing is the memory-safe Rust `base::JSONReader`). The issue asks for an out-of-process parse; there is none to call. The call is in one function, `ParseUntrustedJson` (`arc_import_json.cc`), so a later isolated parser replaces one line. See the open question in the pull request.
- The result of the parse is a plan of plain data. Nothing from the file is evaluated, and no path in the file is opened.
- No network request: the import adds records to the Tab Model, and `PinnedTabsController` makes each tab with `kDeferUntilActivation` (a discarded tab). The Bench check `zero_traffic` covers this.
- Rolled back on failure: the plan is built and checked before the Tab Model is touched. If a write to the Tab Model fails, what the apply added is removed again, in memory. The Tab Model has no batch or transaction API, and the import adds none. On disk the import is one write: each `AddTab` and `AddFolder` only schedules a save (`TabModelPersistence::OnModelChanged`, `tab_model_persistence.cc:120-124`), and the writer joins a burst into one atomic write (`tab_model_persistence.h:60-61`). So a crash during the apply leaves the file as it was before. This holds as long as the apply makes no pause between its writes, which it does not (one synchronous call).
- The page asks for a file with a native file chooser. The page cannot name a path: the handler has no message that takes one.
