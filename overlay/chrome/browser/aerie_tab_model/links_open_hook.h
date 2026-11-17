// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_TAB_MODEL_LINKS_OPEN_HOOK_H_
#define CHROME_BROWSER_AERIE_TAB_MODEL_LINKS_OPEN_HOOK_H_

struct NavigateParams;

namespace aerie_tab_model {

// The one call that chrome::Navigate makes before it does anything else
// (patches/sidebar/0052). It applies "Links open" (issue #141) to a link that
// the page asked to open in a new tab. Returns true when it opened the link in
// the tab of the page itself and the caller must do nothing more (the new
// contents that the page asked for is dropped). This header has no
// dependencies, so that //chrome/browser/ui/navigator can include it without a
// cycle; the definition is in links_open_hook.cc.
//
// The rules, all of which must hold for a rewrite:
//   - The disposition is "foreground tab" and Chromium already made the new
//     contents (a link with target=_blank, or window.open without window
//     features). Ctrl+click, Shift+click, middle-click, "Open link in new
//     tab" and every popup have other dispositions or no contents and are
//     never touched.
//   - The new contents has no opener: a link with the default noopener
//     behaviour. A window.open call that keeps window.opener (OAuth and
//     payment windows talk to their opener) is never touched.
//   - A user gesture started it, the clicked page is the one the tab shows and
//     is not sandboxed against top-level navigation (nor is any frame of it),
//     the address is http or https, and the tab has a Tab Model record.
//   - The tab is in a folder. A tab in no folder is never rewritten, whatever
//     Settings say.
//   - It is not a form sent with POST (a body cannot be passed on).
//   - "Links open" for that tab is "in place": the nearest folder above the
//     tab that sets it, else Settings (the default for the tabs of folders).
// The link keeps the referrer that the page chose for it.
// When the value is "in a new tab" and a folder sets it, the new tab is also
// put inside that folder, next to the tab that opened it.
bool HandleLinkOpen(NavigateParams* params);

}  // namespace aerie_tab_model

#endif  // CHROME_BROWSER_AERIE_TAB_MODEL_LINKS_OPEN_HOOK_H_
