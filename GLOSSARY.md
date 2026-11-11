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
