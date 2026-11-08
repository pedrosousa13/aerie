# First-run setup (`aerie://setup`)

On a profile's first start, Aerie opens `aerie://setup` instead of a new tab. The page walks
through seven short steps and then opens a new tab. It replaces Helium's onboarding page
(issue #46). The look follows Arc, and the typeface is Outfit (SIL OFL 1.1).

| Step | What it does | Browser call |
| --- | --- | --- |
| Welcome | Introduces Aerie. | none |
| Import | Lists the other browsers found on this computer. You pick one and what to bring (bookmarks, saved passwords, history, search engines), or start fresh. | `initializeImportDialog`, `importData` |
| Make it yours | Light, dark or system appearance, and one of seven colours (Neutral first and the default, with no tint). The choices apply straight away and stay changeable in Settings > Appearance. | `aerieSetup.setColorScheme`, `aerieSetup.setThemeColor` |
| Ads and trackers | Explains that blocking is on (EasyList, EasyPrivacy, cosmetic hiding) and links to `aerie://filters`. There is no choice to make yet; levels belong to issue #45. | none |
| Search engine | The built-in engines (DuckDuckGo, Brave Search, Kagi, Startpage). Search suggestions stay off. | `getSearchEnginesList`, `setDefaultSearchEngine` |
| Privacy | A read-only list of the hardening defaults ([hardening.md](hardening.md)). | none |
| Default browser | Asks to become the default browser. Skipped when Aerie already is, or cannot be. | `requestDefaultBrowserState`, `setAsDefaultBrowser` |
| Done | A summary, then "Start browsing" marks setup complete and opens a new tab. | `setPref('completed_onboarding', true)`, `acceptLatestSchema` |

Closing the page before the end leaves setup incomplete, so it opens again on the next start.
The page never sets `helium.services.user_consented`, so Helium services stay off (issue #6).

## How it is built

- **Native side: Helium's.** `helium/core/onboarding-page.patch` adds the setup WebUI, the
  first-run redirect, `OnboardingMessageHandler` (prefs, completion) and the import, search engine
  and default browser handlers. Aerie keeps all of it.
- **Page: Aerie's.** Helium downloads its page, a Svelte app, from `deps.ini [onboarding]`. Aerie
  does not download it (`tools/aerie_build/helium_deps.py`). The page lives in the overlay at
  `overlay/components/aerie_onboarding/`: plain HTML, CSS and a script module, with no build step.
  The script builds every node itself and never assigns HTML, so the default WebUI Trusted Types
  policy is enough.
- **Hook.** `patches/onboarding/0001-serve-aerie-setup-page.patch` points Helium's page controller,
  the resource pak list, the resource IDs and the string parts at `components/aerie_onboarding`,
  and registers `AerieThemeHandler`
  (`overlay/chrome/browser/ui/webui/aerie_onboarding/`). That handler accepts only the seven colour
  ids it knows, never a raw colour.
- **Strings** are in `aerie_onboarding_strings.grdp` (a part of `generated_resources.grd`) with
  their loadTimeData keys in `aerie_onboarding_localized_strings.h`.
  `tests/test_onboarding_page.py` checks that the two lists match, that the page only asks for
  listed keys, and that every page file is in the `.grd`.
- **Font.** `outfit.woff2` is `branding/source/fonts/Outfit-Variable.ttf` converted to WOFF2, with
  its licence served beside it at `aerie://setup/OFL.txt`.

To change the page, edit the files in `overlay/components/aerie_onboarding/resources/`. A new
string needs a message in the `.grdp` and a row in the header.
