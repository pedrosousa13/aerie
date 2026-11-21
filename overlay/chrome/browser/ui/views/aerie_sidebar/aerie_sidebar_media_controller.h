// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_MEDIA_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_MEDIA_CONTROLLER_H_

#include <memory>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_media_card.h"

class BrowserWindowInterface;
class TabStripModel;

namespace content {
class WebContents;
}

// What the media card of the sidebar shows (issue #263). It watches the media
// session of every tab of the window (content::MediaSession, the same data the
// OS media keys and Global Media Controls read; no new web-facing API) and
// picks one tab:
//
//   1. a tab whose media plays, the one that started playing last;
//   2. else the tab the card showed last, while its media is paused and the
//      page still holds the session, so Play can resume it.
//
// With none, the card goes. The page's own track title and its "next track"
// handler are shown as they are: they are page data, drawn as plain text, and
// a page that sets neither gets the tab's title and no Next. Only one Workspace
// exists today, so every tab of the window is in the current Workspace; when
// Workspaces switch, only that Workspace's tabs belong here.
//
// The buttons press the session the way the OS media keys do (Resume,
// Suspend, NextTrack), and Go to tab activates the tab in the window's tab
// strip. Nothing here starts playback on its own, and nothing here enters
// picture-in-picture.
class AerieSidebarMediaController : public TabStripModelObserver,
                                    public AerieSidebarMediaCard::Delegate {
 public:
  AerieSidebarMediaController(BrowserWindowInterface* browser,
                              AerieSidebarMediaCard* card);
  AerieSidebarMediaController(const AerieSidebarMediaController&) = delete;
  AerieSidebarMediaController& operator=(const AerieSidebarMediaController&) =
      delete;
  ~AerieSidebarMediaController() override;

  // AerieSidebarMediaCard::Delegate: the button presses.
  void PlayPause() override;
  void NextTrack() override;
  void GoToTab() override;

  // The tab the card shows now, or null.
  content::WebContents* shown_contents() const;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

 private:
  class TabMedia;

  // Starts watching the tabs that are new and stops for the ones that are
  // gone.
  void SyncTabs();
  // Picks the tab and tells the card.
  void Refresh();
  void OnTabMediaChanged();
  TabMedia* Pick() const;

  const raw_ptr<BrowserWindowInterface> browser_;
  const raw_ptr<AerieSidebarMediaCard> card_;
  std::vector<std::unique_ptr<TabMedia>> tabs_;
  // The tab on the card now.
  raw_ptr<TabMedia> shown_ = nullptr;
  // Counts the starts of playback, so that the last one to start wins.
  int play_clock_ = 0;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_MEDIA_CONTROLLER_H_
