# First-run setup (`aerie://setup`)

On a profile's first start, Aerie opens `aerie://setup` instead of a new tab. The page walks
through seven short steps and then opens a new tab. It replaces Helium's onboarding page
(issue #46). The look follows Arc, and the typeface is Outfit (SIL OFL 1.1).

| Step | What it does | Browser call |
| --- | --- | --- |
| Welcome | Introduces Aerie. | none |
| Import | Lists the other browsers found on this computer, each with its own icon when the system has one (a letter tile otherwise). You pick one and what to bring (bookmarks and history; see below), or start fresh. | `initializeImportDialog`, `importData`, `aerieSetup.getBrowserIcons` |
| Make it yours | Light, dark or system appearance, and one of six colours (Neutral first and the default: the plain grey and white theme, with no colour of its own). The choices apply straight away and stay changeable in Settings > Appearance. | `aerieSetup.setColorScheme`, `aerieSetup.setThemeColor` |
| Ads and trackers | Explains that blocking is on (EasyList, EasyPrivacy, cosmetic hiding) and links to `aerie://filters`. There is no choice to make yet; levels belong to issue #45. | none |
| Search engine | The built-in engines (DuckDuckGo, Brave Search, Kagi, Startpage). Search suggestions stay off. | `getSearchEnginesList`, `setDefaultSearchEngine` |
| Privacy | A read-only list of the hardening defaults ([hardening.md](hardening.md)). | none |
| Default browser | Asks to become the default browser. Skipped when Aerie already is, or cannot be. | `requestDefaultBrowserState`, `setAsDefaultBrowser` |
| Done | A summary, then "Start browsing" marks setup complete and opens a new tab. | `setPref('completed_onboarding', true)`, `acceptLatestSchema` |

Closing the page before the end leaves setup incomplete, so it opens again on the next start.
The page never sets `helium.services.user_consented`, so Helium services stay off (issue #6).

## The import step: detection, icons and what comes over (issue #91)

**Detection** is Chromium's importer list (Helium carries Brave's `chrome_importer_list.cc`), plus
`patches/onboarding/0002`, which adds the Flatpak Chromium-family folders and Helium, labels Flatpak
installs, and lists a source once. A browser shows up when its profile folder holds data the importer
can read (a `Local State` and a `Bookmarks` or `History` file for Chromium-family browsers; a
`profiles.ini` for Firefox and Zen).

**Rows.** One row per install of a browser profile. A native and a Flatpak install of the same
browser are two installs, so they are two rows, and the Flatpak one reads "Chromium (Flatpak)". The
same folder reached by two paths (Zen's second Flatpak id, `io.github.zen_browser.zen`, is a symlink to
`app.zen_browser.zen`) is one row.

**Icons.** `aerieSetup.getBrowserIcons` takes the names of the listed browsers and answers with a map from name to a
`data:image/png;base64,...` URL. The browser reads the `Icon=` of the browser's `.desktop` entry in
`$XDG_DATA_HOME`, `$XDG_DATA_DIRS` and the Flatpak export directories, then looks the icon up in
`icons/hicolor` (sizes near 64 px first), the other themes and `pixmaps`. Only a PNG is used: the first
bytes must be the PNG signature and an `IHDR` chunk, and a file over 256 KiB is skipped. A name that
has no icon (or only an SVG) is left out and the page keeps its letter tile. The page accepts only a
`data:image/png;base64,` value, and it fetches nothing. What the browser enforces: the setup page's
data source gets Chromium's WebUI defaults from `webui::SetupWebUIDataSource` (`ui/webui/webui_util.cc`,
`SetJSModuleDefaults`), and neither Helium's page controller nor Aerie's patch overrides them (the only
override Helium added, Trusted Types, is removed by `patches/onboarding/0001`). So `default-src 'self'`;
`img-src chrome://resources chrome://theme chrome://image chrome://favicon2 chrome://app-icon
chrome://extension-icon chrome://fileicon blob: data: 'self'`; `connect-src chrome://webui-test
chrome://resources chrome://theme 'self'`. No `http:` or `https:` source is allowed for an image or a
connection, and `data:` images are allowed, which is how the icon is shown. (`data:` is broader than
PNG; the page's own check is what limits an icon to a PNG.) The browser test
`TheImportStepShowsAnIconAndMakesNoOutsideRequest` loads the real page to the import step with a
Flatpak Chromium and its icon, records every request, and fails on any that is not `chrome:`, `data:`,
`about:` or `blob:`; it also checks that an `https` image and an `https` `fetch` from the page are
refused. The Python tests check that the script has no network call. The icon files are read only
from the data dirs (for a Flatpak export directory, also its installation): an absolute `Icon=` path or a symlink that leaves them is
refused, and a file is opened once without following a symlink and checked as a regular file of at most
256 KiB. The names are lookup keys in a fixed table, never a path. No browser's trademark is bundled. macOS (the app bundle
icon) comes with the macOS build (M4).

**What comes over.** Passwords and autofill are never imported, from any browser:
`importer_list.cc` removes both from every profile. The step says so (`importNoPasswords`). It does
not point to a CSV export: Helium's `disable-password-manager.patch` forces
`credentials_enable_service` off by policy, so `chrome://password-manager/settings` shows "Offer to
save passwords" switched off and managed and has no "Import passwords" row (checked on the built
browser, 2026-11-25). The note says that Aerie does not save passwords. Extensions are a known
importer flag; the page does not offer them. Re-enabling any of the three needs a security review
and is out of scope.

