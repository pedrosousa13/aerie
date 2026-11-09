# Sidebar and Workspaces design

This is the approved design for Aerie's sidebar, Workspaces, themes, horizontal tabs and motion (issue #142). The maintainer approved the mockups and token values on 2026-11-09. Build the native Views sidebar and the Lit WebUI Workspace editor from this page. The mockup shows the same things as pictures and running code.

Decisions in this page come from the #142 decision-log and approval comments, spec #1 (stories 50 to 63, 74 and 91 to 96) and the folder and tab-name issues (#139, #140, #141, #145, #146, #147). Where the sources do not decide something, it is listed in "Open points" and not decided here.

## Scope and references

| Reference | What Aerie takes from it |
| --- | --- |
| Arc | The main reference for look and feel: the vertical sidebar, Favorites, Pinned, Today, Spaces, per-Space theme, motion. Arc is the only UI reference. |
| Zen Browser | Refinements: the auto-hide peek state machine, folder timings and depth, the Workspace strip and swipe, the pinned-tab close behaviour, the colour wheel. See `prototypes/sidebar/reference/zen.md`. Zen code is MPL-2.0, so Aerie reads it for behaviour and numbers and writes its own. |
| Brave | Guard (blocking features and flows follow Brave Shields). The Guard panel itself is designed elsewhere; the mockup shows a stub. |

No asset, icon, string or code from Arc, Zen or Brave is copied. Dia is not a reference.

### Where to look

| What | Where |
| --- | --- |
| Sidebar mockup | `prototypes/sidebar/index.html` |
| Horizontal tabs | `prototypes/sidebar/horizontal.html` |
| Workspace editor | `prototypes/sidebar/workspace-editor.html` |
| Motion spec, one card per interaction with live demos | `prototypes/sidebar/motion.html` |
| Token spec, themes and the contrast matrix | `prototypes/sidebar/tokens.html` |
| Tokens (source of truth) | `prototypes/sidebar/tokens.json` |
| Screenshots in light and dark | The Tailscale gallery, feature "Sidebar design" (train `design4`) |
| URL parameters, keyboard table, peek machine | `prototypes/sidebar/README.md` |

Decisions: [ADR 0006](adr/0006-workspace-isolation.md) (one storage partition per Workspace) and [ADR 0007](adr/0007-single-design-token-source.md) (one token source). Terms: [GLOSSARY.md](../GLOSSARY.md).

### Native or WebUI

