# Aerie sidebar and Workspaces mockup

Clickable design mockup for issue #142. Plain HTML, CSS and JS. No build step, no network.

## Open it

Open `index.html` in a browser (file:// works), or run `python3 -m http.server` in this folder.

## Files

- `tokens.json` is the source of truth for Design Tokens. `tokens.css` is generated from it: `python3 gen_tokens_css.py`.
- `screens.json` lists screenshot states. `query` is the URL query string for each state. Add `mode=light` or `mode=dark`.
- `check_contrast.py` checks text, muted text and accent contrast for every theme, mode and Workspace colour. Exits non-zero on failure.

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
| `open` | `cmdbar`, `archive`, `contextmenu`, `guard`, `peek` |
| `wscount` | `1` shows a single Workspace: dots hidden, only "+ New workspace". Also a mockup control |
| `pinnedclose` | `reset-sleep-switch` (default), `sleep`, `close`. Also a mockup control. See Pinned tab close |
| `controls` | `0` hides the mockup controls panel |

`theme=light` and `theme=dark` still work as aliases for `mode`.

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

- Collapse: opacity and height over `motion-duration-folder-collapse` (180ms) with `motion-easing-in-out` (cubic-bezier(.4,0,.2,1)). Reveal: `motion-duration-folder-reveal` (200ms). Reduced motion: opacity only.
- Drag over a folder row: the top 20% and bottom 20% (`motion-ratio-folder-edge`, from Zen) mean before / after (an expanded folder's bottom edge means first child). The middle 60% means into the folder.
- Folders nest up to 5 levels. A drop that would go deeper is refused: red outline on the row, `not-allowed` cursor, a hint "Can't nest deeper than 5 levels", and nothing moves (no shaking). Tabs can still go into a level-5 folder.

## Drag a tab onto a Workspace icon

The icon highlights with a growing ring. After 500ms (`motion-timing-ws-drag-hover`) the Workspace switches with the normal switch motion and the drag carries on in the new Workspace (the dragged row becomes a floating ghost), so you can drop it anywhere there. Release over empty space lands it at the end of Today. Dropping on an icon moves the tab to that Workspace's Today without switching. The ghost says "Moves to Work — uses Work's logins", because Workspaces have separate cookies.

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
