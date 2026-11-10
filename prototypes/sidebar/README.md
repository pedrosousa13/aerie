# Aerie sidebar and Workspaces mockup

Clickable design mockup for issue #142. Plain HTML, CSS and JS. No build step, no network.

## Open it

Open `index.html` in a browser (file:// works), or run `python3 -m http.server` in this folder. The pages link to each other (the "Pages" row in the dashed mockup-controls panel, or the header nav on the spec pages) and keep `mode`.

## Pages

| Page | What it is |
|---|---|
| `index.html` | Sidebar mockup: Workspaces, folders, themes, archive, command bar, peek |
| `horizontal.html` | Horizontal tabs (spec story 51). Two rows by default, one row as the dense option |
| `workspace-editor.html` | Workspace editor sheet: colour presets, custom colour wheel, theme and mode, delete confirmation |
| `motion.html` | Motion spec: one card per interaction with tokens, curves, reduced motion and a live demo |
| `tokens.html` | Token spec: every token group, the theme catalogue, Workspace presets and the contrast matrix |

## Files

- `tokens.json` here is a symlink to `overlay/components/aerie_design_tokens/tokens.json`, the one source of Design Tokens (ADR 0007). Edit the component copy. `tokens.css` is a checked-in copy generated from it (see "Tools"); `tests/test_design_tokens.py` fails when it is stale. The generator also checks that the icon-only container query in `horizontal.css` matches `size-hz-tab-icon-only`.
- `screens.json` and `screens-*.json` list screenshot states for every page. `query` is the URL query string, `title` and `note` are the gallery card text, `page` defaults to `index.html`, `fullPage` shoots the whole page.
- `shoot.py` and `build_artifact.py`: see "Tools" below. The token generators and `check_contrast.py` live in `overlay/components/aerie_design_tokens/`.
- `specs.js` and `specs.css` serve `motion.html` and `tokens.html`; `horizontal.*` and `workspace-editor.*` belong to their pages.

## Tools

```
C=../../overlay/components/aerie_design_tokens
python3 $C/gen_tokens_css.py --out tokens.css --check-horizontal horizontal.css   # run after every token change
python3 $C/check_contrast.py [-v]         # exits 1 on failure
python3 shoot.py [--out DIR] [--only id1,id2] [--gallery --train NAME]
python3 build_artifact.py OUT_DIR
```

- `check_contrast.py` (CI runs it through `tests/test_design_tokens.py`) checks text, muted text and accent contrast for every theme, mode and Workspace colour, and the status colours (ok, warn, danger) at 4.5:1 on each theme's popover surface and on their own chip tints, plus text on the danger fill.
- `shoot.py` serves this folder, shoots every entry of `screens*.json` at 1440x900 in light and dark (`--out`, default `/tmp/aerie-sidebar-shots`) and fails on any console or page error. `--only` limits it to some ids. `--gallery` also registers each PNG in the Tailscale gallery under "Sidebar design" (issue 142); `--train NAME` files them under that build train. Needs Playwright for Python.
- `build_artifact.py OUT_DIR` writes a multi-file claude.ai Artifact: `index.html` (the sidebar page as a fragment, no doctype/html/head/body) plus standalone `horizontal.html`, `workspace-editor.html`, `motion.html` and `tokens.html`. CSS, JS (and `tokens.json` for the spec pages, as `<script type="application/json" id="tokens-json">`) are inlined. Artifacts pass no query string, so every page maps `#<screen-id>` (any id from its screens file) to that screen's parameters, for example `horizontal.html#horizontal-onerow`.

## URL parameters

| Parameter | Values |
|---|---|
| `theme` | Global default theme: `vivid` (default), `soft`, `clean`, `oled`, `high-contrast`, `mono` |
| `mode` | `light`, `dark` (default: system) |
| `wstheme` | Theme override for the current Workspace: `default` or any theme |
| `wsmode` | Colour mode override for the current Workspace: `default`, `light`, `dark`, `system` |
| `look` | Old name for `theme` (`neutral` maps to `clean`) |
| `ws` | `personal`, `work`, `side` |
| `density` | `comfortable`, `compact`, `collapsed`, `autohide` |
| `side` | `left`, `right` |
| `reduced` | `1` forces reduced motion (system setting also works) |
| `focus` | `1` shows focus-order badges |
| `open` | `cmdbar`, `archive`, `contextmenu`, `guard`, `peek`, `folder-editor` (with `folder=<id>`, default `f-trips`; `f-docs` with `ws=work`), `autofile-toast`, `smart`, `smart-tip`, `tip-renamed`, `tip-tidied`, `today-sleep` |
| `folder` | Folder id for `open=folder-editor` |
| `rules` | `off` switches the rules of every auto/smart folder off (or only `folder=<id>`) |
| `archived` | `1` archives one Today tab on load (what "Archive now" does, without animation) |
| `archiveafter` | `12h` (default), `24h`, `7d`, `never` |
| `tidy` | `0` turns built-in title tidy off |
| `folderrules` | `0` switches all folder rules off globally |
| `wscount` | `1` shows a single Workspace: dots hidden, only "+ New workspace". Also a mockup control |
| `pinnedclose` | `reset-sleep-switch` (default), `sleep`, `close`. Also a mockup control. See Pinned tab close |
| `controls` | `0` hides the mockup controls panel |

`theme=light` and `theme=dark` still work as aliases for `mode`.

### `horizontal.html`

| Parameter | Values |
|---|---|
| `rows` | `2` (default: tabs on row 1, toolbar in the card top) or `1` (dense: everything in one strip) |
| `ws`, `theme`, `mode`, `density`, `reduced`, `controls` | As on the sidebar page (`density`: `comfortable` or `compact`) |
| `wsoverrides` | `0` ignores per-Workspace theme overrides |
| `crowd` | Number of extra Today tabs, to show shrinking and overflow |
| `select` | `last` selects the last Today tab |
| `open` | `cmdbar`, `wsmenu`, `folder` |

### `workspace-editor.html`

| Parameter | Values |
|---|---|
| `ws` | `personal`, `work`, `side` |
| `tab` | `preset` or `custom` (custom colour wheel) |
| `colour` | A preset name (`dusk`, `tide`, ...) |
| `harmony`, `hue`, `sat`, `lightness`, `intensity`, `grain` | Custom colour settings (`hue` 0-360, `sat` 0-1, the others 0-100) |
| `wstheme`, `wsmode` | The Workspace's own theme and mode |
| `theme`, `mode` | Global theme and mode for the sheet and the frame |
| `confirm` | `1` opens the delete confirmation |
| `reduced`, `controls` | As on the sidebar page |

### `motion.html` and `tokens.html`

| Parameter | Values |
|---|---|
| `mode` | `light` or `dark` |
| `reduced` | `1` (motion page) switches on Reduced motion |


## Themes and Workspace colour

Two independent axes, recorded in `tokens.json` (`_meta.axes`):

- **Theme** (a user choice) is how the chrome is rendered: surfaces, tint strength, shadows, materials. Tokens live in the `theme` group. Themes: Vivid (default), Soft, Clean, OLED (dark only; light mode shows Clean), High contrast (also applied automatically under `prefers-contrast: more`; `forced-colors` falls back to system colours), Mono.
- **Workspace colour** (one of 8 presets per Workspace) is the hue every theme uses in its own way.

Each Workspace can override the global theme and mode: `{ colour, theme: default | <theme>, mode: default | light | dark | system }`. This is data, not tokens. It would live on the Workspace record in the Tab Model. Effective theme is the Workspace theme, else the global default. In the sample data Personal uses the default, Work uses Clean, Side project uses OLED in dark mode. Switching between Workspaces with different looks crossfades the whole chrome over `xslow` (140 ms under reduced motion), using two complete layers (View Transitions), so text is never half-faded on a wrong surface. Changing theme or mode from the controls crossfades over `xslow`, and is instant under reduced motion.

Parameters apply on load with no animation.

## Keyboard

| Keys | Action |
|---|---|
| Ctrl+S | Show or hide the sidebar |
| Ctrl+Alt+S | Auto-hide only: keep the peek open (toggle), with a toast |
| Alt+Left, Alt+Right | Page back and forward |
| Ctrl+T, Ctrl+L, Ctrl+K | Command bar (Ctrl+K is a fallback if the browser keeps Ctrl+T) |
| Ctrl+Shift+1..9 | Switch workspace |
| Ctrl+Shift+[ and ] | Previous and next workspace |
| Ctrl+W | Close selected tab (pinned tabs go to sleep) |
| F6 | Move focus between sidebar and page |
| Arrows, Home, End | Move in Pinned, Today, Favorites and the workspace switcher |
| Enter | Activate; toggles a folder |
| F2 | Rename; Delete closes a tab |
| Shift+wheel, horizontal swipe | Switch workspace on the sidebar (live strip, see below) |
| Right-click, Menu key | Tab context menu |
| Esc | Close command bar, menu, popovers |

Mouse: drag rows to reorder or move between Today, Pinned, folders and Favorites. Middle-click closes. Drag the sidebar's inner edge to resize. With the sidebar hidden, move to the outer screen edge to peek.

## Peek (auto-hide) state machine

One explicit machine (the `Peek` module in `sidebar.js`). All timings are motion tokens, each with a reduced-motion value. The same comment block sits above the code. Test hook: `window.__peek` (`state`, `log`, `simulateOutside(x, y)`).

```
hidden --enter hot zone--> arming --peek-delay (150ms), pointer still in zone--> showing --peek-show (250ms, spring)--> shown
arming --leave zone or window--> hidden
hidden --F6 / Ctrl+Alt+S--> showing
showing --settled + Ctrl+Alt+S on--> pinned           pinned <--Ctrl+Alt+S--> shown
shown --pointer away, not busy, beyond grace--> leaving --re-enter--> shown
leaving --peek-leave (150ms) expired, still away, not busy--> hiding --peek-hide (150ms, accelerate)--> hidden
hiding --re-enter--> showing (reverses from the current offset)
leaving at expiry: busy / back inside / within grace --> shown
Ctrl+S (dock) --> reset to hidden/docked
```

- Never hides while busy: popover, menu or submenu open, tab drag, rename, command bar, sidebar resize, address field focused, or opened with F6 and focus still inside. A leave seen while busy is remembered and re-checked the moment the busy thing ends.
- Grace: leaving the window keeps the peek open while within 200px horizontally and 100px vertically of the sidebar. A web page only sees the exit point, so the mockup uses it as the cursor position and ends the grace after `motion-timing-peek-outside-max`. A native build tracks the OS cursor.
- Zen's bugs and how this avoids them. Shaking on edge hover (#12856): the pointer is tested against a layout footprint, never the moving element, the 6px hot zone only starts arming, and the delay and keep-open timers swallow boundary jitter. Stuck open (#8643): one owner for every timer, cleared on every transition; every busy state ends with a re-check; window exit has an expiry. Early retract (#15995): a leave during the slide-in is deferred until it settles, the 150ms keep-open covers the gap between the hot zone, the 8px inset and the sidebar, busy states and the grace distance protect popovers and close buttons, and a reversing hide starts from the current offset.
- Ctrl+Alt+S toggles "keep peek open" (pinned) and shows a toast. It only applies when the sidebar is auto-hidden.

## Folders

- Collapse: opacity and height over `motion-duration-folder-collapse` (180ms) with `motion-easing-in-out` (cubic-bezier(.4,0,.2,1)). Reveal: `motion-duration-folder-reveal` (200ms) on the height, and the first `motion-timing-stagger-cap` (6) children fade in `motion-timing-stagger` (15ms) apart. Collapse does not stagger. Reduced motion: opacity only, no stagger, height snaps.
- Drag over a folder row: the top 20% and bottom 20% (`motion-ratio-folder-edge`, from Zen) mean before / after (an expanded folder's bottom edge means first child). The middle 60% means into the folder.
- Folders nest up to 5 levels. A drop that would go deeper is refused: red outline on the row, `not-allowed` cursor, a hint "Can't nest deeper than 5 levels", and nothing moves (no shaking). Tabs can still go into a level-5 folder.

## Drag a tab onto a Workspace icon

The icon highlights with a ring that grows from `motion-scale-ws-ring-start` and `motion-opacity-ws-ring-start` on `motion-easing-linear` for the whole dwell (static at `motion-opacity-ws-ring-static` under reduced motion). After 500ms (`motion-timing-ws-drag-hover`) the Workspace switches with the normal switch motion and the drag carries on in the new Workspace (the dragged row becomes a floating ghost), so you can drop it anywhere there. Release over empty space lands it at the end of Today. Dropping on an icon moves the tab to that Workspace's Today without switching. The ghost says "Moves to Work — uses Work's logins", because Workspaces have separate cookies.

## Workspace switch

Full-pane slide (maintainer decision): outgoing and incoming panes sit side by side as a strip and both move one pane width, `motion-duration-workspace-switch` (250ms) with `motion-easing-spring-settle` (critically damped, no bounce). Direction follows Workspace order, no wrap-around. Only the two panes involved animate. The frame and theme crossfade runs in parallel. Reduced motion: 140ms crossfade, no movement. The motion lives in one function, `animateWorkspaceSwitch()`.

The horizontal swipe drags the strip live, 1:1, with the neighbouring pane following. Past the first or last Workspace only the current pane moves, with rubber-banding `x * (1 - |x| / (4.5 * width))` (`motion-ratio-rubber-band`). On release it commits past 80px (`motion-distance-swipe-commit`) or above 0.5 px/ms (`motion-ratio-swipe-velocity`); otherwise both panes spring back.

Mouse back/forward buttons (3 and 4) and Alt+Left/Right are page back and forward. They never switch Workspace (Zen #12491). Only a horizontal swipe, Shift+wheel, the icons, keys and the command bar switch.

With one Workspace the dots are hidden and only "+ New workspace" shows (`wscount=1`, screen `single-workspace`). "Move to workspace" is also hidden.

## Pinned tab close

Ctrl+W or the x on a pinned tab (a future setting, mockup control `pinnedclose`):

| Value | First press | Second press (already asleep) |
|---|---|---|
| `reset-sleep-switch` (default) | Reset to the pinned URL, unload (asleep), select the next tab | If asleep and at the pinned URL: unpin and close (to the archive) |
| `sleep` | Unload and select the next tab, keep the URL | Unpin and close |
| `close` | Unpin and close | |

The tooltip on the x shows the current action: "Reset and sleep", "Sleep" or "Close".

## Folder kinds (#139, #140, #141)

Folders exist in Pinned and in Today. Auto-archive still applies to the unpinned tabs inside a Today folder. A folder has one kind, set in the folder editor:

| Kind | Look | What it does |
|---|---|---|
| Manual | Folder glyph | Holds what you drag in. |
| Auto-file (#139) | Folder glyph plus a small funnel (rule glyph) at the row end. Tooltip lists the rules. Click it to edit. Struck through when rules are off. | A new tab that matches a rule lands at the top of Today as usual, then flies into the folder (FLIP, spring, `motion-timing-autofile-delay` later), the row pulses, and a toast says "Filed into Trips: Undo, Don't file this site" (`motion-timing-toast-action`). Dragging a filed tab out offers "Stop this rule matching this tab" (also in its context menu). |
| Smart (#140) | Dashed folder with a sparkle, italic count. | A live view of every open Pinned and Today tab that matches, without moving them. A row shows a quiet "Today" or "Pinned" hint on hover. Its close button says it closes the real tab. Rows cannot be dragged and nothing can be dropped into it. |

**Rule grammar** (shared by auto and smart, and by #172 routing): Domain is (the host or any subdomain), URL starts with, URL contains, Title contains (page title, case-insensitive). Each rule has an on switch; a folder has an "All" switch; "Folder rules everywhere" in the sheet footer is the global switch.

**Decisions to confirm**

- Smart folders are **per Workspace**. Workspaces have separate cookies and identities, so a view across them would show Work titles in Personal, and closing from the view would have no single owner.
- Rules apply to **new tabs only**. Navigating inside an existing tab never re-files it, so rows do not jump while you read. Matching is top to bottom, Pinned before Today, first folder wins.
- Smart folders have **no behaviour section**. They own no tabs, so each tab keeps the behaviour of where it lives.
- Switching a folder with tabs to Smart moves its tabs out (next to the folder) and says so in a toast.
- Folders in Pinned cannot auto-archive (pinned tabs never do), so that row is disabled there.

**Folder editor sheet** (#141): opens from the folder context menu "Edit folder...", a double-click on the folder icon, or the rule glyph. A popover sheet anchored to the sidebar edge with an arrow at the folder row (`size-sheet-width`, `motion-duration-sheet-enter` / `sheet-exit`). Name and icon, kind (Manual, Auto-file, Smart), rules as chips, exceptions, behaviours (Auto-archive: never or own timeout; Sleep tabs after N; Links open in a new tab or in place). Each behaviour has its own switch; when it is off the row says "Inherits global" and what the global value is. Edits are live. Esc or Done closes.

## Tab names (#145, #146, #147)

- Rename: double-click the title, F2, or the context menu. Enter keeps, Esc cancels, an empty name resets, typing the page title back resets. A caption under the field says so.
- A renamed tab shows a small pencil after the title. Clicking it, or "Reset name" in the context menu, resets. Its tooltip shows the real page title and the precedence list.
- Precedence, shown in the tooltip of renamed and tidied tabs: your name, rename rule, built-in tidy, on-device AI title, page title. The first that applies wins. This mockup has no rename rules and no AI titles, so those lines read "no rule matches" and "off". Built-in tidy is on (`tidy=0` or the control turns it off): "(3) Inbox - Fjord Mail" shows as "Inbox", tooltip "Tidied, original: ...".
- Only the sidebar changes. The page and the command bar's real titles do not.

## Archive and sleeping tabs

- The archive panel has a header with the auto-archive setting ("Archive Today tabs after 12 hours", a menu of 12 hours, 24 hours, 7 days, never), a search field, and rows grouped by Today, Yesterday and Last week. Each row shows its Workspace. Clicking a row restores it: a copy of the row flies into the top of Today (`motion-duration-archive-restore`, spring) and the tab goes back to the Workspace it came from, switching there if needed.
- "Archive now (debug)" in the mockup controls plays the archive moment: a copy of a Today tab shrinks toward the Archive button (`motion-duration-archive-collapse`, `motion-scale-archive-collapse`), then the button pulses once (`motion-duration-archive-pulse`). Reduced motion: the copy only fades, the button flashes colour without scaling.
- Hovering the Today header shows "3 asleep · ~410 MB freed". The numbers are made up (a fixed size per tab); a real build would read the memory report and label it an estimate (the tooltip says so).

## Motion tokens added in this pass

Added: `motion-distance-sidebar-hide-offset` 24px (was a literal in `sidebar.js` and `sidebar.css`), `motion-easing-linear`, `motion-opacity-ws-ring-start` 0.15, `motion-opacity-ws-ring-static` 0.6, `motion-scale-ws-ring-start` 1.25 (reduced 1), `motion-duration-tab-open` 200 (140), `motion-duration-tab-close` 140 (90).
Removed (old crossfade-and-nudge Workspace switch): `motion-distance-workspace-slide`, `motion-timing-overlap`.
Earlier in the pass: `motion-duration`: `sheet-enter` 180 (90), `sheet-exit` 120 (90), `switch` 120 (60), `autofile-pulse` 700 (300), `archive-collapse` 320 (120), `archive-pulse` 360 (140), `archive-restore` 340 (120). `motion-scale`: `archive-collapse` 0.25 (1), `archive-pulse` 1.25 (1). `motion-timing`: `autofile-delay` 280 (120), `toast-action` 6000 (6000). Reduced values are in brackets. `size-sheet-width` 372.

`motion.html` has a card for every motion token and lists none as unused. Small fades the mockup now plays: the favicon fade when a tab falls asleep (also on a pinned close), then a static desaturate; and a tooltip fade-in (`xfast`, `decelerate`). `heightFx()` snaps under reduced motion, like folders.

## Horizontal tabs

Two rows is the default (`rows=2`): row 1 is the Workspace chip, pinned tabs and Today tabs, row 2 is the toolbar (navigation, address pill with Guard, Favorites tiles) drawn at the top of the page card so the selected tab joins it. `rows=1` is the dense option: everything in one strip. All sizes are `size-hz-*` tokens (strip, toolbar, tab, tile, flare, address pill, scroll fade, icon-only threshold). The `--h-*` custom properties in `horizontal.css` only pick which token applies for the density and row count. The Workspace editor uses `size-editor-preview-*`, `tint-grain-max`, and the status colours (`color-status-ok`, `-warn`, `-danger`, `color-danger-fill`, `color-text-on-danger`).