The sidebar, tab strip, command bar and window frame are native Views (spec #1, "Views-first hybrid"). WebUI (Lit) is for settings-like surfaces. In this design that means the Workspace editor. The mockup is HTML only because it is a mockup.

## Layout

### Window frame and page card

The window is a frame with the page in a rounded card on top of it.

- The frame is drawn with the Workspace colour at the theme's tint strength: a gradient at `tint-angle`, a shade layer and a soft glow at the top corner nearest the sidebar. The frame base colour is `color-frame-base`.
- The page card uses `color-content-surface`, radius `radius-xl`, `shadow-card`, and a hairline border where the theme asks for one (`theme-card-border`).
- The sidebar sits on the frame, not on a card. It is `size-sidebar-width` (260 px) wide. The user can drag its inner edge to resize between `size-sidebar-min` (200) and `size-sidebar-max` (400).
- The sidebar goes on the left or right (`data-side`). Everything below mirrors with the side.
- When the sidebar hides or its width changes, the page card box snaps to the new size in one step and the card follows with a transform. Nothing lays out per frame.

### Sidebar regions, top to bottom

| Region | Content |
| --- | --- |
| Top row | Window controls (macOS style dots, `size-window-control`), the sidebar toggle button (Ctrl+S), then Back, Forward and Reload. |
| Address pill | The address field with the Guard icon and blocked count at its end. It opens the command bar surface. |
| Favorites | A grid of icon tiles, `size-favorites-columns` (4) columns. Per Workspace (spec story 52). |
| Pinned | A tree of pinned tabs and folders. Persists across restarts (story 53). |
| Divider and "New Tab" row | A row that opens the command bar (Ctrl+T). |
| Today | A tree of unpinned tabs and folders. Auto-archive applies here. The header shows the sleeping count and an estimate of memory freed on hover. A "Clear" action on the header closes unpinned tabs. |
| Bottom bar | The Archive button, the Workspace switcher (one icon per Workspace), and "New workspace". |

The tab area scrolls. The top row, address pill and bottom bar stay fixed.

### Rows and tiles

| Item | Detail |
| --- | --- |
| Tab row | Favicon, title, then trailing items: audio indicator, a pencil when the tab has a manual name, a "back to pinned page" button when a pinned tab has left its pinned URL, and the close button. A moon badge on the favicon marks a sleeping tab. |
| Folder row | Chevron, folder glyph, name. Auto-filing folders add a funnel glyph at the end. Smart folders use a dashed glyph with a sparkle and an italic count. |
| Favorites tile | The favicon on a rounded tile. A sleeping tile carries the moon badge. |
| Indent | `size-indent` (14 px) per folder level. |
| Selected tab | A tinted row (`color-row-selected`), a Workspace-coloured marker and the theme's selected ring. |
| Focus ring | `size-focus-ring-width` (2 px) with `size-focus-ring-offset` (2 px). |

## Favorites, Pinned, folders and Today

### Favorites

Icons at the top of the sidebar, per Workspace. They are always visible and sit above Pinned. A tab can be dragged between Favorites, Pinned, Today and folders (story 59). Whether Favorites has a cap is not decided (see Open points).

### Pinned tabs

- A pinned tab keeps its original URL. When the user navigates away, a "Back to pinned page" button shows on the row (story 55).
- Pinned tabs persist across restarts and never auto-archive.
- Ctrl+W or the close button on a pinned tab does not close it. See "Pinned tab close".

### Folders

A folder lives in Pinned or in Today. Folders nest up to 5 levels. A folder has one kind, set in the folder editor sheet.

| Kind | What it does |
| --- | --- |
| Manual | Holds what the user drags in. |
| Auto-filing (#139) | URL rules. A new tab that matches a rule is filed into the folder. |
| Smart (#140) | A live view of every open Pinned and Today tab that matches its rules. It moves nothing. |

Each folder also has Folder behaviour (#141), except smart folders.

**Decided rules**

| Rule | Decision |
| --- | --- |
| When rules apply | New tabs only. Navigating inside a tab never moves it, so rows do not jump while the user reads. |
| Two folders match | The first one wins, top to bottom, Pinned before Today. |
| Smart folders and Workspaces | Per Workspace. Workspaces have separate cookies and identities, so a view across them would show one Workspace's titles in another, and closing from the view would have no single owner. |
| Nesting depth | 5 levels. A drop that would go deeper is refused (see below). Tabs can still go into a level-5 folder. |
| Auto-archive in Pinned folders | None. Pinned tabs never archive, so the setting is disabled for folders in Pinned. |
| Auto-archive in Today folders | Still applies to the unpinned tabs inside the folder. |
| Smart folders and behaviour | No behaviour section. They own no tabs, so each tab keeps the behaviour of where it lives. |
| Switching a folder with tabs to Smart | Its tabs move out, next to the folder, and a toast says so. |
| Rules on and off | Each rule has an on switch. A folder has an "All" switch. A global switch ("Folder rules everywhere") sits in the sheet footer. |

**Auto-filing flow.** A new tab that matches lands at the top of Today as usual. After `motion-timing-autofile-delay` it flies into the folder, the folder row pulses, and a toast says "Filed into Trips" with "Undo" and "Don't file this site" (the toast stays `motion-timing-toast-action`, 6 s). Dragging a filed tab out offers "Stop this rule matching this tab". The same action is in the tab's context menu.

**Smart folder rows.** A row shows a quiet "Today" or "Pinned" hint on hover. Its close button closes the real tab, and its tooltip says so. Rows cannot be dragged. Nothing can be dropped into a smart folder.

**Rule grammar** (shared by auto-filing, smart folders and later Workspace link routing, #172): Domain is (the host or any subdomain), URL starts with, URL contains, Title contains (the page title, case-insensitive). The exact grammar, and whether a regular expression is allowed, is an open point (#139, #146).

**Folder behaviour** (#141). Each behaviour has its own switch. When it is off, the row says "Inherits global" and shows the global value.

| Behaviour | Values |
| --- | --- |
| Auto-archive | Never, or the folder's own timeout. Disabled in Pinned. |
| Sleep tabs after | N minutes or hours. |
| Links open | In a new tab, or in place. |

**Folder editor sheet.** A popover sheet anchored to the sidebar edge with an arrow at the folder row, `size-sheet-width` (372 px). It opens from the folder context menu ("Edit folder..."), a double-click on the folder icon, or the funnel glyph. It holds the name and icon, the kind (Manual, Auto-filing, Smart), rules as chips, exceptions and behaviours. Edits apply live. Esc or Done closes it.

**Drag rules over a folder row.** The top 20% and bottom 20% of the row mean "before" and "after" (an expanded folder's bottom edge means "first child"). The middle 60% means "into the folder" (`motion-ratio-folder-edge`). A drop that would nest deeper than 5 levels shows a red outline, a `not-allowed` cursor and the hint "Can't nest deeper than 5 levels". Nothing moves.

**Folder motion.** Collapse takes `motion-duration-folder-collapse` (180 ms) and reveal takes `motion-duration-folder-reveal` (200 ms). Reveal staggers the first 6 children 15 ms apart. Collapse does not stagger. See "Motion".

### Today, auto-archive and the archive panel

Today holds every unpinned tab. Tabs that stay unused for the archive timeout move to the archive (story 56).

- The timeout is a setting: 12 hours (default), 24 hours, 7 days, or never (story 57).
- Tabs inside a Pinned folder never archive. Whether a selected tab or one playing audio is exempt is not decided (see Open points).
- Archived tabs are not lost. The Archive button in the bottom bar opens the archive panel (story 58).

**Archive panel.** A popover with a header that shows the auto-archive setting ("Archive Today tabs after 12 hours", a menu of the four values), a search field, and rows grouped by Today, Yesterday and Last week. Each row shows its Workspace. Clicking a row restores it. The tab goes back to the Workspace it came from, and the sidebar switches there first if needed. A restored tab lands at the top of Today. The command bar also searches and restores archived tabs (story 58, behaviour is M3).

**Archive moment.** A copy of the row shrinks and flies to the Archive button while the real row collapses. The button pulses once when the copy lands.

### Sleeping tabs

An inactive tab sleeps and frees its memory (story 61). The sidebar shows it as follows.

- The favicon fades to `motion-opacity-sleeping` (0.55) and is then desaturated. The desaturate is a static filter that is never animated.
- A moon badge sits on the favicon. The row's accessible name ends with ", asleep".
- The tooltip on a sleeping Favorites tile says "Asleep, frees memory".
- The Today header shows how many tabs sleep and an estimate of memory freed. The tooltip says it is an estimate. The mockup numbers are made up. A native build reads the memory report.
- Selecting a sleeping tab wakes it.

When a tab goes to sleep: after an idle timeout, after a pinned tab is closed (see below), or from "Sleep tab" in the context menu. The idle timeout default is not decided (see Open points).

### Pinned tab close

Ctrl+W or the close button on a pinned tab does not close it by default. A setting offers three actions.

| Setting | First press | Second press (already asleep) |
| --- | --- | --- |
| Reset, sleep, switch (default) | Reset to the pinned URL, put the tab to sleep, select the next tab. The row stays in Pinned. | If asleep and at the pinned URL: unpin and close (it goes to the archive). |
| Sleep only | Put the tab to sleep and select the next tab. The URL stays. | Unpin and close. |
| Close | Unpin and close. | |

The tooltip on the close button names the current action: "Reset and sleep", "Sleep" or "Close".

### Tab rename and title precedence

(#145, #146, #147.) Only the sidebar changes. The page's real `document.title` and the command bar's titles do not change.

- Any tab can be renamed: Pinned, Today, in a folder. Rename with a double-click on the title, F2, the context menu, or a command bar action (M3).
- Enter keeps the name. Esc cancels. An empty name resets. Typing the real page title back resets too. A caption under the field says so.
- A manual name survives navigation and restarts. It is stored in the Tab Model with the tab, so it syncs later.
- A renamed tab shows a small pencil after the title. Clicking it, or "Reset name" in the context menu, resets the name. Its tooltip shows the real page title and the precedence list.

Which title the sidebar shows, first match wins:

| Order | Source | Notes |
| --- | --- | --- |
| 1 | Manual name | #145. |
| 2 | Rename rule | #146. Match by URL or title, rewrite with a pattern. Editable, orderable, can be switched off one by one or all at once. |
| 3 | Built-in tidy | #146. On by default, switchable globally and per site. Drops a site suffix such as " - Gmail", unread counts such as "(3)", and repeated separators. On the device only. |
| 4 | On-device AI title | #147. Opt-in, off by default, never cloud. Falls back to the lines below it while it works. |
| 5 | Page title | |

A tidied or AI-titled tab has a tooltip with the original title and the reason ("Tidied, original: ..."). The mockup has no rename rules and no AI titles, so those lines read "no rule matches" and "off".

## Densities, side and auto-hide

### Densities

| Density | Width | Row height | Favorites tile | Notes |
| --- | --- | --- | --- | --- |
| Comfortable (default) | `size-sidebar-width` (260), resizable | `size-row-height-comfortable` (34) | `size-tile-comfortable` (44) | Favicon `size-favicon-comfortable` (20), inset `size-inset-comfortable` (8). |
| Compact | Same | `size-row-height-compact` (28) | `size-tile-compact` (36) | Favicon 18, inset `size-inset-compact` (4). |
| Collapsed rail | `size-rail-width` (56) | `size-rail-tile` (34) square | One column | Icons only. Titles, trailing items, section labels and the resizer are hidden. The address pill becomes a square Guard button. The Workspace switcher stacks vertically. Tooltips carry the titles. Popovers open to the side. |
| Auto-hide | Hidden at rest | As the chosen density | As the chosen density | The sidebar floats over the page when peeking. See below. |

Collapsed rail and auto-hide are different. The rail keeps a thin strip of icons on screen. Auto-hide shows nothing at rest.

### Left and right

The sidebar can sit on either side. Layout, the hot zone, slide direction, popover placement and the Workspace swipe direction mirror.

### Show, hide and docking

Ctrl+S shows or hides the sidebar. The sidebar slides by its width plus `motion-distance-sidebar-hide-offset` (24 px) so it is fully off screen at rest. Docking an auto-hide sidebar returns it to the layout and resets the peek state.

### Auto-hide peek

One state machine owns the peek. It has one owner for all timers. Every timing is a motion token. The same machine is in `Peek` in `prototypes/sidebar/sidebar.js`, with a test hook `window.__peek`.

**States**

| State | Meaning |
| --- | --- |
| hidden | Off screen, docked away. |
| arming | The pointer is in the hot zone and the dwell timer runs. |
| showing | Sliding in. |
| shown | Visible and settled. |
| pinned | Visible and kept open by Ctrl+Alt+S. |
| leaving | The pointer left and the keep-open timer runs. |
| hiding | Sliding out. |

**Transitions**

| From | Event | To |
| --- | --- | --- |
| hidden | Pointer enters the hot zone (`size-hot-zone`, 6 px) | arming |
| arming | Dwell `motion-timing-peek-delay` (150 ms) passes and the pointer is still in the zone | showing |
| arming | Pointer leaves the zone or window | hidden |
| hidden | F6 or Ctrl+Alt+S | showing |
| showing | Slide settles (`motion-duration-peek-show`, 250 ms, spring) | shown |
| showing | Settled while Ctrl+Alt+S is on | pinned |
| shown and pinned | Ctrl+Alt+S | each other |
| shown | Pointer away, not busy, beyond the grace distance | leaving |
| leaving | Pointer re-enters | shown |
| leaving | Keep-open `motion-timing-peek-leave` (150 ms) ends, still away, not busy | hiding |
| leaving | Timer ends but the state is busy, the pointer is back inside, or it is within the grace distance | shown |
| hiding | Slide ends (`motion-duration-peek-hide`, 150 ms, accelerate) | hidden |
| hiding | Pointer re-enters | showing (reverses from the current offset) |
| any | Ctrl+S (dock) | hidden or docked, reset |

**Busy.** The sidebar never hides while any of these is true: a popover, menu or submenu is open, a tab drag is running, a rename is open, the command bar is open, the sidebar is being resized, the address field has focus, or it was opened with F6 and focus is still inside. A leave that happens while busy is remembered and checked again the moment the busy state ends.

**Grace distance.** When the pointer leaves the window, the peek stays open while the pointer is within `motion-distance-peek-grace-x` (200 px) horizontally and `motion-distance-peek-grace-y` (100 px) vertically of the sidebar. A web page only sees the exit point, so the mockup ends the grace after `motion-timing-peek-outside-max` (3 s). A native build tracks the OS cursor and does not need that timer.

**Known Zen bugs and how this machine avoids them**

| Zen bug | Avoidance |
| --- | --- |
| Shaking at the edge (#12856) | Test the pointer against a layout footprint, never the moving element. The 6 px zone only starts arming. The dwell and keep-open timers swallow boundary jitter. |
| Stuck open (#8643) | One owner for every timer, cleared on every transition. Every busy state ends with a re-check. Window exit has an expiry. |
| Early retract (#15995) | A leave during the slide-in waits until it settles. The 150 ms keep-open covers the gap between the hot zone, the 8 px inset and the sidebar. Busy states and the grace distance protect popovers and close buttons. A reversing hide starts from the current offset. |

Ctrl+Alt+S toggles "keep peek open" and shows a toast. It applies only when the sidebar is auto-hidden.

## Workspaces

A Workspace has its own name, icon, colour, theme and mode override, Favorites, Pinned tabs and Today tabs (spec stories 74, 91 and 52). It also has its own cookies and site storage ([ADR 0006](adr/0006-workspace-isolation.md)). Workspaces ship in M5. The M2 sidebar is built so the switcher and the per-Workspace data fit in.

### Switcher

One icon per Workspace in the bottom bar, as a radio group. The selected icon has a ring in the Workspace accent. "New workspace" follows the icons. The mockup has a Workspace menu on the icon (see `horizontal.html?open=wsmenu`). Its items are not specified here beyond Edit.

**Single-Workspace rule.** With one Workspace, the switcher icons are hidden. Only "New workspace" shows, and "Move to workspace" is hidden from tab menus.

### Switching

| Way | Detail |
| --- | --- |
| Click an icon | Switches. |
| Ctrl+Shift+1 to 9 | Switches to that Workspace by order. |
| Ctrl+Shift+[ and Ctrl+Shift+] | Previous and next Workspace. |
| Horizontal swipe or Shift+wheel on the sidebar | Drags the strip live (see below). |
| Command bar | Behaviour is M3. |
| Drag a tab onto an icon | See below. |
| Mouse back and forward buttons (3 and 4), Alt+Left, Alt+Right | Never switch Workspace. They are page back and forward (Zen #12491). |

**Slide.** The outgoing and incoming tab-list panes sit side by side as a strip. Both move one pane width over `motion-duration-workspace-switch` (250 ms) on `motion-easing-spring-settle` (critically damped, no bounce). Direction follows Workspace order. Only the two panes involved animate. The frame and theme crossfade runs in parallel. Reduced motion uses a 140 ms crossfade and nothing moves.

**Swipe with rubber band.** The strip follows the fingers 1:1 and the neighbouring pane follows. Past the first or last Workspace only the current pane moves, with displacement `x * (1 - |x| / (4.5 * width))` (`motion-ratio-rubber-band`). On release it commits when the swipe passes `motion-distance-swipe-commit` (80 px), or goes faster than `motion-ratio-swipe-velocity` (0.5 px/ms) after at least 20 px. Otherwise both panes spring back. Under reduced motion nothing moves during the swipe, and a release past the thresholds switches with the crossfade.

**Drag a tab onto a Workspace icon.** The icon gets a ring that shrinks from `motion-scale-ws-ring-start` (1.25) and grows in opacity from `motion-opacity-ws-ring-start` (0.15) on a linear curve for the whole dwell. After `motion-timing-ws-drag-hover` (500 ms) the Workspace switches and the drag carries on in the new Workspace, so the tab can be dropped anywhere there. Release over empty space lands it at the end of Today. Dropping directly on an icon moves the tab to that Workspace's Today without switching. The drag ghost says "Moves to Work. Uses Work's logins", because Workspaces have separate cookies (story 95). Reduced motion shows a static ring at `motion-opacity-ws-ring-static` (0.6). The dwell stays 500 ms.

## Themes

Two independent axes.

- **Theme** is how the chrome is drawn: surfaces, tint strength, shadows and materials.
- **Workspace colour** is the hue that every theme uses in its own way.

Mode (System, Light, Dark) is a third, separate choice. Mode is always explicit. The Workspace colour never switches light or dark on its own.

### Theme catalogue

| Theme | Modes | Tint strength (light / dark) | Notes |
| --- | --- | --- | --- |
| Vivid (default) | Light, dark | 0.60 / 0.50 | Strong tint. The accent is the Workspace hue mixed toward the theme base, so it stays at 3:1 or better. |
| Soft | Light, dark | 0.32 / 0.26 | Quieter tint. |
| Clean | Light, dark | 0.08 / 0.08 | Almost no tint. A 3 px colour strip marks the Workspace. |
| OLED | Dark only | 0 | True black surfaces. In light mode it shows Clean. Grain is off. |
| High contrast | Light, dark | 0 | Text and muted text at 7:1. A 4 px strip and a wider focus ring. Grain is off. Switches on under `prefers-contrast: more`. |
| Mono | Light, dark | 0 | Grey surfaces, no hue in the chrome. |

Values are in `tokens.json` under `theme`. A theme can override `color`, `shadow` and `size` tokens while it is active.

### The three levels

| Level | Where it lives | Values |
| --- | --- | --- |
| Global default | A user setting | A theme plus a mode (System, Light, Dark). Default: Vivid, System. |
| Workspace override | On the Workspace record in the Tab Model | `{ colour, theme: default or a theme id, mode: default, light, dark or system }`. |
| Effective look | Computed | Effective theme is the Workspace theme, or the global default when the Workspace says `default`. Effective mode is the same: the Workspace mode (`system` follows the OS), or the global one. |

This is data, not tokens. It is stored in the Tab Model so it syncs later (story 96). In the sample data, Personal uses the default, Work uses Clean, and Side project uses OLED in dark mode.

Switching between Workspaces with different looks crossfades the whole chrome over `motion-duration-xslow` (380 ms, 140 ms under reduced motion). The crossfade uses two complete layers so text is never half-faded on the wrong surface. A theme or mode change from settings crossfades the same way and is instant under reduced motion. A theme set at launch or from settings on startup applies with no animation.

### OLED, High contrast and Mono rules

| Rule | OLED | High contrast | Mono |
| --- | --- | --- | --- |
| Tint | None | None | None |
| Grain | Off | Off | Allowed |
| Intensity slider in the editor | No effect | No effect | No effect |
| Text contrast | 4.5:1 | 7:1 | 4.5:1 |
| Mode | Dark only | Light and dark | Light and dark |

The editor shows a note when a control has no effect in the chosen theme.

### prefers-contrast

Under `prefers-contrast: more`, the effective theme is High contrast, unless the user chose a theme explicitly. Under `forced-colors`, the chrome falls back to system colours. Both paths are untested in a native build (see Open points).

### Workspace colour

Eight presets: dusk, tide, moss, ember, lilac, slate, sand, mint. Each has one to three gradient stops and an accent per mode. A custom colour is up to three dots on a wheel, a harmony mode, and sliders (see the editor).

Text and accent stay readable on every preset. The accent is the Workspace hue mixed toward a theme base colour (`theme-accent-hue-share`, `theme-accent-base`). Vivid keeps its accent darkened to hold 3:1.

## Workspace editor

A WebUI page in Lit, shown as a modal sheet over the window (mockup `workspace-editor.html`). It is a settings-like surface, so it is not native Views.

| Part | Detail |
| --- | --- |
| Header | "Edit Workspace" and the Workspace name. |
| Preview | A small picture of the sidebar and page card in the chosen colour, theme and mode (`size-editor-preview-width` and `-height`). |
| Name | Text field, 24 characters at most. An empty name shows an inline error and blocks Save. |
| Icon | A radio group of icons. |
| Mode | A segmented control: Default (follow global), System, Light, Dark. A note says what Default means now. |
| Link routing | A disabled placeholder, "Open links from these sites here", tagged "Later". Routing is #172 (M5). |
| Cookies note | "This Workspace has its own cookies and logins." |
| Colour | The 8 presets as a radio group, plus "Custom". |
| Custom colour | A wheel with up to 3 dots, a harmony mode (analogous, complementary, triad), and sliders for Lightness (10 to 90), Intensity (0 to 100) and Grain (optional, 0 to 100). |
| Theme | A radio group: Default (follow global) and the six themes. |
| Footer | Save, Cancel and Delete. Delete opens a confirmation (`alertdialog`) with "Keep workspace" focused first. |

**Behaviour.** The wheel works with a pointer and the keyboard: focus a dot, left and right change hue, up and down change saturation, Shift moves faster. A chip shows the contrast result for the chosen colour, for example "Contrast AA 5.1:1". When a custom colour falls below the minimum, the editor adjusts it and the chip says "Adjusted for contrast". Grain is off and the slider is disabled in OLED and High contrast, with a note. Intensity has no effect in OLED, High contrast and Mono, with a note. The Lightness slider stays. The sheet traps focus. Esc cancels, or closes the delete confirmation first.

The editor reads and writes the Workspace record through a Mojo handler. It does not change tokens. Presets are token values. A custom colour is data on the Workspace record.

## Horizontal tabs

For users who prefer the classic layout (story 51). Mockup `horizontal.html`. It uses the same Workspaces, themes and densities.

| Option | Layout |
| --- | --- |
| Two rows (default) | Row 1 is the tab strip: Workspace chip, pinned tabs and Today tabs. Row 2 is the toolbar: navigation, address pill with Guard, and Favorites tiles. The toolbar is drawn at the top of the page card so the selected tab joins it. |
| One row (dense option) | Everything in one strip. |

Sizes are `size-hz-*` tokens: strip and toolbar heights, tab height, `size-hz-tab-max` (180), `size-hz-tab-min` (36), `size-hz-tab-selected-min` (120), `size-hz-tab-pinned` (32), tile sizes, address pill width (240, min 160), scroll fade (28), flare radius (10). Comfortable and compact variants exist for each.

**Overflow.** Tabs shrink from `size-hz-tab-max` toward `size-hz-tab-min`. A tab narrower than `size-hz-tab-icon-only` (72 px) shows its favicon only. The selected tab never shrinks below `size-hz-tab-selected-min`. If even icon-only tabs do not fit, the Today tab group scrolls with fading edges (`size-hz-scroll-fade`) and chevron buttons.

**Motion.** Open: the tab width grows from 0 and the tab scales in over `motion-duration-tab-open` (200 ms). Close: the tab slides 8 px down and fades over `motion-duration-xfast`, then its width collapses over `motion-duration-tab-close` (140 ms). Reordering uses FLIP: neighbours shift on the standard ease and the dragged tab settles on the spring.

**Container query.** CSS container queries cannot read variables, so `horizontal.css` repeats `size-hz-tab-icon-only`. The token generator fails if the two drift apart. A native build reads the token directly.

Horizontal drag between groups (Pinned and Today) is not final (see Open points).

## Command bar surface

Visual only here. Behaviour (search, actions, Workspace and archive commands) is M3.

- Opens with Ctrl+T, Ctrl+L or Ctrl+K. Ctrl+K is the fallback if the browser keeps Ctrl+T.
- A centred dialog over a scrim. A search field with placeholder "Search tabs, workspaces, or type a URL", a result list, and a footer with key hints (Enter open, Up and Down move, Esc close).
- The field is a combobox and the list is a listbox. Esc closes it and focus returns to where it was.
- Motion: the scrim fades and the bar scales from `motion-scale-cmdbar-enter` (0.98) with start opacity `motion-opacity-cmdbar-start` (0.5) over `motion-duration-fast`. Dismiss takes `motion-duration-xfast`.

## Keyboard and screen reader

### Roles and names

| Element | Role and name |
| --- | --- |
| Sidebar | `navigation`, "Sidebar". |
| Favorites | `grid`, "Favorites". Tiles are buttons named by the tab title. |
| Pinned and Today | `tree`, "Pinned tabs" and "Today". |
| Tab row | `treeitem`, `aria-level`, `aria-selected`. The name is the shown title plus ", asleep" for a sleeping tab and ", shown from Today" for a smart folder row. |
| Folder row | `treeitem` with `aria-expanded`. The name includes the kind. |
| Workspace switcher | `radiogroup`, "Workspaces". Icons are `radio` with `aria-checked`. |
| Page card | `main`, "Page". |
| Command bar | `dialog`, "Command bar". Field is a `combobox`, list a `listbox`. |
| Menus | `menu`, with `menuitem` and `menuitemradio`. |
| Switches in sheets | `switch` with `aria-checked`. |
| Toasts | `status`, polite live region. |
| Workspace editor | `dialog` with `aria-modal`. Delete confirmation is `alertdialog`. Presets, icons, modes and themes are radio groups. |
| Resizer | `separator`, vertical, "Resize sidebar". |

The Guard button's name reads "Guard: N blocked on this page".

### Focus order

Top row buttons, address pill, Favorites, Pinned, New Tab, Today, bottom bar (Archive, Workspace switcher, New workspace), then the page card. Each tree and the switcher is one tab stop with a roving tabindex. Arrow keys move inside. The mockup shows the order with `?focus=1`.

F6 moves focus between the sidebar and the page. With the sidebar auto-hidden, F6 peeks it open, and it stays open while focus is inside.

### Shortcuts

| Keys | Action |
| --- | --- |
| Ctrl+S | Show or hide the sidebar. |
| Ctrl+Alt+S | Auto-hide only: keep the peek open (toggle). |
| Alt+Left, Alt+Right | Page back and forward. |
| Ctrl+T, Ctrl+L, Ctrl+K | Command bar. |
| Ctrl+Shift+1 to 9 | Switch Workspace. |
| Ctrl+Shift+[ and ] | Previous and next Workspace. |
| Ctrl+W | Close the selected tab. Pinned tabs follow the pinned close setting. |
| F6 | Move focus between sidebar and page. |
| Arrows, Home, End | Move in Pinned, Today, Favorites and the switcher. Right and Left expand and collapse a folder. |
| Enter | Activate. Toggles a folder. |
| F2 | Rename. |
| Delete | Close the focused tab. |
| Shift+wheel, horizontal swipe | Switch Workspace. |
| Right-click, Menu key | Tab context menu. |
| Esc | Close the command bar, menu or popover. |

On macOS, Cmd replaces Ctrl where the platform expects it. The exact macOS map is an open point.

Mouse: drag rows to reorder or move between Today, Pinned, folders and Favorites. Middle-click closes a tab. Drag the sidebar's inner edge to resize. With the sidebar hidden, move to the outer screen edge to peek.

## Motion

Aerie wants motion with the feel of Arc, without copying its assets (maintainer request, #142). Every row below is also a card in `motion.html` with a live demo and the curve. Values are in `tokens.json` under `motion-*`. Each token with a reduced-motion value carries it in the same file.

| Interaction | What moves | Duration token | Easing token | Reduced-motion version |
| --- | --- | --- | --- | --- |
| Sidebar show and hide | Sidebar `transform` by its width plus `motion-distance-sidebar-hide-offset`. The card follows with a transform. | `slow` (280) show, `base` (200) hide | `decelerate` show, `accelerate` hide | Sidebar fades (opacity, 140 ms). The card snaps. |
| Auto-hide peek | Sidebar `transform` from the edge. | `peek-show` (250), `peek-hide` (150) | `spring` show, `accelerate` hide | 100 ms fade. Delays and grace stay. |
| Workspace switch | Two panes translate one pane width as a strip. | `workspace-switch` (250) | `spring-settle` | 140 ms crossfade of the panes. |
| Theme and tint crossfade | Opacity of two tint layers. A whole-chrome crossfade with two layers when theme or mode changes. | `xslow` (380) | `standard` | 140 ms. A settings change applies instantly. |
| Swipe with rubber band | Strip follows the fingers. Release springs to a pane. | `workspace-switch` (release) | `spring-settle` | Nothing moves while swiping. Release crossfades 140 ms. |
| Open tab | Row content scales from 0.96 to 1 and fades in. Row height grows from 0 (the layout exception). | `base` (height), `fast` (content) | `standard`, `decelerate` | Content fades in 90 ms. Height snaps. No scale. |
| Close tab | Row content slides 8 px toward the outer edge and fades. Then row height collapses. | `fast`, then `base` | `accelerate`, `standard` | Fade 90 ms, then height snaps. |
| Pinned tab close | Favicon opacity to 0.55, then a static desaturate. The next tab takes the selection. | `fast` | `standard` | Instant swap. |
| Pin and unpin | FLIP. The pinned tab moves on the spring. Other rows close the gap. | `spring-duration` (420) and `base` | `spring`, `standard` | Instant. |
| Drag (lift, shift, settle) | Row scales to 1.02. Rows it passes shift one row. The dropped row springs from the release point. | `fast`, `base`, `spring-duration` | `standard`, `spring` | No scale, no shift, instant drop. The row still tracks the pointer. |
| Drag onto a Workspace icon | A ring scales from 1.25 to 1 and rises from 0.15 opacity. Then the Workspace switch. | `ws-drag-hover` (500, timing) | `linear` | Static ring at 0.6. The 500 ms dwell stays. |
| Folder reveal and collapse | Opacity of the children, first 6 staggered 15 ms. Chevron rotates 90 degrees. Container height (layout exception). | `folder-reveal` (200), `folder-collapse` (180), `fast` (chevron) | `in-out`, `standard` (chevron) | Opacity only 90 ms. No stagger. Height and chevron snap. |
| Command bar | Scrim and bar fade. Bar scales from 0.98. | `fast` in, `xfast` (90) out | `decelerate`, `accelerate` | Opacity only, 90 ms. |
| Popover and menu | Popover drops 4 px. Menu scales from 0.97. Both fade in. No closing animation. | `fast`, `xfast` | `decelerate` | Opacity only, 90 ms. |
| Toast | Rises 6 px and fades in and out. | `fast` | `decelerate` in, `accelerate` out | Opacity only. |
| Tooltip | Opacity fade after a rest delay of `tooltip-delay` (400 ms). It disappears at once. | `xfast` | `decelerate` | Same. |
| Sleeping tab | Favicon opacity to 0.55, then a static desaturate. | `fast` | `standard` | Instant. |
| Archive moment | A copy of the row shrinks to 0.25 and flies to the Archive button. The button pulses to 1.25 and back. | `archive-collapse` (320), `archive-pulse` (360) | `accelerate`, `standard` | Copy fades 120 ms. Button flashes colour 140 ms with no scale. |
| Archive restore | A copy flies from the panel into Today on the spring. | `archive-restore` (340) | `spring`, `accelerate` | Fades 120 ms. |
| Auto-file | The row flies into the folder after a pause. The folder row flashes the accent colour. | `autofile-delay` (280, timing), `autofile-pulse` (700) | `spring`, `standard` | Pause 120 ms. The row fades in place. Pulse 300 ms. |
| Folder editor sheet | Slides 10 px from the sidebar edge and fades. | `sheet-enter` (180), `sheet-exit` (120) | `decelerate`, `accelerate` | Opacity only, 90 ms. |
| Switch and chip toggle | The knob slides. The track colour crossfades. | `switch` (120) | `standard` | 60 ms. |
| Horizontal tab open and close | Open: scale 0.96 to 1, width grows from 0. Close: slide 8 px, fade, width collapses. | `tab-open` (200), `tab-close` (140), `xfast` | `decelerate`, `standard`, `accelerate` | Open fades 140 ms. Close fades 90 ms and removes at once. |
| Startup, restore, snaps | Nothing. | `instant` (0) | none | Same. |

Durations are in milliseconds. The `fast` token is 140 ms (90 reduced), `base` 200 (140), `slow` 280 (140). The spring token is stiffness 380, damping 32, mass 1, sampled as a `linear()` curve over 420 ms. `spring-settle` is the critically damped version.

### Performance rules

1. **Compositor layers only.** Animate `transform` and `opacity`. In native Views this means `ui::Layer` with `ui::LayerAnimator` (transform and opacity elements). In WebUI, Web Animations or CSS on the same two properties. Shadows, filters, blur and colour are never animated. They switch with the state. The one colour transition is a 30 px switch track.
2. **No layout per frame, one exception.** The tab-list row height (open tab, close tab, folder reveal and collapse, archive collapse) is the only property that lays out during an animation. It affects one list, runs at most 200 ms and is clipped. The horizontal strip's tab width is the same exception for that one strip. Nothing else resizes while an animation plays. The sidebar width, the page card and the toolbar change in one step.
3. **No animation on startup paths.** First paint, session restore and the first Workspace render run with motion off. Animations start only from a user action after the window is interactive. No animation object, layer or curve is built before then. The curves are constants.
4. **Reduced motion is honoured everywhere.** The OS setting (`prefers-reduced-motion`, and the platform equivalent in Views) and the in-app setting both apply. Every token with a `reduced` value uses it. Movement is removed, and state changes become opacity fades of 90 to 140 ms or an instant swap. Delays that are behaviour (hover dwell, keep-open, drag dwell) stay the same.
5. **One owner per animation.** A new animation on an element cancels or reverses the running one from its current value. No stacked timers, no leaked animators.
6. **A test proves reduced motion.** A browser test, and a Bench check, load the sidebar with reduced motion forced, run each interaction, and assert that no transform animation is created on any layer and that every opacity animation is 140 ms or less. A second test asserts that, in normal mode, no animated property outside `transform`, `opacity` and the tab-list row height is created.

## Design Tokens

[ADR 0007](adr/0007-single-design-token-source.md) records the decision. `prototypes/sidebar/tokens.json` is the seed.

### Groups

| Group | Contents |
| --- | --- |
| `color` | Frame base, text, muted text, surfaces, field, row states, divider, hairline, scrim, status colours (ok, warn, danger), danger fill. Light and dark values. |
| `tint` | Tint angle and grain maximum. |
| `theme` | One entry per theme. Plain keys (`tint-strength`, `accent-hue-share`, `accent-base`, `marker-width`, `strip-height`, `selected-ring`, and others) apply to the active theme and mode. Keys such as `color.text` or `shadow.card` override that token while the theme is active. `_modes` lists the modes a theme supports. |
| `workspace` | The 8 presets: gradient stops and an accent per mode. |
| `shadow`, `radius`, `space`, `type` | Elevation, corner radii (4 to 999), a spacing scale (0 to 32), type sizes, line heights and weights. |
| `size` | Sidebar, rail, rows, tiles, favicons, hot zone, focus ring, sheet, editor preview, and all horizontal-tab sizes (`hz-*`). |
| `motion-*` | `duration`, `easing`, `spring`, `distance`, `scale`, `opacity`, `timing`, `ratio`. |

### Naming and files

- The CSS variable is `--aerie-<group>-<token>`. The C++ constant follows the same name in the generator's naming style.
- A token has either `value` (with an optional `unit`) or `light` and `dark`. A token with a `reduced` value also carries its reduced-motion version.
- Nobody edits generated output by hand.

### Pipeline

| Step | Tool | Output |
| --- | --- | --- |
| Generate CSS | `gen_tokens_css.py` | `tokens.css`: `:root` blocks for shared, light and dark values, one block per theme and mode (`:root[data-theme][data-mode]`), and reduced-motion blocks (`prefers-reduced-motion` and `[data-reduced-motion]`). It also checks the horizontal icon-only threshold. |
| Generate C++ | To build in M2 | A generated header with colour, size and motion constants and per-theme tables, for native Views. |
| Check contrast | `check_contrast.py` | Exit 1 on any failure. |

Both generators move into the source tree in M2 (see the ADR).

### Contrast rules and the check

`check_contrast.py` rebuilds the frame the way the sidebar paints it, samples the sidebar column on both sides of a 1440 by 900 window, and checks every theme, mode and Workspace preset.

| Pair | Minimum |
| --- | --- |
| Text on the frame | 4.5:1 (7:1 in High contrast) |
| Muted text on the frame | 4.5:1 (7:1 in High contrast) |
| Accent and focus ring | 3:1 |
| Status colours (ok, warn, danger) on each theme's popover surface, and on their own chip tints | 4.5:1 |
| Text on the danger fill | 4.5:1 |

The run on this branch reports 0 failing combinations. In M2 the check runs in CI. A change that fails it does not merge.

## Open points carried into M2 planning

The sources do not settle these. They are not decided in this page.

| Point | Detail |
| --- | --- |
| Sleeping-tab desaturate | The desaturate is a static filter that snaps on after the opacity fade, because filters are never animated. A smoother option is a second grey favicon layer crossfaded over the colour one. It costs one extra layer per sleeping tab. Judge it in the native build. |
| High contrast auto-switch | The switch under `prefers-contrast: more`, and `forced-colors`, are untested in a native build. The mockup only simulates them. |
| Rule grammar details | The four rule types are set. Whether a regular expression is allowed, how patterns capture (`$1`) in rename rules, and the exact grammar are open in #139 and #146. |
| Folder rule against routing rule | What wins when a folder rule and a Workspace routing rule both match is open in #172. |
| Horizontal drag between groups | Dragging a tab between Pinned and Today, and into folders, in the horizontal strip is not designed in detail. |
| Idle sleep timeout | The mockup shows sleeping tabs but no default timeout. #141 adds a per-folder "sleep after N". The global default is not set. |
| Favorites cap | Zen caps Essentials at 12. Aerie has 4 columns and no stated cap or scroll behaviour. |
| Archive exceptions | Whether a selected tab or a tab playing audio is exempt from auto-archive is not stated in the sources. |
| Workspace wrap-around | Zen wraps from the last Workspace to the first. The mockup does not wrap. Ctrl+Shift+] on the last Workspace is not decided. |
| Whole-chrome crossfade in Views | The mockup uses View Transitions. Views needs an equivalent that fades two complete layers, such as a snapshot layer. How is not decided. |
| Folder editor surface | The folder editor sheet is a popover. Whether it is native Views or WebUI is not decided. The "WebUI only for settings-like surfaces" rule leans to Views. |
| macOS shortcuts | The mockup lists Ctrl shortcuts only. The macOS map (Cmd and Option) is not set. |
| Custom colour and contrast | The mockup adjusts a custom colour that fails contrast and shows a chip. The adjustment rule (which channel moves, and by how much) is not specified. |
| Smart folder rows in two places | A tab matching a smart folder shows both there and in its real place. The mockup draws both. Selection and drag rules across the two are not fully specified. |
| Zen light and dark from colour | Zen picks light or dark from the Space colour. The maintainer decided against it for Aerie. Only the readable-text idea (accent tinted toward black or white) remains a candidate. |
| Later work | Import from Arc and Zen (#171, M5). Link routing by domain (#172, M5). On-device AI titles (#147). Mobile companions. |
