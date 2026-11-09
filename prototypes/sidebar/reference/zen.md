# Zen Browser reference for Aerie's sidebar, Workspaces, themes and motion

Inspiration only. No Zen asset, icon or code goes into Aerie. Zen's code is MPL-2.0; we read it for behaviour and numbers, and write our own.

Sources: Zen source cloned at `github.com/zen-browser/desktop` commit `2f1b83f` (2026-11-09, Zen 1.24.1b era). Paths below are relative to that repo (`R:`). Docs: `docs.zen-browser.app/user-manual/{workspaces,compact-mode}` (fetched; thin). Release notes: `github.com/zen-browser/desktop/releases` (1.23.3b to 1.24.1b). Issue numbers refer to `zen-browser/desktop`. I did not run Zen.

## Summary

Zen is Firefox with an Arc-like vertical sidebar. It calls Workspaces "Spaces". What it does differently from Arc, in order of value to us:

1. **Theme mode follows the colour.** The light or dark UI is chosen from the Space's gradient by comparing text contrast, not from a user toggle (`shouldBeDarkMode`). Text colour is a tint of the accent at 0.9 opacity. This is the main readability trick.
2. **Compact mode has tuned hover behaviour.** A 0.25s spring-shaped slide, a hover delay, a keep-open timer, an edge grace distance, "flash" on events, and two pinned-open shortcuts. Its bugs are an equally good guide (see Pitfalls).
3. **Ctrl+W on a pinned tab does not close it.** Default is reset to original URL, unload, and switch to another tab.
4. **Essentials are a separate, capped top grid** that can be shared across Spaces or kept per container.
5. **Space Routing:** rules that send a domain to a given Space.
6. **Spaces are one transform-strip** animated with a spring (250ms, no bounce), only the two panes involved move, and a trackpad swipe drags the strip live with rubber-banding.
7. **Gradient picker:** up to 3 dots on a colour wheel with harmony rules, an opacity slider, a texture dial.
8. **Containers are tied to Spaces** with a per-Space default container.

Their weak spots: sidebar shaking and not-hiding bugs in compact mode, window/tab sync confusion, memory, and a pile of prefs where Arc has none.

## Table: Zen refinement -> Aerie action