**Helium is listed.** Helium has no importer type, but its profile has Chromium's format, so
`~/.config/net.imput.helium` is offered as "Helium" and imports as Chrome. Reason: the people most
likely to move to Aerie are Helium's users. A Helium folder that holds no profile (only crash
reports) is not listed.

Audit (Linux; this is what the code detects, checked against the maintainer's machine on 2026-11-25):

| Browser | Install type | Detected | Items importable |
| --- | --- | --- | --- |
| Google Chrome (Stable, Beta, Dev) | native, `~/.config/google-chrome*` | yes | bookmarks, history |
| Google Chrome | Flatpak `com.google.Chrome` | yes (#91) | bookmarks, history |
| Chromium | native, `~/.config/chromium` | yes | bookmarks, history |
| Chromium | Flatpak `org.chromium.Chromium` | yes (#91; on this machine) | bookmarks, history |
| Chromium | snap | no (gap) | none |
| Brave | native, `~/.config/BraveSoftware/Brave-Browser` | yes | bookmarks, history |
| Brave | Flatpak `com.brave.Browser` | yes (#91) | bookmarks, history |
| Microsoft Edge | native, `~/.config/microsoft-edge` | yes | bookmarks, history |
| Microsoft Edge | Flatpak `com.microsoft.Edge` | yes (#91) | bookmarks, history |
| Vivaldi | native, `~/.config/vivaldi` | yes | bookmarks, history |
| Vivaldi | Flatpak `com.vivaldi.Vivaldi` | yes (#91) | bookmarks, history |
| Opera | native, snap | yes | bookmarks, history |
| Opera | Flatpak `com.opera.Opera` | yes (#91) | bookmarks, history |
| Yandex, NAVER Whale | native | yes | bookmarks, history |
| Helium | native, `~/.config/net.imput.helium` | yes when it holds a profile (#91); the folder on this machine holds none | bookmarks, history |
| Firefox | native, snap or Flatpak `org.mozilla.firefox` | the first `profiles.ini` found only; Flatpak alone is found (on this machine) | bookmarks, history |
| Firefox | native and Flatpak both installed | only one of them (gap) | bookmarks, history |
| Zen | native `~/.zen` or Flatpak `app.zen_browser.zen` | the first `profiles.ini` found only; Flatpak alone is found (on this machine); `io.github.zen_browser.zen` is the same folder | bookmarks, history |
| Zen | native and Flatpak both installed | only one of them (gap) | bookmarks, history |
| Bookmarks HTML file | file | listed by Chromium, hidden by the page | bookmarks |
| Arc, Dia, Perplexity Comet, Safari, IE and Edge (legacy) | macOS or Windows only | not on Linux | n/a |

Gaps the audit found and did not close here are filed as issues.

## How it is built

- **Native side: Helium's.** `helium/core/onboarding-page.patch` adds the setup WebUI, the
  first-run redirect and the import, search engine and default browser handlers. Aerie keeps
  these. It does not keep `OnboardingMessageHandler` (issue #69): its `setPref` sets any
  `helium.*` pref and it installs extensions. `AerieSetupHandler` replaces it and answers only
  `setPref('completed_onboarding', <bool>)` (any other name is rejected and changes nothing) and
  `acceptLatestSchema`, and the icon lookup `aerieSetup.getBrowserIcons` (issue #91, see the
  import step above). `getExtensions` and `installExtension` have no handler.
- **Page: Aerie's.** Helium downloads its page, a Svelte app, from `deps.ini [onboarding]`. Aerie
  does not download it (`tools/aerie_build/helium_deps.py`). The page lives in the overlay at
  `overlay/components/aerie_onboarding/`: plain HTML, CSS and a script module, with no build step.
  The script builds every node itself and never assigns HTML, so the default WebUI Trusted Types
  policy is enough.
- **Hook.** `patches/onboarding/0001-serve-aerie-setup-page.patch` points Helium's page controller,
  the resource pak list, the resource IDs and the string parts at `components/aerie_onboarding`,
  and registers `AerieThemeHandler`
  (`overlay/chrome/browser/ui/webui/aerie_onboarding/`). That handler accepts only the six colour
  ids it knows, never a raw colour. Neutral sets the grayscale pref and no user colour; the others
  set a seed colour. It never calls `UseDefaultTheme()`, which would also reset the appearance.
- **Strings** are in `aerie_onboarding_strings.grdp` (a part of `generated_resources.grd`) with
  their loadTimeData keys in `aerie_onboarding_localized_strings.h`.
  `tests/test_onboarding_page.py` checks that the two lists match, that the page only asks for
  listed keys, and that every page file is in the `.grd`.
- **Font.** `outfit.woff2` is `branding/source/fonts/Outfit-Variable.ttf` converted to WOFF2, with
  its licence served beside it at `aerie://setup/OFL.txt`.

To change the page, edit the files in `overlay/components/aerie_onboarding/resources/`. A new
string needs a message in the `.grdp` and a row in the header.
