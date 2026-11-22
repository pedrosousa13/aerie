# Glossary

Domain terms for Aerie. Use these words in issues, code, tests and docs. Design detail is in [docs/sidebar-design.md](docs/sidebar-design.md).

## Sidebar

**Sidebar**
The vertical panel with the address pill, Favorites, Pinned, Today, and the bottom bar (Archive, Workspace switcher). It is the default tab layout (spec #1, story 50). It is native Views. It can sit on the left or right and has four Densities. The classic horizontal layout is a separate option (story 51).

**Favorites**
Icon tiles at the top of the Sidebar. They belong to one Workspace (story 52). Not the same as bookmarks.

**Pinned tab**
A tab in the Pinned section. It persists across restarts within its Workspace and keeps its original URL, with a way back after the user navigates away (stories 53, 55). It never auto-archives. Ctrl+W puts it to sleep instead of closing it, by default. Its page comes back after a restart without loading until the user selects it. The close setting `aerie.tabs.pinned_close` chooses what Ctrl+W and the close button do.

**Today**
The section of unpinned tabs. Auto-archive applies to them (story 56).

**Folder**
A group of tabs in Pinned or Today. Folders nest up to 5 levels. This entry is for a manual Folder: it holds what the user drags in. The other two kinds are Auto-filing folder and Smart folder. One folder has one kind.

**Auto-filing folder**
A Folder with URL rules (#139). A new tab that matches a rule is filed into it. Rules apply to new tabs only. When two folders match, the first one wins, top to bottom, Pinned before Today.

**Smart folder**
A Folder that is a live view (#140). It lists every open Pinned and Today tab of its Workspace that matches its rules, and moves nothing. Closing a row closes the real tab. It has no Folder behaviour.

**Folder behaviour**
Per-folder settings (#141): auto-archive (never, or its own timeout; not in Pinned), sleep after a set time, and open links in a new tab or in place. A folder without a setting inherits the global one.

**Archive** and **auto-archive**
Auto-archive moves a Today tab to the Archive after the user's timeout (12 hours by default, or 24 hours, 7 days, never). The Archive is the list of archived tabs, searchable and restorable (stories 56 to 58). A restored tab goes back to the Workspace it came from.

**Sleeping tab**
A tab that is unloaded to free memory (story 61). The Sidebar shows a moon badge and a dimmed favicon. Selecting it wakes it. Chromium's discard does the unloading; Aerie adds the idle timer (`aerie.tabs.sleep_after_minutes`, 1 hour by default) and the display. Not the same as an archived tab, which is closed.

**Recent tabs switcher**
Ctrl+Tab (#269). It goes to the tab used before and shows a card overlay of the most recently used tabs of the window; holding Ctrl and pressing Tab again steps along, Ctrl+Shift+Tab steps back, releasing Ctrl switches and Esc cancels. The window stands in for the current Workspace until Workspaces exist. The Settings switch `aerie.sidebar.recent_tab_switcher` ("Ctrl+Tab cycles recent tabs", on by default) turns it off, and Ctrl+Tab then goes along the tab order again.

**Tab preview card** (code: HoverCard)
The card beside a Sidebar row that shows after the pointer rests on it for the tooltip delay, or at once when the row takes keyboard focus (#270): title, host, the full URL on its own line and a snapshot of the page when the tab has one. The snapshot is in memory only and never exists for an incognito tab (or, later, a Private Workspace). The Settings switch `aerie.sidebar.hover_card_snapshots` ("Show page previews on tab cards", on by default) turns it off and keeps the text. Not the same as the Recent tabs switcher, which is the Ctrl+Tab overlay of cards, nor the Media card at the bottom of the Sidebar.

**Kept tab** (Keep loaded)
A tab the user marked "Keep loaded" (#267). The idle sleep timer and auto-archive skip it, whatever its folder says. The flag lives on the tab record, so it survives a restart. The row shows a small mark. Not the same as a Pinned tab, which can still sleep and never archives only because of its section.

**Rename rule**
A user-written rule (#146) that changes the title the Sidebar shows for a tab. It matches by Domain, URL starts with, URL contains or Title contains (the same grammar as folder rules) and rewrites the title with a template (`{title}`, `{host}`) or by removing text. Rules are ordered, the first that matches wins, and each can be switched off, or all at once. A rule beats the Tidy; a manual name beats a rule. Only the Sidebar changes: the page's `document.title` does not.

**Tidy** (tidied title)
The built-in, on-device clean-up of a title (#146). It drops a trailing site name such as " - Gmail", unread counts such as "(3)", and repeated separators. On by default; it can be switched off globally and for one site. The row of a tidied tab has a tooltip with the original title.

## Workspaces and look

**Workspace**
A context such as work, personal or a side project, inside one window (stories 91 to 95). It has its own name, icon, Workspace colour, Favorites, Pinned tabs and Today tabs, and its own cookies and site storage, as one storage partition in one profile ([ADR 0006](docs/adr/0006-workspace-isolation.md)). Every tab belongs to a Workspace. The Workspace record lives in the Tab Model and syncs later.

**Workspace colour**
The hue of a Workspace: one of eight presets (dusk, tide, moss, ember, lilac, slate, sand, mint) or a custom colour. Every Theme draws it in its own way. It is separate from the Theme and does not choose light or dark.

**Theme**
How the chrome is drawn: surfaces, tint strength, shadows and materials. The themes are Vivid (default), Soft, Clean, OLED (dark only), High contrast and Mono. Not the same as the Workspace colour.

**Mode**
Light, dark or system. Always an explicit user choice.

**Workspace override**
A Workspace's own Theme and Mode, stored on its record in the Tab Model as `theme` and `mode`. Each can be `default`, which means "use the global setting". The effective Theme and Mode are the override, or else the global default.

**Peek**
Showing an auto-hidden Sidebar by moving the pointer to the screen edge, or with F6 or Ctrl+Alt+S. One state machine controls it.

**Density**
How much room the Sidebar takes: comfortable, compact, collapsed rail (icons only), or auto-hide.

**Command bar**
The centred search and action surface opened with Ctrl+T, Ctrl+L or Ctrl+K. M2 designs how it looks. M3 builds how it behaves.

## Platform

**Design Tokens**
The named values for colour, radius, spacing, type, size and motion, defined once in `tokens.json` and generated for WebUI and native Views ([ADR 0007](docs/adr/0007-single-design-token-source.md)). Motion tokens carry reduced-motion values.

**Guard**
Aerie's blocking and protection feature: the ad and tracker Filter Engine, plus per-site controls and the fingerprinting switch. It shows as an icon at the end of the address pill. Its panel opens from that icon, and its section of Settings is `chrome://settings/aerieGuard` ([docs/adblock.md](docs/adblock.md)). Blocking features and flows follow Brave Shields.

## Privacy

**Crash report**
A minidump the crash handler writes to `Crash Reports/pending` in the browser's config folder when a process crashes. It stays on the device, is readable by the user only, and is never uploaded. Settings > Crash reports lists them (time, process type, version) and deletes them. At most 20 are kept; the oldest go first.
