// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_audio_watcher.h"

#include <algorithm>
#include <optional>

#include "base/callback_list.h"
#include "chrome/browser/ui/tabs/alert/tab_alert_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_alert.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"

// The two signals of one tab: the mute state of its contents (also for a tab
// that never made sound or has gone silent) and the "recently audible" alert.
class AerieSidebarAudioWatcher::TabWatch : public content::WebContentsObserver {
 public:
  TabWatch(content::WebContents* contents,
           tabs::TabAlertController* alerts,
           base::RepeatingClosure changed)
      : content::WebContentsObserver(contents),
        changed_(std::move(changed)),
        alert_subscription_(alerts->AddAlertToShowChangedCallback(
            base::BindRepeating(
                [](TabWatch* self, std::optional<tabs::TabAlert>) {
                  self->changed_.Run();
                },
                base::Unretained(this)))) {}

  content::WebContents* contents() const { return web_contents(); }

  // content::WebContentsObserver:
  void DidUpdateAudioMutingState(bool muted) override { changed_.Run(); }

 private:
  const base::RepeatingClosure changed_;
  base::CallbackListSubscription alert_subscription_;
};

AerieSidebarAudioWatcher::AerieSidebarAudioWatcher(
    TabStripModel* model,
    base::RepeatingClosure changed)
    : model_(model), changed_(std::move(changed)) {
  model_->AddObserver(this);
  Sync();
}

AerieSidebarAudioWatcher::~AerieSidebarAudioWatcher() {
  model_->RemoveObserver(this);
}

// static
AerieSidebarAudioState AerieSidebarAudioWatcher::StateOf(
    tabs::TabInterface* tab) {
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!contents) {
    return AerieSidebarAudioState::kNone;
  }
  if (contents->IsAudioMuted()) {
    return AerieSidebarAudioState::kMuted;
  }
  const tabs::TabAlertController* alerts = tabs::TabAlertController::From(tab);
  return alerts && alerts->IsAlertActive(tabs::TabAlert::kAudioPlaying)
             ? AerieSidebarAudioState::kPlaying
             : AerieSidebarAudioState::kNone;
}

void AerieSidebarAudioWatcher::OnTabStripModelChanged(
    TabStripModel*,
    const TabStripModelChange&,
    const TabStripSelectionChange&) {
  Sync();
}

void AerieSidebarAudioWatcher::Sync() {
  // A tab that the model no longer holds goes at once (this runs inside the
  // notification of its removal, when the tab is already out of the model), and
  // so does a tab whose contents were replaced (discarded).
  std::erase_if(watches_, [&](const auto& entry) {
    tabs::TabInterface* tab = entry.first.Get();
    if (!tab || !entry.second->contents() ||
        tab->GetContents() != entry.second->contents()) {
      return true;
    }
    return model_->GetIndexOfTab(tab) == TabStripModel::kNoTab;
  });
  for (int i = 0; i < model_->count(); ++i) {
    tabs::TabInterface* tab = model_->GetTabAtIndex(i);
    tabs::TabAlertController* alerts =
        tab ? tabs::TabAlertController::From(tab) : nullptr;
    if (!alerts || !tab->GetContents()) {
      continue;
    }
    const tabs::TabHandle handle = tab->GetHandle();
    if (std::ranges::any_of(watches_, [&](const auto& entry) {
          return entry.first == handle;
        })) {
      continue;
    }
    watches_.emplace_back(handle, std::make_unique<TabWatch>(
                                      tab->GetContents(), alerts, changed_));
  }
}