| Zen refinement | Action | Why |
|---|---|---|
| Light/dark chosen from the Space colour by contrast (`shouldBeDarkMode`) | Adapt | Fixes unreadable text on custom colours. Keep the user's explicit mode override (Zen has `zen.theme.use-system-colors`). Our themes already have fixed palettes; apply this only to the per-Workspace colour preset |
| Text colour = accent tinted 12% toward black/white, alpha 0.9 | Adopt | Text stays harmonised with the Space colour yet readable. Verify to WCAG AA in tests |
| Dark-mode bias (0.3): lowers the light-text contrast so dark mode is picked more often | Skip (maybe later) | Taste knob. Our themes are curated, so we do not need it |
| Compact slide 0.25s spring-like `linear()` easing, ~1.1% overshoot | Adapt | Matches our `spring` token. Use 250ms and a near-critically damped curve |
| Hover-in delay (`HOVER_HACK_DELAY`), keep-open 150ms, edge grace 200px horizontal / 100px vertical when the cursor leaves the window | Adopt | Prevents flicker and lets the user reach the window edge or close buttons. Zen shipped bugs here (#15995, #12856) |
| Pinned-open shortcuts: Ctrl+S toggle compact, Ctrl+Alt+S toggle floating sidebar (stays until pressed again) | Adapt | Peek on hover plus a sticky key. Use our own shortcut |
| "Flash" the sidebar for a short time on events (new background tab toast, 800ms option) | Adapt (optional) | Cheap way to confirm an action while hidden. Default off, as in Zen |
| Ctrl+W on a pinned tab: reset to pinned URL, unload, switch to neighbour (`reset-unload-switch`); if already unloaded, close | Adopt | Matches Arc's feel and our "sleeping tabs". Make the behaviour a setting with the same four options |
| Reset-to-pinned-URL button on a changed pinned tab, plus original-icon restore | Adopt | We already have pinned tabs; the "changed" marker is the useful bit |
| Essentials: capped grid at the top, max 12 (`zen.tabs.essentials.max`), auto-fit columns, shared across Spaces unless `separate-essentials` | Adapt | Maps to our Favorites grid. Cap the count and make columns responsive. Decide Favorites scope (global vs per Workspace) explicitly |
| Folders: nested to 5 levels (`zen.folders.max-subfolders`), collapse 180ms / reveal 200ms with opacity+height, drag-over threshold 20% of folder height, unload-all animation | Adopt | We have nested folders. Use 180ms ease-in-out collapse and a drag threshold |
| Folder hover search (500ms hover delay) | Skip for now | Extra. Revisit with command bar |
| Space switch: spring 250ms, bounce 0, only visible panes animate, wrap-around, Alt+1..0 (Ctrl+1..0 on macOS), Ctrl+Alt+Left/Right | Adapt | Our Ctrl+Shift+1..9 is fine. Add prev/next keys and wrap-around. Animate only the two panes |
| Live swipe on trackpad: strip follows finger with rubber-band (`1 - |x|/(width*4.5)`), edge swipe opens Library or "add Space" | Adapt | Our horizontal swipe should follow the finger, not fire on release. Edge-swipe to add Space is a nice discoverable extra |
| Drag a tab onto a Space icon: switch Space after 500ms hover (`zen.tabs.dnd-switch-space-delay`) | Adopt | Needed for moving tabs between Workspaces |
| Per-Space default container + option to jump to the Space whose container a tab opens in | Adapt | Our per-Workspace cookie jar is the same idea. Do not leak the container name into the UI by default (Zen hides the default container indicator) |
| Space Routing: domain/regex rules open links in a chosen Space (case-insensitive) | Adapt, later | Strong for people with work/personal Spaces. Needs a UI and care with redirects |
| Gradient picker: wheel with up to 3 dots, harmony modes (complementary, split, analogous, triadic, floating), lightness, opacity slider, textures | Adapt | Our colour presets can start as a curated set; a wheel is a later upgrade. Keep the 3-dot cap and the harmony list as design guidance |
| Grain/texture dial: none/grain/sand/denim/tweed, strength default 10/16 | Skip textures; adapt grain | Grain is cheap and gives a physical feel. Off by default for OLED and High contrast. Costs GPU, so keep it optional |
| Acrylic/blur sidebar (`blur(42px)`, 80% opaque) with `prefers-reduced-transparency` fallback | Adapt | Only if the native Views stack can blur cheaply. Always honour reduced transparency |
| Mica/translucent window, window alpha 0.35 to 1 | Skip | Platform-specific, a performance risk |
| Squircle corners, `--zen-squircle-value` 1.3 | Skip | Cosmetic, hard on native Views |
| "Tab not visible" indicator (1.24b: shows a tab is unloaded) | Adopt | Our sleeping tabs need a clear dimmed state. Zen's greyed favicon and `pending` attribute are the model |
| Glance (Alt+click preview, 350ms open, background scales to 0.97, bounce 0.2) | Out of scope | Arc's Peek is out of scope. Noted for later only |
| Split view (Ctrl+Alt+G/V/H/U, drag tab to side, 80-90ms inset transitions) | Planned after M5 | Read section 6 then |
| Window Sync (all windows mirror tabs) | Skip | Biggest confusion source (#7079, #12008) |
| Import Spaces from Arc and Dia (1.24b) | Consider | A cheap acquisition path for Arc users. Needs a data-format check |
| Boosts, Zen Mods | Out of scope | Boosts is on Arc's out-of-scope list |
| Reduced motion | Adopt | Zen guards only some tab-height transitions with `prefers-reduced-motion` (`vertical-tabs.css:139`). We must cover everything |

## 1. Workspaces ("Spaces")

**Switching.** Space icons sit in a strip at the bottom of the sidebar; the current Space's name/icon is also an indicator at the top (`.zen-current-workspace-indicator`, `R:src/zen/spaces/zen-workspaces.css:176`). Triggers:
- Click an icon. Icons are greyscale at 70% opacity when inactive and full colour when active or hovered; transition `filter 0.2s, opacity 0.2s, width 0.1s, transform 0.2s` (`zen-workspaces.css:78`). Inactive icons with no emoji become an 8px dot.
- Keys: Alt+1..9,0 (Ctrl+1..0 on macOS) (`R:src/browser/base/content/zen-keysets.inc.xhtml:11-25`), Ctrl+Alt+Right / Left for next/previous (`:26-27`). Create-Space has no default key.
- Trackpad swipe: `ZenSpacesSwipe.mjs`. The strip tracks the finger (`delta * 100`, pref `zen.workspaces.swipe-actions.delta-multiplier`), with a resistance factor `1 - |x| / (stripWidth * 4.5)` that stops dragging past about half the multiplier (`:204-216`). On release it calls `changeWorkspaceShortcut(direction)`. Swiping past the first or last Space (pref `swipe-actions.edge-actions`) opens the Library or the "add Space" gesture (`ZenSpaceAddSwipe.mjs`).
- Mouse wheel over the icon strip with a modifier (`zen.workspaces.scroll-modifier-key` = ctrl), optional natural scroll.
- Wrap-around: on (`zen.workspaces.wrap-around-navigation`). Shortest-path direction is computed so wrap animates the short way (`ZenSpaceManager.mjs:2325-2330`).
- Drag a tab over an icon: switch after 500ms (`zen.tabs.dnd-switch-space-delay`; `zen.workspaces.dnd-switch-padding` = 20).

**Animation** (`ZenSpaceManager.mjs:2301-2520`, `#animateTabs`): each Space is an absolutely placed pane; switching animates `transform: translateX(offset*100%)` with `type: "spring", bounce: 0, duration: 0.25` (pref `zen.workspaces.switch-animation-duration` = 250). Panes that will not be visible are set instantly, only the incoming/outgoing panes animate. The code comment says why: "to avoid having multiple workspaces animating off-screen at the same time which can cause performance issues". The background gradient cross-fades in the same call (`--zen-background-opacity` from previous value to 1). A 50ms safety timeout resolves the promise if an animation hangs (`:2512`).

**Per-Space theming.** Each Space stores `theme = {type:"gradient", gradientColors, opacity, texture, textureType}` (`ZenGradientGenerator.getTheme`, `ZenGradientGenerator.mjs:1780`). On switch the sidebar and window background are repainted and `--zen-primary-color`, `--toolbox-textcolor`, `--toolbar-color-scheme` are recomputed (`:2140-2190`). There is a window-level scheme (system/light/dark, `windowSchemeType`) and private windows are always dark (`:270-275`). Unlike Arc, Zen has no separate per-Space "mode override"; the mode comes from the colour (see section 4).

**Gradient/colour picker** (`ZenGradientGenerator.mjs`, panel `PanelUI-zen-gradient-generator`):
- A colour wheel. Click to add a dot, max 3 (`static MAX_DOTS = 3`, `ZenGradientGenerator.mjs:157`). Distance from the centre sets lightness (`:707-721`); the first dot is primary and the others follow a harmony: complementary [180], singleAnalogous [310], splitComplementary [150,210], analogous [50,310], triadic [120,240], or floating (free) (`:284-292`).
- Predefined colour swatches, a custom colour list (accepts alpha hex), scheme buttons, an opacity slider and a texture dial. The dial is a knob: drag to set strength, click or swipe on it to cycle texture (`:441-570`; 1.24.1b notes: "New grain options ... cycle ... by clicking on the circle inside the dial").
- The gradient is built in OKLab with 12 stops and layered overlays (`kGradientStops = 12`, `kOverlayStrips = 64`, `kCustomGradientAngle = -30`, constants at `:6-26`).
- Opacity mapped through a squared curve between min and max layer alpha: `layerAlpha = kMin + (kMax - kMin) * intensity^2`, with `kMinLayerAlpha 0.2`, `kMaxLayerAlpha 0.88`, window alpha 0.35..1 (`:6-11, :1438-1456`). Sidebar translucency is only allowed on supported platforms (`canBeTransparent`).

**Containers per Space.** Each Space has `containerTabId` (`ZenSpaceCreation.mjs:283`). New tabs in that Space open in that container. Docs: "Each workspace can have a default container", and an option jumps to the Space whose default container matches a container tab opened elsewhere (`docs.zen-browser.app/user-manual/workspaces`; prefs `zen.workspaces.force-container-workspace`, `hide-default-container-indicator`). Default Firefox containers (Personal/Work/Banking/Shopping) are not created on new profiles (`zen.containers.create-default-identities` false). The docs note containers split cookies/sessions but not history or extensions. Their model is simpler than ours: the container is chosen at Space creation and the user sees Firefox container names.

## 2. Essentials, Pinned, normal tabs; folders; unload/archive

- **Essentials**: a grid above everything (`.zen-essentials-container`, `R:src/zen/tabs/zen-tabs/vertical-tabs.css:1242`). Columns `repeat(auto-fit, minmax(max(23.7%, tab-min-height+4px), 1fr))` (so about 4 per row, shrinking on narrow widths, `:1251`), gap 4px, `max-height` and column changes animate 0.3s ease-out (`:1246`). Max 12 (`zen.tabs.essentials.max`). A selected essential gets a translucent card (light: white 85%; dark: primary colour mixed with grey) (`:1340-1347`). Essentials are shared across Spaces unless `zen.workspaces.separate-essentials` (default true in prefs; per-container otherwise, `ZenSpaceAddSwipe.mjs:224` compares the `container` attribute). When they differ per Space, they animate in/out on Space switch (`shouldAnimateEssentials`).
- **Pinned**: tabs listed under the Space header, per Space, with the Space indicator collapsing them (`collapsedpinnedtabs`). A pinned tab keeps its original entry in `_zenPinnedInitialState`. When the URL changes, `zen-pinned-changed` is set and a reset button appears (`vertical-tabs.css:938`); it restores the URL and original favicon (`ZenPinnedTabManager.mjs:223, 467, 987-1020`). Pref `zen.pinned-tab-manager.restore-pinned-tabs-to-pinned-url` (default false) controls restore at session start. `zen.tabs.open-pinned-in-new-tab` (true).
- **Close shortcut on pinned** (`zen.pinned-tab-manager.close-shortcut-behavior`, default `reset-unload-switch`; options `close`, `reset`, `switch`, `reset-switch`, `unload-switch`, `reset-unload-switch`; `ZenPinnedTabManager.mjs:340-460`). Default: switch to another tab, unload this one (discard, keeps the row greyed), and reset to its pinned URL. If the tab is already unloaded and `wheel-close-if-pending` is true (default), the next press really closes it. There is a separate key to reset (`zen-pinned-tab-reset-shortcut`, unbound by default) and Ctrl+Shift+D to pin/unpin.
- **Normal tabs**: below pinned. "Close all unpinned tabs" is Ctrl+Shift+K (`zen-keysets`). New-tab button is in the sidebar (`zen.tabs.show-newtab-vertical`). If the last unpinned tab is closed, an optional new tab opens (`open-new-tab-if-last-unpinned-tab-is-closed`, false). Tab rename inline (`ZenTabRenamer.mjs`).
- **Folders** (`R:src/zen/folders/`): nested to 5 (`zen.folders.max-subfolders`). Collapse animates `opacity 1->0, height auto->0`, 0.18s, `ease-in-out` via Web Animations (`ZenFolders.mjs:1710-1788`); reveal 0.2s (`:1714`). Dropping onto a folder triggers at 20% of folder height (`zen.tabs.folder-dragover-threshold-percent`). Closing a folder's active tab animates an "unload all". Live folders (GitHub PRs/issues, RSS) exist (`R:docs/live-folders-specs.md`), unverified in detail.
- **Archive**: I found no Arc-style auto-archive of old tabs in Zen. Unverified: the Library (Alt+Shift+L) and history sections handle recall. Tab unloading is Firefox's: a "memory based unloader" plus manual "Unload tab"; the 1.24b release adds a visual indicator when a tab is not visible. Issue #8822 (43 thumbs-up) says auto-unload stopped working after a change, so Zen's automatic unloading is unreliable. Auto-archive remains our own feature.

## 3. Compact mode, hide/peek, right side, collapsed rail

- **Two toggles**: `zen.view.compact.hide-tabbar` (default true) and `hide-toolbar` (false). In "single toolbar" layout (default `zen.view.use-single-toolbar` true) compact mode just hides the sidebar. Ctrl+S toggles; Ctrl+Alt+S shows the sidebar until pressed again; Ctrl+Alt+W does the same for the top toolbar (`zen-keysets.inc.xhtml:8-9`; docs).
- **Hidden state**: sidebar is `position: fixed`, `translate` by `-(100% - float - 1px)` (a 1px sliver stays as the hit area; `float` = element separation, 8px normally, 10px with no padding) (`R:src/zen/compact-mode/sidebar.inc.css:44-65`). It floats as a rounded card with a shadow and its own blur background. The window content stays full width.
- **Show**: attributes `zen-has-hover`, `zen-user-show`, `flash-popup`, `has-popup-menu`, `movingtab`, `zen-compact-mode-active` on the toolbox set `translate: none` with `transition: translate 0.25s <spring linear()>` (`sidebar.inc.css:~165-280`). The `linear()` curve reaches 1.0 at ~58% and peaks at 1.0109 at ~73% (about 1% overshoot), then settles to 1.0034. Hide uses `translate 0.15s ease` plus `visibility 0.15s` (`:73-76`).
- **Hover logic** (`ZenCompactMode.mjs:735-900`): mouse-enter sets a pending timer (`HOVER_HACK_DELAY`, pref `zen.view.compact.hover-hack-delay`); entering from a popup is ignored; mouse-leave waits one tick and re-checks `:hover` (to cope with window-drag making Firefox report a fake leave, see the bug link in the source, #7746); the sidebar then stays for `zen.view.compact.sidebar-keep-hover.duration` = 150ms; the toolbar for `toolbar-hide-after-hover.duration` = 1000ms. Leaving the window only hides after the pointer goes 200px (horizontal) / 100px (vertical) past the edge (`outside-window-edge-offset.*`). It never hides while a tab is being dragged, a popup menu is open, or a tab is being renamed.
- **Transition into/out of compact mode** is a separate Motion animation of the sidebar margin (`marginLeft 0 -> -sidebarWidth`, `type: "spring", bounce: 0, duration: 0.1`, `ZenCompactMode.mjs:476-537`), so the layout changes in 100ms and the hidden panel later slides with the CSS curve.
- **Flash**: `flashSidebar` shows the sidebar for 800ms (`toolbar-flash-popup.duration`, feature off by default) and `show-background-tab-toast` (true) shows a toast for tabs opened in the background.
- **Right side**: `zen.tabs.vertical.right-side` flips everything: translate sign, left/right offsets, splitter (`sidebar.inc.css:51-55, 128-140`). Window buttons move too (`zen-window-buttons-reversed`).
- **Collapsed/icon-only**: `zen-sidebar-expanded` false gives a rail with `--zen-toolbox-max-width: 62px`, essentials become a single column, Space icons min 36px (`sidebar.inc.css:44`, `zen-workspaces.css:105`). Expanded max width 500px (`zen.view.sidebar-expanded.max-width`). Toggle via `zen-toggle-sidebar` (unbound by default).

## 4. Theme system and readability

- **Light/dark**: window scheme pref (system/light/dark) feeds `isDarkMode`. For gradient Spaces the effective mode is recomputed from the dominant colour: `shouldBeDarkMode` blends the "dark text" and "light text" candidates onto the background, computes WCAG contrast ratios, and returns whichever wins (`ZenGradientGenerator.mjs:1759-1781`; `kTextOpacity 0.9`, `kTextTint 0.12`). It sets `zen-should-be-dark-mode` on the root. Text colour = accent blended 12% to white or black, alpha 0.9 (`titleColors`, `toolbarTextStyles`, `:1884-1895`). The UI accent (`--zen-primary-color`) is also adjusted toward fixed lightness 0.42 (light) / 0.62 (dark), +0.3 saturation, mixing 60/40 with the picked colour (`getAccentColorForUI`, `:1864-1880`), with greys left grey.
- **Dark-mode bias** `zen.theme.dark-mode-bias` = 0.3: reduces the light-text contrast score so dark mode is chosen for borderline colours (applies only when the sidebar can be transparent).
- **Default theme** uses fixed `#e9e9e9` / `#131313` backgrounds (`zen-compact-mode.css:11`).
- **Opacity**: slider changes layer alpha with a squared curve (section 1). On non-transparent platforms the colour is composited over an underlay of white or `#131313`.
- **Grain**: layer controlled by `--zen-grainy-background-opacity`; textures none/grain/sand/denim/tweed (`kTextures`, `:26`); default strength 10/16. Applied to both the window and the toolbar backgrounds.
- **Blur**: with `zen.theme.acrylic-sidebar` on and no `prefers-reduced-transparency`, sidebar is `rgb(233 233 233 / 80%)` / `rgb(19 19 19 / 80%)` + `backdrop-filter: blur(42px)`, with a comment "Limit backdrop bleed so toolbar text stays readable over bright pages" (`zen-compact-mode.css:20-31`). These prefs are `@IS_TWILIGHT@` (the pre-release channel only), so they are not default on release. Unverified how stable this is.
- **System colours**: `zen.theme.use-system-colors` skips the contrast logic. **Border radius** `zen.theme.border-radius` -1 = platform native. **Element separation** `zen.theme.content-element-separation` 8px (the gap between the card-like content area and the sidebar).
- **Pitfall in this system**: legacy themes (`zen.theme.gradient-legacy-version`) disable the contrast check, which they surface by marking the controls as "invalidate-controls" (`:2160-2170`). Issues #4261, #9465, #6965 and #8105 show colour-input confusion (alpha hex, duplicate colours).

## 5. Motion inventory (values from source)

| Interaction | Mechanism | Duration / easing | File |
|---|---|---|---|
| Sidebar show (compact) | CSS `translate` | 0.25s, spring-like `linear()` (~1% overshoot) | `compact-mode/sidebar.inc.css:~172-275` |
| Sidebar hide | CSS `translate` + `visibility` | 0.15s `ease` | `sidebar.inc.css:73-76` |
| Enter/leave compact (layout) | Motion spring on margin | 0.1s, bounce 0 | `ZenCompactMode.mjs:476-537` |
| Space switch | Motion spring on `transform` + background opacity | 250ms (pref), bounce 0 | `ZenSpaceManager.mjs:2314-2500` |
| Space swipe | live translate, rubber-band `1 - |x|/(4.5*width)`, multiplier 100 | follows finger | `ZenSpacesSwipe.mjs:204-216` |
| Add-Space swipe handle | CSS var transition | 0.2s ease-in-out | `zen-workspaces.css` / `ZenSpaceAddSwipe` |
| Space icon hover | CSS | filter/opacity 0.2s, transform 0.2s, width 0.1s | `zen-workspaces.css:78` |
| Workspace chevron / indent | CSS | 0.1s / 0.15s | `zen-workspaces.css:496-506` |
| Folder collapse | Web Animations | 0.18s ease-in-out (opacity+height) | `ZenFolders.mjs:1710-1788` |
| Folder reveal | Web Animations | 0.2s | `ZenFolders.mjs:1714` |
| Tab row height/padding/opacity | CSS (reduced-motion guarded) | 0.08s / 0.08s / 0.06s ease-in-out | `vertical-tabs.css:139-143` |
| Tab drag over / indent | CSS vars | `margin-inline-start 0.1s ease-in-out` | `common/styles/zen-theme.css:204` |
| Moving tab lift | CSS | `scale 0.1s ease`, `transform 0.1s ease` while `movingtab` | `vertical-tabs.css:1163-1165` |
| Essentials grid reflow | CSS | `max-height`, `grid-template-columns` 0.3s ease-out | `vertical-tabs.css:1246` |
| Toolbar show/hide (hidden-toolbar) | CSS vars | 0.15s ease-in-out | `zen-theme.css:205-206` |
| Library panel | CSS vars | 0.3s `cubic-bezier(0.22, 1, 0.36, 1)` | `common/styles/zen-library-widget.css:10-11` |
| Glance open | Motion spring + WAAPI | 350ms pref, bounce 0.2, bg scale 0.97, `ease-in-out` | `glance/ZenGlanceManager.mjs:9-10, 150-200, 642-653` |
| Split view | CSS inset | 0.08-0.09s ease-out; fade 0.2s | `split-view/zen-split-view.css:24, 74-76` |
| Boost share spring | CSS `linear()` var `--spring-easing` | 1.2s transform | `boosts/zen-boosts-share.css:283` |
| Spring overshoot easing used elsewhere | CSS | `scale 0.25s cubic-bezier(0.34, 1.56, 0.64, 1)` (2 uses) | several |
| Reduced motion | only partial | `prefers-reduced-motion` seen at `vertical-tabs.css:139`; `prefers-reduced-transparency` in blur rule | see text |

Patterns worth copying as ideas: most micro-interactions are 80-200ms; structural motion (sidebar, Space) is 250ms with no bounce; real springs are done in JS with the Motion library (`R:src/zen/vendor/motion.min.mjs`, loaded lazily in `ZenUIManager.mjs:101`); only the panes that will be visible animate; animation promises have a timeout fallback; during an animation, hover logic is suppressed (`zen-compact-animating`).

Zen's token system is weak: 40+ distinct literals, only a few CSS variables (`--zen-hidden-toolbar-transition`, `--zen-tabbox-element-indent-transition`, `--zen-library-open-*`). Our motion tokens are already better organised.

## 6. Other things users like (brief)

- **Glance** (Alt+click, Ctrl+O expands to a tab): Arc Peek equivalent; out of scope.
- **Split view**: Ctrl+Alt+G (grid), V, H, U (unsplit); drag a tab to a window edge; Ctrl+Shift+* for an empty split. Prefs: `zen.splitView.enable-drag-over-split`, `min-resize-width`. Planned after M5.
- **URL bar**: in sidebar mode it floats over the page (`#urlbar[zen-floating-urlbar]`), domain-only display in the sidebar (`zen.urlbar.show-domain-only-in-sidebar`), quick actions (`zen.urlbar.suggestions.quick-actions`), `ZenUBGlobalActions`. 1.24b adds Ctrl-hover "Paste & Search" on the new tab button.
- **Tab rename**, **Space emojis/icons picker** (`ZenEmojiPicker.mjs`), **Library** with downloads and media, **CLI `--space`**, **live folders**, **Mods** (user CSS themes), **Boosts** (Arc's feature, out of scope), **Arc/Dia import** (1.24b).
- Haptic feedback pref on trackpad (`zen.haptic-feedback.enabled`). Unverified how it is used.

## 7. Pitfalls and complaints (from GitHub issues)

- **Compact mode instability**: sidebar shaking (#12856, #12505), stuck open after about an hour (#8643), retracts before the pointer reaches the close button (#15995, fixed in 1.23.4b), hover time setting ignored (#12489), cannot install add-ons from a popup in compact mode (#11667), video controls under the screen in fullscreen (#10594, 36 thumbs-up). Lesson: hover state is the hard part. Use one state machine, not attributes plus timers; keep the sidebar open during popups and drags; test with window drag and fullscreen.
- **Window/tab sync**: shipped in 1.18b without an off switch, widely disliked (#7079 meta, 166 thumbs-up, #12008, 79). Essentials disappearing after close (#12493, #9389), empty Spaces after wake (#12512). Lesson: do not make windows mirror each other by default. Session restore for pinned and favorite tabs must be tested.
- **Memory and performance**: #8932 (meta, 75), #504, #12496, #6302 picture caching (114 thumbs-up), laggy scrolling on 120Hz (#9842). Gradient layers and blur are a cost. Lesson: gate blur and grain, test on 120Hz, make animation cheap (transform/opacity only) and avoid running many animations at once.
- **Auto-unload unreliable** (#8822).
- **Accidental Space switches**: a mouse back button switched Spaces (#12491). Lesson: map swipe only to horizontal trackpad gestures, with a threshold.
- **New tab confusion**: Ctrl+T showing search instead of a tab (#12510); "Workspaces button does not hide with one Space" (#4256). Lesson: hide Space UI when there is one Workspace.
- **Many prefs**: more than 30 compact/workspace prefs live in `about:config`, not in Settings. Arc has almost none. Lesson: expose few, well-chosen settings.
- **Colour picker bugs**: alpha hex input broken (#6965), duplicate colours (#8105), a transparent gradient covering the current tab (#12497).
- **Dark mode lost after update** (#9229, 28 thumbs-up). Lesson: theme migration needs tests.

## Unverified

- Arc-style auto-archive in Zen (found none).
- Full user-manual text for Essentials/Spaces (docs pages are thin; Essentials page 404).
- Exact release-channel status of the acrylic prefs (`@IS_TWILIGHT@` suggests Twilight only).
- `HOVER_HACK_DELAY` default value (read from a pref `zen.view.compact.hover-hack-delay` that is not in `prefs/zen/compact-mode.yaml`; fallback in code not checked).
- Live folder details and haptic feedback behaviour.
- All behaviour is from source reading; I did not run Zen 1.24.1b.
