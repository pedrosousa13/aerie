// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_AUDIO_WATCHER_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_AUDIO_WATCHER_H_

#include <memory>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_audio_state.h"
#include "components/tabs/public/tab_interface.h"

class TabStripModel;

// Tells the sidebar when the audio mark of a tab changes (issue #268). A tab
// is "muted" for as long as it is muted (the tab's own mute state, so the mark
// stays and the user can unmute a tab that has gone silent), and "playing"
// while Chromium's tab alerts (TabAlertController, the state of the stock tab
// strip's audio indicator) say it was recently audible.
class AerieSidebarAudioWatcher : public TabStripModelObserver {
 public:
  // `changed` runs after the audio state of any tab of `model` changed.
  AerieSidebarAudioWatcher(TabStripModel* model,
                           base::RepeatingClosure changed);
  AerieSidebarAudioWatcher(const AerieSidebarAudioWatcher&) = delete;
  AerieSidebarAudioWatcher& operator=(const AerieSidebarAudioWatcher&) = delete;
  ~AerieSidebarAudioWatcher() override;

  static AerieSidebarAudioState StateOf(tabs::TabInterface* tab);

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

 private:
  class TabWatch;

  void Sync();

  const raw_ptr<TabStripModel> model_;
  const base::RepeatingClosure changed_;
  std::vector<std::pair<tabs::TabHandle, std::unique_ptr<TabWatch>>> watches_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_AUDIO_WATCHER_H_
