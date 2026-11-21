// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_AUDIO_WATCHER_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_AUDIO_WATCHER_H_

#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/tabs/public/tab_interface.h"

class TabStripModel;

// Tells the sidebar when the audio mark of a tab changes (issue #268). It uses
// the tab alerts of Chromium (TabAlertController), the same state that the stock
// tab strip's audio indicator shows: a tab is "playing" while it was recently
// audible, and "muted" while it is muted and was recently audible.
class AerieSidebarAudioWatcher : public TabStripModelObserver {
 public:
  enum class State { kNone, kPlaying, kMuted };

  // `changed` runs after the audio state of any tab of `model` changed.
  AerieSidebarAudioWatcher(TabStripModel* model,
                           base::RepeatingClosure changed);
  AerieSidebarAudioWatcher(const AerieSidebarAudioWatcher&) = delete;
  AerieSidebarAudioWatcher& operator=(const AerieSidebarAudioWatcher&) = delete;
  ~AerieSidebarAudioWatcher() override;

  static State StateOf(tabs::TabInterface* tab);

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

 private:
  void Sync();

  const raw_ptr<TabStripModel> model_;
  const base::RepeatingClosure changed_;
  std::vector<std::pair<tabs::TabHandle, base::CallbackListSubscription>> subs_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_AUDIO_WATCHER_H_
